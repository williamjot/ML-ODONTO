# Métricas com intervalo de confiança.

#' Proporção com intervalo de confiança de Wilson (95%)
wilson <- function(x, n, z = 1.96) {
  if (is.na(n) || n == 0) return(list(x = x, n = n, p = NA_real_, inf = NA_real_, sup = NA_real_))
  p <- x / n
  d <- 1 + z^2 / n
  c0 <- (p + z^2 / (2 * n)) / d
  h <- z * sqrt(p * (1 - p) / n + z^2 / (4 * n^2)) / d
  list(x = x, n = n, p = p, inf = max(0, c0 - h), sup = min(1, c0 + h))
}

#' "83% (IC 95% 70–91%; 45/54)" ou "—"
formatar_proporcao <- function(w, curto = FALSE) {
  if (is.na(w$p)) return("—")
  pct <- function(v) paste0(format(round(100 * v), big.mark = ".", decimal.mark = ","), "%")
  if (curto) return(pct(w$p))
  sprintf("%s (IC 95%% %s–%s; %d/%d)", pct(w$p), sub("%", "", pct(w$inf)), pct(w$sup), w$x, w$n)
}

virgula <- function(x, d = 2) formatC(x, format = "f", digits = d, decimal.mark = ",")
