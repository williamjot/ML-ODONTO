# Migração do ML ODONTO para C++ nativo

Data: 07/11/2026 · Código: [`cpp/desktop`](../cpp/desktop) · Montagem: `python cpp/tools/build_desktop.py`

O programa deixou de depender de R. A versão nativa (C++20, Qt 6.8 LTS, ONNX Runtime) tem todas as telas
do app R e usa **o mesmo banco SQLite**, com as mesmas migrações (`software/odontolesao/inst/sql`), o mesmo
manual e o mesmo cache de imagens. As duas versões podem ser usadas no mesmo computador, sobre os mesmos dados.

## O que corresponde a quê

| App R | Programa nativo |
|---|---|
| `fct_banco.R` (SQL, migrações, estatísticas) | `Database.cpp`: as mesmas consultas, uma a uma |
| `fct_metricas.R`, `fct_resultados.R` | `Metrics.cpp`: Wilson, FROC, bootstrap por paciente (quantil tipo 7), kappa |
| ggplot2 e Graphviz (`fct_graficos*.R`) | `Charts.cpp`: QPainter, estilo Nature, PNG em 3× |
| `visor.js` | `ImageViewer.cpp`: zoom, arraste, contraste/brilho, contornos, desenho, modo cego, atalhos |
| `utils_layout.R`, `estilo.css` | `Pane.cpp`: painéis com abas, divisórias arrastáveis, tema |
| `mod_inicio.R` … `mod_config.R` | `pages/*Page.cpp`, uma por tela |
| `fct_aplicar.R` + motor C++ por processo externo | `AplicarPage.cpp`: pipeline C++ dentro do programa, numa thread |
| `cartao_auditoria()` | `AuditoriaPage.cpp` (mesmos nomes de cache: `aud_<id>_*.png`) |
| `lesoes_da_mascara()` | `Contours.cpp`: componentes com vizinhança 8 e contorno de Moore |

Só módulos LGPL do Qt (Core, Gui, Widgets, Sql, e o plugin de TIFF); os gráficos são desenhados à mão,
sem o Qt Charts (GPL).

## Testes feitos (cópia do banco real, 422 panorâmicas)

| Teste | Resultado |
|---|---|
| Início: contagens, precisão, sensibilidade (31/45), atividade | iguais às do app R |
| Resultados: FROC, métricas com IC, mapa das 45 lesões, tabela, auditoria | iguais ao app R ("nenhum modelo encontrou 12 de 45") |
| Auditoria: 14 cartões com as quatro imagens e as respostas | ok |
| Revisão: confirmar, rejeitar, concluir com aviso de detecções sem decisão | gravado no banco, evento registrado, abre a próxima |
| Aplicar nas radiografias 03 e 15 | **as mesmas 7 lesões do app R** (centro, área e confiança), ~11 s por radiografia |
| Banco novo, sem nada do R | cria o esquema, registra os modelos e aplica normalmente |
| Abertura do programa | ~3 s |

## O que ainda não foi portado

- **Importação do acervo do projeto** (`importar_projeto()` e `importar_referencia()`): as 422 panorâmicas,
  as máscaras de `Resultados/aplicacao_unet_v2` e a referência das 20 marcadas. É feita uma única vez e o
  banco deste computador já a tem; num computador novo, faça-a uma vez pela versão em R ou copie o banco.
- Treinar e Relatórios, que já eram "em breve" no app R.
- O desenho à mão de lesões e os atalhos de teclado do visor foram escritos e revisados, mas não testados
  automaticamente (o teste usa a acessibilidade do Windows, que aciona botões, não o mouse).
