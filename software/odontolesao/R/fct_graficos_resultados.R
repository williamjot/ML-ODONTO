# Gráficos da tela de Resultados (funções puras: dados -> ggplot).

pct_rot <- function(x) paste0(round(100 * x), "%")
vazio <- function(texto) ggplot2::ggplot() + ggplot2::annotate("text", x = 0, y = 0, label = texto, colour = CINZA, size = 4.5) + ggplot2::theme_void()

grafico_froc <- function(curvas, operacao) {
  if (!nrow(curvas)) return(vazio("Sem dados de validação"))
  op <- unique(operacao[, c("modelo", "fp_img")])
  op$sens <- operacao$p[operacao$metrica == "Sensibilidade"][match(op$modelo, operacao$modelo[operacao$metrica == "Sensibilidade"])]
  op$modelo <- factor(op$modelo, levels(curvas$modelo))
  ggplot2::ggplot(curvas, ggplot2::aes(fp, sens, colour = modelo)) +
    ggplot2::geom_step(linewidth = .8, direction = "hv") +
    ggplot2::geom_point(data = op, ggplot2::aes(fp_img, sens, fill = modelo), shape = 21, colour = "black", size = 3, stroke = .4) +
    ggplot2::scale_colour_manual(values = CORES_MODELO, name = NULL) + ggplot2::scale_fill_manual(values = CORES_MODELO, guide = "none") +
    ggplot2::scale_y_continuous(labels = pct_rot, limits = c(0, 1)) + ggplot2::coord_cartesian(xlim = c(0, 6)) +
    ggplot2::labs(x = "Falsos positivos por imagem", y = "Sensibilidade por lesão (n = 45)",
                  caption = "Pontos: limiar de operação (melhor F1)") +
    tema_grafico() + ggplot2::theme(legend.position = "right", plot.caption = ggplot2::element_text(colour = CINZA))
}

grafico_metricas <- function(operacao) {
  if (!nrow(operacao)) return(vazio("Sem dados de validação"))
  operacao$modelo <- factor(operacao$modelo, intersect(names(CORES_MODELO), unique(operacao$modelo)))
  operacao$metrica <- factor(operacao$metrica, c("Sensibilidade", "Precisão", "Dice"))
  ggplot2::ggplot(operacao, ggplot2::aes(metrica, p, fill = modelo)) +
    ggplot2::geom_col(position = ggplot2::position_dodge(.85), width = .85, colour = "black", linewidth = .2) +
    ggplot2::geom_errorbar(ggplot2::aes(ymin = inf, ymax = sup), position = ggplot2::position_dodge(.85), width = 0, linewidth = .4) +
    ggplot2::scale_fill_manual(values = CORES_MODELO, name = NULL) +
    ggplot2::scale_y_continuous(labels = pct_rot, limits = c(0, 1.02), expand = c(0, 0)) +
    ggplot2::labs(x = NULL, y = "Valor no limiar de operação (IC 95%)") + tema_grafico() +
    ggplot2::theme(legend.position = "right")
}

grafico_mapa_lesoes <- function(mapa) {
  if (!nrow(mapa)) return(vazio("Sem lesões de referência"))
  ordem <- unique(mapa[, c("id", "arcada", "diam_eq_px")])
  ordem <- ordem[order(factor(ordem$arcada, c("superior", "inferior")), ordem$diam_eq_px), ]
  ordem$pos <- seq_len(nrow(ordem))
  mapa$pos <- ordem$pos[match(mapa$id, ordem$id)]
  mapa$modelo <- factor(mapa$modelo, rev(intersect(names(CORES_MODELO), unique(mapa$modelo))))
  mapa$cor <- ifelse(mapa$achada == 1, as.character(mapa$modelo), "não")
  corte <- sum(ordem$arcada == "superior") + .5
  nenhum <- sum(tapply(mapa$achada, mapa$id, sum) == 0)
  ggplot2::ggplot(mapa, ggplot2::aes(pos, modelo)) +
    ggplot2::geom_tile(ggplot2::aes(fill = cor), colour = "white", linewidth = .4) +
    ggplot2::geom_vline(xintercept = corte, linetype = "22", colour = "grey30") +
    ggplot2::annotate("text", x = c(corte / 2, (corte + nrow(ordem)) / 2), y = length(levels(mapa$modelo)) + .75,
                      label = c(sprintf("Maxila (n = %d)", sum(ordem$arcada == "superior")), sprintf("Mandíbula (n = %d)", sum(ordem$arcada == "inferior"))),
                      fontface = "bold", size = 3.6) +
    ggplot2::scale_fill_manual(values = c(CORES_MODELO, "não" = "grey93"), guide = "none") +
    ggplot2::scale_x_continuous(expand = c(0, 0)) + ggplot2::scale_y_discrete(expand = ggplot2::expansion(add = c(.6, 1.1))) +
    ggplot2::labs(x = sprintf("Lesões marcadas, da menor para a maior em cada metade. Nenhum modelo encontrou %d de %d.", nenhum, nrow(ordem)), y = NULL) +
    tema_grafico() + ggplot2::theme(axis.text.x = ggplot2::element_blank(), axis.ticks.x = ggplot2::element_blank(),
                                    panel.border = ggplot2::element_blank())
}

grafico_precisao_revisao <- function(est) {
  linhas <- list()
  for (g in c("amostra", "todas")) for (m in c("precisao", "precisao_pessimista", "sensibilidade")) {
    w <- est[[g]][[m]]
    linhas[[length(linhas) + 1]] <- data.frame(grupo = if (g == "amostra") "Amostra aleatória" else "Todas as revisadas",
      medida = c(precisao = "Precisão", precisao_pessimista = "Precisão (dúvida = rejeitada)", sensibilidade = "Sensibilidade estimada")[[m]],
      p = w$p, inf = w$inf, sup = w$sup, rot = if (is.na(w$p)) "" else sprintf("%d/%d", w$x, w$n))
  }
  d <- do.call(rbind, linhas)
  if (all(is.na(d$p))) return(vazio("Nenhuma revisão concluída ainda"))
  d$grupo <- factor(d$grupo, c("Amostra aleatória", "Todas as revisadas"))
  ggplot2::ggplot(d, ggplot2::aes(medida, p, fill = grupo)) +
    ggplot2::geom_col(position = ggplot2::position_dodge(.8), width = .75, colour = "black", linewidth = .2, na.rm = TRUE) +
    ggplot2::geom_errorbar(ggplot2::aes(ymin = inf, ymax = sup), position = ggplot2::position_dodge(.8), width = 0, linewidth = .4, na.rm = TRUE) +
    ggplot2::geom_text(ggplot2::aes(y = 0.03, label = rot), position = ggplot2::position_dodge(.8), vjust = 0, size = 3.2, colour = "white") +
    ggplot2::scale_fill_manual(values = c("Amostra aleatória" = AZUL_ESCURO, "Todas as revisadas" = "#7FB2DE"), name = NULL) +
    ggplot2::scale_y_continuous(labels = pct_rot, limits = c(0, 1.02), expand = c(0, 0)) +
    ggplot2::labs(x = NULL, y = "Estimativa (IC 95%)") + tema_grafico() + ggplot2::theme(legend.position = "top")
}

grafico_deteccoes_imagem <- function(d) {
  if (!nrow(d)) return(vazio("Sem detecções"))
  t <- as.data.frame(table(n = d$n)); t$n <- factor(t$n, levels = sort(unique(d$n)))
  ggplot2::ggplot(t, ggplot2::aes(n, Freq)) +
    ggplot2::geom_col(fill = AZUL, colour = "black", linewidth = .2, width = .75) +
    ggplot2::geom_text(ggplot2::aes(label = Freq), vjust = -.4, size = 3.4) +
    ggplot2::scale_y_continuous(expand = ggplot2::expansion(mult = c(0, .12))) +
    ggplot2::labs(x = "Lesões detectadas na imagem", y = sprintf("Imagens (n = %d)", nrow(d))) + tema_grafico()
}
