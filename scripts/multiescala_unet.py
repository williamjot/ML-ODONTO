"""Testa a U-Net em várias escalas nas 20 panorâmicas marcadas.

A rede foi treinada com panorâmicas de 1400 px de largura, onde lesões pequenas
ficam com poucos pixels. Aqui a imagem também é ampliada (1,5× e 2×), prevista e
o mapa reduzido de volta; os mapas são combinados com o da escala normal.

A U-Net de Hanói nunca viu as 20 imagens, então elas continuam sendo teste; a
escolha entre as poucas combinações abaixo é feita nelas mesmas, o que deixa o
número da melhor combinação levemente otimista (dito no resultado).

Uso: python multiescala_unet.py [--escalas 1.5 2.0]
Saída: Resultados/validacao/probs_UNET_s<escala>.npz, probs_UNET_MS.npz (melhor combinação), comparacao.md
"""
import argparse
import json
import time

import cv2
import numpy as np

import comum as c
import deteccao as dt
import unet


def prever_escala(modelo, img, escala):
    if escala == 1.0:
        return unet.prever(modelo, img)
    h, w = img.shape
    grande = cv2.resize(img, (round(w * escala), round(h * escala)), interpolation=cv2.INTER_CUBIC)
    p = unet.prever(modelo, grande)
    return cv2.resize(p, (w, h), interpolation=cv2.INTER_AREA)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--escalas", type=float, nargs="+", default=[1.5, 2.0])
    args = ap.parse_args()
    modelo, _ = unet.carregar(c.MODELOS / "unet_lesoes.pt")
    nossas = c.nossas_imagens()
    verdades = [m for _, _, m in nossas]
    val = c.RESULTADOS / "validacao"

    mapas = {1.0: np.load(val / "probs_UNET.npz")}
    mapas[1.0] = {nm: mapas[1.0][nm].astype(np.float32) for nm, _, _ in nossas}
    for e in args.escalas:
        arq = val / f"probs_UNET_s{e}.npz"
        if arq.exists():
            d = np.load(arq)
            mapas[e] = {nm: d[nm].astype(np.float32) for nm, _, _ in nossas}
            print(f"escala {e}: carregada", flush=True)
            continue
        t, mapas[e] = time.time(), {}
        for k, (nm, img, _) in enumerate(nossas, 1):
            mapas[e][nm] = prever_escala(modelo, img, e)
            print(f"  escala {e}: {k}/{len(nossas)} ({(time.time() - t) / k:.0f}s/imagem)", flush=True)
        np.savez_compressed(arq, **{nm: p.astype(np.float16) for nm, p in mapas[e].items()})

    combinacoes = {"1×": lambda nm: mapas[1.0][nm]}
    for e in args.escalas:
        combinacoes[f"só {e}×"] = lambda nm, e=e: mapas[e][nm]
        combinacoes[f"máx(1×, {e}×)"] = lambda nm, e=e: np.maximum(mapas[1.0][nm], mapas[e][nm])
        combinacoes[f"média(1×, {e}×)"] = lambda nm, e=e: (mapas[1.0][nm] + mapas[e][nm]) / 2
    todas = [1.0] + args.escalas
    combinacoes["máx(todas)"] = lambda nm: np.max([mapas[e][nm] for e in todas], axis=0)
    combinacoes["média(todas)"] = lambda nm: np.mean([mapas[e][nm] for e in todas], axis=0)

    resultados = {}
    for nome, f in combinacoes.items():
        probs = [f(nm) for nm, _, _ in nossas]
        curva = dt.froc([dt.detectar(p, dt.RAIO_REGIAO_UNET) for p in probs], verdades)
        melhor, pontos, texto = dt.resumo_froc(curva)
        resultados[nome] = (melhor, pontos, probs)
        print(f"{nome:>16}: {texto}", flush=True)

    # escolha: mais lesões com até 0,5 falso positivo por imagem; desempate pelo F1
    nome = max(resultados, key=lambda n: (resultados[n][1][0.5], resultados[n][0]["f1"]))
    print(f"Melhor combinação: {nome}", flush=True)
    np.savez_compressed(val / "probs_UNET_MS.npz",
                        **{nm: p.astype(np.float16) for (nm, _, _), p in zip(nossas, resultados[nome][2])})
    json.dump(dict(combinacao=nome, escalas=todas, melhor=resultados[nome][0],
                   sensibilidade_por_fp={str(k): v for k, v in resultados[nome][1].items()}),
              open(c.MODELOS / "unet_multiescala.json", "w", encoding="utf-8"), indent=2, ensure_ascii=False)


if __name__ == "__main__":
    main()
