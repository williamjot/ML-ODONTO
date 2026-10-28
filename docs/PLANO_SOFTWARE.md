# OdontoLesão: plano do software standalone em R

Software único, em R, que reúne num só lugar o que hoje está espalhado em scripts Python, páginas HTML e
arquivos .bat: revisão das detecções pelo dentista, auditoria das lesões não encontradas, aplicação do
modelo a novas radiografias, treinamento (ajuste fino local e preparo do retreino no Colab), gráficos e
diagramas que se atualizam com os dados, relatórios e manuais.

Data do plano: 27/10/2026 · Autor: William Jota (LEMAG/UnB)

---

## 1. Objetivos e requisitos

| # | Requisito | Como será atendido |
|---|---|---|
| R1 | Um programa só, instalado e aberto com um clique, sem o usuário instalar R | Pacote R + R portátil + atalho; instalador Windows (Inno Setup) |
| R2 | Funciona offline e nenhuma radiografia de paciente sai do computador | Shiny em `127.0.0.1`, banco local SQLite, sem chamadas externas |
| R3 | Uso por dentistas, sem conhecimento técnico | Interface em português, fluxo guiado, atalhos de teclado, manual embutido |
| R4 | Revisão: confirmar/rejeitar/dúvida, desenhar lesões, modo cego, amostra aleatória | Módulo de revisão com visualizador próprio (canvas) |
| R5 | Estatísticas e gráficos sempre atualizados | Gráficos reativos (ggplot2, estilo Nature do projeto) recalculados a cada revisão |
| R6 | Aplicar o modelo em novas radiografias | Inferência em R com `torch` (CPU), pesos convertidos da U-Net v2 |
| R7 | Treinamento | Ajuste fino local (decodificador, CPU) com as revisões; exportação de pacote + notebook para retreino completo no Colab |
| R8 | Rastreabilidade | Cada revisão, modelo e resultado com data, revisor e versão; trilha de auditoria |
| R9 | Profissional | Tema próprio (bslib, paleta azul do artigo), relatórios PDF/HTML, ícone, tela de abertura, versão |

**Fora do escopo:** treinar a U-Net inteira do zero no computador local. Sem GPU, isso levaria dias; o
software prepara os dados e o notebook, e o treino pesado continua no Colab, como hoje.

---

## 2. Arquitetura

```
┌───────────────────────── OdontoLesão (pacote R "odontolesao") ─────────────────────────┐
│ Interface Shiny + bslib (navegador local, 127.0.0.1)                                    │
│  Início │ Revisão │ Auditoria │ Aplicar │ Treinar │ Resultados │ Relatórios │ Ajuda │ ⚙ │
├──────────────────────────────────────────────────────────────────────────────────────┤
│ Módulos Shiny (mod_*.R)      Componente JS "visor" (canvas: zoom, contornos, desenho)   │
├──────────────────────────────────────────────────────────────────────────────────────┤
│ Núcleo (fct_*.R): pré-processamento · inferência torch · detecção · métricas · gráficos │
├──────────────────────────────────────────────────────────────────────────────────────┤
│ Dados: SQLite (revisões, imagens, detecções, modelos, auditoria) + pastas de imagens    │
│        Pesos .safetensors · configuração YAML · logs                                     │
└──────────────────────────────────────────────────────────────────────────────────────┘
```

**Escolhas técnicas**

| Camada | Escolha | Motivo |
|---|---|---|
| Interface | `shiny` + `bslib` (Bootstrap 5) + `bsicons` | padrão do R, visual moderno, temas |
| Estrutura | pacote R com módulos (convenção `golem`, sem dependência dele em produção) | testável, versionado, instalável |
| Visor de imagem | componente JavaScript próprio (canvas), reaproveitando a lógica de `scripts/revisao/index.html` | zoom/arrastar/desenhar com fluidez; Shiny puro não dá conta |
| Tabelas | `DT` | filtros e ordenação |
| Gráficos | `ggplot2` + `patchwork` com `style_nature.R` do projeto; `plotly` só onde interação ajuda | mesma identidade das figuras do artigo |
| Diagramas | `DiagrammeR` (Graphviz) com contagens vindas do banco | diagrama do fluxo sempre atualizado |
| Imagens | `magick` (leitura/gravação) + `EBImage` (CLAHE, rótulos, morfologia, suavização) | cobre todas as operações do OpenCV usadas |
| Modelos | `torch` + `torchvision` do R, pesos em `.safetensors` | inferência e ajuste fino sem Python |
| Banco | `RSQLite` + `DBI` | um arquivo, transações, sem servidor |
| Tarefas longas | `mirai`/`future` + `promises` (ExtendedTask do Shiny) | aplicar/treinar sem travar a interface |
| Relatórios e manuais | Quarto (`.qmd`) ou R Markdown → HTML/PDF | manuais versionados, relatórios reproduzíveis |
| Distribuição | R portátil + `renv` + lançador + Inno Setup | instala sem permissões de administrador e roda offline |

---

## 3. Módulos (telas)

1. **Início (painel).** Cartões com: imagens no acervo, revisadas (amostra aleatória x/60), detecções
   confirmadas/rejeitadas, precisão estimada com IC 95%, lesões adicionadas, modelo ativo. Diagrama do
   fluxo com os números atuais. Atalhos para "continuar revisão".
2. **Revisão de detecções.** Lista com filtros (grupo: amostra aleatória, 4+ detecções, sem detecção,
   demais; status). Visor com zoom/arraste, contraste/brilho, modo cego (H), contornos numerados,
   teclas 1/2/3, desenho de lesão (M), "imagem inadequada", observações, salvamento automático.
3. **Auditoria das lesões não encontradas.** A página atual de 14 casos, generalizada: qualquer lesão de
   referência não encontrada pelo modelo ativo, com recorte ampliado, traço original, máscara e
   probabilidade; respostas gravadas no banco.
4. **Aplicar modelo.** Escolher pasta ou arquivos → pré-processamento → U-Net v2 → detecções no banco,
   máscaras e sobreposições; barra de progresso; cancelamento.
5. **Treinar.** (a) Ajuste fino do decodificador da U-Net com as lesões confirmadas/desenhadas, com
   validação cruzada por paciente e comparação automática com o modelo ativo; (b) exportar pacote de
   dados anonimizado + notebook Colab para retreino completo; (c) registro de modelos (versão, data,
   métricas), com "ativar" e "voltar ao anterior".
6. **Resultados.** Gráficos reativos: FROC, métricas com IC, mapa de lesões encontradas, distribuição
   das detecções, concordância entre revisores (kappa) quando houver dois. Exportar PNG/PDF/SVG.
7. **Relatórios.** Gera relatório de revisão (HTML/PDF) com tabelas e gráficos do momento, para anexar
   ao artigo ou enviar ao orientador.
8. **Ajuda.** Manual do dentista, manual técnico, perguntas frequentes, atalhos, "sobre/versão/licenças".
9. **Configurações.** Pastas de dados, nome do revisor, limiar de operação, tamanho da amostra
   aleatória, idioma das datas, backup/restauração do banco.

---

## 4. Modelo de dados (SQLite)

| Tabela | Campos principais |
|---|---|
| `imagens` | id anônimo (A001…), arquivo, hash, largura, altura, grupo, data de importação |
| `modelos` | id, nome, arquivo de pesos, tipo, limiar, métricas (JSON), ativo, criado_em |
| `deteccoes` | id, imagem_id, modelo_id, índice, contorno (JSON), centro, confiança |
| `revisoes` | id, imagem_id, revisor, iniciada_em, concluída_em, inadequada, observações |
| `decisoes` | revisao_id, deteccao_id, decisão (confirmada/rejeitada/dúvida), data |
| `lesoes_adicionadas` | revisao_id, contorno (JSON), data |
| `referencia` | lesões marcadas (PDF de treinamento): paciente, contorno, origem |
| `auditoria_perdidas` | lesão de referência, modelo, resposta, observação, revisor |
| `eventos` | trilha de auditoria: quem, quando, o quê |

As revisões atuais em `Revisao/revisoes/*.json` e o CSV da página de lesões perdidas serão importados na
primeira execução.

---

## 5. Modelos em R

1. Converter uma vez, em Python, os pesos (`unet_lesoes_v2.pt`, `localizador_apices.pt` etc.) para
   `.safetensors`, apenas o `state_dict`.
2. Reescrever em R torch a U-Net ResNet34 (`unet.py`) com os **mesmos nomes de parâmetros**.
3. Reescrever o pré-processamento (largura 1.400/2.000 px, CLAHE 2,0 / 8×8) e a detecção (`deteccao.py`).
4. **Teste de equivalência obrigatório:** nas 20 panorâmicas marcadas, o R deve reproduzir os
   resultados do Python: mesmas lesões encontradas (31/45 no limiar 0,5) e Dice entre máscaras R e
   Python ≥ 0,95. Sem isso, o módulo Aplicar não é liberado.

---

## 6. Distribuição standalone (Windows)

- R portátil (4.6.x) dentro da pasta do programa; bibliotecas fixadas com `renv.lock`.
- `OdontoLesao.exe` ou atalho `.vbs` que abre o R portátil sem janela preta, inicia o Shiny e abre o
  navegador padrão em modo aplicativo (`--app=`), com ícone próprio.
- Instalador Inno Setup: instala em `%LOCALAPPDATA%\OdontoLesao` (sem administrador), cria atalho no
  menu e na área de trabalho, mantém os dados do usuário em atualizações.
- Dados do usuário separados do programa: `Documentos\OdontoLesao\` (banco, imagens importadas,
  relatórios, backups automáticos diários).

---

## 7. Etapas e entregas

| Fase | Entrega | Critério de aceite |
|---|---|---|
| 0. Ambiente | Pacotes instalados, `renv` iniciado, pesos em safetensors | `renv::status()` limpo; pesos carregam em R |
| 1. Esqueleto | Pacote `odontolesao`, tema, navegação, configurações, banco + importação dos dados atuais | App abre; dados atuais aparecem no Início |
| 2. Revisão | Visor JS + módulo de revisão + amostra aleatória + salvamento | Revisão completa de uma imagem por teclado; dados no banco |
| 3. Auditoria | Módulo das lesões não encontradas | 14 casos atuais navegáveis e respondidos |
| 4. Resultados | Estatísticas e gráficos reativos, diagrama atualizável | Números iguais aos de `consolidar_revisao.py` |
| 5. Modelo em R | U-Net v2 em R torch + pré-processamento + detecção | Teste de equivalência (seção 5) aprovado |
| 6. Aplicar | Lote de imagens com progresso | 422 imagens processadas; resultados no banco |
| 7. Treinar | Ajuste fino local + exportação Colab + registro de modelos | Modelo novo comparado e ativável |
| 8. Relatórios e ajuda | Manuais e relatórios Quarto | Manual do dentista e relatório de revisão gerados |
| 9. Distribuição | R portátil + lançador + instalador | Instala e roda num PC sem R |
| 10. Testes | `testthat` (núcleo) + `shinytest2` (telas) | Testes passando |

Ordem de prioridade: as fases 1 a 4 entregam o que o dentista precisa já (revisão e números); as fases 5
a 7 trocam o Python pelo R na inferência e no treino; as fases 8 a 10 deixam o produto apresentável.

---

## 8. Riscos

| Risco | Mitigação |
|---|---|
| CLAHE/redimensionamento do R diferentes do OpenCV mudam o resultado do modelo | Teste de equivalência na fase 5; se necessário, implementar CLAHE próprio igual ao do OpenCV |
| `torch` do R exige download do libtorch (~200 MB) na instalação | Embutir libtorch no pacote standalone; instalar uma vez com `torch::install_torch()` |
| Inferência lenta em CPU (≈15 s/imagem) | Processamento em segundo plano com progresso; lote noturno |
| Visor de imagem pesado em panorâmicas grandes | Enviar imagem reduzida (2.000 px) e coordenadas originais, como a ferramenta atual |
| Dados de pacientes | Tudo local; exportações para o Colab só com dados anonimizados e com confirmação explícita na tela |

---

## 9. Skills criadas para o desenvolvimento

Ficam em `.claude/skills/` do projeto e orientam cada parte do trabalho:

| Skill | Para quê |
|---|---|
| `odonto-shiny-app` | Arquitetura, convenções do pacote, tema, módulos, banco, privacidade |
| `odonto-visor-revisao` | Componente JS do visor e o fluxo de revisão/auditoria |
| `odonto-torch-r` | Portar modelos PyTorch para R torch, safetensors, teste de equivalência |
| `odonto-graficos-reativos` | Gráficos e diagramas atualizáveis no estilo do artigo |
| `odonto-manuais-relatorios` | Manuais e relatórios em Quarto/R Markdown |
| `odonto-standalone-windows` | Empacotamento com R portátil, lançador e instalador |
