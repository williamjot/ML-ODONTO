# Estilo compartilhado das figuras (padrão Nature: moldura, ticks para dentro, Arial 7 pt).
suppressPackageStartupMessages({
  library(ggplot2); library(patchwork); library(dplyr); library(tidyr); library(scales)
})
FONT <- "Arial"; FS <- 7; LW_FR <- 0.45
DADOS <- file.path("Resultados", "figuras", "dados")
SAIDA <- file.path("Resultados", "figuras", "R")
dir.create(SAIDA, showWarnings = FALSE, recursive = TRUE)

# Cores fixas por modelo em todas as figuras
CORES_MODELO <- c("RF (Hanói)" = "#B8C0C8", "RF (Hanói + nossas)" = "#6B7782",
                  "U-Net (Hanói)" = "#7FB2DE", "U-Net + ajuste fino" = "#7A3E9D",
                  "U-Net v2" = "#1F6FB4", "Duas etapas" = "#0B2E59")
FORMAS_MODELO <- c("RF (Hanói)" = 22, "RF (Hanói + nossas)" = 23,
                   "U-Net (Hanói)" = 21, "U-Net + ajuste fino" = 24,
                   "U-Net v2" = 21, "Duas etapas" = 25)
virgula <- label_number(decimal.mark = ",", big.mark = ".")
pct <- label_percent(accuracy = 1, decimal.mark = ",")

theme_nat <- function(base = FS) theme_classic(base_size = base, base_family = FONT) + theme(
  panel.border = element_rect(fill = NA, colour = "black", linewidth = LW_FR),
  axis.line = element_blank(), axis.ticks = element_blank(), axis.ticks.length = unit(0, "mm"),
  axis.text = element_text(size = base, colour = "black"),
  axis.text.x = element_text(margin = margin(t = 2.5)), axis.text.y = element_text(margin = margin(r = 2.5)),
  axis.title = element_text(size = base + 0.6), legend.key = element_blank(),
  legend.background = element_rect(fill = alpha("white", 0.9), colour = NA),
  legend.text = element_text(size = base - 0.4), legend.key.size = unit(3.2, "mm"),
  legend.title = element_blank(),
  strip.background = element_blank(), strip.text = element_text(size = base, face = "bold"),
  plot.tag = element_text(size = 9, face = "bold"), plot.margin = margin(3, 4, 3, 3),
  panel.grid = element_blank())

# Ticks para dentro nos quatro lados (maiores e menores). W/H = tamanho aproximado do painel em mm.
ticks_in <- function(xlim, ylim, xmaj, ymaj, xmin = NULL, ymin = NULL, W = 60, H = 60,
                     Lmaj = 1.4, Lmin = 0.8, lw = 0.4, clip = "on") {
  dy <- function(L) L / H * diff(ylim); dx <- function(L) L / W * diff(xlim)
  xmaj <- xmaj[xmaj > xlim[1] & xmaj < xlim[2]]; ymaj <- ymaj[ymaj > ylim[1] & ymaj < ylim[2]]
  xmin <- setdiff(xmin[xmin > xlim[1] & xmin < xlim[2]], xmaj)
  ymin <- setdiff(ymin[ymin > ylim[1] & ymin < ylim[2]], ymaj)
  seg <- function(x, y, xe, ye) annotate("segment", x = x, y = y, xend = xe, yend = ye, linewidth = lw)
  out <- list()
  for (s in list(list(v = xmaj, L = Lmaj), list(v = xmin, L = Lmin))) if (length(s$v))
    out <- c(out, seg(s$v, ylim[1], s$v, ylim[1] + dy(s$L)), seg(s$v, ylim[2], s$v, ylim[2] - dy(s$L)))
  for (s in list(list(v = ymaj, L = Lmaj), list(v = ymin, L = Lmin))) if (length(s$v))
    out <- c(out, seg(xlim[1], s$v, xlim[1] + dx(s$L), s$v), seg(xlim[2], s$v, xlim[2] - dx(s$L), s$v))
  c(out, list(coord_cartesian(xlim = xlim, ylim = ylim, expand = FALSE, clip = clip)))
}
ticks_nativos <- theme(axis.ticks = element_line(linewidth = .4), axis.ticks.length = unit(-1, "mm"))

GREY_NOTE <- "grey55"
note <- function(x, y, label, hjust = 0.5, vjust = 0.5, size = 2.45, angle = 0, colour = GREY_NOTE, ...)
  annotate("text", x = x, y = y, label = label, hjust = hjust, vjust = vjust, size = size, angle = angle,
           family = FONT, colour = colour, lineheight = 0.9, ...)
note_arrow <- function(x, y, xend, yend, curv = 0.25, colour = GREY_NOTE, lw = 0.55)
  annotate("curve", x = x, y = y, xend = xend, yend = yend, curvature = curv, colour = colour,
           linewidth = lw, arrow = arrow(length = unit(1.4, "mm"), type = "open", angle = 25))

wilson <- function(x, n, z = 1.96) {
  p <- x / n; d <- 1 + z^2 / n; c0 <- (p + z^2 / (2 * n)) / d
  h <- z * sqrt(p * (1 - p) / n + z^2 / (4 * n^2)) / d
  tibble(p = p, lo = pmax(0, c0 - h), hi = pmin(1, c0 + h))
}

ler <- function(nome) read.csv(file.path(DADOS, nome), encoding = "UTF-8", check.names = FALSE)
fonte <- function(df, nome) write.csv(df, file.path(SAIDA, paste0(nome, "_source.csv")), row.names = FALSE,
                                      fileEncoding = "UTF-8")

save_nat <- function(plot, filename, width_mm = 183, height_mm = 120, dpi = 600) {
  f <- file.path(SAIDA, filename); w <- width_mm / 25.4; h <- height_mm / 25.4
  svglite::svglite(paste0(f, ".svg"), width = w, height = h); print(plot); invisible(dev.off())
  grDevices::cairo_pdf(paste0(f, ".pdf"), width = w, height = h, family = FONT); print(plot); invisible(dev.off())
  ragg::agg_tiff(paste0(f, ".tiff"), width = w, height = h, units = "in", res = dpi, compression = "lzw")
  print(plot); invisible(dev.off())
  ragg::agg_png(paste0(f, "_preview.png"), width = w, height = h, units = "in", res = 300); print(plot)
  invisible(dev.off())
}
