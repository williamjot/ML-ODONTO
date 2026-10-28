"""Do mapa de probabilidade às lesões detectadas, e a avaliação por lesão (FROC)."""
import cv2
import numpy as np
from scipy import ndimage as ndi

import comum as c

RAIO_PICO = 15      # px (escala de trabalho): picos mais próximos que isso se fundem
RAIO_REGIAO = 40    # px: a região de uma lesão não passa disso a partir do pico (RF)
RAIO_REGIAO_UNET = 100  # px: idem para a U-Net, cujo mapa é nítido
SUAVIZACAO = 4
PICO_MIN = 0.05


def detectar(prob, raio_regiao=RAIO_REGIAO):
    """Lista de (regiao_bool_no_recorte, (y0, x0), confianca), da mais para a menos confiante.

    Cada pico do mapa suavizado é uma lesão candidata; a região é o que está
    acima de metade do pico, ligado a ele, a até raio_regiao px do pico. O RF
    usa o raio padrão (o mapa dele é difuso); a U-Net, com mapa nítido, usa
    RAIO_REGIAO_UNET para lesões grandes não saírem com um lado cortado.
    """
    h, w = prob.shape
    ps = cv2.GaussianBlur(prob, (0, 0), SUAVIZACAO)
    picos = (ps == ndi.maximum_filter(ps, size=2 * RAIO_PICO + 1)) & (ps >= PICO_MIN)
    ys, xs = np.nonzero(picos)
    saida = []
    for i in np.argsort(-ps[ys, xs]):
        y, x = ys[i], xs[i]
        pk = float(ps[y, x])
        y0, y1 = max(0, y - raio_regiao), min(h, y + raio_regiao + 1)
        x0, x1 = max(0, x - raio_regiao), min(w, x + raio_regiao + 1)
        _, rot = cv2.connectedComponents((ps[y0:y1, x0:x1] >= 0.5 * pk).astype(np.uint8))
        saida.append((rot == rot[y - y0, x - x0], (y0, x0), pk))
    return saida


def mascara(deteccoes, limiar, forma):
    m = np.zeros(forma, bool)
    for reg, (y0, x0), conf in deteccoes:
        if conf >= limiar:
            m[y0:y0 + reg.shape[0], x0:x0 + reg.shape[1]] |= reg
    return m


def froc(deteccoes_por_img, verdades, limiares=np.round(np.arange(0.05, 0.96, 0.025), 3)):
    """Para cada limiar de confiança: sensibilidade, falsos por imagem, precisão, Dice."""
    linhas = []
    for t in limiares:
        r = c.resumir([c.avaliar(mascara(d, t, v.shape), v) for d, v in zip(deteccoes_por_img, verdades)])
        r["limiar"] = float(t)
        r["f1"] = 2 * r["sensibilidade"] * r["precisao"] / max(r["sensibilidade"] + r["precisao"], 1e-9)
        linhas.append(r)
    return linhas


def sensibilidade_em(curva, falsos_por_imagem):
    ok = [r["sensibilidade"] for r in curva if r["falsos_por_imagem"] <= falsos_por_imagem]
    return max(ok) if ok else 0.0


def resumo_froc(curva):
    melhor = max(curva, key=lambda r: r["f1"])
    pontos = {fp: sensibilidade_em(curva, fp) for fp in (0.5, 1, 2, 4)}
    texto = " | ".join(f"{v:.0%} c/ {fp} FP/img" for fp, v in pontos.items())
    return melhor, pontos, (f"{texto} || melhor F1 {melhor['f1']:.2f} (limiar {melhor['limiar']}: "
                            f"sens {melhor['sensibilidade']:.0%}, prec {melhor['precisao']:.0%}, "
                            f"{melhor['falsos_por_imagem']:.1f} FP/img, Dice {melhor['dice']:.2f})")
