---
name: odonto-graficos-reativos
description: Gráficos, tabelas e diagramas que se atualizam com os dados dentro do OdontoLesão (Shiny), no mesmo estilo Nature e paleta azul das figuras do artigo. Use ao criar ou alterar mod_inicio.R, mod_resultados.R, fct_graficos.R, fct_metricas.R ou o diagrama do fluxo.
---

# Gráficos e diagramas reativos

Base visual: `figuras_R/style_nature.R` (tema `theme_nat`, `CORES_MODELO`, `FORMAS_MODELO`, `wilson`,
`ticks_in`, `note`) e as figuras `figuras_R/fig1_validacao.R`, `fig2_aplicacao.R`, `fig3_diagrama.R`.
Para regras gerais de estilo, consulte também as skills `nature-style-r` e `nature-figure`.

## Princípios

- Copiar `style_nature.R` para `R/fct_graficos.R` como funções do pacote (sem `source()`), com
  `base_family` configurável: na tela usar a fonte do tema (Inter, 12 pt); na exportação, Arial 7 pt.
- Paleta azul fixa por modelo; **sem laranja**. Cores de estado da revisão iguais às do visor.
- Cada gráfico é uma função pura `grafico_x(dados, ...)` que devolve um ggplot; o módulo só faz
  `renderPlot(grafico_x(dados_reativos()), res = 110)`. Assim o mesmo gráfico serve à tela, ao
  relatório e à exportação.
- Dados vêm de consultas em `fct_banco.R` encapsuladas em `reactive()`, invalidadas por um
  `reactiveVal` "versão dos dados" que o módulo de revisão incrementa a cada gravação
  (`ctx$dados_mudaram()`). Nada de `invalidateLater` em loop.
- `bindCache()` nos gráficos caros, com a versão dos dados como chave.
- Textos dos gráficos só com dados (rótulos, contagens, IC). Comentários interpretativos ficam no
  manual ou no relatório, não dentro do gráfico.

## Catálogo

| Gráfico | Fonte | Onde |
|---|---|---|
| Cartões (value boxes): revisadas, amostra x/60, precisão (IC), lesões adicionadas | revisões | Início |
| Diagrama do fluxo com contagens atuais | `DiagrammeR::grViz` gerado de um template DOT com números do banco | Início, Ajuda |
| Progresso da revisão ao longo dos dias | revisões | Início |
| FROC e métricas com IC por modelo | detecções × referência | Resultados |
| Mapa de lesões encontradas (lesão × modelo, ordenado por arcada e diâmetro) | referência | Resultados |
| Precisão na amostra aleatória vs. todas | revisões | Resultados |
| Lesões por imagem, posição das detecções, diâmetro, confiança | detecções | Resultados |
| Concordância entre revisores (kappa de Cohen com IC bootstrap) | revisões duplas | Resultados |
| Curvas de treino do ajuste fino | log do treino | Treinar |

## Exportação

Botão em cada gráfico: PNG 300 dpi, PDF (cairo) e SVG (`svglite`), tamanhos de revista (89 e 183 mm),
como `save_nat()`. Nome de arquivo com data e versão do modelo.

## Conferência

Os números exibidos devem coincidir com `scripts/consolidar_revisao.py` (revisão) e com
`Resultados/figuras/dados/*.csv` (validação). Escrever um teste por métrica.
