"""Avalia a abordagem em duas etapas nas 20 panorâmicas marcadas (CPU, local).

Os dois modelos vêm do Colab (treino_duas_etapas.py) e nunca viram as 20 imagens.
Além das métricas por lesão (mesma detecção e mesmo critério dos outros modelos),
mede quantas lesões marcadas caem dentro de algum recorte: o teto da etapa 2.

Uso: python avaliar_duas_etapas.py [--pasta Modelos/duas_etapas]
Saída: Resultados/validacao/probs_DUAS.npz, <pasta>/avaliacao.json e Resultados/comparacao.md
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
import duas_etapas as de
import unet


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--pasta", default=str(c.MODELOS / "duas_etapas"))
    ap.add_argument("--chave", default="DUAS")
    ap.add_argument("--limite", type=int, default=0, help="só para testes rápidos")
    args = ap.parse_args()
    pasta = Path(args.pasta)
    localizador, meta1 = unet.carregar(pasta / "localizador_apices.pt")
    segmentador, meta2 = unet.carregar(pasta / "lesoes_recortes.pt")
    nossas = c.nossas_imagens()
    if args.limite:
        nossas = nossas[:args.limite]
    meio = de.RECORTE / 2 * c.LARGURA / de.LARGURA_RECORTE  # meia largura do recorte na escala de trabalho
    probs, cobertas, total, n_apices, t = [], 0, 0, [], time.time()
    for k, (nm, img, m) in enumerate(nossas, 1):
        original = c.ler_cinza(c.NOSSAS / "imagens" / f"{nm}.png")
        p, apices = de.probabilidade_duas_etapas(localizador, segmentador, original)
        if p.shape != img.shape:
            p = cv2.resize(p, (img.shape[1], img.shape[0]), interpolation=cv2.INTER_AREA)
        probs.append(p)
        n_apices.append(len(apices))
        janela = np.zeros(img.shape, bool)
        for x, y in apices:
            janela[max(0, int(y - meio)):int(y + meio), max(0, int(x - meio)):int(x + meio)] = True
        n, rot = cv2.connectedComponents(m.astype(np.uint8))
        for j in range(1, n):
            total += 1
            cobertas += bool((janela & (rot == j)).any())
        print(f"  {k}/{len(nossas)}: {len(apices)} ápices ({(time.time() - t) / k:.1f}s/imagem)", flush=True)
    np.savez_compressed(c.RESULTADOS / "validacao" / f"probs_{args.chave}.npz",
                        **{nm: pr.astype(np.float16) for (nm, _, _), pr in zip(nossas, probs)})
    curva = dt.froc([dt.detectar(p, dt.RAIO_REGIAO_UNET) for p in probs], [m for _, _, m in nossas])
    melhor, pontos, texto = dt.resumo_froc(curva)
    print(f"Lesões dentro de algum recorte: {cobertas}/{total} | ápices por imagem: mediana {np.median(n_apices):.0f}")
    print(f"Duas etapas: {texto}")
    json.dump(dict(melhor=melhor, sensibilidade_por_fp={str(f): v for f, v in pontos.items()}, curva=curva,
                   lesoes_em_recortes=[cobertas, total], apices_por_imagem=n_apices,
                   meta_localizador=meta1, meta_segmentador=meta2),
              open(pasta / "avaliacao.json", "w", encoding="utf-8"), indent=2, ensure_ascii=False)
    comparar.main()


if __name__ == "__main__":
    main()
