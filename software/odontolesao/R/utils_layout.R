# Blocos de layout do ambiente de trabalho: painéis, painéis com abas e divisórias arrastáveis.

intercalar_divisorias <- function(filhos) {
  filhos <- Filter(Negate(is.null), filhos)
  saida <- list()
  for (k in seq_along(filhos)) {
    if (k > 1) saida[[length(saida) + 1]] <- htmltools::div(class = "ml-gutter", role = "separator")
    saida[[length(saida) + 1]] <- filhos[[k]]
  }
  saida
}

#' Painéis lado a lado (divisória vertical arrastável). `tamanhos` em CSS flex-basis, ex.: c("260px", NA)
ml_linha <- function(..., tamanhos = NULL) ml_caixa("ml-row", list(...), tamanhos)
#' Painéis empilhados (divisória horizontal arrastável)
ml_coluna <- function(..., tamanhos = NULL) ml_caixa("ml-col", list(...), tamanhos)

ml_caixa <- function(classe, filhos, tamanhos) {
  filhos <- Filter(Negate(is.null), filhos)
  if (!is.null(tamanhos)) filhos <- Map(function(f, t) {
    if (is.na(t)) f else htmltools::tagAppendAttributes(f, style = sprintf("flex: 0 0 %s;", t))
  }, filhos, rep_len(tamanhos, length(filhos)))
  htmltools::div(class = classe, intercalar_divisorias(filhos))
}

#' Área de trabalho de altura total (entre a barra de ferramentas e a barra de status)
ml_area <- function(...) htmltools::div(class = "ml-area", ...)
#' Área com rolagem, para telas de conteúdo longo
ml_rolar <- function(...) htmltools::div(class = "ml-rolar", ...)

icones_painel <- function() htmltools::span(class = "ml-icones", bsicons::bs_icon("dash-square"), bsicons::bs_icon("window"))

#' Painel com cabeçalho (título e ferramentas) e corpo rolável
painel <- function(titulo, ..., ferramentas = NULL, icone = NULL, corpo_classe = "", corpo_estilo = NULL) {
  htmltools::div(class = "card ml-pane",
    htmltools::div(class = "card-header", if (!is.null(icone)) bsicons::bs_icon(icone), titulo,
                   htmltools::span(class = "ms-auto d-flex gap-1 align-items-center", ferramentas), icones_painel()),
    htmltools::div(class = paste("ml-pane-body", corpo_classe), style = corpo_estilo, ...))
}

#' Painel com abas no cabeçalho (como os painéis do RStudio)
painel_abas <- function(id, ..., ferramentas = NULL) {
  abas <- list(...)
  ids <- paste0(id, "_", seq_along(abas))
  cab <- htmltools::tags$ul(class = "nav nav-tabs", role = "tablist",
    lapply(seq_along(abas), function(k) htmltools::tags$li(class = "nav-item", role = "presentation",
      htmltools::tags$button(class = paste("nav-link", if (k == 1) "active"), `data-bs-toggle` = "tab",
                             `data-bs-target` = paste0("#", ids[k]), type = "button", role = "tab", names(abas)[k]))))
  corpo <- htmltools::div(class = "tab-content ml-pane-body p-0",
    lapply(seq_along(abas), function(k) htmltools::div(class = paste("tab-pane h-100", if (k == 1) "active show"),
                                                       id = ids[k], role = "tabpanel", abas[[k]])))
  htmltools::div(class = "card ml-pane",
    htmltools::div(class = "card-header", cab, htmltools::span(class = "ms-auto d-flex gap-1", ferramentas), icones_painel()),
    corpo)
}
