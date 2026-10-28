# Do mapa de probabilidade às lesões detectadas e avaliação por lesão (como scripts/deteccao.py e comum.avaliar).

RAIO_PICO <- 15         # px: picos mais próximos que isso se fundem (janela 31 x 31)
RAIO_REGIAO_UNET <- 100 # px: a região de uma lesão não passa disso a partir do pico
SUAVIZACAO <- 4
PICO_MIN <- 0.05
FRACAO_ACERTO <- 0.2

#' Componentes conectados com vizinhança 8 (como cv2.connectedComponents), matriz de rótulos 0..n
rotular8 <- function(m) {
  rot <- EBImage::bwlabel(m)                       # vizinhança 4
  n <- max(rot)
  if (n < 2) return(rot)
  h <- nrow(rot); w <- ncol(rot)
  pares <- rbind(
    cbind(as.vector(rot[-h, -w]), as.vector(rot[-1, -1])),   # diagonal ↘
    cbind(as.vector(rot[-1, -w]), as.vector(rot[-h, -1])))   # diagonal ↗
  pares <- unique(pares[pares[, 1] > 0 & pares[, 2] > 0 & pares[, 1] != pares[, 2], , drop = FALSE])
  if (!nrow(pares)) return(rot)
  pai <- seq_len(n)
  raiz <- function(i) { while (pai[i] != i) i <- pai[i]; i }
  for (k in seq_len(nrow(pares))) { a <- raiz(pares[k, 1]); b <- raiz(pares[k, 2]); if (a != b) pai[max(a, b)] <- min(a, b) }
  final <- vapply(seq_len(n), raiz, 0)
  novo <- match(final, unique(final))
  rot[rot > 0] <- novo[rot[rot > 0]]
  rot
}

refletir101 <- function(i, n) { i <- ifelse(i < 1, 2 - i, i); ifelse(i > n, 2 * n - i, i) }

#' Suavização gaussiana como cv2.GaussianBlur(p, (0, 0), sigma) (núcleo 8 sigma + 1, borda reflect-101)
suavizar <- function(p, sigma = SUAVIZACAO) {
  k <- 2 * round(sigma * 4) + 1; r <- (k - 1) / 2
  g <- exp(-((-r):r)^2 / (2 * sigma^2)); g <- g / sum(g)
  conv <- function(m) {                              # ao longo das linhas de cada coluna
    n <- nrow(m); idx <- refletir101((1 - r):(n + r), n)
    mp <- m[idx, , drop = FALSE]
    s <- matrix(0, n, ncol(m))
    for (j in seq_along(g)) s <- s + g[j] * mp[j:(j + n - 1), , drop = FALSE]
    s
  }
  t(conv(t(conv(p))))
}

#' Máximo numa janela (2r+1) x (2r+1), borda refletida (como scipy maximum_filter, mode = "reflect")
maximo_janela <- function(p, r = RAIO_PICO) {
  refl <- function(i, n) { i <- ifelse(i < 1, 1 - i, i); ifelse(i > n, 2 * n + 1 - i, i) }
  um <- function(m) {
    n <- nrow(m); mp <- m[refl((1 - r):(n + r), n), , drop = FALSE]
    s <- mp[1:n, , drop = FALSE]
    for (j in 2:(2 * r + 1)) s <- pmax(s, mp[j:(j + n - 1), , drop = FALSE])
    s
  }
  t(um(t(um(p))))
}

#' Lesões candidatas: lista de (linhas, colunas, confiança), da mais para a menos confiante
detectar_lesoes <- function(prob, raio_regiao = RAIO_REGIAO_UNET) {
  h <- nrow(prob); w <- ncol(prob)
  ps <- suavizar(prob)
  picos <- which(ps == maximo_janela(ps) & ps >= PICO_MIN, arr.ind = TRUE)
  if (!nrow(picos)) return(list())
  conf <- ps[picos]
  picos <- picos[order(-conf), , drop = FALSE]; conf <- sort(conf, decreasing = TRUE)
  lapply(seq_len(nrow(picos)), function(k) {
    y <- picos[k, 1]; x <- picos[k, 2]; pk <- conf[k]
    ys <- max(1, y - raio_regiao):min(h, y + raio_regiao); xs <- max(1, x - raio_regiao):min(w, x + raio_regiao)
    rot <- rotular8(ps[ys, xs, drop = FALSE] >= 0.5 * pk)
    list(linhas = ys, colunas = xs, regiao = rot == rot[y - ys[1] + 1, x - xs[1] + 1], confianca = pk, y = y, x = x)
  })
}

mascara_lesoes <- function(deteccoes, limiar, forma) {
  m <- matrix(FALSE, forma[1], forma[2])
  for (d in deteccoes) if (d$confianca >= limiar) m[d$linhas, d$colunas] <- m[d$linhas, d$colunas] | d$regiao
  m
}

#' Avaliação por lesão (como comum.avaliar): contagens e, por lesão verdadeira, se foi encontrada
avaliar_lesoes <- function(pred, verdade) {
  rv <- rotular8(verdade); rp <- rotular8(pred)
  nv <- max(rv); np <- max(rp)
  area <- tabulate(rp[rp > 0], np); sobre <- tabulate(rp[verdade & rp > 0], np)
  corretas <- which(sobre >= FRACAO_ACERTO * pmax(area, 1))
  tocadas <- unique(rv[rp %in% corretas & rv > 0])
  list(inter = sum(pred & verdade), soma = sum(pred) + sum(verdade), lesoes = nv,
       lesoes_achadas = length(tocadas), previstas = np, previstas_corretas = length(corretas),
       achada_por_lesao = seq_len(nv) %in% tocadas, rotulos_verdade = rv)
}
