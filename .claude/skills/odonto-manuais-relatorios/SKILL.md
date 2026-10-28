---
name: odonto-manuais-relatorios
description: Manuais (dentista e técnico), ajuda contextual e relatórios gerados pelo OdontoLesão em Quarto/R Markdown, em português, offline. Use ao escrever ou alterar inst/manuais, inst/relatorios, mod_ajuda.R ou mod_relatorios.R.
---

# Manuais e relatórios

## Manuais (inst/manuais/)

| Arquivo | Público | Conteúdo |
|---|---|---|
| `manual_dentista.qmd` | dentista revisor | abrir o programa, revisar (amostra aleatória primeiro), atalhos, auditoria das lesões não encontradas, dúvidas comuns |
| `manual_tecnico.qmd` | pesquisador | instalação, pastas de dados, aplicar modelo, ajuste fino, exportar para o Colab, registro de modelos, backup |
| `metodos.qmd` | orientador/revisores | resumo dos métodos e métricas (critério por lesão, FROC, Wilson, bootstrap), com referências |
| `faq.qmd`, `atalhos.qmd`, `sobre.qmd` | todos | perguntas frequentes, tabela de teclas, versão e licenças (inclui CC BY-NC-SA do DENTEX) |

Base de conteúdo: `Revisao/GUIA_DO_DENTISTA.html` e `Revisao/COMO_REVISAR.md`.

- Renderizar os manuais **no build** (não em tempo de execução) para HTML autocontido
  (`embed-resources: true`) e mostrar em `mod_ajuda.R` com `includeHTML`/iframe. Assim o usuário final
  não precisa de Quarto.
- Botão "?" em cada tela abre a seção correspondente do manual (âncoras estáveis: `#revisao`,
  `#auditoria`, `#aplicar`, ...).
- Capturas de tela geradas por `shinytest2` com dados de demonstração (nunca radiografias de pacientes);
  usar panorâmicas do DENTEX/Hanói ou imagens sintéticas.

## Estilo de escrita

- Português claro, frases curtas, voz ativa no manual do dentista ("Clique em…", "Aperte M").
- Instruções numeradas para procedimentos; tabelas para atalhos e opções.
- Sem jargão de aprendizado de máquina no manual do dentista; quando inevitável, explicar uma vez.
- Nada de promessas de desempenho diagnóstico; o programa apoia a revisão, não substitui o especialista.

## Relatórios (inst/relatorios/)

- `relatorio_revisao.qmd`: parâmetros `banco`, `revisor`, `modelo`, `data`. Conteúdo: resumo, tabela de
  métricas com IC (amostra aleatória e todas), gráficos de `fct_graficos.R`, lista de lesões adicionadas
  por imagem (IDs anônimos), concordância entre revisores se houver.
- `relatorio_modelo.qmd`: comparação de modelos do registro (validação nas 20 marcadas e na amostra
  revisada).
- Gerados em tarefa de fundo para `Documentos/OdontoLesao/relatorios/AAAA-MM-DD_tipo.html|pdf`.
- Exigem Quarto embutido na distribuição (ver skill odonto-standalone-windows) ou, na falta dele,
  `rmarkdown` com `pagedown`/HTML; detectar e avisar na tela.
