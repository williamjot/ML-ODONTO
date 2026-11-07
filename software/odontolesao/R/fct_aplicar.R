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
#' `motor`: "cpp" (executável C++/ONNX, bem mais rápido), "r" (R torch) ou NULL para escolher sozinho.
processar_lote <- function(arquivos, arq_banco, pasta_saida, modelo_nome = "unet_lesoes_v2", limiar = 0.5,
                           arq_progresso = tempfile(fileext = ".json"), pular_existentes = TRUE, motor = NULL) {
  con <- abrir_banco(arq_banco); on.exit(DBI::dbDisconnect(con), add = TRUE)
  mod <- DBI::dbGetQuery(con, "SELECT * FROM modelos WHERE nome = ?", params = list(modelo_nome))
  motor <- motor %||% motor_inferencia(mod$arquivo)
  dir.create(pasta_saida, showWarnings = FALSE, recursive = TRUE)
  erros <- character(); feitas <- 0L
  progresso <- function(estado, atual = "") jsonlite::write_json(
    list(total = length(arquivos), feitas = feitas, estado = estado, atual = atual, erros = erros, motor = motor,
         em = format(Sys.time(), "%H:%M:%S")), arq_progresso, auto_unbox = TRUE)

  # o que processar: pula as que já estão no banco e protege as já revisadas
  arquivos <- normalizePath(arquivos, winslash = "/")
  existe <- lapply(arquivos, function(a) DBI::dbGetQuery(con, "SELECT id FROM imagens WHERE caminho = ?", params = list(normalizePath(a))))
  pendente <- vapply(seq_along(arquivos), function(i) {
    if (!nrow(existe[[i]])) return(TRUE)
    if (pular_existentes) return(FALSE)
    revisada <- DBI::dbGetQuery(con, "SELECT COUNT(*) AS n FROM revisoes WHERE imagem_id = ? AND modelo_id = ?",
                                params = list(existe[[i]]$id, mod$id))$n > 0
    if (revisada) erros[length(erros) + 1] <<- sprintf("%s: já revisada; detecções mantidas", basename(arquivos[i]))
    !revisada
  }, TRUE)
  pend <- which(pendente)
  feitas <- length(arquivos) - length(pend)

  gravar <- function(i, p, altura, largura, segundos) {
    dets <- detectar_lesoes(p)
    m <- mascara_lesoes(dets, limiar, dim(p))
    lesoes <- lesoes_da_mascara(m, dets, limiar, largura / ncol(p))
    DBI::dbWithTransaction(con, {
      id <- if (nrow(existe[[i]])) existe[[i]]$id else proximo_id_imagem(con)
      if (!nrow(existe[[i]])) DBI::dbExecute(con, "INSERT INTO imagens (id, arquivo, caminho, largura, altura, amostra, importada_em) VALUES (?, ?, ?, ?, ?, 0, ?)",
                                             params = list(id, basename(arquivos[i]), normalizePath(arquivos[i]), largura, altura, agora()))
      DBI::dbExecute(con, "DELETE FROM deteccoes WHERE imagem_id = ? AND modelo_id = ?", params = list(id, mod$id))
      for (l in lesoes) DBI::dbExecute(con, "INSERT INTO deteccoes (imagem_id, modelo_id, indice, contorno, cx, cy, area, confianca)
                                             VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                                       params = list(id, mod$id, l$indice, as.character(jsonlite::toJSON(unname(l$contorno))), l$cx, l$cy, l$area, l$confianca))
      DBI::dbExecute(con, "INSERT INTO aplicacoes (imagem_id, modelo_id, limiar, n_lesoes, segundos, em) VALUES (?, ?, ?, ?, ?, ?)",
                     params = list(id, mod$id, limiar, length(lesoes), segundos, agora()))
    })
    EBImage::writeImage(EBImage::Image(t(m) * 1), file.path(pasta_saida, paste0(id, "_mascara.png")))
  }
  falha <- function(i, msg) erros[length(erros) + 1] <<- sprintf("%s: %s", basename(arquivos[i]), msg)

  if (length(pend) && motor == "cpp") {
    progresso("carregando o modelo (C++)", basename(arquivos[pend[1]]))
    t0 <- Sys.time()
    tryCatch(executar_cpp(arquivos[pend], mod$arquivo, mod$largura %||% 2000, function(k, r) {
      i <- pend[k]
      if (!is.null(r$erro)) falha(i, r$erro)
      else tryCatch(gravar(i, r$prob, r$altura, r$largura, as.numeric(difftime(Sys.time(), t0, units = "secs"))),
                    error = function(e) falha(i, conditionMessage(e)))
      feitas <<- feitas + 1L; t0 <<- Sys.time()
      progresso("processando", if (k < length(pend)) basename(arquivos[pend[k + 1]]) else "")
    }), error = function(e) erros[length(erros) + 1] <<- conditionMessage(e))
  } else if (length(pend)) {
    progresso("carregando o modelo")
    modelo <- carregar_unet(caminho_pesos(mod$arquivo))
    for (i in pend) {
      progresso("processando", basename(arquivos[i]))
      t0 <- Sys.time()
      tryCatch({
        original <- ler_radiografia(arquivos[i])
        p <- prever_radiografia(modelo, original, mod$largura %||% 2000)
        gravar(i, p, nrow(original), ncol(original), as.numeric(difftime(Sys.time(), t0, units = "secs")))
      }, error = function(e) falha(i, conditionMessage(e)))
      feitas <- feitas + 1L
    }
  }
  registrar_evento(con, "", "aplicacao", sprintf("%d radiografias, modelo %s, limiar %s, motor %s", length(arquivos), modelo_nome, limiar, motor))
  feitas <- length(arquivos)
  progresso("concluido")
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
