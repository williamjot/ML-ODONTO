# Aplicar o modelo a novas radiografias, em processo separado, com andamento na tela.

mod_aplicar_ui <- function(id) {
  ns <- NS(id)
  ml_area(ml_linha(
    tamanhos = c("380px", NA),
    painel("Novas radiografias", icone = "folder-plus",
      htmltools::p(class = "small text-muted", "Indique uma pasta com radiografias (JPG, PNG ou TIFF) ou selecione os arquivos.",
                   "As imagens não saem deste computador."),
      textInput(ns("pasta"), "Pasta", placeholder = "Ex.: D:\\Radiografias\\2026-11", width = "100%"),
      actionButton(ns("listar"), "Procurar imagens na pasta", icon = icon("magnifying-glass"), class = "btn-outline-primary btn-sm"),
      htmltools::div(class = "my-2 text-muted small text-center", "ou"),
      fileInput(ns("arquivos"), NULL, multiple = TRUE, accept = c(".jpg", ".jpeg", ".png", ".tif", ".tiff", ".bmp"),
                buttonLabel = "Selecionar arquivos…", placeholder = "nenhum arquivo", width = "100%"),
      uiOutput(ns("encontradas")),
      htmltools::hr(),
      sliderInput(ns("limiar"), "Limiar de confiança", min = 0.05, max = 0.95, value = 0.5, step = 0.05, width = "100%"),
      htmltools::p(class = "small text-muted mt-n2", "0,5 é o limiar de operação validado (69% das lesões, sem falsos positivos nas 20 panorâmicas marcadas).",
                   "Valores menores encontram mais lesões e mais alarmes falsos."),
      checkboxInput(ns("pular"), "Pular radiografias que já estão no banco", value = TRUE),
      htmltools::div(class = "d-flex gap-2",
        actionButton(ns("aplicar"), "Aplicar o modelo", icon = icon("play"), class = "btn-primary"),
        actionButton(ns("cancelar"), "Cancelar", icon = icon("stop"), class = "btn-outline-danger"))),
    ml_coluna(
      tamanhos = c("190px", NA),
      painel("Andamento", icone = "hourglass-split", uiOutput(ns("andamento"))),
      painel("Radiografias processadas", icone = "table",
             ferramentas = htmltools::tagList(
               downloadButton(ns("baixar_csv"), "CSV", class = "btn-sm btn-outline-secondary"),
               actionButton(ns("revisar"), "Revisar selecionada", icon = icon("square-check"), class = "btn-sm btn-primary")),
             DT::DTOutput(ns("tabela"))))
  ))
}

mod_aplicar_server <- function(id, ctx) {
  moduleServer(id, function(input, output, session) {
    fila <- reactiveVal(character())
    proc <- reactiveVal(NULL)
    arq_prog <- file.path(ctx$dados, "logs", "aplicacao_progresso.json")
    inicio <- reactiveVal(NULL)

    observe(updateSliderInput(session, "limiar", value = ctx$modelo()$limiar %||% 0.5))

    observeEvent(input$listar, {
      p <- trimws(gsub('"', "", input$pasta))
      if (!dir.exists(p)) { showNotification("Pasta não encontrada.", type = "error"); return() }
      a <- list.files(p, pattern = "\\.(jpe?g|png|tiff?|bmp)$", ignore.case = TRUE, full.names = TRUE)
      fila(a)
    })
    observeEvent(input$arquivos, {
      destino <- file.path(ctx$dados, "radiografias_importadas", format(Sys.Date()))
      dir.create(destino, showWarnings = FALSE, recursive = TRUE)
      novos <- file.path(destino, input$arquivos$name)
      file.copy(input$arquivos$datapath, novos, overwrite = TRUE)
      fila(novos)
    })
    output$encontradas <- renderUI({
      n <- length(fila())
      if (!n) return(NULL)
      htmltools::div(class = "alert alert-info py-1 px-2 small mt-2 mb-0",
        sprintf("%d radiografia(s) prontas. Tempo estimado: cerca de %s.", n, formatar_duracao(n * 27)))
    })

    observeEvent(input$aplicar, {
      if (!length(fila())) { showNotification("Indique uma pasta ou selecione arquivos primeiro.", type = "warning"); return() }
      if (!is.null(proc()) && proc()$is_alive()) { showNotification("Já há uma aplicação em andamento.", type = "warning"); return() }
      unlink(arq_prog)
      p <- callr::r_bg(function(arquivos, banco, saida, modelo, limiar, prog, pular)
                         odontolesao:::processar_lote(arquivos, banco, saida, modelo, limiar, prog, pular),
                       args = list(arquivos = fila(), banco = file.path(ctx$dados, "odontolesao.sqlite"),
                                   saida = file.path(ctx$dados, "aplicacao"), modelo = ctx$modelo()$nome,
                                   limiar = input$limiar, prog = arq_prog, pular = isTRUE(input$pular)),
                       supervise = TRUE, stderr = file.path(ctx$dados, "logs", "aplicacao_erros.log"))
      proc(p); inicio(Sys.time())
      registrar_evento(ctx$con, ctx$revisor(), "aplicacao_iniciada", sprintf("%d radiografias", length(fila())))
    })
    observeEvent(input$cancelar, {
      if (!is.null(proc()) && proc()$is_alive()) { proc()$kill(); showNotification("Aplicação cancelada. As radiografias já processadas foram mantidas.", type = "warning") }
      ctx$dados_mudaram()
    })

    estado <- reactive({
      invalidateLater(1500)
      if (!file.exists(arq_prog)) return(NULL)
      tryCatch(jsonlite::read_json(arq_prog), error = function(e) NULL)
    })
    # ao terminar, atualiza a tela e a fila de revisão
    observe({
      e <- estado()
      if (!is.null(e) && identical(e$estado, "concluido") && !is.null(isolate(proc()))) {
        isolate({ proc(NULL); ctx$dados_mudaram() })
        showNotification(sprintf("Aplicação concluída: %d radiografia(s). As detecções já estão na fila de revisão.", e$total), type = "message", duration = 8)
      }
    })

    output$andamento <- renderUI({
      e <- estado(); vivo <- !is.null(proc()) && proc()$is_alive()
      if (is.null(e)) return(htmltools::p(class = "text-muted", "Nenhuma aplicação em andamento."))
      frac <- if (e$total > 0) e$feitas / e$total else 0
      decorrido <- if (!is.null(inicio())) as.numeric(difftime(Sys.time(), inicio(), units = "secs")) else NA
      resta <- if (!is.na(decorrido) && e$feitas > 0) decorrido / e$feitas * (e$total - e$feitas) else NA
      htmltools::tagList(
        htmltools::div(class = "d-flex justify-content-between small",
          htmltools::span(htmltools::strong(sprintf("%d de %d", e$feitas, e$total)), " · ", e$estado,
                          if (nzchar(e$atual %||% "")) paste0(" · ", e$atual)),
          htmltools::span(if (!is.na(resta) && vivo) paste("faltam cerca de", formatar_duracao(resta)))),
        htmltools::div(class = "progress my-2", style = "height:10px",
          htmltools::div(class = paste("progress-bar", if (vivo) "progress-bar-striped progress-bar-animated"), style = sprintf("width:%.0f%%", 100 * frac))),
        if (!vivo && !identical(e$estado, "concluido")) htmltools::p(class = "small text-danger", "O processo terminou antes do fim (cancelado ou com erro). Veja logs/aplicacao_erros.log."),
        if (length(e$erros)) htmltools::div(class = "small text-warning", htmltools::strong("Avisos: "), paste(unlist(e$erros), collapse = " · ")))
    })

    aplicacoes <- reactive({ ctx$versao(); invalidateLater(if (!is.null(proc())) 5000 else 600000); lista_aplicacoes(ctx$con, ctx$modelo()$id) })
    output$tabela <- DT::renderDT({
      a <- aplicacoes()
      a$conf_max <- ifelse(is.na(a$conf_max), "—", virgula(a$conf_max))
      a$limiar <- virgula(a$limiar); a$segundos <- virgula(a$segundos, 1)
      names(a) <- c("ID", "Arquivo", "Lesões", "Segundos", "Limiar", "Processada em", "Confiança máx.")
      DT::datatable(a, rownames = FALSE, selection = "single", class = "compact", fillContainer = FALSE,
                    options = list(pageLength = 15, dom = "ftip", language = list(search = "Buscar:", info = "_START_–_END_ de _TOTAL_",
                                   paginate = list(previous = "Anterior", `next` = "Próxima"), emptyTable = "Nenhuma radiografia processada nesta tela ainda.")))
    }, server = FALSE)
    observeEvent(input$revisar, {
      s <- input$tabela_rows_selected
      if (!length(s)) { showNotification("Selecione uma linha da tabela.", type = "warning"); return() }
      ctx$abrir_na_revisao(aplicacoes()$id[s])
    })
    output$baixar_csv <- downloadHandler(
      filename = function() sprintf("ml_odonto_aplicacoes_%s.csv", format(Sys.Date())),
      content = function(f) {
        d <- DBI::dbGetQuery(ctx$con, "SELECT d.imagem_id, i.arquivo, d.indice AS lesao, ROUND(d.cx) AS x, ROUND(d.cy) AS y, ROUND(d.area) AS area_px,
                                       ROUND(d.confianca, 3) AS confianca FROM deteccoes d JOIN imagens i ON i.id = d.imagem_id
                                       WHERE d.modelo_id = ? AND d.imagem_id IN (SELECT imagem_id FROM aplicacoes) ORDER BY d.imagem_id, d.indice",
                             params = list(ctx$modelo()$id))
        utils::write.csv2(d, f, row.names = FALSE, fileEncoding = "UTF-8")
      })
  })
}

formatar_duracao <- function(seg) {
  if (is.na(seg)) return("—")
  if (seg < 90) return(sprintf("%d s", round(seg)))
  if (seg < 5400) return(sprintf("%d min", round(seg / 60)))
  sprintf("%.1f h", seg / 3600)
}
