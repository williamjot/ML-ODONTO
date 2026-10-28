"""Abordagem em duas etapas: localizar os ápices radiculares, depois segmentar a lesão
num recorte ampliado em volta de cada ápice.

Etapa 1 (localizador): U-Net que prevê um mapa de calor de ápices na panorâmica
normalizada a 1400 px de largura (comum.LARGURA). Treinada com os contornos de dentes
do DENTEX (apices.py).

Etapa 2 (lesão): U-Net que segmenta a lesão em recortes de RECORTE x RECORTE pixels,
tirados da panorâmica normalizada a LARGURA_RECORTE (~ resolução original, o dobro da
escala de trabalho), centrados em cada ápice detectado.

Usado no Colab (treino) e localmente (avaliação/aplicação). Só depende de comum, unet,
OpenCV, NumPy e SciPy.
"""
import cv2
import numpy as np
import torch
from scipy import ndimage as ndi

import comum as c
import unet

SIGMA_APICE = 6          # px (escala 1400) do alvo gaussiano de cada ápice
LIMIAR_APICE = 0.3       # pico mínimo do mapa de calor para virar ápice
RAIO_NMS_APICE = 10      # px (escala 1400): ápices mais próximos que isso se fundem
MAX_APICES = 60          # por imagem; uma dentição completa tem ~40 ápices
LARGURA_RECORTE = 2800   # px: largura em que os recortes são tirados
RECORTE = 256            # px (escala LARGURA_RECORTE) = 128 px na escala de trabalho
MARGEM_AUMENTO = 16      # px extras guardados em volta do recorte, para deslocamento aleatório no treino


def mapa_alvo(forma, pts, sigma=SIGMA_APICE):
    """Mapa de calor alvo: máximo de gaussianas centradas nos ápices."""
    m = np.zeros(forma, np.float32)
    r = int(4 * sigma)
    for x, y in pts:
        x0, x1 = int(max(0, x - r)), int(min(forma[1], x + r + 1))
        y0, y1 = int(max(0, y - r)), int(min(forma[0], y + r + 1))
        if x0 >= x1 or y0 >= y1:
            continue
        yy, xx = np.mgrid[y0:y1, x0:x1]
        g = np.exp(-((xx - x) ** 2 + (yy - y) ** 2) / (2 * sigma ** 2)).astype(np.float32)
        m[y0:y1, x0:x1] = np.maximum(m[y0:y1, x0:x1], g)
    return m


def picos(p, limiar=LIMIAR_APICE, raio=RAIO_NMS_APICE, maximo=MAX_APICES):
    """Coordenadas (x, y) dos máximos locais do mapa de calor, no máximo `maximo`, os mais fortes primeiro."""
    ps = cv2.GaussianBlur(p, (0, 0), 1.5)
    m = (ps == ndi.maximum_filter(ps, size=2 * raio + 1)) & (ps >= limiar)
    ys, xs = np.nonzero(m)
    ordem = np.argsort(-ps[ys, xs])[:maximo]
    return np.column_stack([xs[ordem], ys[ordem]]).astype(np.float32)


def detectar_apices(localizador, img_trabalho):
    """Ápices (x, y) na escala de trabalho (imagem normalizada a comum.LARGURA)."""
    return picos(unet.prever(localizador, img_trabalho))


def recorte_em(img, cx, cy, tam, preencher=0):
    """Recorte tam x tam centrado em (cx, cy), completando fora da imagem com `preencher`."""
    h, w = img.shape[:2]
    x0, y0 = int(round(cx)) - tam // 2, int(round(cy)) - tam // 2
    out = np.full((tam, tam), preencher, img.dtype)
    sx0, sy0 = max(0, x0), max(0, y0)
    sx1, sy1 = min(w, x0 + tam), min(h, y0 + tam)
    if sx1 > sx0 and sy1 > sy0:
        out[sy0 - y0:sy1 - y0, sx0 - x0:sx1 - x0] = img[sy0:sy1, sx0:sx1]
    return out, (x0, y0)


@torch.no_grad()
def prever_recortes(modelo, recortes, lote=16, espelhar=True):
    """Probabilidades de vários recortes RECORTE x RECORTE de uma vez (em lotes), como unet.prever."""
    if not recortes:
        return []
    disp = next(modelo.parameters()).device
    saida = []
    for i in range(0, len(recortes), lote):
        x = torch.from_numpy(np.stack(recortes[i:i + lote]).astype(np.float32) / 255.0)[:, None].to(disp)
        p = torch.sigmoid(modelo(x))
        if espelhar:
            p = (p + torch.sigmoid(modelo(x.flip(-1))).flip(-1)) / 2
        saida.extend(p[:, 0].float().cpu().numpy())
    return saida


def probabilidade_duas_etapas(localizador, segmentador, original):
    """Mapa de probabilidade na escala de trabalho a partir das duas etapas.

    Fora dos recortes a probabilidade é 0: a etapa 2 só examina a vizinhança dos
    ápices detectados. Recortes sobrepostos são combinados pelo máximo.
    Devolve (prob_trabalho, apices_trabalho).
    """
    img_t, esc_t = c.normalizar(original)
    apices = detectar_apices(localizador, img_t)
    img_r, esc_r = c.normalizar(original, LARGURA_RECORTE)
    fator = esc_r / esc_t
    prob_r = np.zeros(img_r.shape, np.float32)
    recs = [recorte_em(img_r, x * fator, y * fator, RECORTE) for x, y in apices]
    probs = prever_recortes(segmentador, [r for r, _ in recs])
    for p, (_, (x0, y0)) in zip(probs, recs):
        sx0, sy0 = max(0, x0), max(0, y0)
        sx1, sy1 = min(img_r.shape[1], x0 + RECORTE), min(img_r.shape[0], y0 + RECORTE)
        if sx1 > sx0 and sy1 > sy0:
            alvo = prob_r[sy0:sy1, sx0:sx1]
            np.maximum(alvo, p[sy0 - y0:sy1 - y0, sx0 - x0:sx1 - x0], out=alvo)
    prob_t = cv2.resize(prob_r, (img_t.shape[1], img_t.shape[0]), interpolation=cv2.INTER_AREA)
    return prob_t, apices
