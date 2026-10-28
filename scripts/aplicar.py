"""Aplica o detector (RF ou U-Net + picos do mapa de probabilidade) às panorâmicas de uma pasta.

Saída em Resultados/aplicacao_<modelo>/:
  mascaras/<nome>.png      máscara na resolução original (255 = lesão)
  overlay/<nome>.jpg       radiografia com as lesões contornadas e a confiança (0-1)
  lesoes.csv               uma linha por lesão: arquivo, posição, tamanho, confiança

Uso: python aplicar.py [--modelo rf|unet|unet_ft|unet_v2] [pasta]   (padrão: rf, Dados/Aplicacao)
"""
import argparse
import csv
import json
import time
from pathlib import Path

import cv2
import joblib
import numpy as np

import comum as c
import deteccao as dt

EXTENSOES = {".jpg", ".jpeg", ".png", ".tif", ".tiff", ".bmp"}


def salvar(caminho, img):
    ok, buf = cv2.imencode(Path(caminho).suffix, img)
    buf.tofile(str(caminho))  # tofile lida com acentos no caminho


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--modelo", choices=("rf", "unet", "unet_ft", "unet_v2"), default="rf")
    ap.add_argument("pasta", nargs="?", default=str(c.RAIZ / "Dados" / "Aplicacao"))
    args = ap.parse_args()
    pasta = Path(args.pasta)
    if args.modelo == "rf":
        modelo = joblib.load(c.MODELOS / "rf_lesoes.joblib")
        cfg = json.load(open(c.MODELOS / "rf_lesoes.json", encoding="utf-8"))
        assert cfg["versao_atributos"] == c.VERSAO_ATRIBUTOS, "modelo treinado com outra versão dos atributos"
        limiar = cfg["limiar"]
        probabilidade = lambda img: c.prever_probabilidade(modelo, img)
        raio = dt.RAIO_REGIAO
    else:
        import unet
        from avaliar_unet import prever_na_escala_de_trabalho
        nome = {"unet": "unet_lesoes", "unet_ft": "unet_lesoes_ft", "unet_v2": "unet_lesoes_v2"}[args.modelo]
        modelo, meta = unet.carregar(c.MODELOS / f"{nome}.pt")
        limiar = json.load(open(c.MODELOS / f"{nome}.json", encoding="utf-8"))["melhor"]["limiar"]
        largura_modelo = meta.get("largura", c.LARGURA)
        raio = dt.RAIO_REGIAO_UNET
    saida = c.RESULTADOS / f"aplicacao_{args.modelo}"
    (saida / "mascaras").mkdir(parents=True, exist_ok=True)
    (saida / "overlay").mkdir(parents=True, exist_ok=True)

    arquivos = sorted(p for p in pasta.iterdir() if p.suffix.lower() in EXTENSOES)
    with open(saida / "lesoes.csv", "w", newline="", encoding="utf-8-sig") as fcsv:
        w = csv.writer(fcsv, delimiter=";")
        w.writerow(["arquivo", "lesao", "x_centro", "y_centro", "largura", "altura", "area_px", "confianca"])
        t0 = time.time()
        for n, arq in enumerate(arquivos, 1):
            original = c.ler_cinza(arq)
            img, esc = c.normalizar(original)
            if args.modelo == "rf":
                prob = probabilidade(img)
            else:
                prob = prever_na_escala_de_trabalho(modelo, original, largura_modelo, img.shape)
            deteccoes = dt.detectar(prob, raio)
            mask = dt.mascara(deteccoes, limiar, img.shape)
            # confiança de cada pixel = a maior entre as detecções aceitas que o cobrem
            mapa_conf = np.zeros(img.shape, np.float32)
            for reg, (y0, x0), p2 in deteccoes:
                if p2 >= limiar:
                    sub = mapa_conf[y0:y0 + reg.shape[0], x0:x0 + reg.shape[1]]
                    sub[reg] = np.maximum(sub[reg], p2)

            H, W = original.shape
            mask_orig = cv2.resize(mask.astype(np.uint8) * 255, (W, H), interpolation=cv2.INTER_NEAREST)
            salvar(saida / "mascaras" / f"{arq.stem}.png", mask_orig)

            ov = cv2.cvtColor(original, cv2.COLOR_GRAY2BGR)
            k, rot, st, cent = cv2.connectedComponentsWithStats(mask.astype(np.uint8))
            for j in range(1, k):
                pj = mapa_conf[rot == j].max()
                x, y, bw, bh, area = st[j]
                w.writerow([arq.name, j, round(cent[j][0] / esc), round(cent[j][1] / esc), round(bw / esc),
                            round(bh / esc), round(area / esc ** 2), f"{pj:.3f}"])
                comp = cv2.resize((rot == j).astype(np.uint8), (W, H), interpolation=cv2.INTER_NEAREST)
                cnts, _ = cv2.findContours(comp, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
                cv2.drawContours(ov, cnts, -1, (0, 165, 255), 3)
                cv2.putText(ov, f"{pj:.2f}", (int((x + bw) / esc) + 5, int(y / esc)),
                            cv2.FONT_HERSHEY_SIMPLEX, 1.0, (0, 165, 255), 2)
            salvar(saida / "overlay" / f"{arq.stem}.jpg", ov)
            print(f"[{n}/{len(arquivos)}] {arq.name}: {k - 1} lesões "
                  f"({(time.time() - t0) / n:.1f}s/imagem)", flush=True)


if __name__ == "__main__":
    main()
