# Início: quatro painéis (fluxo, ambiente com os números atuais, atividade e progresso).

mod_inicio_ui <- function(id) {
  ns <- NS(id)
  ml_area(ml_coluna(
    tamanhos = c("58%", NA),
    ml_linha(
      tamanhos = c("62%", NA),
      painel("Fluxo de trabalho", icone = "diagram-3",
             ferramentas = actionButton(ns("ir_revisao"), "Continuar revisão", icon = icon("play"), class = "btn btn-sm btn-primary"),
             DiagrammeR::grVizOutput(ns("diagrama"), height = "100%"), corpo_estilo = "padding:4px"),
      painel_abas(ns("amb"),
        "Ambiente" = uiOutput(ns("ambiente")),
        "Modelo" = uiOutput(ns("modelo")))
    ),
    ml_linha(
      tamanhos = c("50%", NA),
      painel_abas(ns("cons"),
        "Atividade" = uiOutput(ns("atividade"), class = "ml-console"),
        "Sobre" = htmltools::div(class = "p-3 small",
          htmltools::p(htmltools::strong(NOME_APP), " reúne a revisão das detecções por especialistas, a auditoria das lesões",
                       "não encontradas, os resultados de validação e, nas próximas versões, a aplicação e o treinamento dos modelos."),
          htmltools::p(class = "text-muted mb-0", "Tudo roda neste computador. Nenhuma radiografia é enviada para a internet."))),
      painel_abas(ns("graf"),
        "Progresso" = plotOutput(ns("progresso"), height = "100%"),
        "Detecções por imagem" = plotOutput(ns("porimagem"), height = "100%"))
    )
  ))
}

linha_amb <- function(nome, valor) htmltools::tags$tr(htmltools::tags$td(nome), htmltools::tags$td(valor))

mod_inicio_server <- function(id, ctx) {
  moduleServer(id, function(input, output, session) {
    estat <- reactive({ ctx$versao(); estatisticas_revisao(ctx$con, ctx$modelo()$id, if (nzchar(ctx$revisor())) ctx$revisor()) })
    cont <- reactive({ ctx$versao(); contagens_gerais(ctx$con, ctx$modelo()$id) })

    output$diagrama <- DiagrammeR::renderGrViz(diagrama_fluxo(cont(), estat(), ctx$modelo()))

    output$ambiente <- renderUI({
      c <- cont(); e <- estat()
      ref <- DBI::dbGetQuery(ctx$con, "SELECT COUNT(*) AS l, COUNT(DISTINCT paciente) AS p FROM referencia")
      aud <- if (nzchar(ctx$revisor())) sensibilidade_auditada(ctx$con, "U-Net v2", ctx$revisor())
      fmt <- function(w) formatar_proporcao(w)
      htmltools::tags$table(class = "ml-ambiente",
        htmltools::tags$thead(htmltools::tags$tr(htmltools::tags$th(colspan = 2, "Dados"))),
        htmltools::tags$tbody(
          linha_amb("acervo", sprintf("%d panorâmicas", c$imagens)),
          linha_amb("deteccoes", sprintf("%s lesões em %d imagens", format(c$deteccoes, big.mark = ".", decimal.mark = ","), c$com_deteccao)),
          linha_amb("referencia", sprintf("%d lesões em %d pacientes", ref$l, ref$p)),
          linha_amb("revisores", c$revisores)),
        htmltools::tags$thead(htmltools::tags$tr(htmltools::tags$th(colspan = 2, "Revisão"))),
        htmltools::tags$tbody(
          linha_amb("amostra_revisada", sprintf("%d / %d", c$amostra_concluidas, c$amostra)),
          linha_amb("precisao_amostra", fmt(e$amostra$precisao)),
          linha_amb("precisao_todas", fmt(e$todas$precisao)),
          linha_amb("decisoes", sprintf("%d confirmadas · %d rejeitadas · %d dúvidas", e$todas$confirmadas, e$todas$rejeitadas, e$todas$duvidas)),
          linha_amb("lesoes_adicionadas", e$todas$adicionadas)),
        if (!is.null(aud)) htmltools::tagList(
          htmltools::tags$thead(htmltools::tags$tr(htmltools::tags$th(colspan = 2, "Validação (U-Net v2)"))),
          htmltools::tags$tbody(
            linha_amb("sensibilidade", fmt(aud$original)),
            linha_amb("sens_corrigida", sprintf("%s · auditoria %d/%d", fmt(aud$corrigida), aud$respondidas, aud$perdidas)))))
    })

    output$modelo <- renderUI({
      m <- ctx$modelo()
      htmltools::tags$table(class = "ml-ambiente",
        htmltools::tags$tbody(
          linha_amb("nome", m$nome), linha_amb("descricao", m$descricao), linha_amb("tipo", m$tipo),
          linha_amb("largura", paste(m$largura, "px")), linha_amb("limiar", virgula(m$limiar)),
          linha_amb("pesos", m$arquivo), linha_amb("registrado_em", m$criado_em)))
    })

    output$atividade <- renderUI({
      ctx$versao()
      ev <- DBI::dbGetQuery(ctx$con, "SELECT em, revisor, tipo, detalhe FROM eventos ORDER BY id DESC LIMIT 60")
      if (!nrow(ev)) return(htmltools::span(class = "t", "> nenhuma atividade registrada ainda"))
      htmltools::tagList(lapply(seq_len(nrow(ev)), function(i) htmltools::div(
        htmltools::span(class = "p", "> "), htmltools::span(class = "t", substr(ev$em[i], 6, 19), " "),
        htmltools::span(class = "r", ev$revisor[i] %||% ""), " ",
        htmltools::span(class = if (grepl("concluida", ev$tipo[i])) "ok", ev$tipo[i]), " ", ev$detalhe[i])))
    })

    output$progresso <- renderPlot(grafico_progresso(progresso_diario(ctx$con, ctx$modelo()$id)), res = 100, bg = "white")
    output$porimagem <- renderPlot({ ctx$versao(); grafico_deteccoes_imagem(deteccoes_por_imagem(ctx$con, ctx$modelo()$id)) }, res = 100, bg = "white")
    observeEvent(input$ir_revisao, ctx$ir_para("Revisão"))
  })
}
