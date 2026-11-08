#' Pasta padrão dos dados do usuário (Documentos/OdontoLesao)
caminho_dados_padrao <- function() {
  docs <- file.path(Sys.getenv("USERPROFILE", path.expand("~")), "Documents")
  if (!dir.exists(docs)) docs <- path.expand("~")
  file.path(docs, "OdontoLesao")
}

#' Abre o ML ODONTO
#'
#' @param dados pasta dos dados do usuário (banco, cache, relatórios, backups)
#' @param projeto pasta do projeto ML Odonto, usada na primeira importação
#' @param porta porta local (NULL = escolhida automaticamente)
#' @param abrir abre o navegador
#' @export
run_app <- function(dados = caminho_dados_padrao(),
                    projeto = getOption("odontolesao.projeto", "D:/WILLIAM/ELDER/ML Odonto"),
                    porta = NULL, abrir = TRUE) {
  for (sub in c("", "cache", "backups", "relatorios", "logs")) dir.create(file.path(dados, sub), showWarnings = FALSE, recursive = TRUE)
  arq_banco <- file.path(dados, "odontolesao.sqlite")
  con <- abrir_banco(arq_banco)
  fazer_backup(con, file.path(dados, "backups"))
  if (DBI::dbGetQuery(con, "SELECT COUNT(*) AS n FROM imagens")$n == 0 && dir.exists(projeto)) {
    message("Primeira execução: importando os dados do projeto (pode levar alguns minutos)...")
    importar_projeto(con, projeto, progresso = function(v, t) message(t))
  }
  if (DBI::dbGetQuery(con, "SELECT COUNT(*) AS n FROM referencia")$n == 0 && dir.exists(projeto)) {
    message("Importando as lesões de referência (20 panorâmicas marcadas)...")
    importar_referencia(con, projeto)
  }
  shiny::addResourcePath("odonto", system.file("app", "www", package = "odontolesao"))
  shiny::addResourcePath("cache", file.path(dados, "cache"))
  shiny::addResourcePath("manuais", system.file("manuais", package = "odontolesao"))
  app <- shiny::shinyApp(
    ui = app_ui,
    server = function(input, output, session) app_server(input, output, session, con = con, dados = dados),
    onStart = function() shiny::onStop(function() DBI::dbDisconnect(con))
  )
  shiny::runApp(app, host = "127.0.0.1", port = porta %||% httpuv::randomPort(), launch.browser = abrir)
}

#' Uma cópia do banco por dia (odontolesao_AAAA-MM-DD.sqlite), mantendo as 7 mais recentes
#'
#' Usa VACUUM INTO no banco aberto: copiar o arquivo .sqlite não basta, porque no modo WAL as gravações
#' recentes ficam no arquivo -wal e a cópia sairia vazia ou desatualizada. As cópias feitas à mão ficam.
fazer_backup <- function(con, pasta) {
  destino <- file.path(pasta, paste0("odontolesao_", format(Sys.Date()), ".sqlite"))
  if (!file.exists(destino) || file.size(destino) < 16 * 1024) {      # refaz a de hoje se saiu vazia
    if (file.exists(destino)) file.remove(destino)
    DBI::dbExecute(con, sprintf("VACUUM INTO '%s'", gsub("'", "''", normalizePath(destino, mustWork = FALSE))))
  }
  diarias <- sort(list.files(pasta, "^odontolesao_[0-9]{4}-[0-9]{2}-[0-9]{2}[.]sqlite$", full.names = TRUE), decreasing = TRUE)
  if (length(diarias) > 7) file.remove(diarias[-(1:7)])
  invisible(destino)
}
