# Fase 5: U-Net v2 em R — teste de equivalência com o Python

Data: 27/10/2026 · Script: `odontolesao/tests/equivalencia.R` · Resultado por paciente:
`teste_dados/equivalencia/equivalencia_resultado.csv`

Nas 20 panorâmicas marcadas (45 lesões), limiar 0,5:

| Etapa testada | Resultado |
|---|---|
| (A) probabilidade do Python + detecção/avaliação em R | 31/45, **idêntico lesão a lesão** nos 20 pacientes |
| (B) tudo em R (leitura, redimensionamento, CLAHE, rede, detecção) | 31/45, **idêntico lesão a lesão** nos 20 pacientes, 0 falsos positivos |
| Correlação das probabilidades R × Python | ≥ 0,998 em todos (mínimo 0,99814) |
| Dice entre as máscaras R e Python | ≥ 0,988 (paciente 17: as duas vazias, concordância total) |
| Rede isolada (mesma entrada) | diferença máxima de probabilidade 0,0002 |

Critério do plano (mesmas lesões e Dice ≥ 0,95): **aprovado**.

Tempo por panorâmica em CPU de 4 núcleos: ~27 s (rede com espelhamento 17 s; pré-processamento 4 s;
detecção 4 s). O Python levava ~13–15 s; a diferença está no pré-processamento e na detecção escritos em R.

Detalhes que importaram para bater com o OpenCV:
- `EBImage::bwlabel` usa vizinhança 4; o OpenCV usa 8 → `rotular8()` une rótulos vizinhos na diagonal.
- CLAHE próprio (`clahe()`), com a mesma completação de bordas, corte, redistribuição e interpolação do OpenCV
  (posição `x / tamanho_do_bloco - 0,5`, sem meio pixel).
- Redução por área com matriz esparsa de cobertura (equivalente a `INTER_AREA`).
- `torch_set_num_threads(detectCores())`: com 3 threads em 4 núcleos a rede ficava 2,5× mais lenta.
- Diferenças de ±1 na redução por área podem virar até ±6 níveis depois do CLAHE em alguns pacientes,
  sem efeito nas lesões detectadas.
