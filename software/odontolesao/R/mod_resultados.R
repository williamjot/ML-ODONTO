# Resultados: validação nas 20 panorâmicas marcadas e revisão do acervo, sempre atualizados.

cartao_grafico <- function(ns, id, titulo, altura = "380px", nota = NULL) {
  bslib::card(full_screen = TRUE,
    bslib::card_header(class = "d-flex justify-content-between align-items-center", titulo,
      downloadButton(ns(paste0("baixar_", id)), "PNG", class = "btn-sm btn-outline-secondary")),
    plotOutput(ns(id), height = altura),
    if (!is.null(nota)) htmltools::p(class = "small text-muted mb-0", nota))
}

mod_resultados_ui <- function(id) {
  ns <- NS(id)
  bslib::navset_card_underline(
    bslib::nav_panel("Validação (20 panorâmicas marcadas)",
      bslib::layout_columns(col_widths = c(6, 6),
        cartao_grafico(ns, "froc", "Curvas FROC"),
        cartao_grafico(ns, "metricas", "Sensibilidade, precisão e Dice no limiar de operação",
                       nota = "Barras de erro: IC 95% (Wilson; bootstrap por paciente para o Dice).")),
      cartao_grafico(ns, "mapa", "Lesões encontradas por modelo", altura = "300px"),
      bslib::layout_columns(col_widths = c(7, 5),
        bslib::card(bslib::card_header("Tabela por modelo"), DT::DTOutput(ns("tabela"))),
        bslib::card(bslib::card_header("Referência corrigida pela auditoria"),
          selectInput(ns("modelo_aud"), "Modelo", choices = NULL),
          checkboxGroupInput(ns("excluir"), "Excluir da referência as lesões julgadas como",
                             choices = c("Não é lesão / erro de marcação" = "erro_marcacao", "Não visível na panorâmica" = "nao_visivel"),
                             selected = c("erro_marcacao", "nao_visivel")),
          uiOutput(ns("auditada"))))
    ),
    bslib::nav_panel("Revisão do acervo",
      bslib::layout_columns(col_widths = c(7, 5),
        cartao_grafico(ns, "precisao", "Desempenho estimado pela revisão",
                       nota = "Use a amostra aleatória para relatar o desempenho; as demais imagens seguem a ordem de prioridade."),
        cartao_grafico(ns, "porimagem", "Detecções por imagem no acervo")),
      bslib::card(bslib::card_header("Concordância entre revisores"), uiOutput(ns("kappa")))
    )
  )
}

mod_resultados_server <- function(id, ctx) {
  moduleServer(id, function(input, output, session) {
    curvas <- reactive(curvas_froc(ctx$con))
    operacao <- reactive(metricas_operacao(ctx$con))
    mapa <- reactive(mapa_lesoes(ctx$con))
    est <- reactive({ ctx$versao(); estatisticas_revisao(ctx$con, ctx$modelo()$id, if (nzchar(ctx$revisor())) ctx$revisor()) })
    det_img <- reactive(deteccoes_por_imagem(ctx$con, ctx$modelo()$id))

    graficos <- list(
      froc = reactive(grafico_froc(curvas(), operacao())),
      metricas = reactive(grafico_metricas(operacao())),
      mapa = reactive(grafico_mapa_lesoes(mapa())),
      precisao = reactive(grafico_precisao_revisao(est())),
      porimagem = reactive(grafico_deteccoes_imagem(det_img()))
    )
    tamanhos <- list(froc = c(180, 110), metricas = c(180, 110), mapa = c(183, 80), precisao = c(160, 100), porimagem = c(120, 90))
    for (g in names(graficos)) local({
      nome <- g
      output[[nome]] <- renderPlot(graficos[[nome]](), res = 110, bg = "transparent")
      output[[paste0("baixar_", nome)]] <- downloadHandler(
        filename = function() sprintf("odontolesao_%s_%s.png", nome, format(Sys.Date())),
        content = function(f) ggplot2::ggsave(f, graficos[[nome]]() + ggplot2::theme(plot.background = ggplot2::element_rect(fill = "white", colour = NA)),
                                              width = tamanhos[[nome]][1], height = tamanhos[[nome]][2], units = "mm", dpi = 300, scale = 1.6))
    })

    output$tabela <- DT::renderDT({
      o <- operacao(); req(nrow(o))
      fmt <- function(m) { x <- o[o$metrica == m, ]; stats::setNames(sprintf("%s (%s–%s)", pct_rot(x$p), round(100 * x$inf), round(100 * x$sup)), x$modelo) }
      dice <- o[o$metrica == "Dice", ]
      t <- data.frame(Modelo = unique(o$modelo))
      t$Limiar <- virgula(o$limiar[match(t$Modelo, o$modelo)], 3)
      t$Sensibilidade <- fmt("Sensibilidade")[t$Modelo]; t$`Precisão` <- fmt("Precisão")[t$Modelo]
      t$`FP/imagem` <- virgula(o$fp_img[match(t$Modelo, o$modelo)], 2)
      t$Dice <- sprintf("%s (%s–%s)", virgula(dice$p), virgula(dice$inf), virgula(dice$sup))[match(t$Modelo, dice$modelo)]
      t <- t[order(match(t$Modelo, names(CORES_MODELO))), ]
      DT::datatable(t, rownames = FALSE, options = list(dom = "t", pageLength = 20), class = "compact")
    })

    observe({
      ms <- intersect(names(CORES_MODELO), unique(mapa()$modelo))
      updateSelectInput(session, "modelo_aud", choices = ms, selected = if ("U-Net v2" %in% ms) "U-Net v2" else ms[1])
    })
    output$auditada <- renderUI({
      ctx$versao(); req(input$modelo_aud, nzchar(ctx$revisor()))
      s <- sensibilidade_auditada(ctx$con, input$modelo_aud, ctx$revisor(), excluir = input$excluir %||% character())
      req(s)
      htmltools::tagList(
        htmltools::p(sprintf("Lesões não encontradas: %d · respondidas na auditoria por %s: %d · excluídas: %d",
                             s$perdidas, ctx$revisor(), s$respondidas, s$excluidas)),
        htmltools::tags$table(class = "table table-sm",
          htmltools::tags$tbody(
            htmltools::tags$tr(htmltools::tags$td("Sensibilidade com a referência original"), htmltools::tags$td(class = "text-end", formatar_proporcao(s$original))),
            htmltools::tags$tr(class = "table-primary", htmltools::tags$td(htmltools::strong("Com a referência corrigida")),
                               htmltools::tags$td(class = "text-end", htmltools::strong(formatar_proporcao(s$corrigida)))))),
        htmltools::p(class = "small text-muted", "Critério por lesão no limiar de operação. Responda a auditoria para atualizar este número."))
    })

    output$kappa <- renderUI({
      ctx$versao()
      k <- kappa_revisores(ctx$con, ctx$modelo()$id)
      if (is.null(k) || !nrow(k)) return(htmltools::p(class = "text-muted",
        "Disponível quando dois revisores tiverem concluído as mesmas imagens. A concordância entre especialistas é o teto realista para qualquer modelo."))
      k$concordancia <- pct_rot(k$concordancia); k$kappa <- round(k$kappa, 2)
      names(k) <- c("Revisor 1", "Revisor 2", "Detecções em comum", "Concordância", "Kappa de Cohen")
      DT::datatable(k, rownames = FALSE, options = list(dom = "t"), class = "compact")
    })
  })
}
