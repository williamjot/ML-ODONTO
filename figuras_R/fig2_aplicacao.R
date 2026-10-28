# Figura 2 — Aplicação da U-Net v2 às 422 panorâmicas sem marcação.
# Rode a partir da pasta do projeto: Rscript figuras_R/fig2_aplicacao.R
source(file.path("figuras_R", "style_nature.R"))

img <- ler("aplicacao_imagens.csv")
apl <- ler("aplicacao_lesoes.csv")
les <- ler("lesoes_validacao.csv")
COR_UNET <- CORES_MODELO[["U-Net v2"]]

# ---- a: lesões detectadas por imagem ----
cont <- img |> count(n_lesoes) |> mutate(p = n / sum(n))
fonte(cont, "fig2a_lesoes_por_imagem")
pa <- ggplot(cont, aes(factor(n_lesoes), n)) +
  geom_col(fill = COR_UNET, colour = "black", linewidth = .2, width = .75) +
  geom_text(aes(label = n), vjust = -.5, size = 2.35, family = FONT) +
  labs(x = "Lesões detectadas na imagem", y = "Imagens (n = 422)") +
  coord_cartesian(ylim = c(0, max(cont$n) * 1.18), expand = FALSE) +
  note(nrow(cont) - 1.5, max(cont$n) * .75, sprintf("%d de 422 imagens (%s)\ncom ao menos uma detecção",
                         sum(img$n_lesoes > 0), pct(mean(img$n_lesoes > 0))), hjust = .5) +
  theme_nat() + ticks_nativos + theme(axis.ticks.x = element_blank())

# ---- b: onde ficam as detecções na panorâmica ----
fonte(apl, "fig2b_posicoes")
pb <- ggplot(apl, aes(x_rel, y_rel)) +
  stat_density_2d(aes(fill = after_stat(level)), geom = "polygon", contour_var = "ndensity",
                  bins = 7, alpha = .55, colour = NA) +
  geom_point(shape = 21, fill = COR_UNET, colour = "black", size = .9, stroke = .2, alpha = .8) +
  geom_hline(yintercept = .5, colour = "grey60", linewidth = .35, linetype = "22") +
  geom_vline(xintercept = .5, colour = "grey60", linewidth = .35, linetype = "22") +
  scale_fill_gradientn(colours = c("#DCE9F5", "#A9C9E6", "#6FA3D2", "#3B7DBE", "#1F5F99"), guide = "none") +
  scale_y_reverse(labels = virgula, breaks = seq(0, 1, .25)) +
  scale_x_continuous(labels = virgula, breaks = seq(0, 1, .25)) +
  annotate("text", x = c(.02, .98), y = .04, label = c("direita do paciente", "esquerda do paciente"),
           hjust = c(0, 1), size = 2.35, family = FONT) +
  note(.985, .955, "tracejado: meia altura da imagem (≈ plano oclusal)", hjust = 1) +
  labs(x = "Posição horizontal (fração da largura)", y = "Posição vertical (fração da altura)") +
  coord_fixed(ratio = .5, xlim = c(0, 1), ylim = c(1, 0), expand = FALSE) +
  theme_nat() + ticks_nativos

# ---- c: tamanho das lesões detectadas (aplicação) × marcadas pelo dentista (validação) ----
tam <- bind_rows(
  apl |> transmute(grupo = sprintf("Detectadas na aplicação (n = %d)", n()), d = diam_eq_px),
  les |> filter(achada_UNET_V2 == 1) |> transmute(grupo = sprintf("Marcadas e achadas (validação, n = %d)", n()), d = diam_eq_px),
  les |> filter(achada_UNET_V2 == 0) |> transmute(grupo = sprintf("Marcadas e perdidas (validação, n = %d)", n()), d = diam_eq_px))
grupos <- unique(tam$grupo)
cores_c <- setNames(c(COR_UNET, "grey25", "grey65"), grupos)
med <- tam |> group_by(grupo) |> summarise(m = median(d), .groups = "drop")
fonte(tam, "fig2c_tamanhos")
pc <- ggplot(tam, aes(d, colour = grupo)) +
  geom_vline(data = med, aes(xintercept = m, colour = grupo), linetype = "22", linewidth = .4) +
  stat_ecdf(linewidth = .6, pad = FALSE) +
  scale_colour_manual(values = cores_c) +
  scale_x_continuous(labels = virgula) + scale_y_continuous(labels = pct) +
  labs(x = "Diâmetro equivalente (px; imagem com 1.400 px de largura)", y = "Fração acumulada") +
  note(70, .32, "tracejado: medianas", hjust = 0) +
  ticks_in(c(0, 120), c(0, 1.02), seq(0, 120, 20), seq(0, 1, .25), seq(0, 120, 10), NULL, W = 80, H = 50) +
  theme_nat() + theme(legend.position = c(.99, .03), legend.justification = c(1, 0),
                      legend.background = element_rect(fill = "white", colour = "black", linewidth = .35))

# ---- d: confiança atribuída pela rede ----
fonte(apl |> select(imagem, confianca), "fig2d_confianca")
pd <- ggplot(apl, aes(confianca)) +
  geom_histogram(breaks = seq(0.4, 1, .025), fill = COR_UNET, colour = "black", linewidth = .2) +
  scale_x_continuous(labels = virgula) +
  labs(x = "Confiança da detecção (pico de probabilidade)", y = "Lesões detectadas") +
  note(.43, Inf, vjust = 1.4, sprintf("%s das detecções com confiança ≥ 0,95",
                         pct(mean(apl$confianca >= .95))), hjust = 0) +
  coord_cartesian(xlim = c(.4, 1.005), expand = FALSE, ylim = c(0, max(hist(apl$confianca, breaks = seq(0, 1, .025), plot = FALSE)$counts) * 1.08)) +
  theme_nat() + ticks_nativos

fig <- (pa | pb) / (pc | pd) + plot_layout(widths = c(1, 1.25)) + plot_annotation(tag_levels = "a")
save_nat(fig, "fig2_aplicacao", width_mm = 183, height_mm = 130)
cat("Figura 2 salva\n")
