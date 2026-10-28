---
name: odonto-shiny-app
description: Arquitetura e convenções do OdontoLesão, o software standalone em R (Shiny + bslib) que reúne revisão, auditoria, aplicação do modelo, treinamento, gráficos, relatórios e manuais do projeto ML Odonto. Use ao criar ou alterar qualquer parte do pacote R `odontolesao` (estrutura, módulos, tema, banco SQLite, configurações, tarefas longas, privacidade).
---

# OdontoLesão: arquitetura do pacote R

Plano completo: `docs/PLANO_SOFTWARE.md`. Leia antes de começar uma fase nova.

## Estrutura

```
software/odontolesao/            pacote R (instalável com devtools::install / pak::local_install)
  DESCRIPTION  NAMESPACE  LICENSE
  R/
    run_app.R                    run_app(dados = caminho_dados_padrao(), porta = NULL, abrir = TRUE)
    app_ui.R  app_server.R       page_navbar() + chamada dos módulos
    mod_inicio.R                 painel
    mod_revisao.R                lista + visor + decisões
    mod_auditoria.R              lesões de referência não encontradas
    mod_aplicar.R                inferência em lote (ExtendedTask)
    mod_treinar.R                ajuste fino, exportação Colab, registro de modelos
    mod_resultados.R             gráficos e tabelas reativos
    mod_relatorios.R             geração de relatórios
    mod_ajuda.R                  manuais
    mod_config.R                 configurações e backup
    fct_banco.R                  conexão, migrações, consultas (só aqui há SQL)
    fct_imagem.R                 leitura, normalização, CLAHE, recortes
    fct_modelo.R                 definição da U-Net em torch, carregar pesos, prever
    fct_deteccao.R               picos, regiões, avaliação por lesão, FROC
    fct_metricas.R               Wilson, bootstrap, kappa
    fct_graficos.R               tema e gráficos (ver skill odonto-graficos-reativos)
    fct_importar.R               importação dos dados atuais (JSON/CSV/PNG do projeto)
    utils_*.R
  inst/
    app/www/                     visor.js, visor.css, logo.svg, favicon
    manuais/*.qmd                manuais (ver skill odonto-manuais-relatorios)
    relatorios/*.qmd
    sql/001_inicial.sql ...      migrações numeradas
    modelos/                     pesos .safetensors + modelos.yml
  tests/testthat/                testes do núcleo
  tests/shinytest2/              testes das telas
```

## Convenções

- **Idioma:** interface, mensagens, nomes de funções e variáveis em português (como o resto do projeto:
  `carregar_indice`, `detectar`, `limiar`). Datas `dd/mm/aaaa`, decimal com vírgula na tela.
- **Módulos:** `mod_x_ui(id)` / `mod_x_server(id, ctx)`. `ctx` é uma lista com `banco` (conexão),
  `config` (reativo), `modelo_ativo` (reativo) e `evento()` (registra na trilha de auditoria). Módulos
  conversam só por `ctx` e por reativos devolvidos, nunca por variáveis globais.
- **Lógica fora da UI:** toda regra (métricas, detecção, SQL) vive em `fct_*.R`, testável sem Shiny.
- **Banco:** SQLite em `<dados>/odontolesao.sqlite`, `PRAGMA foreign_keys = ON`, `journal_mode = WAL`.
  Esquema por migrações numeradas em `inst/sql/`; tabela `versao_esquema`. Escritas sempre em
  `DBI::dbWithTransaction`. Nunca montar SQL colando texto: usar `params =`.
- **Tarefas longas** (aplicar, treinar, relatórios): `shiny::ExtendedTask` + `mirai`, com progresso por
  arquivo de estado e botão cancelar. A interface nunca congela.
- **Tema:** `bslib::bs_theme(version = 5, primary = "#1F6FB4", secondary = "#6B7782",
  base_font = font_google("Inter", local = TRUE))`. Fontes embutidas (offline). Paleta de modelos igual
  à de `figuras_R/style_nature.R`. Nada de laranja como cor de destaque (pedido do usuário); estados de
  revisão: pendente cinza-azulado, confirmada verde, rejeitada vermelho, dúvida amarelo, adicionada azul.
- **Dados do usuário fora do programa:** `caminho_dados_padrao()` = `Documentos/OdontoLesao`. O programa
  pode ser substituído numa atualização sem perder dados. Backup automático diário do banco (7 cópias).
- **IDs anônimos:** imagens aparecem como A001… (ordem alfabética do arquivo, como em
  `scripts/exportar_figuras.py`). Nomes de pacientes nunca em gráficos, relatórios ou exportações.

## Privacidade (inegociável)

- Servidor só em `127.0.0.1`. Nenhuma chamada de rede em tempo de execução (sem CDN, sem fontes
  remotas, sem telemetria). Conferir com `grep -r "http" inst/app` antes de cada versão.
- Exportação para o Colab: só recortes/máscaras anonimizados, com tela de confirmação explicando o que
  sai do computador.

## Ao terminar uma mudança

1. `devtools::test()` e, se mexeu em tela, `shinytest2::test_app()`.
2. Rodar `odontolesao::run_app()` e conferir a tela no navegador do painel (preview).
3. Atualizar o manual correspondente em `inst/manuais/` se o comportamento visível mudou.
