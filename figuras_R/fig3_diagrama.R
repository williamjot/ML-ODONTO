# Figura 3 — Diagrama do fluxo: dados, pré-processamento, modelos, validação e aplicação.
# Rode a partir da pasta do projeto: Rscript figuras_R/fig3_diagrama.R
source(file.path("figuras_R", "style_nature.R"))

# contagens da aplicação lidas da tabela exportada (U-Net v2 nas 422 panorâmicas)
apl <- ler("aplicacao_imagens.csv")
txt_apl <- sprintf("Aplicação\nU-Net v2 → %d panorâmicas\n%s lesões detectadas\nem %d imagens\n\nmáscaras · contornos\ntabela CSV",
                   nrow(apl), format(sum(apl$n_lesoes), big.mark = ".", decimal.mark = ","), sum(apl$n_lesoes > 0))

caixas <- tribble(
  ~id,      ~x,   ~y,   ~w,   ~h,   ~fill,     ~label,
  "hanoi",  1.0,  5.2,  1.75, .9,   "white",   "Hanói (Mendeley, CC BY)\n3.924 panorâmicas com lesões\ncaixas → elipses",
  "dentex", 1.0,  3.95, 1.75, .9,   "white",   "DENTEX (CC BY-NC-SA)\n634 panorâmicas, 18.095 dentes\npolígonos → ápices",
  "nossas", 1.0,  2.7,  1.75, .9,   "white",   "Panorâmicas marcadas\n20 pacientes, 45 lesões\nPDF → máscaras",
  "hub",    1.0,  1.45, 1.75, .9,   "white",   "HUB: 422 panorâmicas\nsem marcação",
  "prep",   2.75, 3.95, 1.25, 1.1,  "grey92",  "Pré-processamento\nlargura 1.400 px\n(2.000 / 2.800 px)\n+ CLAHE",
  "rf",     4.55, 5.55, 1.6,  .62,  "#E3E7EB", "Random Forest por pixel\n38 atributos (linha de base)",
  "unet",   4.55, 4.75, 1.6,  .7,   "#D5E5F3", "U-Net v1 (ResNet34), 1.400 px\nBCE + Dice · ajuste fino local",
  "v2",     4.55, 3.9,  1.6,  .7,   "#B9D3EC", "U-Net v2, 2.000 px\nperda de Tversky (β = 0,7)",
  "loc",    4.55, 2.95, 1.6,  .7,   "#D5E5F3", "Etapa 1: localizador de ápices\nmapa de calor (DENTEX)",
  "rec",    4.55, 2.0,  1.6,  .7,   "#B9D3EC", "Etapa 2: segmentador de recortes\n256 px nos ápices (Hanói)",
  "val",    6.45, 3.6,  1.5,  1.7,  "grey92",  "Validação\n20 pacientes, 45 lesões\nnão vistos no treino\n\nFROC · sensibilidade\nprecisão · Dice\nIC 95%",
  "apl",    8.15, 3.6,  1.45, 1.7,  "#D5E5F3", txt_apl
) |> mutate(xmin = x - w / 2, xmax = x + w / 2, ymin = y - h / 2, ymax = y + h / 2)
borda <- function(id, lado) { b <- caixas[caixas$id == id, ]
  switch(lado, d = c(b$xmax, b$y), e = c(b$xmin, b$y), c = c(b$x, b$ymax), b = c(b$x, b$ymin)) }
seta <- function(de, para, ld = "d", lp = "e") { a <- borda(de, ld); b <- borda(para, lp)
  tibble(x = a[1], y = a[2], xend = b[1], yend = b[2]) }
setas <- bind_rows(seta("hanoi", "prep"), seta("dentex", "prep"), seta("nossas", "prep"),
                   lapply(c("rf", "unet", "v2", "loc"), function(m) seta("prep", m)),
                   seta("loc", "rec", "b", "c"),
                   lapply(c("rf", "unet", "v2", "rec"), function(m) seta(m, "val")),
                   seta("val", "apl"))
# o HUB só entra na aplicação, depois do mesmo pré-processamento
hub_caminho <- tibble(x = c(borda("hub", "b")[1], borda("hub", "b")[1], borda("apl", "b")[1]),
                      y = c(borda("hub", "b")[2], .6, .6))
fonte(caixas, "fig3_caixas")

p <- ggplot() +
  annotate("rect", xmin = 3.6, xmax = 5.5, ymin = 1.3, ymax = 5.2, fill = "#EAF2FA") +
  note(4.55, 1.44, "nuvem (Colab, GPU T4): só dados públicos", size = 2.1) +
  note(.18, .22, "todo o resto roda localmente: radiografias de pacientes não saem do computador",
       hjust = 0, size = 2.2) +
  geom_segment(data = setas, aes(x, y, xend = xend, yend = yend), linewidth = .35,
               arrow = arrow(length = unit(1.3, "mm"), type = "closed", angle = 22)) +
  geom_path(data = hub_caminho, aes(x, y), linewidth = .4, linetype = "22", colour = "grey35") +
  annotate("segment", x = borda("apl", "b")[1], xend = borda("apl", "b")[1], y = .6, yend = borda("apl", "b")[2],
           linewidth = .4, linetype = "22", colour = "grey35",
           arrow = arrow(length = unit(1.4, "mm"), type = "closed", angle = 22)) +
  note(4.55, .72, "mesmo pré-processamento", size = 2.2) +
  geom_rect(data = caixas, aes(xmin = xmin, xmax = xmax, ymin = ymin, ymax = ymax, fill = fill),
            colour = "black", linewidth = .35) +
  geom_text(data = caixas, aes(x, y, label = label), size = 2.1, family = FONT, lineheight = .95) +
  annotate("text", x = c(1.0, 2.75, 4.55, 6.45, 8.15), y = 6.1, size = 2.6, family = FONT, fontface = "bold",
           label = c("Dados", "Preparo", "Modelos", "Validação", "Aplicação")) +
  scale_fill_identity() +
  coord_cartesian(xlim = c(.1, 8.95), ylim = c(.08, 6.3), expand = FALSE) +
  theme_void(base_family = FONT)

save_nat(p, "fig3_diagrama", width_mm = 183, height_mm = 108)
cat("Figura 3 salva\n")
