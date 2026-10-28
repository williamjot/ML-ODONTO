# Importa para o banco os dados que hoje estão nas pastas do projeto ML Odonto.

EXTENSOES <- c("jpg", "jpeg", "png", "tif", "tiff", "bmp")

#' Importa imagens, modelos, detecções e revisões do projeto
#'
#' @param con conexão do banco
#' @param projeto pasta do projeto ML Odonto (com Dados/, Resultados/, Revisao/)
#' @param progresso função(valor 0-1, texto) para informar o andamento
#' @export
importar_projeto <- function(con, projeto, progresso = function(v, t) NULL) {
  pasta_img <- file.path(projeto, "Dados", "Aplicacao")
  pasta_res <- file.path(projeto, "Resultados", "aplicacao_unet_v2")
  stopifnot(dir.exists(pasta_img), dir.exists(pasta_res))

  # modelos (registro em inst/modelos/modelos.yml)
  reg <- yaml::read_yaml(system.file("modelos", "modelos.yml", package = "odontolesao"))
  for (nome in names(reg)) {
    m <- reg[[nome]]
    DBI::dbExecute(con, "INSERT OR IGNORE INTO modelos (nome, arquivo, tipo, largura, limiar, descricao, metricas, ativo, criado_em)
                         VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
                   params = list(nome, m$arquivo, m$tipo, m$largura %||% NA, m$limiar %||% NA, m$descricao %||% "",
                                 jsonlite::toJSON(m$meta, auto_unbox = TRUE), as.integer(isTRUE(m$ativo)), agora()))
  }
  mod <- modelo_ativo(con)

  # imagens: id anônimo pela ordem alfabética do nome do arquivo (como em exportar_figuras.py)
  arqs <- list.files(pasta_img)
  arqs <- arqs[tolower(tools::file_ext(arqs)) %in% EXTENSOES]
  arqs <- arqs[order(arqs, method = "radix")]
  ids <- sprintf("A%03d", seq_along(arqs))
  amostra <- readLines(system.file("extdata", "amostra_aleatoria.txt", package = "odontolesao"))
  det_csv <- utils::read.csv2(file.path(pasta_res, "lesoes.csv"), fileEncoding = "UTF-8-BOM", check.names = FALSE)

  DBI::dbWithTransaction(con, {
    for (k in seq_along(arqs)) {
      if (k %% 20 == 0) progresso(k / length(arqs), sprintf("Importando %d de %d imagens", k, length(arqs)))
      comps <- componentes_mascara(file.path(pasta_res, "mascaras", paste0(tools::file_path_sans_ext(arqs[k]), ".png")))
      dims <- attr(comps, "dims")
      DBI::dbExecute(con, "INSERT OR IGNORE INTO imagens (id, arquivo, caminho, largura, altura, amostra, importada_em)
                           VALUES (?, ?, ?, ?, ?, ?, ?)",
                     params = list(ids[k], arqs[k], normalizePath(file.path(pasta_img, arqs[k])), dims[1], dims[2],
                                   as.integer(ids[k] %in% amostra), agora()))
      conf_img <- det_csv[det_csv$arquivo == arqs[k], ]
      for (cp in comps) {
        conf <- if (nrow(conf_img)) {
          j <- which.min((conf_img$x_centro - cp$cx)^2 + (conf_img$y_centro - cp$cy)^2)
          as.numeric(sub(",", ".", conf_img$confianca[j]))
        } else NA_real_
        DBI::dbExecute(con, "INSERT OR IGNORE INTO deteccoes (imagem_id, modelo_id, indice, contorno, cx, cy, area, confianca)
                             VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                       params = list(ids[k], mod$id, cp$indice, as.character(jsonlite::toJSON(unname(cp$contorno))),
                                     cp$cx, cp$cy, cp$area, conf))
      }
    }
  })
  importar_revisoes_json(con, file.path(projeto, "Revisao", "revisoes"), mod$id)
  gravar_config(con, "projeto", normalizePath(projeto))
  registrar_evento(con, "", "importacao", sprintf("%d imagens de %s", length(arqs), projeto))
  progresso(1, "Importação concluída")
  invisible(length(arqs))
}

#' Revisões feitas na ferramenta anterior (Revisao/revisoes/A###.json)
importar_revisoes_json <- function(con, pasta, modelo_id) {
  arqs <- list.files(pasta, "^A\\d{3}\\.json$", full.names = TRUE)
  for (a in arqs) {
    r <- jsonlite::fromJSON(a, simplifyVector = FALSE)
    revisor <- r$revisor %||% "importado"
    rid <- obter_revisao(con, r$id, modelo_id, revisor)
    for (ind in names(r$decisoes)) gravar_decisao(con, rid, r$id, modelo_id, as.integer(ind), r$decisoes[[ind]])
    for (p in r$adicionadas) adicionar_lesao(con, rid, do.call(rbind, lapply(p, unlist)))
    gravar_campos_revisao(con, rid, inadequada = isTRUE(r$imagem_inadequada), observacoes = r$observacoes %||% "",
                          concluida = if (isTRUE(r$concluida)) TRUE else NULL)
  }
  invisible(length(arqs))
}
