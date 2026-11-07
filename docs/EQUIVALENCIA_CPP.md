# Motor C++ (ONNX Runtime) — teste de equivalência

Data: 07/11/2026 · Script: `cpp/tools/validate_pipeline.py` · Executável: `ml-odonto-cli run`
(Zig/clang 22, ONNX Runtime 1.31.1, CPU) · Modelo: `unet_lesoes_v2.onnx`, exportado de
`unet_lesoes_v2.safetensors` (erro máximo PyTorch × ONNX: 1,3 × 10⁻⁷)

Nas 20 panorâmicas marcadas (45 lesões), limiar 0,5, tudo em C++ — leitura do PNG, redução por área,
CLAHE, rede com espelhamento, redução da probabilidade para 1.400 px, detecção e máscara:

| Critério | Exigido | Resultado |
|---|---|---|
| Lesões encontradas, uma a uma | as mesmas da U-Net v2 em Python | 31/45, **idêntico lesão a lesão** nos 20 pacientes |
| Falsos positivos | nenhum | 0 |
| Correlação das probabilidades C++ × Python | ≥ 0,998 | ≥ 0,998 em todos (mínimo 0,99814) |
| Dice das máscaras C++ × Python | ≥ 0,95 | ≥ 0,988 (paciente 17: as duas vazias) |
| Só a detecção (mesma probabilidade, `compare_reference.py`) | — | Dice 1,000 nos 20 |

Critério do plano: **aprovado**. Os números coincidem com os do R
([`EQUIVALENCIA_R_PYTHON.md`](EQUIVALENCIA_R_PYTHON.md)): as pequenas diferenças de probabilidade vêm da
normalização (±1 nível na redução por área, amplificado pelo CLAHE), não da rede nem da detecção.

| Paciente | Lesões | Achadas (Python) | Achadas (C++) | Iguais | Falsos | Correlação | Dice | Tempo |
|---|---|---|---|---|---|---|---|---|
| 01 | 1 | 1 | 1 | sim | 0 | 1,00000 | 1,000 | 10,7 s |
| 02 | 3 | 1 | 1 | sim | 0 | 0,99984 | 0,997 | 14,8 s |
| 03 | 6 | 4 | 4 | sim | 0 | 0,99908 | 0,998 | 16,9 s |
| 04 | 1 | 1 | 1 | sim | 0 | 0,99919 | 0,994 | 14,9 s |
| 05 | 1 | 1 | 1 | sim | 0 | 0,99974 | 1,000 | 15,9 s |
| 06 | 1 | 1 | 1 | sim | 0 | 0,99814 | 0,988 | 14,6 s |
| 07 | 2 | 2 | 2 | sim | 0 | 0,99998 | 0,999 | 12,2 s |
| 08 | 1 | 1 | 1 | sim | 0 | 0,99996 | 0,998 | 15,1 s |
| 09 | 2 | 1 | 1 | sim | 0 | 0,99995 | 0,998 | 11,4 s |
| 10 | 3 | 2 | 2 | sim | 0 | 0,99984 | 0,997 | 11,8 s |
| 11 | 1 | 1 | 1 | sim | 0 | 0,99998 | 0,999 | 15,6 s |
| 12 | 2 | 1 | 1 | sim | 0 | 0,99937 | 1,000 | 11,1 s |
| 13 | 2 | 1 | 1 | sim | 0 | 0,99999 | 1,000 | 10,8 s |
| 14 | 1 | 1 | 1 | sim | 0 | 0,99994 | 0,998 | 10,0 s |
| 15 | 5 | 4 | 4 | sim | 0 | 0,99846 | 0,993 | 10,5 s |
| 16 | 4 | 4 | 4 | sim | 0 | 0,99970 | 0,997 | 12,1 s |
| 17 | 1 | 0 | 0 | sim | 0 | 0,99850 | 1,000 | 12,7 s |
| 18 | 1 | 1 | 1 | sim | 0 | 0,99992 | 0,998 | 13,5 s |
| 19 | 2 | 1 | 1 | sim | 0 | 0,99989 | 0,997 | 12,1 s |
| 20 | 5 | 2 | 2 | sim | 0 | 0,99994 | 0,999 | 9,2 s |

**Tempo:** 12,8 s por panorâmica em média (PC de 4 núcleos e 8 GB, 20 em sequência com o modelo
carregado uma vez), contra ~19–27 s do R torch. Na tela Aplicar, um lote de teste com duas radiografias
levou 32 s em C++ e 85 s em R, e os dois motores gravaram no banco as mesmas 7 lesões (centro, área e
confiança iguais).

Detalhes que importaram:
- Arredondamento metade para o par (`std::nearbyint`), como `round()` do R e `cvRound` do OpenCV —
  `std::lround` arredonda 0,5 para longe do zero e mudaria alguns pixels da normalização.
- A redução da probabilidade de 2.000 para 1.400 px é por área em ponto flutuante (sem arredondar).
- O espelhamento fica dentro do grafo ONNX (`InferenceGraph` em `export_onnx.py`), desvirado antes do
  recorte, como no R.
- No Windows, `argv` chega na página de código ANSI; os caminhos são relidos com `CommandLineToArgvW`,
  para nomes de pacientes com acentos.
