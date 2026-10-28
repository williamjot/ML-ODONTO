"""Extrai do PDF de treinamento as radiografias e as máscaras das lesões marcadas.

Cada página traz a panorâmica dividida em faixas horizontais (imagens) e as
lesões desenhadas como vetores laranja por cima. Aqui as faixas são reunidas
numa imagem só e os vetores são rasterizados nas mesmas coordenadas.

Saída em Treinamento/extraido/:
  imagens/NN.png     radiografia em tons de cinza
  contornos/NN.png   traço da marcação (como desenhado)
  mascaras/NN.png    região da lesão preenchida (255 = lesão)
  overlay/NN.jpg     conferência visual
"""
import sys
from pathlib import Path

import cv2
import fitz
import numpy as np
from skimage.morphology import skeletonize

RAIZ = Path(__file__).resolve().parent.parent
PDF = RAIZ / "Treinamento" / "lesões segmentadas.pdf"
SAIDA = RAIZ / "Treinamento" / "extraido"
LARANJA = (1.0, 0.757, 0.078)
AREA_MINIMA_LADO = 12  # px; marcações com lado menor que isso são resquícios do traço


def eh_marcacao(d):
    f = d.get("fill")
    return f is not None and all(abs(a - b) < 0.05 for a, b in zip(f, LARANJA))


def pontos_do_path(d, n_curva=8):
    pts = []
    for it in d["items"]:
        if it[0] == "l":
            pts += [it[1], it[2]]
        elif it[0] == "c":
            p0, p1, p2, p3 = it[1:5]
            for t in np.linspace(0, 1, n_curva):
                a = (1 - t) ** 3
                b = 3 * (1 - t) ** 2 * t
                c = 3 * (1 - t) * t ** 2
                e = t ** 3
                pts.append(fitz.Point(a * p0.x + b * p1.x + c * p2.x + e * p3.x,
                                      a * p0.y + b * p1.y + c * p2.y + e * p3.y))
        elif it[0] == "re":
            r = it[1]
            pts += [r.tl, r.tr, r.br, r.bl]
    return pts


def processar_pagina(pag):
    infos = sorted(pag.get_image_info(xrefs=True), key=lambda i: i["bbox"][1])
    if not infos:
        return None
    faixas = []
    for info in infos:
        pix = fitz.Pixmap(pag.parent, info["xref"])
        if pix.n - pix.alpha > 1:
            pix = fitz.Pixmap(fitz.csGRAY, pix)
        arr = np.frombuffer(pix.samples, np.uint8).reshape(pix.h, pix.w, pix.n)[..., 0]
        faixas.append(arr)
    w = min(f.shape[1] for f in faixas)
    img = np.vstack([f[:, :w] for f in faixas])

    x0 = min(i["bbox"][0] for i in infos)
    y0 = min(i["bbox"][1] for i in infos)
    x1 = max(i["bbox"][2] for i in infos)
    y1 = max(i["bbox"][3] for i in infos)
    sx = img.shape[1] / (x1 - x0)
    sy = img.shape[0] / (y1 - y0)

    contorno = np.zeros(img.shape, np.uint8)
    for d in pag.get_drawings():
        if not eh_marcacao(d):
            continue
        pts = pontos_do_path(d)
        if len(pts) < 3:
            continue
        poly = np.array([[(p.x - x0) * sx, (p.y - y0) * sy] for p in pts], np.int32)
        cv2.fillPoly(contorno, [poly], 255)

    return img, contorno, preencher_marcacoes(contorno)


def pontas(esqueleto):
    viz = cv2.filter2D(esqueleto.astype(np.uint8), -1, np.ones((3, 3), np.float32))
    ys, xs = np.nonzero(esqueleto & (viz == 2))
    return list(zip(xs, ys))


def preencher_marcacoes(contorno):
    """Transforma cada traço na região que ele envolve.

    Muitos traços são arcos abertos em "∩" que contornam a lesão deixando a raiz
    de fora; ligar as pontas soltas e preencher o interior preserva esse recorte
    (o fecho convexo incluiria a raiz). Se não houver interior, usa o fecho.
    """
    unido = cv2.morphologyEx(contorno, cv2.MORPH_CLOSE, np.ones((5, 5), np.uint8))
    mascara = np.zeros_like(contorno)
    n, rot = cv2.connectedComponents(unido)
    for k in range(1, n):
        comp = (rot == k).astype(np.uint8) * 255
        ys, xs = np.nonzero(comp)
        if max(np.ptp(xs), np.ptp(ys)) < AREA_MINIMA_LADO:
            continue  # respingo de caneta, não é uma marcação
        fechado = comp.copy()
        livres = pontas(skeletonize(comp > 0))
        while len(livres) >= 2:
            a = livres.pop(0)
            b = min(livres, key=lambda p: (p[0] - a[0]) ** 2 + (p[1] - a[1]) ** 2)
            livres.remove(b)
            cv2.line(fechado, tuple(map(int, a)), tuple(map(int, b)), 255, 3)
        cnts, _ = cv2.findContours(fechado, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
        cheio = np.zeros_like(comp)
        cv2.drawContours(cheio, cnts, -1, 255, cv2.FILLED)
        if (cheio > 0).sum() < 1.5 * (comp > 0).sum():
            ys, xs = np.nonzero(comp)
            cv2.fillPoly(cheio, [cv2.convexHull(np.column_stack([xs, ys]).astype(np.int32))], 255)
        mascara |= cheio
    return mascara


def main():
    for sub in ("imagens", "contornos", "mascaras", "overlay"):
        (SAIDA / sub).mkdir(parents=True, exist_ok=True)
    doc = fitz.open(PDF)
    paciente = 0
    for pag in doc:
        r = processar_pagina(pag)
        if r is None:
            print(f"página {pag.number + 1}: sem radiografia, ignorada")
            continue
        paciente += 1
        img, contorno, mascara = r
        nome = f"{paciente:02d}.png"
        cv2.imwrite(str(SAIDA / "imagens" / nome), img)
        cv2.imwrite(str(SAIDA / "contornos" / nome), contorno)
        cv2.imwrite(str(SAIDA / "mascaras" / nome), mascara)
        ov = cv2.cvtColor(img, cv2.COLOR_GRAY2BGR)
        ov[mascara > 0] = (0.5 * ov[mascara > 0] + [0, 60, 127]).astype(np.uint8)
        ov[contorno > 0] = (0, 165, 255)
        cv2.imwrite(str(SAIDA / "overlay" / nome.replace(".png", ".jpg")), ov)
        n_les = cv2.connectedComponents(mascara)[0] - 1
        print(f"página {pag.number + 1} -> paciente {paciente:02d}: {img.shape[1]}x{img.shape[0]}, "
              f"{n_les} lesões, {100 * (mascara > 0).mean():.3f}% da imagem")


if __name__ == "__main__":
    sys.exit(main())
