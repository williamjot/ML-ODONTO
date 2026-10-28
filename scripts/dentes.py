"""Classificador por dente: "este ápice tem lesão periapical?".

Usa os ápices do localizador (duas_etapas.detectar_apices) e examina cada um num recorte com mais
contexto que o da segunda etapa (RECORTE px a LARGURA_RECORTE, ou seja, RECORTE / 2 px na escala de
trabalho). Os recortes de ápices inferiores são espelhados na vertical, para que a raiz aponte sempre
para cima: assim maxila e mandíbula compartilham o que a rede aprende.

Saída de uma panorâmica: lista de (x, y, probabilidade) na escala de trabalho (comum.LARGURA).
"""
import numpy as np
import torch
import torch.nn as nn
import torchvision

import comum as c
import duas_etapas as de

LARGURA_RECORTE = 2800   # px de largura da panorâmica de onde saem os recortes (2x a escala de trabalho)
RECORTE = 384            # px a LARGURA_RECORTE = 192 px na escala de trabalho
MARGEM = 16              # px extras guardados em volta, para deslocamento aleatório no treino
R_POS = 40               # px (escala de trabalho): ápice a esta distância de uma lesão é positivo
R_NEG = 90               # px: ápice a mais que isso de qualquer lesão é negativo (entre os dois: ignorado)
RAIO_FUSAO = 25          # px: ápices previstos mais próximos que isso viram uma detecção só


class ClassificadorDente(nn.Module):
    def __init__(self, pre_treinada=False):
        super().__init__()
        self.rede = torchvision.models.resnet34(weights="IMAGENET1K_V1" if pre_treinada else None)
        self.rede.fc = nn.Linear(512, 1)
        self.register_buffer("media", torch.tensor([0.485, 0.456, 0.406]).view(1, 3, 1, 1), persistent=False)
        self.register_buffer("desvio", torch.tensor([0.229, 0.224, 0.225]).view(1, 3, 1, 1), persistent=False)

    def forward(self, x):
        """x: (N, 1, H, W) em [0, 1]. Devolve logits (N,)."""
        return self.rede((x.repeat(1, 3, 1, 1) - self.media) / self.desvio)[:, 0]


def carregar(caminho, dispositivo="cpu"):
    ck = torch.load(caminho, map_location=dispositivo, weights_only=False)
    m = ClassificadorDente()
    m.load_state_dict(ck["modelo"])
    return m.to(dispositivo).eval(), ck.get("meta", {})


def recortes_apices(img_r, apices, altura_trabalho, tam=RECORTE):
    """Recortes tam x tam (escala LARGURA_RECORTE) centrados nos ápices (escala de trabalho).

    Ápices na metade inferior da imagem são espelhados na vertical (raiz para cima).
    """
    fator = LARGURA_RECORTE / c.LARGURA
    saida = []
    for x, y in apices:
        r, _ = de.recorte_em(img_r, x * fator, y * fator, tam, 0)
        saida.append(r[::-1] if y > altura_trabalho / 2 else r)
    return saida


def rotular_apices(apices, lesoes, r_pos=R_POS, r_neg=R_NEG):
    """1 = lesão, 0 = sem lesão, -1 = ignorado. `lesoes`: caixas (x0, y0, x1, y1) na escala de trabalho.

    Distância do ápice à caixa (0 se dentro dela).
    """
    rot = np.zeros(len(apices), np.int8)
    if not len(lesoes):
        return rot
    a = np.asarray(apices, np.float32).reshape(-1, 2)
    b = np.asarray(lesoes, np.float32).reshape(-1, 4)
    dx = np.maximum(0, np.maximum(b[None, :, 0] - a[:, None, 0], a[:, None, 0] - b[None, :, 2]))
    dy = np.maximum(0, np.maximum(b[None, :, 1] - a[:, None, 1], a[:, None, 1] - b[None, :, 3]))
    d = np.sqrt(dx ** 2 + dy ** 2).min(1)
    rot[d <= r_pos] = 1
    rot[(d > r_pos) & (d <= r_neg)] = -1
    return rot


@torch.no_grad()
def classificar(modelo, recortes, lote=32, espelhar=True):
    if not recortes:
        return np.zeros(0, np.float32)
    disp = next(modelo.parameters()).device
    saida = []
    for i in range(0, len(recortes), lote):
        x = torch.from_numpy(np.stack(recortes[i:i + lote]).astype(np.float32) / 255.0)[:, None].to(disp)
        p = torch.sigmoid(modelo(x))
        if espelhar:
            p = (p + torch.sigmoid(modelo(x.flip(-1)))) / 2
        saida.append(p.float().cpu().numpy())
    return np.concatenate(saida)


def detectar(localizador, classificador, original):
    """[(x, y, p)] na escala de trabalho para uma panorâmica original (tons de cinza)."""
    img_t, _ = c.normalizar(original)
    apices = de.detectar_apices(localizador, img_t)
    img_r, _ = c.normalizar(original, LARGURA_RECORTE)
    p = classificar(classificador, recortes_apices(img_r, apices, img_t.shape[0]))
    return [(float(x), float(y), float(q)) for (x, y), q in zip(apices, p)]


def fundir(deteccoes, raio=RAIO_FUSAO):
    """Junta ápices vizinhos, mantendo o de maior probabilidade (ápices de um mesmo dente)."""
    restantes = sorted(deteccoes, key=lambda d: -d[2])
    saida = []
    for x, y, p in restantes:
        if all((x - a) ** 2 + (y - b) ** 2 > raio ** 2 for a, b, _ in saida):
            saida.append((x, y, p))
    return saida
