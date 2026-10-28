"""Ápices radiculares a partir dos contornos de dentes do DENTEX (conjunto de numeração).

Primeira etapa da abordagem em duas etapas: localizar as pontas das raízes para,
depois, examinar um recorte ampliado em volta de cada uma.

No DENTEX cada dente tem um polígono, o quadrante (category_id_1: 0-3 = quadrantes
FDI 1-4) e o número (category_id_2: 0-7 = dentes 1-8). O ápice é a extremidade do
polígono do lado da raiz: para cima nos quadrantes 1 e 2 (superiores) e para baixo
nos 3 e 4 (inferiores). Molares (6-8) recebem dois ápices, um em cada metade da faixa
apical, porque têm mais de uma raiz.
"""
import json
from functools import lru_cache
from pathlib import Path

import numpy as np

import comum as c

DENTEX = c.RAIZ / "Externos" / "DENTEX"
FAIXA_APICAL = 0.12  # fração da altura do dente considerada "ponta da raiz"


def _lista(v):
    return json.loads(v) if isinstance(v, str) else v


@lru_cache(maxsize=1)
def anotacoes():
    d = json.load(open(DENTEX / "train_quadrant_enumeration.json", encoding="utf-8"))
    imagens = {str(i["id"]): i for i in d["images"]}
    dentes = {}
    for a in d["annotations"]:
        pol = np.array(_lista(a["segmentation"])[0], float).reshape(-1, 2)
        dentes.setdefault(str(a["image_id"]), []).append(
            dict(poligono=pol, quadrante=int(a["category_id_1"]) + 1, dente=int(a["category_id_2"]) + 1))
    return imagens, dentes


def apices_do_dente(pol, quadrante, dente):
    """Lista de (x, y) dos ápices de um dente, em pixels da imagem original."""
    superior = quadrante in (1, 2)
    y = pol[:, 1]
    altura = y.max() - y.min()
    ext = y.min() if superior else y.max()
    faixa = pol[np.abs(y - ext) <= FAIXA_APICAL * altura]
    if dente >= 6 and len(faixa) >= 2:
        xm = faixa[:, 0].mean()
        lados = [faixa[faixa[:, 0] <= xm], faixa[faixa[:, 0] > xm]]
        pts = []
        for lado in lados:
            if len(lado):
                k = lado[:, 1].argmin() if superior else lado[:, 1].argmax()
                pts.append(tuple(lado[k]))
        if len(pts) == 2 and abs(pts[0][0] - pts[1][0]) > 0.25 * np.ptp(pol[:, 0]):
            return pts
    k = y.argmin() if superior else y.argmax()
    return [tuple(pol[k])]


def apices_da_imagem(image_id):
    """[(x, y, quadrante, dente)] de todos os ápices de uma imagem do DENTEX."""
    _, dentes = anotacoes()
    saida = []
    for t in dentes.get(str(image_id), []):
        for x, y in apices_do_dente(t["poligono"], t["quadrante"], t["dente"]):
            saida.append((float(x), float(y), t["quadrante"], t["dente"]))
    return saida


def caminho_imagem(image_id):
    imagens, _ = anotacoes()
    return DENTEX / "enumeracao" / Path(imagens[str(image_id)]["file_name"]).name
