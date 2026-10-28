VERSAO_APP <- "0.2.0"
NOME_APP <- "ML ODONTO"

tema_odonto <- function() {
  bslib::bs_theme(version = 5, primary = "#2B6CB0", secondary = "#5F6B7A", success = "#2FB36B",
                  danger = "#E5484D", warning = "#E8C33A", info = "#3FA9FF",
                  base_font = bslib::font_collection("Segoe UI", "Lucida Grande", "Helvetica Neue", "Arial", "sans-serif"),
                  code_font = bslib::font_collection("Consolas", "Cascadia Mono", "Lucida Console", "monospace"),
                  "font-size-base" = "0.8125rem", "border-radius" = "3px", "navbar-bg" = "#eceff3")
}

barra_ferramentas <- function() {
  botao <- function(id, rotulo, icone, titulo = rotulo)
    actionButton(id, rotulo, icon = shiny::icon(icone), class = "btn", title = titulo)
  htmltools::div(class = "ml-toolbar",
    botao("bt_revisar", "Continuar revisão", "play", "Abrir a próxima imagem pendente"),
    botao("bt_auditar", "Auditoria", "magnifying-glass"),
    botao("bt_resultados", "Resultados", "chart-line"),
    htmltools::span(class = "sep"),
    botao("bt_atualizar", NULL, "rotate", "Atualizar painéis e gráficos"),
    botao("bt_backup", NULL, "floppy-disk", "Fazer backup do banco agora"),
    htmltools::span(class = "sep"),
    botao("bt_ajuda", NULL, "circle-question", "Abrir a ajuda"),
    htmltools::div(class = "direita", uiOutput("toolbar_direita", inline = TRUE))
  )
}

app_ui <- function(request) {
  bslib::page_navbar(
    id = "aba",
    title = htmltools::tags$span(htmltools::tags$img(src = "odonto/logo.svg", alt = ""), NOME_APP,
                                 htmltools::tags$span(class = "sub", "Lesões periapicais")),
    theme = tema_odonto(),
    window_title = NOME_APP,
    fillable = FALSE,
    navbar_options = bslib::navbar_options(bg = "#eceff3", theme = "light"),
    header = htmltools::tagList(
      htmltools::tags$link(rel = "stylesheet", href = "odonto/estilo.css"),
      htmltools::tags$link(rel = "icon", href = "odonto/logo.svg"),
      htmltools::tags$script(src = "odonto/visor.js"),
      htmltools::tags$script(src = "odonto/paineis.js"),
      barra_ferramentas(),
      uiOutput("barra_status", class = "ml-status")
    ),
    bslib::nav_panel("Início", icon = bsicons::bs_icon("grid-1x2"), mod_inicio_ui("inicio")),
    bslib::nav_panel("Revisão", icon = bsicons::bs_icon("check2-square"), mod_revisao_ui("revisao")),
    bslib::nav_panel("Auditoria", icon = bsicons::bs_icon("search"), ml_rolar(mod_auditoria_ui("auditoria"))),
    bslib::nav_panel("Resultados", icon = bsicons::bs_icon("graph-up"), ml_rolar(mod_resultados_ui("resultados"))),
    bslib::nav_panel("Aplicar", icon = bsicons::bs_icon("cpu"), mod_aplicar_ui("aplicar")),
    bslib::nav_panel("Treinar", icon = bsicons::bs_icon("mortarboard"),
                     em_breve("Treinamento", 7, "Ajuste fino com as lesões revisadas, exportação para o Google Colab e registro de versões dos modelos.")),
    bslib::nav_panel("Relatórios", icon = bsicons::bs_icon("file-earmark-text"),
                     em_breve("Relatórios", 8, "Relatórios em HTML e PDF da revisão e dos modelos.")),
    bslib::nav_spacer(),
    bslib::nav_panel("Ajuda", icon = bsicons::bs_icon("question-circle"), ml_rolar(mod_ajuda_ui("ajuda"))),
    bslib::nav_panel("Configurações", icon = bsicons::bs_icon("gear"), ml_rolar(mod_config_ui("config")))
  )
}

em_breve <- function(titulo, fase, texto) {
  ml_rolar(htmltools::div(class = "em-breve",
    painel(titulo, htmltools::p(texto),
           htmltools::p(class = "small text-muted mb-0", sprintf("Em desenvolvimento (fase %d do plano).", fase)))))
}
