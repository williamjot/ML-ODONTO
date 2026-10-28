# Auditoria das lesões de referência que o modelo não encontrou.

mod_auditoria_ui <- function(id) {
  ns <- NS(id)
  bslib::layout_sidebar(
    sidebar = bslib::sidebar(
      width = 300, title = "Auditoria",
      selectInput(ns("modelo"), "Modelo avaliado", choices = NULL),
      radioButtons(ns("filtro"), NULL, c("Todas" = "todas", "Sem resposta" = "pendentes"), inline = TRUE),
      uiOutput(ns("resumo")),
      htmltools::hr(),
      htmltools::p(class = "small text-muted",
        "Cada cartão é uma lesão marcada pelo especialista que o modelo não encontrou. Julgue se ela é visível",
        htmltools::strong("nesta panorâmica"), ", independentemente do que o modelo fez."),
      htmltools::p(class = "small text-muted",
        htmltools::span(style = "color:#ff8c00", "■"), " traço desta lesão · ",
        htmltools::span(style = "color:#00c7ff", "■"), " máscara usada na avaliação · branco: outras lesões marcadas")
    ),
    uiOutput(ns("cartoes"))
  )
}

mod_auditoria_server <- function(id, ctx) {
  moduleServer(id, function(input, output, session) {
    ns <- session$ns
    observe({
      ms <- DBI::dbGetQuery(ctx$con, "SELECT DISTINCT modelo FROM achados_referencia")$modelo
      ms <- intersect(names(CORES_MODELO), ms)
      updateSelectInput(session, "modelo", choices = ms, selected = if ("U-Net v2" %in% ms) "U-Net v2" else ms[1])
    })

    perdidas <- reactive({
      ctx$versao(); req(input$modelo, nzchar(ctx$revisor()))
      lesoes_perdidas(ctx$con, input$modelo, ctx$revisor())
    })

    output$resumo <- renderUI({
      p <- perdidas(); n_resp <- sum(!is.na(p$resposta))
      tab <- table(factor(p$resposta, levels = RESPOSTAS_AUDITORIA, labels = names(RESPOSTAS_AUDITORIA)))
      htmltools::tagList(
        htmltools::p(htmltools::strong(sprintf("%d de %d respondidas", n_resp, nrow(p)))),
        htmltools::div(class = "progress mb-2", style = "height:6px",
          htmltools::div(class = "progress-bar", style = sprintf("width:%.0f%%", 100 * n_resp / max(1, nrow(p))))),
        htmltools::tags$table(class = "table table-sm small mb-0",
          htmltools::tags$tbody(lapply(names(tab), function(n) htmltools::tags$tr(htmltools::tags$td(n), htmltools::tags$td(class = "text-end", tab[[n]])))))
      )
    })

    output$cartoes <- renderUI({
      p <- isolate(perdidas()); input$filtro; input$modelo
      if (identical(input$filtro, "pendentes")) p <- p[is.na(p$resposta), ]
      if (!nrow(p)) return(bslib::card(htmltools::p(class = "text-muted m-3", "Nenhuma lesão neste filtro.")))
      arqs <- withProgress(message = "Preparando as imagens", value = 0, {
        lapply(seq_len(nrow(p)), function(i) { incProgress(1 / nrow(p)); cartao_auditoria(ctx$con, p$id[i], file.path(ctx$dados, "cache")) })
      })
      htmltools::tagList(lapply(seq_len(nrow(p)), function(i) cartao_ui(ns, p[i, ], arqs[[i]], i)))
    })

    observeEvent(input$resposta, {
      r <- input$resposta
      gravar_auditoria(ctx$con, as.integer(r$id), input$modelo, ctx$revisor(),
                       resposta = if (!is.null(r$valor)) r$valor else NULL, observacao = if (!is.null(r$obs)) r$obs else NULL)
      registrar_evento(ctx$con, ctx$revisor(), "auditoria", sprintf("referencia %s: %s", r$id, r$valor %||% "obs"))
      ctx$dados_mudaram()
    })
  })
}

cartao_ui <- function(ns, l, arqs, k) {
  url <- function(a) sprintf("cache/%s?v=%s", basename(a), as.integer(file.mtime(a)))
  info <- sprintf("Paciente %s, lesão %d · %s · diâmetro %s px · área %s px%s", l$paciente, l$lesao,
                  if (identical(l$arcada, "superior")) "maxila" else "mandíbula", round(l$diam_eq_px), format(round(l$area), big.mark = ".", decimal.mark = ","),
                  if (is.na(l$dist_achada)) "" else sprintf(" · a %s px da lesão encontrada mais próxima", round(l$dist_achada)))
  alerta <- c(if (!is.na(l$dist_achada) && l$dist_achada <= 40) "vizinha de uma lesão encontrada (pode ser a mesma lesão)",
              if (l$diam_eq_px <= 12) "muito pequena (pode ser resíduo do traço)")
  nome <- ns(paste0("r", l$id))
  radios <- lapply(names(RESPOSTAS_AUDITORIA), function(rotulo) {
    v <- RESPOSTAS_AUDITORIA[[rotulo]]
    htmltools::div(class = "form-check form-check-inline",
      htmltools::tags$input(class = "form-check-input", type = "radio", name = nome, id = paste0(nome, v), value = v,
        checked = if (identical(l$resposta, v)) NA else NULL,
        onchange = sprintf("Shiny.setInputValue('%s', {id: %d, valor: this.value, t: Date.now()}, {priority: 'event'})", ns("resposta"), l$id)),
      htmltools::tags$label(class = "form-check-label", `for` = paste0(nome, v), rotulo))
  })
  figura <- function(a, legenda) htmltools::tags$figure(class = "m-0",
    htmltools::tags$img(src = url(a), class = "img-fluid rounded border", alt = legenda),
    htmltools::tags$figcaption(class = "small text-muted", legenda))
  bslib::card(
    fill = FALSE, class = if (!is.na(l$resposta)) "border-success",
    bslib::card_header(htmltools::strong(sprintf("%d. ", k)), info,
                       if (length(alerta)) htmltools::span(class = "badge text-bg-warning ms-2", paste(alerta, collapse = "; "))),
    bslib::layout_columns(col_widths = c(3, 3, 3, 3),
      figura(arqs[1], "Panorâmica: posição"), figura(arqs[2], "Ampliado, sem marcação"),
      figura(arqs[3], "Traço e máscara"), figura(arqs[4], "Probabilidade do modelo (amarelo = alta)")),
    htmltools::div(class = "d-flex flex-wrap gap-3 align-items-center", radios),
    htmltools::tags$input(type = "text", class = "form-control form-control-sm", placeholder = "Observação (opcional)",
      value = l$observacao %||% "",
      onchange = sprintf("Shiny.setInputValue('%s', {id: %d, obs: this.value, t: Date.now()}, {priority: 'event'})", ns("resposta"), l$id))
  )
}
