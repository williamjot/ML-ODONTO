# Configurações: revisor, preferências, dados e backup.

mod_config_ui <- function(id) {
  ns <- NS(id)
  bslib::layout_columns(
    col_widths = c(6, 6),
    bslib::card(
      bslib::card_header("Revisor e preferências"),
      textInput(ns("revisor"), "Nome do revisor", width = "100%"),
      checkboxInput(ns("cego"), "Abrir imagens ainda não revisadas com as marcações ocultas (modo cego)"),
      actionButton(ns("salvar"), "Salvar", class = "btn-primary", icon = icon("floppy-disk"))
    ),
    bslib::card(
      bslib::card_header("Dados"),
      htmltools::p(htmltools::strong("Pasta dos dados: "), textOutput(ns("pasta"), inline = TRUE)),
      htmltools::p(class = "small text-muted", "Banco, cópias reduzidas das imagens, relatórios e backups ficam nesta pasta.",
                   "Um backup do banco é feito automaticamente a cada dia (7 cópias)."),
      actionButton(ns("backup"), "Fazer backup agora", icon = icon("box-archive"), class = "btn-outline-primary"),
      htmltools::hr(),
      htmltools::p(htmltools::strong("Programa: "), NOME_APP, " ", VERSAO_APP),
      htmltools::p(class = "small text-muted", "Uso acadêmico e não comercial. O programa apoia a revisão por especialistas e não substitui o diagnóstico clínico.")
    )
  )
}

mod_config_server <- function(id, ctx) {
  moduleServer(id, function(input, output, session) {
    observe({
      updateTextInput(session, "revisor", value = ctx$revisor())
      updateCheckboxInput(session, "cego", value = identical(ler_config(ctx$con, "modo_cego_inicial", "FALSE"), "TRUE"))
    })
    output$pasta <- renderText(normalizePath(ctx$dados))
    observeEvent(input$salvar, {
      nome <- trimws(input$revisor)
      if (!nzchar(nome)) { showNotification("Informe o nome do revisor.", type = "warning"); return() }
      gravar_config(ctx$con, "revisor", nome); ctx$revisor(nome)
      gravar_config(ctx$con, "modo_cego_inicial", if (isTRUE(input$cego)) "TRUE" else "FALSE")
      registrar_evento(ctx$con, nome, "config_salva")
      showNotification("Configurações salvas.", type = "message")
    })
    observeEvent(input$backup, {
      destino <- file.path(ctx$dados, "backups", paste0("odontolesao_", format(Sys.time(), "%Y-%m-%d_%H%M%S"), ".sqlite"))
      DBI::dbExecute(ctx$con, sprintf("VACUUM INTO '%s'", gsub("'", "''", normalizePath(destino, mustWork = FALSE))))
      showNotification(paste("Backup salvo em", destino), type = "message", duration = 8)
    })
  })
}
