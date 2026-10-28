# Aplicação do modelo a novas radiografias (roda em processo separado, ver mod_aplicar.R).

#' Lesões (componentes da máscara) com contorno em pixels da imagem original
lesoes_da_mascara <- function(mascara, deteccoes, limiar, fator) {
  rot <- rotular8(mascara)
  n <- max(rot)
  if (n == 0) return(list())
  conts <- EBImage::ocontour(t(rot))                  # EBImage indexa [x, y]
  idx <- which(rot > 0, arr.ind = TRUE); lab <- rot[idx]
  ordem <- unique(lab[order(idx[, 1], idx[, 2])])      # ordem de varredura por linhas (como o OpenCV)
  picos <- Filter(function(d) d$confianca >= limiar, deteccoes)
  lapply(seq_along(ordem), function(k) {
    r <- ordem[k]
    px <- idx[lab == r, , drop = FALSE]
    conf <- max(c(0, vapply(picos, function(d) if (rot[d$y, d$x] == r) d$confianca else 0, 0)))
    cont <- conts[[as.character(r)]]
    if (nrow(cont) > 6) cont <- cont[seq(1, nrow(cont), by = 2), , drop = FALSE]
    list(indice = k, contorno = round((cont - 1) * fator), cx = (mean(px[, 2]) - 1) * fator, cy = (mean(px[, 1]) - 1) * fator,
         area = nrow(px) * fator^2, confianca = conf)
  })
}

#' Processa uma lista de radiografias e grava imagens e detecções no banco
#'
#' Escreve o andamento em `arq_progresso` (JSON) a cada imagem, para a tela acompanhar.
processar_lote <- function(arquivos, arq_banco, pasta_saida, modelo_nome = "unet_lesoes_v2", limiar = 0.5,
                           arq_progresso = tempfile(fileext = ".json"), pular_existentes = TRUE) {
  con <- abrir_banco(arq_banco); on.exit(DBI::dbDisconnect(con), add = TRUE)
  mod <- DBI::dbGetQuery(con, "SELECT * FROM modelos WHERE nome = ?", params = list(modelo_nome))
  dir.create(pasta_saida, showWarnings = FALSE, recursive = TRUE)
  progresso <- function(i, estado, atual = "", erros = character()) jsonlite::write_json(
    list(total = length(arquivos), feitas = i, estado = estado, atual = atual, erros = erros,
         em = format(Sys.time(), "%H:%M:%S")), arq_progresso, auto_unbox = TRUE)
  progresso(0, "carregando o modelo")
  modelo <- carregar_unet(caminho_pesos(mod$arquivo))
  erros <- character(); t0 <- Sys.time()
  for (i in seq_along(arquivos)) {
    arq <- normalizePath(arquivos[i], winslash = "/")
    progresso(i - 1, "processando", basename(arq), erros)
    existe <- DBI::dbGetQuery(con, "SELECT id FROM imagens WHERE caminho = ?", params = list(normalizePath(arq)))
    if (pular_existentes && nrow(existe)) next          # já está no banco (importada ou aplicada antes)
    if (nrow(existe) && DBI::dbGetQuery(con, "SELECT COUNT(*) AS n FROM revisoes WHERE imagem_id = ? AND modelo_id = ?",
                                        params = list(existe$id, mod$id))$n > 0) {
      erros[length(erros) + 1] <- sprintf("%s: já revisada; detecções mantidas", basename(arq)); next
    }
    res <- tryCatch({
      original <- ler_radiografia(arq)
      p <- prever_radiografia(modelo, original, mod$largura %||% 2000)
      dets <- detectar_lesoes(p)
      m <- mascara_lesoes(dets, limiar, dim(p))
      lesoes <- lesoes_da_mascara(m, dets, limiar, ncol(original) / ncol(p))
      DBI::dbWithTransaction(con, {
        id <- if (nrow(existe)) existe$id else proximo_id_imagem(con)
        if (!nrow(existe)) DBI::dbExecute(con, "INSERT INTO imagens (id, arquivo, caminho, largura, altura, amostra, importada_em) VALUES (?, ?, ?, ?, ?, 0, ?)",
                                          params = list(id, basename(arq), normalizePath(arq), ncol(original), nrow(original), agora()))
        DBI::dbExecute(con, "DELETE FROM deteccoes WHERE imagem_id = ? AND modelo_id = ?", params = list(id, mod$id))
        for (l in lesoes) DBI::dbExecute(con, "INSERT INTO deteccoes (imagem_id, modelo_id, indice, contorno, cx, cy, area, confianca)
                                               VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                                         params = list(id, mod$id, l$indice, as.character(jsonlite::toJSON(unname(l$contorno))), l$cx, l$cy, l$area, l$confianca))
        DBI::dbExecute(con, "INSERT INTO aplicacoes (imagem_id, modelo_id, limiar, n_lesoes, segundos, em) VALUES (?, ?, ?, ?, ?, ?)",
                       params = list(id, mod$id, limiar, length(lesoes), as.numeric(difftime(Sys.time(), t0, units = "secs")), agora()))
      })
      EBImage::writeImage(EBImage::Image(t(m) * 1), file.path(pasta_saida, paste0(id, "_mascara.png")))
      TRUE
    }, error = function(e) { erros[length(erros) + 1] <<- sprintf("%s: %s", basename(arq), conditionMessage(e)); FALSE })
    t0 <- Sys.time()
  }
  registrar_evento(con, "", "aplicacao", sprintf("%d radiografias, modelo %s, limiar %s", length(arquivos), modelo_nome, limiar))
  progresso(length(arquivos), "concluido", "", erros)
  invisible(TRUE)
}

proximo_id_imagem <- function(con) {
  ids <- DBI::dbGetQuery(con, "SELECT id FROM imagens")$id
  n <- suppressWarnings(max(c(0, as.integer(sub("^A", "", ids))), na.rm = TRUE))
  sprintf("A%03d", n + 1)
}

#' Resumo das aplicações (para a tabela da tela)
lista_aplicacoes <- function(con, modelo_id) {
  DBI::dbGetQuery(con, "SELECT a.imagem_id AS id, i.arquivo, a.n_lesoes, ROUND(a.segundos, 1) AS segundos, a.limiar, a.em,
                               (SELECT MAX(confianca) FROM deteccoes d WHERE d.imagem_id = a.imagem_id AND d.modelo_id = a.modelo_id) AS conf_max
                        FROM aplicacoes a JOIN imagens i ON i.id = a.imagem_id WHERE a.modelo_id = ? ORDER BY a.id DESC",
                  params = list(modelo_id))
}
