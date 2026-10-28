"""Avalia o classificador por dente nas 20 panorâmicas marcadas e compara com a U-Net v2.

O classificador dá um ponto por dente (o ápice), sem contorno; por isso a comparação usa um
critério por ponto, aplicado igualmente aos dois modelos: a lesão marcada é encontrada se algum
ponto previsto está a até DIST px dela; um ponto previsto é correto se está a até DIST px de
alguma lesão. Para a U-Net v2, os pontos são os picos do mapa de probabilidade (deteccao.detectar).
Também avalia a união dos dois (ponto da U-Net v2 OU do classificador).

Uso: python avaliar_dentes.py [--modelo Modelos/dentes/classificador_dentes.pt]
Saída: Resultados/validacao/dentes.json e linhas no terminal.
"""
import argparse
import json

import cv2
import numpy as np
from scipy import ndimage as ndi

import comum as c
import deteccao as dt
import dentes as dn
import unet

DIST = 30  # px na escala de trabalho


def pico_unet(prob):
    """Pontos (x, y, confiança) dos picos do mapa suavizado da U-Net."""
    ps = cv2.GaussianBlur(prob, (0, 0), dt.SUAVIZACAO)
    pk = (ps == ndi.maximum_filter(ps, size=2 * dt.RAIO_PICO + 1)) & (ps >= dt.PICO_MIN)
    ys, xs = np.nonzero(pk)
    # platôs (probabilidade saturada) dão vários pixels de máximo: funde pontos vizinhos
    return dn.fundir([(float(x), float(y), float(ps[y, x])) for y, x in zip(ys, xs)])


def avaliar_pontos(pontos_por_img, verdades, limiar):
    achadas = lesoes = corretas = previstas = 0
    por_lesao = []
    for pts, v in zip(pontos_por_img, verdades):
        n, rot = cv2.connectedComponents(v.astype(np.uint8))
        dist = cv2.distanceTransform((~v).astype(np.uint8), cv2.DIST_L2, 5)
        sel = [(x, y) for x, y, p in pts if p >= limiar]
        perto = set()
        for x, y in sel:
            xi, yi = int(round(x)), int(round(y))
            if dist[yi, xi] <= DIST:
                corretas += 1
                # lesões a até DIST px deste ponto
                y0, y1, x0, x1 = max(0, yi - DIST), yi + DIST + 1, max(0, xi - DIST), xi + DIST + 1
                yy, xx = np.mgrid[y0:min(y1, v.shape[0]), x0:min(x1, v.shape[1])]
                dentro = (yy - yi) ** 2 + (xx - xi) ** 2 <= DIST ** 2
                perto |= set(np.unique(rot[y0:y1, x0:x1][dentro & v[y0:y1, x0:x1]]).tolist()) - {0}
        previstas += len(sel)
        lesoes += n - 1
        achadas += len(perto)
        por_lesao += [k in perto for k in range(1, n)]
    sens = achadas / lesoes
    prec = corretas / max(previstas, 1)
    return dict(limiar=float(limiar), sensibilidade=sens, precisao=prec, achadas=achadas, lesoes=lesoes,
                falsos_por_imagem=(previstas - corretas) / len(verdades),
                f1=2 * sens * prec / max(sens + prec, 1e-9), por_lesao=por_lesao)


def curva(pontos, verdades):
    linhas = [avaliar_pontos(pontos, verdades, t) for t in np.round(np.arange(0.05, 0.96, 0.025), 3)]
    melhor = max(linhas, key=lambda r: r["f1"])
    sens_fp = {fp: max([r["sensibilidade"] for r in linhas if r["falsos_por_imagem"] <= fp] or [0])
               for fp in (0.5, 1, 2, 4)}
    return melhor, sens_fp, linhas


def main():
    a = argparse.ArgumentParser()
    a.add_argument("--modelo", default=str(c.MODELOS / "dentes" / "classificador_dentes.pt"))
    args = a.parse_args()
    nossas = c.nossas_imagens()
    verdades = [m for _, _, m in nossas]
    loc, _ = unet.carregar(c.MODELOS / "duas_etapas" / "localizador_apices.pt")
    cls, meta = dn.carregar(args.modelo)
    print("classificador:", meta)
    pts_cls = []
    for nm, _, _ in nossas:
        orig = c.ler_cinza(c.NOSSAS / "imagens" / f"{nm}.png")
        pts_cls.append(dn.fundir(dn.detectar(loc, cls, orig)))
        print(nm, len(pts_cls[-1]), "ápices", flush=True)
    P = np.load(c.RESULTADOS / "validacao" / "probs_UNET_V2.npz")
    pts_v2 = [pico_unet(P[nm].astype(np.float32)) for nm, _, _ in nossas]
    lim_v2 = json.load(open(c.MODELOS / "unet_lesoes_v2.json", encoding="utf-8"))["melhor"]["limiar"]
    res = {}
    for nome, pts in (("Classificador por dente", pts_cls), ("U-Net v2 (pontos)", pts_v2)):
        melhor, sfp, _ = curva(pts, verdades)
        res[nome] = dict(melhor={k: v for k, v in melhor.items() if k != "por_lesao"}, sens_fp=sfp,
                         por_lesao=melhor["por_lesao"])
        print(f"{nome}: " + " | ".join(f"{v:.0%} c/ {fp} FP/img" for fp, v in sfp.items()) +
              f" || melhor F1 {melhor['f1']:.2f} (limiar {melhor['limiar']}: {melhor['achadas']}/{melhor['lesoes']}, "
              f"prec {melhor['precisao']:.0%}, {melhor['falsos_por_imagem']:.2f} FP/img)")
    # união: U-Net v2 no seu limiar de operação + classificador em cada limiar
    v2_fixo = [[(x, y, 1.0) for x, y, p in pts if p >= lim_v2] for pts in pts_v2]
    uniao = [dn.fundir(a_ + b_) for a_, b_ in zip(v2_fixo, pts_cls)]
    melhor, sfp, linhas = curva(uniao, verdades)
    res["U-Net v2 + classificador"] = dict(melhor={k: v for k, v in melhor.items() if k != "por_lesao"},
                                           sens_fp=sfp, por_lesao=melhor["por_lesao"])
    print("U-Net v2 (limiar 0,5) + classificador: " + " | ".join(f"{v:.0%} c/ {fp} FP/img" for fp, v in sfp.items()) +
          f" || melhor F1 {melhor['f1']:.2f} (limiar classif. {melhor['limiar']}: {melhor['achadas']}/{melhor['lesoes']}, "
          f"prec {melhor['precisao']:.0%}, {melhor['falsos_por_imagem']:.2f} FP/img)")
    json.dump(res, open(c.RESULTADOS / "validacao" / "dentes.json", "w", encoding="utf-8"), ensure_ascii=False, indent=1)


if __name__ == "__main__":
    main()
