# Comparação: Random Forest × U-Net (20 panorâmicas marcadas, 45 lesões)

Cada paciente é avaliado por um modelo que não o viu no treino. Lesão achada = alguma região prevista com ≥20% de sobreposição com ela. Sensibilidade em função de falsos positivos (FP) por imagem; depois, o ponto de melhor F1.

| Modelo | Sens. 0,5 FP | Sens. 1 FP | Sens. 2 FP | Sens. 4 FP | F1 | Sens. | Precisão | FP/img | Dice | Limiar |
|---|---|---|---|---|---|---|---|---|---|---|
| RF B (só Hanói) | 4% | 4% | 7% | 11% | 0.08 | 16% | 6% | 5.8 | 0.04 | 0.325 |
| RF C (Hanói + nossas, val. cruzada) | 4% | 11% | 11% | 11% | 0.15 | 11% | 25% | 0.6 | 0.14 | 0.65 |
| U-Net (só Hanói) | 69% | 69% | 69% | 69% | 0.78 | 64% | 100% | 0.0 | 0.57 | 0.5 |
| U-Net + ajuste fino nas nossas (val. cruzada) | 69% | 69% | 69% | 69% | 0.80 | 67% | 100% | 0.0 | 0.60 | 0.6 |
| U-Net v2 (2000 px + Tversky, só Hanói) | 76% | 76% | 76% | 76% | 0.82 | 69% | 100% | 0.0 | 0.58 | 0.5 |
| U-Net v2 + ajuste fino nas nossas (val. cruzada) | 71% | 76% | 76% | 76% | 0.79 | 67% | 97% | 0.1 | 0.62 | 0.95 |
| Duas etapas: ápice (DENTEX) + recorte (Hanói) | 64% | 67% | 69% | 69% | 0.70 | 64% | 76% | 0.5 | 0.53 | 0.95 |
