---
name: odonto-visor-revisao
description: Componente JavaScript de visualização de radiografias (zoom, arraste, contraste, contornos numerados, desenho de lesões, modo cego) e o fluxo de revisão e de auditoria das lesões não encontradas no OdontoLesão. Use ao criar ou alterar mod_revisao.R, mod_auditoria.R ou inst/app/www/visor.js.
---

# Visor e fluxo de revisão

Base existente para portar: `scripts/revisao/index.html` (lógica de canvas, atalhos, estados) e
`scripts/revisao_servidor.py` (API, índice, amostra aleatória), `scripts/consolidar_revisao.py`
(estatísticas), `scripts/galeria_perdidas.py` (auditoria). Reproduzir o comportamento, não o código.

## Componente `visor`

- Arquivo único `inst/app/www/visor.js` + `visor.css`, sem bibliotecas externas (offline).
- Função R: `visor_ui(id)` gera `<div class="visor" id=...>` e inclui os assets com
  `htmltools::htmlDependency("visor", versao, src = "app/www", package = "odontolesao")`.
- Comunicação:
  - R → JS: `session$sendCustomMessage("visor:carregar", list(id, url_imagem, largura, altura,
    deteccoes = list(list(indice, pontos, confianca, decisao)), adicionadas, modo_cego))`.
  - JS → R: `Shiny.setInputValue(id + "_evento", {tipo, indice, valor, pontos, t: Date.now()},
    {priority: "event"})` para `decisao`, `adicionar`, `remover`, `concluir`, `inadequada`.
- Imagem servida reduzida a 2.000 px por `session$registerDataObj` (ou `addResourcePath` para uma pasta
  temporária); **coordenadas sempre em pixels da imagem original** (o JS converte pela razão).
- Atalhos: `1/2/3` confirma/rejeita/dúvida na lesão selecionada, `Tab` próxima, `M` desenhar, `H` modo
  cego, `0` enquadrar, `Enter` concluir, `Esc` cancelar desenho, roda = zoom no cursor, arrastar = mover.
  Ignorar atalhos quando o foco estiver em campo de texto.
- Contraste/brilho por filtro CSS no canvas (não reprocessar a imagem).
- Acessibilidade: botões com `aria-label`, foco visível, contraste AA; cores de estado também têm ícone.

## Fluxo de revisão

1. Lista à esquerda (DT ou lista própria) agrupada: **amostra aleatória** (sorteio fixo, semente 2026,
   tamanho em Configurações, padrão 60) primeiro; depois 4+ detecções, sem detecção, demais. Status:
   pendente, parcial, concluída.
2. Ao abrir: se nunca revisada e `config$modo_cego_inicial`, abre em modo cego.
3. Cada evento grava imediatamente no banco (`decisoes`, `lesoes_adicionadas`) com revisor e data; não
   há botão "salvar".
4. "Concluir" só é permitido com todas as detecções decididas ou com "imagem inadequada".
5. Vários revisores: cada revisão é por (imagem, revisor). Estatísticas usam a revisão do revisor
   selecionado; o kappa usa imagens revisadas por dois.

## Auditoria das lesões não encontradas

- Fonte: tabela `referencia` (lesões marcadas) × detecções do modelo ativo, critério por lesão de
  `fct_deteccao.R` (≥20% da região prevista sobre a lesão).
- Cartão por lesão: panorâmica com retângulo, recorte ampliado sem marcação, traço original (o traço
  desta lesão destacado; os de lesões vizinhas em branco), probabilidade do modelo. Atenção: o traço de
  uma lesão vizinha pode parecer "desalinhado" com a máscara; desenhar cada traço com a cor da sua lesão.
- Respostas: visível / duvidosa / não visível / erro de marcação, mais observação. Gravadas em
  `auditoria_perdidas`.
- Mostrar a distância até a lesão encontrada mais próxima e o diâmetro equivalente (pistas de traço
  dividido ou resíduo).

## Estatísticas da revisão

Precisão = confirmadas / (confirmadas + rejeitadas), também no cenário pessimista (dúvida = rejeitada);
sensibilidade estimada = confirmadas / (confirmadas + adicionadas); FP por imagem; tudo com IC 95% de
Wilson e **separado para a amostra aleatória** (é a que vai para o artigo). Conferir os números contra
`scripts/consolidar_revisao.py` nos testes.
