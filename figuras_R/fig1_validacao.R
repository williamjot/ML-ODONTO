# Figura de validação — comparação dos modelos nas 20 panorâmicas marcadas (45 lesões).
# Rode a partir da pasta do projeto: Rscript figuras_R/fig1_validacao.R
source(file.path("figuras_R", "style_nature.R"))
set.seed(1)

froc <- ler("froc.csv")
pac <- ler("pacientes.csv")
les <- ler("lesoes_validacao.csv")
treino <- ler("treino.csv")
MODELOS <- c("RF (Hanói + nossas)", "U-Net (Hanói)", "U-Net + ajuste fino", "U-Net v2", "Duas etapas")
cores <- CORES_MODELO[MODELOS]; formas <- FORMAS_MODELO[MODELOS]

# ---- a: FROC (maior sensibilidade para cada nº de falsos positivos por imagem) ----
curva <- froc |> filter(modelo %in% MODELOS) |>
  mutate(fp = (previstas - corretas) / n_imagens, sens = achadas / lesoes) |>
  group_by(modelo, fp) |> slice_max(sens, n = 1, with_ties = FALSE) |>
  group_by(modelo) |> arrange(fp, .by_group = TRUE) |> mutate(sens = cummax(sens)) |> ungroup() |>
  mutate(modelo = factor(modelo, MODELOS))
op <- pac |> filter(modelo %in% MODELOS) |> group_by(modelo, limiar) |>
  summarise(achadas = sum(lesoes_achadas), lesoes = sum(lesoes), previstas = sum(previstas),
            corretas = sum(previstas_corretas), n = n(), .groups = "drop") |>
  mutate(fp = (previstas - corretas) / n, sens = achadas / lesoes, modelo = factor(modelo, MODELOS))
fonte(curva, "fig3a_froc"); fonte(op, "fig3a_ponto_operacao")

pa <- ggplot(curva, aes(fp, sens, colour = modelo)) +
  geom_step(linewidth = .6, direction = "hv") +
  geom_point(data = op, aes(shape = modelo, fill = modelo), colour = "black", size = 2.2, stroke = .4) +
  scale_colour_manual(values = cores) + scale_fill_manual(values = cores) + scale_shape_manual(values = formas) +
  note(3.0, .93, "pontos: limiar de operação
(melhor F1)", hjust = 0) +
  scale_x_continuous(labels = virgula) + scale_y_continuous(labels = pct) +
  labs(x = "Falsos positivos por imagem", y = "Sensibilidade por lesão (n = 45)") +
  ticks_in(c(0, 6.2), c(0, 1), 0:6, seq(0, 1, .2), seq(0, 6, .5), seq(0, 1, .1), W = 78, H = 58) +
  theme_nat() + theme(legend.position = c(.99, .42), legend.justification = c(1, .5),
                      legend.background = element_rect(fill = "white", colour = "black", linewidth = .35))

# ---- b: métricas no ponto de operação com IC 95% ----
boot_dice <- function(d, B = 2000) {
  est <- 2 * sum(d$inter) / sum(d$soma)
  r <- replicate(B, { i <- sample(nrow(d), replace = TRUE); 2 * sum(d$inter[i]) / sum(d$soma[i]) })
  tibble(p = est, lo = quantile(r, .025), hi = quantile(r, .975))
}
met <- bind_rows(
  op |> rowwise() |> mutate(w = list(wilson(achadas, lesoes))) |> tidyr::unnest(w) |>
    transmute(modelo, metrica = "Sensibilidade", p, lo, hi),
  op |> rowwise() |> mutate(w = list(wilson(corretas, pmax(previstas, 1)))) |> tidyr::unnest(w) |>
    transmute(modelo, metrica = "Precisão", p, lo, hi),
  pac |> filter(modelo %in% MODELOS) |> group_by(modelo) |> group_modify(~ boot_dice(.x)) |> ungroup() |>
    transmute(modelo = factor(modelo, MODELOS), metrica = "Dice", p, lo, hi)
) |> mutate(metrica = factor(metrica, c("Sensibilidade", "Precisão", "Dice")))
fonte(met, "fig3b_metricas")

pb <- ggplot(met, aes(metrica, p, fill = modelo)) +
  geom_col(position = position_dodge(.84), width = .84, colour = "black", linewidth = .2) +
  geom_errorbar(aes(ymin = lo, ymax = hi), position = position_dodge(.84), width = 0, linewidth = .35) +
  scale_fill_manual(values = cores, guide = "none") +
  scale_y_continuous(labels = pct) +
  labs(x = NULL, y = "Valor no limiar de operação (IC 95%)") +
  coord_cartesian(ylim = c(0, 1.04), expand = FALSE) +
  theme_nat() + ticks_nativos + theme(axis.ticks.x = element_blank())

# ---- c: quais lesões cada modelo encontra (lesões ordenadas por metade da imagem e tamanho) ----
chaves <- c("RF (Hanói + nossas)" = "achada_C", "U-Net (Hanói)" = "achada_UNET",
            "U-Net v2" = "achada_UNET_V2", "Duas etapas" = "achada_DUAS")
ordem <- les |> mutate(metade = factor(arcada, c("superior", "inferior"), c("Maxila", "Mandíbula"))) |>
  arrange(metade, diam_eq_px) |> mutate(pos = row_number())
mapa <- bind_rows(lapply(names(chaves), function(m)
  ordem |> transmute(pos, metade, diam_eq_px, modelo = m, achada = .data[[chaves[[m]]]] == 1))) |>
  mutate(modelo = factor(modelo, rev(names(chaves))))
nenhum <- ordem |> filter(achada_C == 0, achada_UNET == 0, achada_UNET_V2 == 0, achada_DUAS == 0)
corte <- max(ordem$pos[ordem$metade == "Maxila"]) + .5
fonte(mapa, "fig3c_mapa_lesoes")
pc <- ggplot(mapa, aes(pos, modelo)) +
  geom_tile(aes(fill = ifelse(achada, as.character(modelo), "não")), colour = "white", linewidth = .35) +
  geom_tile(data = ordem, aes(pos, 0.25, alpha = diam_eq_px), fill = "grey20", height = .38, inherit.aes = FALSE) +
  geom_vline(xintercept = corte, linewidth = .45, linetype = "22", colour = "grey30") +
  annotate("text", x = c(corte / 2, (corte + nrow(ordem)) / 2 + .25), y = 4.72, size = 2.4, family = FONT,
           fontface = "bold", label = c("Maxila (n = 20)", "Mandíbula (n = 25)")) +
  scale_fill_manual(values = c(CORES_MODELO[names(chaves)], "não" = "grey93"), guide = "none") +
  scale_alpha_continuous(range = c(.12, 1), guide = "none") +
  scale_y_discrete(expand = expansion(add = c(.95, 1.0))) +
  scale_x_continuous(expand = c(0, 0)) +
  labs(x = sprintf("Lesões marcadas, da menor para a maior em cada metade; faixa cinza = diâmetro. Nenhum modelo encontrou %d de 45.",
                   nrow(nenhum)), y = NULL) +
  theme_nat() + theme(axis.text.x = element_blank(), panel.border = element_blank(),
                      axis.title.x = element_text(size = FS - .2))

# ---- d: treinamento no Colab ----
tr <- treino |> filter(metrica %in% c("dice_validacao", "f1_apice_20px")) |>
  mutate(painel = recode(modelo, "U-Net (Hanói)" = "U-Net v1: Dice (validação)",
                         "Localizador de ápices" = "Localizador: F1 dos ápices (≤20 px)",
                         "Segmentador de recortes" = "Duas etapas: Dice dos recortes"),
         painel = factor(painel, c("U-Net v1: Dice (validação)", "Localizador: F1 dos ápices (≤20 px)",
                                   "Duas etapas: Dice dos recortes")))
fonte(tr, "fig3d_treino")
pd <- ggplot(tr, aes(iteracao, valor)) +
  geom_line(colour = CORES_MODELO[["Duas etapas"]], linewidth = .45) +
  geom_point(shape = 21, fill = CORES_MODELO[["U-Net v2"]], size = 1.5, stroke = .35) +
  facet_wrap(~painel, ncol = 1, scales = "free") +
  scale_x_continuous(labels = virgula) + scale_y_continuous(labels = virgula) +
  labs(x = "Iteração", y = NULL) +
  theme_nat() + ticks_nativos + theme(strip.text = element_text(face = "plain", hjust = 0), panel.spacing = unit(1.5, "mm"))

fig <- (pa | pb) / (pc | pd) + plot_layout(widths = c(1.35, 1), heights = c(1, .95)) +
  plot_annotation(tag_levels = "a")
save_nat(fig, "fig1_validacao", width_mm = 183, height_mm = 145)
cat("Figura de validação salva\n")
