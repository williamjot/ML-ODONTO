"""Avalia uma U-Net nas 20 panorâmicas marcadas e atualiza a comparação com os outros modelos.

Roda localmente (CPU): as radiografias de pacientes não saem da máquina.
Serve para U-Nets treinadas só com Hanói, que nunca viram essas 20 imagens. Se o
modelo foi treinado noutra largura (v2: 2000 px), a imagem é normalizada nessa
largura e o mapa de probabilidade volta à escala de trabalho (1400 px) para a
comparação usar exatamente as mesmas máscaras e a mesma detecção.

Uso: python avaliar_unet.py [--modelo Modelos/unet_lesoes.pt] [--chave UNET]
Saída: Resultados/validacao/probs_<CHAVE>.npz, Modelos/<modelo>.json e Resultados/comparacao.md
"""
import argparse
import json
import time
from pathlib import Path

import cv2
import numpy as np

import comparar
import comum as c
import deteccao as dt
import unet


def prever_na_escala_de_trabalho(modelo, original, largura_modelo, forma):
    """Probabilidade na forma `forma` (imagem normalizada a c.LARGURA), prevendo na largura do modelo."""
    img, _ = c.normalizar(original, largura_modelo)
    p = unet.prever(modelo, img)
    if p.shape != forma:
        p = cv2.resize(p, (forma[1], forma[0]), interpolation=cv2.INTER_AREA)
    return p


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", default=str(c.MODELOS / "unet_lesoes.pt"))
    ap.add_argument("--chave", default="UNET", help="nome da variante em comparar.VARIANTES")
    args = ap.parse_args()
    caminho = Path(args.modelo)
    modelo, meta = unet.carregar(caminho)
    largura = meta.get("largura", c.LARGURA)
    nossas = c.nossas_imagens()
    probs, t = [], time.time()
    for k, (nm, img, _) in enumerate(nossas, 1):
        original = c.ler_cinza(c.NOSSAS / "imagens" / f"{nm}.png")
        probs.append(prever_na_escala_de_trabalho(modelo, original, largura, img.shape))
        print(f"  {k}/{len(nossas)} ({(time.time() - t) / k:.1f}s/imagem)", flush=True)
    np.savez_compressed(c.RESULTADOS / "validacao" / f"probs_{args.chave}.npz",
                        **{nm: p.astype(np.float16) for (nm, _, _), p in zip(nossas, probs)})

    curva = dt.froc([dt.detectar(p, dt.RAIO_REGIAO_UNET) for p in probs], [m for _, _, m in nossas])
    melhor, pontos, _ = dt.resumo_froc(curva)
    json.dump(dict(melhor=melhor, sensibilidade_por_fp={str(f): v for f, v in pontos.items()}, curva=curva,
                   meta=meta), open(caminho.with_suffix(".json"), "w", encoding="utf-8"), indent=2, ensure_ascii=False)
    comparar.main()


if __name__ == "__main__":
    main()
