# Lesões de referência (20 panorâmicas marcadas), achados por modelo e cartões da auditoria.

# colunas achada_* de lesoes_validacao.csv -> rótulo do modelo (como nas figuras do artigo)
MODELOS_VALIDACAO <- c(B = "RF (Hanói)", C = "RF (Hanói + nossas)", UNET = "U-Net (Hanói)",
                       UNET_FT = "U-Net + ajuste fino", UNET_V2 = "U-Net v2", DUAS = "Duas etapas")
CORES_MODELO <- c("RF (Hanói)" = "#B8C0C8", "RF (Hanói + nossas)" = "#6B7782", "U-Net (Hanói)" = "#7FB2DE",
                  "U-Net + ajuste fino" = "#7A3E9D", "U-Net v2" = "#1F6FB4", "Duas etapas" = "#0B2E59")
RESPOSTAS_AUDITORIA <- c("Lesão visível na panorâmica" = "visivel", "Duvidosa" = "duvidosa",
                         "Não visível na panorâmica" = "nao_visivel", "Não é lesão / erro de marcação" = "erro_marcacao")

#' Importa as 20 panorâmicas marcadas, as lesões, os achados e os resultados de validação
importar_referencia <- function(con, projeto, progresso = function(v, t) NULL) {
  ext <- file.path(projeto, "Treinamento", "extraido")
  dados_fig <- file.path(projeto, "Resultados", "figuras", "dados")
  if (!dir.exists(ext) || !file.exists(file.path(dados_fig, "lesoes_validacao.csv"))) return(invisible(0))
  les <- utils::read.csv(file.path(dados_fig, "lesoes_validacao.csv"), colClasses = c(paciente = "character"))
  pac <- sort(unique(les$paciente))
  DBI::dbWithTransaction(con, {
    for (p in pac) {
      progresso(match(p, pac) / length(pac), paste("Lesões de referência: paciente", p))
      arq <- function(sub) normalizePath(file.path(ext, sub, paste0(p, ".png")), mustWork = FALSE)
      prob <- file.path(projeto, "Resultados", "validacao", "probs_UNET_V2_png", paste0(p, ".png"))
      comps <- componentes_mascara(arq("mascaras"), passo = 1)
      d <- attr(comps, "dims")
      DBI::dbExecute(con, "INSERT OR REPLACE INTO referencia_imagens VALUES (?, ?, ?, ?, ?, ?, ?)",
                     params = list(p, arq("imagens"), arq("mascaras"), arq("contornos"),
                                   if (file.exists(prob)) normalizePath(prob) else NA, d[1], d[2]))
      lp <- les[les$paciente == p, ]
      for (i in seq_len(nrow(lp))) {
        # casa a lesão do CSV com o componente da máscara pelo centroide relativo
        dist <- vapply(comps, function(cp) (cp$cx / d[1] - lp$x_rel[i])^2 + (cp$cy / d[2] - lp$y_rel[i])^2, 0)
        cp <- comps[[which.min(dist)]]
        DBI::dbExecute(con, "INSERT OR REPLACE INTO referencia (paciente, lesao, contorno, cx, cy, area, diam_eq_px, arcada)
                             VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                       params = list(p, lp$lesao[i], as.character(jsonlite::toJSON(unname(cp$contorno))),
                                     cp$cx, cp$cy, cp$area, lp$diam_eq_px[i], lp$arcada[i]))
        rid <- DBI::dbGetQuery(con, "SELECT id FROM referencia WHERE paciente = ? AND lesao = ?", params = list(p, lp$lesao[i]))$id
        for (ch in names(MODELOS_VALIDACAO)) {
          col <- paste0("achada_", ch)
          if (col %in% names(lp)) DBI::dbExecute(con, "INSERT OR REPLACE INTO achados_referencia VALUES (?, ?, ?)",
                                                 params = list(rid, MODELOS_VALIDACAO[[ch]], as.integer(lp[[col]][i])))
        }
      }
    }
    for (tab in c("froc", "pacientes")) {
      f <- file.path(dados_fig, paste0(tab, ".csv"))
      if (file.exists(f)) {
        x <- utils::read.csv(f, colClasses = if (tab == "pacientes") c(paciente = "character") else NA, check.names = FALSE)
        DBI::dbExecute(con, paste0("DELETE FROM validacao_", tab))
        DBI::dbAppendTable(con, paste0("validacao_", tab), x[, DBI::dbListFields(con, paste0("validacao_", tab))])
      }
    }
  })
  importar_auditoria_csv(con, file.path(projeto, "Revisao", "lesoes_perdidas", "revisao_lesoes_perdidas.csv"))
  invisible(length(pac))
}

#' Respostas dadas na página HTML anterior (revisao_lesoes_perdidas.csv), se existirem
importar_auditoria_csv <- function(con, arq, revisor = "importado") {
  if (!file.exists(arq)) return(invisible(0))
  x <- utils::read.csv2(arq, fileEncoding = "UTF-8-BOM", colClasses = "character")
  x <- x[nzchar(x$resposta), ]
  for (i in seq_len(nrow(x))) {
    rid <- DBI::dbGetQuery(con, "SELECT id FROM referencia WHERE paciente = ? AND lesao = ?",
                           params = list(x$paciente[i], as.integer(x$lesao[i])))$id
    if (length(rid)) gravar_auditoria(con, rid, "U-Net v2", revisor, x$resposta[i], x$observacao[i] %||% "")
  }
  invisible(nrow(x))
}

gravar_auditoria <- function(con, referencia_id, modelo, revisor, resposta = NULL, observacao = NULL) {
  atual <- DBI::dbGetQuery(con, "SELECT resposta, observacao FROM auditoria_perdidas WHERE referencia_id = ? AND modelo = ? AND revisor = ?",
                           params = list(referencia_id, modelo, revisor))
  resp <- if (is.null(resposta)) (if (nrow(atual)) atual$resposta else NA) else resposta
  obs <- if (is.null(observacao)) (if (nrow(atual)) atual$observacao else "") else observacao
  DBI::dbExecute(con, "INSERT INTO auditoria_perdidas (referencia_id, modelo, revisor, resposta, observacao, em) VALUES (?, ?, ?, ?, ?, ?)
                       ON CONFLICT(referencia_id, modelo, revisor) DO UPDATE SET resposta = excluded.resposta,
                       observacao = excluded.observacao, em = excluded.em",
                 params = list(referencia_id, modelo, revisor, resp, obs, agora()))
}

#' Lesões de referência não encontradas por um modelo, com pistas e respostas do revisor
lesoes_perdidas <- function(con, modelo, revisor) {
  x <- DBI::dbGetQuery(con, "
    SELECT r.*, a.resposta, a.observacao
    FROM referencia r JOIN achados_referencia f ON f.referencia_id = r.id AND f.modelo = ?
    LEFT JOIN auditoria_perdidas a ON a.referencia_id = r.id AND a.modelo = ? AND a.revisor = ?
    WHERE f.achada = 0 ORDER BY r.paciente, r.lesao", params = list(modelo, modelo, revisor))
  if (!nrow(x)) return(x)
  todas <- DBI::dbGetQuery(con, "SELECT r.id, r.paciente, r.contorno, f.achada FROM referencia r
                                 JOIN achados_referencia f ON f.referencia_id = r.id AND f.modelo = ?", params = list(modelo))
  dims <- DBI::dbGetQuery(con, "SELECT paciente, largura FROM referencia_imagens")
  # distância (escala 1.400 px) até a lesão encontrada mais próxima no mesmo paciente
  x$dist_achada <- vapply(seq_len(nrow(x)), function(i) {
    ach <- todas[todas$paciente == x$paciente[i] & todas$achada == 1, ]
    if (!nrow(ach)) return(NA_real_)
    a <- jsonlite::fromJSON(x$contorno[i])
    dmin <- min(vapply(ach$contorno, function(cc) { b <- jsonlite::fromJSON(cc)
      min(sqrt(outer(a[, 1], b[, 1], "-")^2 + outer(a[, 2], b[, 2], "-")^2)) }, 0))
    dmin * 1400 / dims$largura[dims$paciente == x$paciente[i]]
  }, 0)
  x
}

# ---------------------------------------------------------------- cartões (imagens) da auditoria

ler_cinza_ebi <- function(f) { m <- EBImage::readImage(f); if (length(dim(m)) > 2) m <- m[, , 1]; EBImage::imageData(m) }

borda <- function(m) m & !EBImage::erode(m, EBImage::makeBrush(3, "box"))

pintar <- function(rgb, msk, cor) { for (c in 1:3) { canal <- rgb[, , c]; canal[msk] <- cor[c]; rgb[, , c] <- canal }; rgb }

redim <- function(a, w, h, filtro = "bilinear") EBImage::imageData(EBImage::resize(a, w = w, h = h, filter = filtro))

#' Gera (ou reaproveita do cache) as quatro imagens do cartão de uma lesão de referência
cartao_auditoria <- function(con, referencia_id, pasta_cache, janela = 150, largura_saida = 520) {
  dir.create(pasta_cache, showWarnings = FALSE, recursive = TRUE)
  nomes <- file.path(pasta_cache, sprintf("aud_%d_%s.png", referencia_id, c("pano", "limpo", "marcado", "prob")))
  if (all(file.exists(nomes))) return(nomes)
  r <- DBI::dbGetQuery(con, "SELECT * FROM referencia WHERE id = ?", params = list(referencia_id))
  ri <- DBI::dbGetQuery(con, "SELECT * FROM referencia_imagens WHERE paciente = ?", params = list(r$paciente))
  img <- ler_cinza_ebi(ri$caminho_imagem); W <- nrow(img); H <- ncol(img)
  msk <- ler_cinza_ebi(ri$caminho_mascara) > .5
  trc <- ler_cinza_ebi(ri$caminho_traco) > .5
  rot <- EBImage::bwlabel(msk)
  alvo <- rot == rot[round(r$cx) + 1, round(r$cy) + 1]
  if (!any(alvo)) { cont <- jsonlite::fromJSON(r$contorno); alvo <- rot %in% unique(rot[cbind(cont[, 1] + 1, cont[, 2] + 1)]) & msk; dim(alvo) <- dim(msk) }
  f <- W / 1400; meia <- round(janela * f)
  x0 <- max(1, round(r$cx) - meia); x1 <- min(W, round(r$cx) + meia)
  y0 <- max(1, round(r$cy) - meia); y1 <- min(H, round(r$cy) + meia)
  wc <- largura_saida; hc <- round(wc * (y1 - y0 + 1) / (x1 - x0 + 1))
  rc <- function(a, filtro = "bilinear") redim(a[x0:x1, y0:y1] * 1, wc, hc, filtro)
  base <- rc(img); rgb_base <- array(rep(base, 3), c(wc, hc, 3))
  # painel limpo
  EBImage::writeImage(EBImage::Image(rgb_base, colormode = "Color"), nomes[2])
  # painel marcado: traço desta lesão em laranja, outras em branco, máscara usada em ciano
  perto <- EBImage::dilate(alvo * 1, EBImage::makeBrush(9, "box")) > 0
  m_esta <- rc(trc & perto, "none") > .5; m_outras <- rc(trc & !perto, "none") > .5
  m_borda <- borda(rc(alvo, "none") > .5)
  marc <- pintar(pintar(pintar(rgb_base, m_outras, c(.92, .92, .92)), m_borda, c(0, .78, 1)), m_esta, c(1, .55, 0))
  EBImage::writeImage(EBImage::Image(marc, colormode = "Color"), nomes[3])
  # painel de probabilidade (viridis) com o contorno da lesão em branco
  if (!is.na(ri$caminho_prob) && file.exists(ri$caminho_prob)) {
    pr <- redim(ler_cinza_ebi(ri$caminho_prob), W, H)
    pc <- rc(pr); cores <- grDevices::col2rgb(grDevices::hcl.colors(256, "viridis")) / 255
    idx <- pmin(256, pmax(1, round(pc * 255) + 1))
    ov <- array(0, c(wc, hc, 3)); for (c in 1:3) ov[, , c] <- .55 * base + .45 * cores[c, idx]
  } else ov <- rgb_base
  ov <- pintar(ov, m_borda, c(1, 1, 1))
  EBImage::writeImage(EBImage::Image(ov, colormode = "Color"), nomes[4])
  # panorâmica com retângulo
  wp <- 640; hp <- round(wp * H / W); pano <- redim(img, wp, hp); rp <- array(rep(pano, 3), c(wp, hp, 3))
  bx <- matrix(FALSE, wp, hp); s <- wp / W
  xs <- max(1, round(x0 * s)):min(wp, round(x1 * s)); ys <- max(1, round(y0 * s)):min(hp, round(y1 * s))
  for (t in 0:2) { bx[xs, pmin(hp, min(ys) + t)] <- TRUE; bx[xs, pmax(1, max(ys) - t)] <- TRUE
                   bx[pmin(wp, min(xs) + t), ys] <- TRUE; bx[pmax(1, max(xs) - t), ys] <- TRUE }
  EBImage::writeImage(EBImage::Image(pintar(rp, bx, c(1, .55, 0)), colormode = "Color"), nomes[1])
  nomes
}
