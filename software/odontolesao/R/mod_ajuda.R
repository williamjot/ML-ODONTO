# Ajuda: manuais em Markdown (inst/manuais), mostrados como página.

MANUAIS <- c("Manual do dentista" = "manual_dentista.md")

mod_ajuda_ui <- function(id) {
  ns <- NS(id)
  bslib::layout_sidebar(
    sidebar = bslib::sidebar(
      width = 250, title = "Ajuda",
      radioButtons(ns("manual"), NULL, choices = MANUAIS),
      downloadButton(ns("baixar"), "Baixar manual (HTML)", class = "btn-sm btn-outline-secondary"),
      htmltools::hr(),
      htmltools::h6("Atalhos da revisão"),
      htmltools::tags$table(class = "table table-sm small",
        htmltools::tags$tbody(lapply(list(c("1 / 2 / 3", "confirma / rejeita / dúvida"), c("Tab", "próxima detecção"),
                                          c("M", "desenhar lesão"), c("H", "ocultar marcações"), c("0", "ajustar à tela"),
                                          c("Enter", "concluir imagem"), c("← / →", "imagem anterior / próxima")),
                                     function(x) htmltools::tags$tr(htmltools::tags$td(htmltools::tags$kbd(x[1])), htmltools::tags$td(x[2])))))
    ),
    uiOutput(ns("conteudo"), class = "ml-manual")
  )
}

manual_html <- function(arquivo) {
  md <- paste(readLines(system.file("manuais", arquivo, package = "odontolesao"), encoding = "UTF-8"), collapse = "\n")
  commonmark::markdown_html(md, extensions = TRUE)
}

mod_ajuda_server <- function(id, ctx) {
  moduleServer(id, function(input, output, session) {
    output$conteudo <- renderUI(htmltools::HTML(manual_html(input$manual)))
    output$baixar <- downloadHandler(
      filename = function() "Manual_do_dentista_ML_ODONTO.html",
      content = function(f) writeLines(c("<!doctype html><html lang='pt-BR'><head><meta charset='utf-8'><title>Manual do dentista — ML ODONTO</title>",
        "<style>body{font-family:Segoe UI,Arial,sans-serif;max-width:860px;margin:30px auto;padding:0 16px;line-height:1.55;color:#1f2a37}",
        "table{border-collapse:collapse;width:100%}td,th{border:1px solid #c9d1db;padding:5px 8px;text-align:left}th{background:#eef1f5}",
        "blockquote{border-left:4px solid #2b6cb0;background:#eef4fa;margin:12px 0;padding:8px 14px}code{background:#eef1f5;padding:0 4px}</style></head><body>",
        manual_html(input$manual), "</body></html>"), f, useBytes = TRUE))
  })
}
