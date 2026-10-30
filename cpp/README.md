# ML ODONTO C++

Primeiro marco da migração incremental do ML ODONTO para C++20. Esta pasta não substitui o aplicativo
R: ela contém o núcleo matemático que será validado contra a implementação de referência antes da
migração da interface.

## Estado atual

- redução por área equivalente ao pré-processamento de referência;
- CLAHE 8 × 8, limite 2,0;
- suavização gaussiana com borda `reflect-101`;
- filtro de máximos locais com borda refletida;
- componentes conexos com vizinhança 8;
- extração de candidatos e construção da máscara no limiar escolhido;
- leitura/escrita de PGM e PFM para testes de equivalência;
- CLI e testes unitários sem dependências externas.

O adaptador da U-Net com ONNX Runtime já está disponível; a leitura direta de JPEG/PNG/TIFF virá com o
adaptador OpenCV. Os pesos não são versionados neste repositório; consulte
`software/odontolesao/inst/modelos/LEIAME.md`. A opção ONNX fica desligada por padrão para manter os
testes do núcleo independentes de bibliotecas externas.

## Compilar

Requisitos: CMake 3.24+, Ninja e um compilador C++20.

```powershell
cd cpp
cmake --preset default
cmake --build --preset default
ctest --preset default
```

## CLI de equivalência

O formato PFM preserva as probabilidades `float32` geradas pelo R/Python sem introduzir quantização.

```powershell
ml-odonto-cli detect probabilidade.pfm mascara.pgm deteccoes.json 0.5
ml-odonto-cli preprocess radiografia.pgm normalizada.pgm 2000
```

### Inferência ONNX

Converta os pesos e valide o grafo:

```powershell
python cpp/tools/export_onnx.py `
  software/odontolesao/inst/modelos/unet_lesoes_v2.safetensors `
  software/odontolesao/inst/modelos/unet_lesoes_v2.onnx --verify
```

Configure o build apontando para o pacote binário do ONNX Runtime:

```powershell
cmake --preset default -DML_ODONTO_WITH_ONNX=ON -DONNXRUNTIME_ROOT=C:\caminho\onnxruntime
cmake --build --preset default
ml-odonto-cli infer unet_lesoes_v2.onnx normalizada.pgm probabilidade.pfm
```

Para comparar o pós-processamento nas 20 probabilidades validadas:

```powershell
python cpp/tools/compare_reference.py `
  --cli cpp/build/default/ml-odonto-cli.exe `
  --probabilities Resultados/validacao/probs_UNET_V2.npz
```

As coordenadas na API C++ são baseadas em zero. O arquivo JSON tem `schema_version` para permitir a
evolução do contrato sem quebrar o aplicativo R durante a migração.

## Critério para o próximo marco

Antes de liberar inferência ou interface em C++, o executável deve ser comparado nas 20 panorâmicas de
referência:

- mesmas 31/45 lesões encontradas no limiar 0,5;
- zero falsos positivos;
- Dice C++ × R das máscaras ≥ 0,95;
- correlação das probabilidades ≥ 0,998.

Esses são os critérios já aprovados em `docs/EQUIVALENCIA_R_PYTHON.md`.
