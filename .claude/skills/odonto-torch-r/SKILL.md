---
name: odonto-torch-r
description: Portar os modelos PyTorch do projeto ML Odonto (U-Net ResNet34 de lesões, localizador de ápices, classificador por dente) para R com o pacote torch, incluindo conversão de pesos para safetensors, pré-processamento equivalente ao OpenCV (CLAHE, redimensionamento), detecção por picos e o teste de equivalência R × Python. Use ao mexer em fct_modelo.R, fct_imagem.R, fct_deteccao.R ou mod_aplicar.R/mod_treinar.R.
---

# Modelos em R torch

Referências Python (fonte da verdade): `scripts/unet.py`, `scripts/comum.py` (normalizar, CLAHE,
avaliar), `scripts/deteccao.py`, `scripts/duas_etapas.py`, `scripts/dentes.py`,
`scripts/ajuste_fino_unet.py`.

## 1. Converter pesos (uma vez, em Python)

```python
import torch
from safetensors.torch import save_file
ck = torch.load("Modelos/unet_lesoes_v2.pt", map_location="cpu", weights_only=False)
sd = {k: v.contiguous() for k, v in ck["modelo"].items()}   # só tensores
save_file(sd, "software/odontolesao/inst/modelos/unet_lesoes_v2.safetensors",
          metadata={k: str(v) for k, v in ck["meta"].items()})
```

`pip install safetensors` se faltar. Guardar o `meta` (limiar, largura, iteração) também em
`inst/modelos/modelos.yml`.

## 2. Arquitetura em R

- Reescrever `UNetResNet34` com `torch::nn_module`, usando `torchvision::model_resnet34()` como
  codificador e **os mesmos nomes de submódulos** do Python (`stem`, `pool`, `c1`..`c4`, `d4`..`d0`,
  `saida`), para que `model$load_state_dict(safetensors::safe_load_file(arq, framework = "torch"))`
  case nome a nome. `stem` = `nn_sequential(conv1, bn1, relu)` com índices 0,1,2 como no Python.
- Buffers `media`/`desvio` não persistentes (não estão no arquivo).
- Conferir: `setdiff(names(model$state_dict()), names(pesos))` vazio nos dois sentidos, e formas iguais.
- `model$eval()`, `torch::with_no_grad()`, `torch_set_num_threads(parallel::detectCores() - 1)`.
- Previsão = média da imagem e da versão espelhada (como `unet.prever(..., espelhar=True)`); entrada
  com altura e largura completadas até múltiplo de 32 e recortadas de volta.

## 3. Pré-processamento equivalente

- Ler em tons de cinza 8 bits (`magick::image_read` → `image_convert(colorspace = "gray")` →
  matriz). Atenção à orientação: `as.integer(image_data(...))` vem como [canal, largura, altura].
- Redimensionar para a largura alvo (1.400; 2.000 na U-Net v2; 2.800 nos recortes): reduzir com filtro
  de área (equivalente a `INTER_AREA`), ampliar com cúbica.
- CLAHE com limite 2,0 e grade 8×8: `EBImage::clahe(x, nx = 8, ny = 8, limit = 2)`; se o teste de
  equivalência falhar por causa dele, implementar o CLAHE do OpenCV em R/C++ (`Rcpp`): histograma
  por bloco com 256 bins, recorte em `limite * área_bloco / 256`, redistribuição uniforme, interpolação
  bilinear entre blocos.

## 4. Detecção (idêntica a deteccao.py)

Suavização gaussiana σ=4 → máximos locais numa janela 31×31 com valor ≥ 0,05 → para cada pico, região
conectada acima de 0,5 × pico, limitada a 100 px (redes) → máscara final pelo limiar de operação do
modelo (0,5 na U-Net v2). Avaliação por lesão: região prevista correta se ≥20% da sua área cai em lesão;
lesão encontrada se tocada por região correta.

## 5. Teste de equivalência (obrigatório antes de liberar Aplicar)

`tests/testthat/test-equivalencia.R`, com as 20 panorâmicas marcadas (lê de `Treinamento/extraido`,
pulado se a pasta não existir — dados de paciente não vão para o repositório):

- Probabilidade R × `Resultados/validacao/probs_UNET_V2.npz` (ler com `reticulate` só no teste, ou
  exportar antes para `.rds`): correlação de Pearson ≥ 0,99 por imagem.
- Mesmas lesões encontradas no limiar 0,5: 31/45, e o vetor por lesão idêntico.
- Dice entre máscaras R e Python ≥ 0,95.

## 6. Ajuste fino em R (CPU)

Congelar o codificador e as estatísticas de BatchNorm (`model$c1..c4$eval()` e `requires_grad_(FALSE)`),
treinar só o decodificador, recortes 384×576, lote 2, 150–300 passos, AdamW 3e-4, perda BCE + Tversky
(α=0,3, β=0,7) com faixa de 4 px ignorada na borda do contorno. Validação cruzada por paciente.
Salvar como novo modelo no registro (`modelos`) com métricas; nunca sobrescrever o modelo ativo.

## 7. Retreino completo

Não fazer em CPU. Exportar recortes anonimizados + máscaras + `notebook` gerado a partir de
`scripts/gerar_notebook*.py`; o modelo treinado no Colab volta como `.pt`, é convertido (passo 1) e
entra no registro após passar na avaliação local.

## Estado (27/10/2026)

Fase 5 concluída e aprovada: `R/fct_modelo.R` (U-Net, `ler_radiografia`, `redimensionar`, `clahe`, `prever_unet`,
`prever_radiografia`) e `R/fct_deteccao.R` (`detectar_lesoes`, `mascara_lesoes`, `avaliar_lesoes`, `rotular8`).
Resultado e lições em `docs/EQUIVALENCIA_R_PYTHON.md`. Armadilhas já resolvidas: `bwlabel` tem vizinhança 4
(use `rotular8`); `pmax(0, m)` perde as dimensões da matriz (use `m[] <- ...`); use todas as threads; redução por
área com `Matrix::sparseMatrix`. Rode `Rscript odontolesao/tests/equivalencia.R` (~10 min) após mexer nessas funções.
