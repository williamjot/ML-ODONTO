# Figuras em R

Gerar de novo (a partir da pasta do projeto):

```
python scripts/exportar_figuras.py          # tabelas em Resultados/figuras/dados/
Rscript figuras_R/fig1_validacao.R
Rscript figuras_R/fig2_aplicacao.R
Rscript figuras_R/fig3_diagrama.R
```

Cada figura sai em SVG (texto editável), PDF, TIFF 600 dpi (LZW) e PNG de prévia, com um `*_source.csv` por painel.
Estilo: `figuras_R/style_nature.R`. Pacotes: ggplot2, patchwork, dplyr, tidyr, scales, viridisLite, svglite, ragg.

## Legendas (rascunho)

**Figura 1 | Validação e escolha do modelo nas 20 panorâmicas marcadas (45 lesões).**
Cada paciente foi avaliado por um modelo que não o viu no treino (RF e U-Net com ajuste fino: validação
cruzada em 5 partes; RF e U-Net de Hanói: nunca viram as 20 imagens). Uma lesão conta como achada quando
alguma região prevista tem ≥20% da sua área sobre ela.
**a**, Curvas FROC: sensibilidade por lesão em função de falsos positivos por imagem (fronteira de
melhor sensibilidade em cada nível de falsos positivos); faixas, IC 95% de Wilson (n = 45); pontos, limiar de
operação (melhor F1). As curvas da U-Net terminam perto de zero porque ela quase não gera candidatos falsos.
**b**, Sensibilidade, precisão e Dice no limiar de operação; barras de erro, IC 95% (Wilson para proporções;
bootstrap por paciente, 2.000 reamostragens, para o Dice). Cores como em a.
**c**, Lesões marcadas segundo o diâmetro equivalente (px, imagem com 1.400 px de largura) e a arcada,
achadas (preenchidas) ou perdidas (abertas) pela U-Net de Hanói; tracejados, medianas. A arcada é
aproximada pela posição vertical na imagem (acima ou abaixo da meia altura). Dispersão vertical apenas para
visualização.
**d**, Treino da U-Net no Google Colab: perda média a cada 50 iterações (acima) e Dice na validação interna de
Hanói (200 imagens) a cada 500 iterações (abaixo); linha vertical, iteração do modelo salvo (5.500).

**Figura 2 | Aplicação da U-Net às 422 panorâmicas sem marcação.**
**a**, Número de lesões detectadas por imagem.
**b**, Posição dos centros das 673 detecções (fração da largura e da altura da imagem); manchas, densidade
2D (kernel) normalizada. As linhas tracejadas (meia largura e meia altura) são referências aproximadas, não
anatômicas.
**c**, Distribuição acumulada do diâmetro equivalente das lesões detectadas na aplicação e das lesões marcadas
na validação (achadas e perdidas); tracejados, medianas. As detecções da aplicação têm tamanho semelhante ao
das lesões achadas, o que sugere que lesões pequenas também passam despercebidas nas 422 imagens
(interpretação; não há marcação nelas para confirmar).
**d**, Confiança das detecções (pico do mapa de probabilidade suavizado). Sem marcação de referência, a
precisão na aplicação não pode ser medida.

**Figura 3 | Fluxo de trabalho.** Dados, pré-processamento, modelos, validação e aplicação. Só o conjunto público
de Hanói foi enviado à nuvem (treino da U-Net no Colab); as radiografias de pacientes foram processadas
apenas no computador local.
