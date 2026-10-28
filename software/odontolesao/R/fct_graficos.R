# Gráficos e diagramas (estilo das figuras do artigo, paleta azul).

AZUL <- "#1F6FB4"; AZUL_ESCURO <- "#0B2E59"; CINZA <- "#6B7782"

tema_grafico <- function(base = 12) {
  ggplot2::theme_classic(base_size = base) + ggplot2::theme(
    panel.border = ggplot2::element_rect(fill = NA, colour = "black", linewidth = .45),
    axis.line = ggplot2::element_blank(),
    axis.ticks = ggplot2::element_line(linewidth = .4), axis.ticks.length = grid::unit(-1.2, "mm"),
    axis.text = ggplot2::element_text(colour = "black"),
    axis.text.x = ggplot2::element_text(margin = ggplot2::margin(t = 4)),
    axis.text.y = ggplot2::element_text(margin = ggplot2::margin(r = 4)),
    panel.grid = ggplot2::element_blank(), plot.background = ggplot2::element_rect(fill = "transparent", colour = NA),
    panel.background = ggplot2::element_rect(fill = "white", colour = NA))
}

#' Revisões concluídas por dia (acumulado)
grafico_progresso <- function(prog) {
  if (!nrow(prog)) {
    return(ggplot2::ggplot() + ggplot2::annotate("text", x = 0, y = 0, label = "Nenhuma revisão concluída ainda",
                                                 colour = CINZA, size = 4.5) + ggplot2::theme_void())
  }
  prog$dia <- as.Date(prog$dia); prog$acumulado <- cumsum(prog$n)
  ggplot2::ggplot(prog, ggplot2::aes(dia)) +
    ggplot2::geom_col(ggplot2::aes(y = n), fill = "#B9D3EC", colour = "#5f86b3", linewidth = .3, width = .8) +
    ggplot2::geom_line(ggplot2::aes(y = acumulado), colour = AZUL, linewidth = .8) +
    ggplot2::geom_point(ggplot2::aes(y = acumulado), shape = 21, fill = AZUL, colour = "black", size = 2.4, stroke = .35) +
    ggplot2::scale_x_date(date_labels = "%d/%m", expand = ggplot2::expansion(add = 1)) +
    ggplot2::scale_y_continuous(limits = c(0, NA), breaks = scales::breaks_pretty(), expand = ggplot2::expansion(mult = c(0, .1))) +
    ggplot2::labs(x = NULL, y = "Imagens revisadas", caption = "Barras: por dia · linha: acumulado") +
    tema_grafico() + ggplot2::theme(plot.caption = ggplot2::element_text(colour = CINZA))
}

#' Diagrama do fluxo de trabalho com as contagens atuais (Graphviz)
diagrama_fluxo <- function(cont, est, modelo) {
  fmt <- function(x) format(x, big.mark = ".", decimal.mark = ",")
  prec <- if (is.na(est$amostra$precisao$p)) "—" else paste0(round(100 * est$amostra$precisao$p), "%")
  dot <- sprintf('
digraph fluxo {
  graph [rankdir = LR, bgcolor = "transparent", nodesep = .35, ranksep = .45, fontname = "Helvetica"]
  node  [shape = box, style = "rounded,filled", fontname = "Helvetica", fontsize = 11, color = "#0B2E59", penwidth = 1.2, margin = "0.18,0.1"]
  edge  [color = "#5b6b7c", arrowsize = .7]
  dados   [label = "Dados públicos\\nHanói + DENTEX", fillcolor = "#EAF2FA"]
  modelo  [label = "%s", fillcolor = "#B9D3EC"]
  acervo  [label = "Acervo local\\n%s panorâmicas", fillcolor = "white"]
  det     [label = "Detecções\\n%s lesões em %s imagens", fillcolor = "#D5E5F3"]
  rev     [label = "Revisão do especialista\\n%s imagens concluídas\\namostra: %s de %s", fillcolor = "#D9F2E3"]
  metr    [label = "Precisão na amostra\\n%s", fillcolor = "white"]
  treino  [label = "Novas marcações\\n%s lesões adicionadas", fillcolor = "white"]
  dados -> modelo -> det; acervo -> det -> rev -> metr; rev -> treino; treino -> modelo [style = dashed, label = " ajuste fino", fontsize = 9, fontcolor = "#5b6b7c"]
}', gsub('"', "'", modelo$descricao %||% modelo$nome), fmt(cont$imagens), fmt(cont$deteccoes), fmt(cont$com_deteccao),
     fmt(cont$concluidas), cont$amostra_concluidas, cont$amostra, prec, fmt(est$todas$adicionadas))
  DiagrammeR::grViz(dot)
}
