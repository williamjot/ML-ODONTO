# Tela de revisão das detecções.

visor_ui <- function(id) htmltools::div(id = id, class = "visor")

mod_revisao_ui <- function(id) {
  ns <- NS(id)
  ml_area(ml_linha(
    tamanhos = c("280px", NA),
    painel("Imagens", icone = "folder2-open", corpo_classe = "p-0 d-flex flex-column",
      htmltools::div(class = "ml-lista-cab",
        uiOutput(ns("progresso")),
        radioButtons(ns("filtro"), NULL, inline = TRUE,
                     choices = c("Todas" = "todas", "Pendentes" = "pendentes", "Concluídas" = "concluidas"))),
      uiOutput(ns("lista"), class = "lista-imagens flex-grow-1")),
    painel("Radiografia", icone = "image", corpo_classe = "p-0 d-flex flex-column",
      ferramentas = htmltools::span(class = "small text-muted fw-normal", textOutput(ns("salvo"), inline = TRUE)),
      htmltools::div(class = "flex-grow-1", style = "min-height:0", visor_ui(ns("visor"))),
      htmltools::div(class = "ml-rodape-revisao",
        htmltools::div(class = "flex-grow-1", textAreaInput(ns("obs"), NULL, rows = 1, width = "100%", placeholder = "Observações (opcional)")),
        checkboxInput(ns("inadequada"), "Imagem inadequada"),
        actionButton(ns("anterior"), NULL, icon = icon("arrow-left"), class = "btn-outline-secondary btn-sm", title = "Imagem anterior (←)"),
        actionButton(ns("proxima"), NULL, icon = icon("arrow-right"), class = "btn-outline-secondary btn-sm", title = "Próxima imagem (→)"),
        actionButton(ns("concluir"), "Concluir e ir para a próxima", icon = icon("check"), class = "btn-success btn-sm")))
  ))
}

mod_revisao_server <- function(id, ctx) {
  moduleServer(id, function(input, output, session) {
    ns <- session$ns
    atual <- reactiveVal(NULL)
    revisao_id <- reactiveVal(NULL)
    salvo_em <- reactiveVal("")

    lista <- reactive({
      ctx$versao()
      req(nzchar(ctx$revisor()), nrow(ctx$modelo()) == 1)
      lista_revisao(ctx$con, ctx$revisor(), ctx$modelo()$id)
    })
    lista_filtrada <- reactive({
      l <- lista()
      switch(input$filtro, pendentes = l[l$situacao != "concluida", ], concluidas = l[l$situacao == "concluida", ], l)
    })

    output$progresso <- renderUI({
      l <- lista(); conc <- sum(l$situacao == "concluida"); am <- l[l$grupo == 1, ]
      am_conc <- sum(am$situacao == "concluida")
      htmltools::tagList(
        htmltools::div(class = "small", sprintf("Amostra aleatória: %d de %d", am_conc, nrow(am))),
        htmltools::div(class = "progress mb-2", style = "height:6px",
          htmltools::div(class = "progress-bar bg-success", style = sprintf("width:%.0f%%", 100 * am_conc / max(1, nrow(am))))),
        htmltools::div(class = "small text-muted mb-1", sprintf("Todas: %d de %d concluídas", conc, nrow(l)))
      )
    })

    output$lista <- renderUI({
      l <- lista_filtrada(); a <- atual()
      if (!nrow(l)) return(htmltools::div(class = "text-muted small p-2", "Nenhuma imagem neste filtro."))
      itens <- list(); g_ant <- 0
      for (k in seq_len(nrow(l))) {
        if (l$grupo[k] != g_ant) { g_ant <- l$grupo[k]; itens[[length(itens) + 1]] <- htmltools::div(class = "grupo", NOMES_GRUPO[g_ant]) }
        itens[[length(itens) + 1]] <- htmltools::div(
          class = paste("item", if (identical(l$id[k], a)) "atual"),
          onclick = sprintf("Shiny.setInputValue('%s', '%s', {priority: 'event'})", ns("abrir"), l$id[k]),
          title = l$arquivo[k],
          htmltools::span(class = paste("bola", l$situacao[k])), htmltools::span(class = "id", l$id[k]),
          htmltools::span(class = "text-muted small text-truncate", style = "max-width:140px", l$arquivo[k]),
          htmltools::span(class = "n", l$n[k]))
      }
      htmltools::tagList(itens)
    })

    # abre a primeira imagem pendente quando a lista fica disponível
    observe({
      l <- lista()
      if (is.null(isolate(atual())) && nrow(l)) {
        p <- l[l$situacao != "concluida", ]
        atual(if (nrow(p)) p$id[1] else l$id[1])
      }
    })
    observeEvent(input$abrir, atual(input$abrir))
    observeEvent(ctx$pedido_revisao(), { updateRadioButtons(session, "filtro", selected = "todas"); atual(ctx$pedido_revisao()$id) })

    # envia a imagem ao visor
    observeEvent(list(atual(), ctx$revisor(), ctx$modelo()), {
      req(atual(), nzchar(ctx$revisor()))
      d <- dados_imagem(ctx$con, atual(), ctx$modelo()$id, ctx$revisor())
      img <- d$imagem
      arq_cache <- imagem_visor(img, file.path(ctx$dados, "cache"))
      revisao_id(if (nrow(d$revisao)) d$revisao$id else NULL)
      dets <- lapply(seq_len(nrow(d$deteccoes)), function(i) list(
        indice = d$deteccoes$indice[i], pontos = jsonlite::fromJSON(d$deteccoes$contorno[i]),
        centro = c(d$deteccoes$cx[i], d$deteccoes$cy[i]), confianca = d$deteccoes$confianca[i]))
      decs <- stats::setNames(as.list(d$decisoes$decisao), d$decisoes$indice)
      adds <- lapply(seq_len(nrow(d$adicionadas)), function(i)
        list(id = d$adicionadas$id[i], pontos = jsonlite::fromJSON(d$adicionadas$contorno[i])))
      nunca <- !nrow(d$revisao)
      session$sendCustomMessage("visor:carregar", list(
        alvo = ns("visor"), id = img$id, titulo = sprintf("%s · %d detecção(ões)", img$id, length(dets)),
        url = sprintf("cache/%s?v=%s", basename(arq_cache), as.integer(file.mtime(arq_cache))),
        largura = img$largura, altura = img$altura, deteccoes = dets,
        decisoes = if (length(decs)) decs else structure(list(), names = character()),
        adicionadas = adds, cego = nunca && isTRUE(ler_config(ctx$con, "modo_cego_inicial", "FALSE") == "TRUE")))
      updateTextAreaInput(session, "obs", value = if (nrow(d$revisao)) d$revisao$observacoes else "")
      updateCheckboxInput(session, "inadequada", value = nrow(d$revisao) > 0 && d$revisao$inadequada == 1)
      salvo_em(if (nrow(d$revisao)) paste("Salvo em", format(as.POSIXct(d$revisao$atualizada_em), "%d/%m/%Y %H:%M")) else "")
    })

    rid <- function() {
      if (is.null(revisao_id())) revisao_id(obter_revisao(ctx$con, atual(), ctx$modelo()$id, ctx$revisor()))
      revisao_id()
    }
    marcar_salvo <- function() { salvo_em(paste("Salvo às", format(Sys.time(), "%H:%M:%S"))); ctx$dados_mudaram() }
    output$salvo <- renderText(salvo_em())

    # eventos do visor
    observeEvent(input$visor_evento, {
      ev <- input$visor_evento
      req(atual())
      switch(ev$tipo,
        decisao = { gravar_decisao(ctx$con, rid(), atual(), ctx$modelo()$id, ev$indice, ev$decisao); marcar_salvo() },
        adicionar = {
          pts <- do.call(rbind, lapply(ev$pontos, function(p) as.numeric(unlist(p))))
          lid <- adicionar_lesao(ctx$con, rid(), pts)
          session$sendCustomMessage("visor:adicionada", list(alvo = ns("visor"), k = ev$k, lesao_id = lid))
          registrar_evento(ctx$con, ctx$revisor(), "lesao_adicionada", atual()); marcar_salvo() },
        remover = { if (!is.null(ev$lesao_id)) remover_lesao(ctx$con, rid(), ev$lesao_id); marcar_salvo() },
        concluir = concluir(),
        navegar = navegar(ev$passo))
    })

    obs_lenta <- debounce(reactive(input$obs), 800)
    observeEvent(obs_lenta(), {
      req(atual(), !is.null(revisao_id()) || nzchar(obs_lenta()))
      r <- dados_imagem(ctx$con, atual(), ctx$modelo()$id, ctx$revisor())$revisao
      if (nrow(r) && identical(r$observacoes, obs_lenta())) return()
      gravar_campos_revisao(ctx$con, rid(), observacoes = obs_lenta())
      salvo_em(paste("Salvo às", format(Sys.time(), "%H:%M:%S")))
    }, ignoreInit = TRUE)
    observeEvent(input$inadequada, {
      req(atual())
      r <- dados_imagem(ctx$con, atual(), ctx$modelo()$id, ctx$revisor())$revisao
      if (!nrow(r) && !isTRUE(input$inadequada)) return()
      if (nrow(r) && (r$inadequada == 1) == isTRUE(input$inadequada)) return()
      gravar_campos_revisao(ctx$con, rid(), inadequada = isTRUE(input$inadequada)); marcar_salvo()
    }, ignoreInit = TRUE)

    navegar <- function(passo) {
      l <- lista_filtrada(); i <- match(atual(), l$id)
      if (is.na(i)) i <- 0
      j <- min(nrow(l), max(1, i + passo)); if (nrow(l)) atual(l$id[j])
    }
    observeEvent(input$proxima, navegar(1))
    observeEvent(input$anterior, navegar(-1))

    concluir <- function(confirmado = FALSE) {
      d <- dados_imagem(ctx$con, atual(), ctx$modelo()$id, ctx$revisor())
      falta <- nrow(d$deteccoes) - nrow(d$decisoes)
      inad <- isTRUE(input$inadequada)
      if (falta > 0 && !inad && !confirmado) {
        showModal(modalDialog(title = "Detecções sem decisão",
          sprintf("%d detecção(ões) desta imagem ainda não têm decisão. Concluir mesmo assim?", falta),
          footer = htmltools::tagList(modalButton("Voltar"), actionButton(ns("concluir_mesmo"), "Concluir", class = "btn-warning"))))
        return(invisible())
      }
      gravar_campos_revisao(ctx$con, rid(), concluida = TRUE)
      registrar_evento(ctx$con, ctx$revisor(), "revisao_concluida", atual())
      ctx$dados_mudaram()
      l <- isolate(lista())
      prox <- l[l$situacao != "concluida" & l$id != atual(), ]
      if (nrow(prox)) atual(prox$id[1]) else showNotification("Todas as imagens foram revisadas. Obrigado!", type = "message")
    }
    observeEvent(input$concluir, concluir())
    observeEvent(input$concluir_mesmo, { removeModal(); concluir(confirmado = TRUE) })
  })
}
