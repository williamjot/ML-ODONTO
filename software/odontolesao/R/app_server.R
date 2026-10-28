app_server <- function(input, output, session, con, dados) {
  ctx <- list(
    con = con,
    dados = dados,
    versao = reactiveVal(0),                         # muda a cada gravação: atualiza painéis e gráficos
    revisor = reactiveVal(ler_config(con, "revisor", "")),
    modelo = reactiveVal(modelo_ativo(con)),
    ultima_gravacao = reactiveVal(NULL)
  )
  ctx$dados_mudaram <- function() { ctx$ultima_gravacao(Sys.time()); ctx$versao(ctx$versao() + 1) }
  ctx$ir_para <- function(aba) bslib::nav_select("aba", aba, session = session)
  ctx$pedido_revisao <- reactiveVal(NULL)
  ctx$abrir_na_revisao <- function(id) { ctx$pedido_revisao(list(id = id, t = Sys.time())); ctx$ir_para("Revisão") }

  mod_inicio_server("inicio", ctx)
  mod_revisao_server("revisao", ctx)
  mod_auditoria_server("auditoria", ctx)
  mod_resultados_server("resultados", ctx)
  mod_aplicar_server("aplicar", ctx)
  mod_ajuda_server("ajuda", ctx)
  mod_config_server("config", ctx)

  # barra de ferramentas
  observeEvent(input$bt_revisar, ctx$ir_para("Revisão"))
  observeEvent(input$bt_auditar, ctx$ir_para("Auditoria"))
  observeEvent(input$bt_resultados, ctx$ir_para("Resultados"))
  observeEvent(input$bt_ajuda, ctx$ir_para("Ajuda"))
  observeEvent(input$bt_atualizar, ctx$versao(ctx$versao() + 1))
  observeEvent(input$bt_backup, {
    destino <- file.path(dados, "backups", paste0("odontolesao_", format(Sys.time(), "%Y-%m-%d_%H%M%S"), ".sqlite"))
    DBI::dbExecute(con, sprintf("VACUUM INTO '%s'", gsub("'", "''", normalizePath(destino, mustWork = FALSE))))
    showNotification(paste("Backup salvo em", destino), type = "message", duration = 6)
  })
  output$toolbar_direita <- renderUI({
    htmltools::tagList(
      htmltools::span(class = "pilula", bsicons::bs_icon("person"), " ", if (nzchar(ctx$revisor())) ctx$revisor() else "revisor não definido"),
      htmltools::span(class = "pilula", bsicons::bs_icon("cpu"), " ", sub(" [(].*", "", ctx$modelo()$descricao)))
  })

  # barra de status
  output$barra_status <- renderUI({
    ctx$versao()
    c <- contagens_gerais(con, ctx$modelo()$id)
    ult <- ctx$ultima_gravacao()
    htmltools::tagList(
      htmltools::span(class = "item", htmltools::span(class = "ponto"), "Pronto"),
      htmltools::span(class = "item", bsicons::bs_icon("shuffle"), sprintf("Amostra %d/%d", c$amostra_concluidas, c$amostra)),
      htmltools::span(class = "item", bsicons::bs_icon("images"), sprintf("%d de %d revisadas", c$concluidas, c$imagens)),
      htmltools::span(class = "item", bsicons::bs_icon("clock-history"),
                      if (is.null(ult)) "nada gravado nesta sessão" else paste("gravado às", format(ult, "%H:%M:%S"))),
      htmltools::span(class = "item direita", bsicons::bs_icon("database"), normalizePath(file.path(dados, "odontolesao.sqlite")),
                      htmltools::span(style = "margin-left:14px", paste(NOME_APP, VERSAO_APP))))
  })

  # na primeira vez, pede o nome do revisor
  if (!nzchar(isolate(ctx$revisor()))) pedir_revisor(ctx, session)
}

pedir_revisor <- function(ctx, session) {
  showModal(modalDialog(
    title = paste("Bem-vindo ao", NOME_APP),
    htmltools::p("Informe seu nome. Ele identifica as suas revisões; cada revisor tem as próprias respostas."),
    textInput("nome_revisor_inicial", "Nome do revisor", width = "100%"),
    footer = actionButton("salvar_revisor_inicial", "Começar", class = "btn-primary"),
    easyClose = FALSE
  ))
  observeEvent(session$input$salvar_revisor_inicial, {
    nome <- trimws(session$input$nome_revisor_inicial)
    if (!nzchar(nome)) return()
    gravar_config(ctx$con, "revisor", nome)
    ctx$revisor(nome)
    registrar_evento(ctx$con, nome, "revisor_definido")
    removeModal()
  }, ignoreInit = TRUE, once = TRUE)
}
