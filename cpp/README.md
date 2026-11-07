# ML ODONTO C++

Motor de inferência C++20 do ML ODONTO. Faz o caminho completo da radiografia às lesões com a U-Net v2
exportada para ONNX e reproduz a implementação de referência (Python e R) lesão a lesão. O aplicativo R
usa este motor na tela **Aplicar** quando ele está instalado; sem ele, continua em R torch.

## O que faz

| Etapa | Arquivo | Equivale a |
|---|---|---|
| Leitura de PNG, JPEG, BMP, PGM (cinza 8 bits) | `src/io.cpp` (stb) | `ler_radiografia()` |
| Redução por área e CLAHE 8 × 8, limite 2,0 | `src/preprocessing.cpp` | `cv2.INTER_AREA`, `cv2.createCLAHE` |
| U-Net ResNet34 + sigmoide + média com o espelhado | `src/onnx_engine.cpp` (grafo de `tools/export_onnx.py`) | `prever_unet()` |
| Probabilidade 2.000 → 1.400 px por área | `src/pipeline.cpp` | `prever_radiografia()` |
| Suavização gaussiana (reflect-101), máximos locais, regiões com vizinhança 8 | `src/detection.cpp` | `scripts/deteccao.py`, `detectar_lesoes()` |
| Máscara no limiar, componentes, JSON | `src/detection.cpp`, `src/io.cpp` | `mascara_lesoes()` |

Os arredondamentos seguem o R e o OpenCV (metade para o par). TIFF não é lido diretamente: o aplicativo R
converte para PNG antes de chamar o executável. Caminhos com acentos funcionam no Windows (a linha de
comando é lida em UTF-16).

## Compilar e instalar no aplicativo

Num comando, sem Visual Studio (compilador Zig/clang e CMake/Ninja instalados pelo pip):

```bash
python -m pip install cmake ninja ziglang onnx onnxruntime torch safetensors
python cpp/tools/build.py
R CMD INSTALL --no-test-load software/odontolesao
```

O `build.py`:

1. baixa uma vez o ONNX Runtime oficial (Microsoft, GitHub) para `cpp/.deps/`;
2. compila em `cpp/build/zig-onnx/` e roda os testes unitários;
3. chama `tools/install_into_app.py`, que copia `ml-odonto-cli.exe` e as DLLs para
   `software/odontolesao/inst/bin/` e exporta `unet_lesoes_v2.onnx` ao lado dos pesos (com
   `--verify`: erro máximo PyTorch × ONNX de 1,3 × 10⁻⁷).

Com Visual Studio ou GCC, use `python cpp/tools/build.py --system-compiler`. Só o núcleo, sem ONNX:

```bash
cmake --preset default
cmake --build --preset default
ctest --preset default
```

Nada disso vai para o repositório: `cpp/build/`, `cpp/.deps/`, `*.onnx` e `inst/bin/` estão no `.gitignore`.

## Linha de comando

```bash
# radiografias -> <nome>_probabilidade.pfm, <nome>_mascara.png, <nome>_deteccoes.json
ml-odonto-cli run unet_lesoes_v2.onnx saida/ rad1.jpg rad2.png --threshold 0.5

# etapas isoladas, para testes de equivalência
ml-odonto-cli preprocess radiografia.pgm normalizada.pgm 2000
ml-odonto-cli infer unet_lesoes_v2.onnx normalizada.pgm probabilidade.pfm
ml-odonto-cli detect probabilidade.pfm mascara.pgm deteccoes.json 0.5
```

O `run` carrega o modelo uma vez e processa a lista; imprime uma linha `ok <nome> ...` ou
`error <nome>: ...` por radiografia, na ordem dada (o aplicativo R acompanha o andamento por elas). Uma
radiografia com erro não interrompe as demais. A probabilidade e a máscara ficam na escala de trabalho
(1.400 px de largura); o JSON traz `schema_version`, o tamanho original, o tempo e os candidatos.
Coordenadas são baseadas em zero.

## Integração com o aplicativo R

`software/odontolesao/R/fct_motor_cpp.R` localiza o executável (`inst/bin`, a opção `mlodonto.cli` ou a
variável `ML_ODONTO_CLI`) e o `.onnx`; `processar_lote()` usa o C++ para a probabilidade e mantém a detecção
em R. Para forçar o R torch: `options(mlodonto.motor = "r")`. No teste com duas radiografias, os dois
motores gravaram no banco as mesmas 7 lesões, com o mesmo centro, área e confiança.

## Validação

```bash
python cpp/tools/validate_pipeline.py --cli cpp/build/zig-onnx/ml-odonto-cli.exe
```

Roda o `run` nas 20 panorâmicas marcadas e compara com os mapas do Python (`probs_UNET_V2.npz`) e com a
avaliação por lesão publicada (`lesoes_validacao.csv`). Resultado em
[`docs/EQUIVALENCIA_CPP.md`](../docs/EQUIVALENCIA_CPP.md): **aprovado**.

| Critério | Exigido | C++ |
|---|---|---|
| Lesões encontradas, uma a uma | as mesmas do Python (31/45) | 31/45, todas iguais |
| Falsos positivos | 0 | 0 |
| Correlação das probabilidades | ≥ 0,998 | ≥ 0,998 (mínimo 0,99814) |
| Dice das máscaras C++ × Python | ≥ 0,95 | ≥ 0,988 |

`tools/compare_reference.py` isola só o pós-processamento (mesma probabilidade, detecção C++ × Python).
