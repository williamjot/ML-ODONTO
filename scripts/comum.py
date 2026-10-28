"""Peças compartilhadas: carregamento, normalização, atributos por pixel e métricas."""
import xml.etree.ElementTree as ET
from pathlib import Path

import cv2
import numpy as np
from scipy import ndimage as ndi
from skimage.feature import hessian_matrix, hessian_matrix_eigvals

RAIZ = Path(__file__).resolve().parent.parent
NOSSAS = RAIZ / "Treinamento" / "extraido"
HANOI = RAIZ / "Externos" / "Hanoi"
MODELOS = RAIZ / "Modelos"
RESULTADOS = RAIZ / "Resultados"

# Todas as panorâmicas são levadas a esta largura antes de tudo: as originais têm
# ~2400-2900 px e as lesões ~60-100 px, que viram ~30-50 px aqui.
LARGURA = 1400


def ler_cinza(caminho):
    # imdecode lida com acentos no caminho (cv2.imread não, no Windows)
    dados = np.fromfile(str(caminho), np.uint8)
    img = cv2.imdecode(dados, cv2.IMREAD_GRAYSCALE)
    if img is None:
        raise ValueError(f"não consegui ler {caminho}")
    return img


def normalizar(img, largura=None):
    """Redimensiona para `largura` (padrão LARGURA) e equaliza o contraste (CLAHE) para aproximar aparelhos diferentes."""
    largura = largura or LARGURA
    escala = largura / img.shape[1]
    interp = cv2.INTER_AREA if escala <= 1 else cv2.INTER_CUBIC
    img = cv2.resize(img, (largura, round(img.shape[0] * escala)), interpolation=interp)
    img = cv2.createCLAHE(clipLimit=2.0, tileGridSize=(8, 8)).apply(img)
    return img, escala


def redimensionar_mascara(m, forma):
    return cv2.resize(m, (forma[1], forma[0]), interpolation=cv2.INTER_NEAREST) > 0


NOMES_ATRIBUTOS = []
VERSAO_ATRIBUTOS = 2  # muda quando atributos() muda, para invalidar amostras em cache


def deslocar(A, dy, dx):
    """Valor de A no ponto (y+dy, x+dx), repetindo a borda fora da imagem."""
    p = max(abs(dy), abs(dx))
    Ap = np.pad(A, p, mode="edge")
    h, w = A.shape
    return Ap[p + dy:p + dy + h, p + dx:p + dx + w]


def atributos(img):
    """Pilha (H, W, F) de atributos por pixel, no estilo ilastik/Weka.

    A lesão periapical é uma área mais escura que o osso ao redor, perto da ponta
    da raiz: por isso brilho em várias escalas, contraste com a vizinhança (DoG),
    bordas, forma local (Hessiana), textura e posição na imagem.
    """
    I = img.astype(np.float32) / 255.0
    h, w = I.shape
    camadas, nomes = [I], ["brilho"]
    G = {s: cv2.GaussianBlur(I, (0, 0), s) for s in (1, 2, 4, 8, 16, 32)}
    for s in (1, 2, 4, 8, 16):
        camadas.append(G[s]); nomes.append(f"gauss_{s}")
    for a, b in ((1, 4), (2, 8), (4, 16), (8, 32)):
        camadas.append(G[a] - G[b]); nomes.append(f"dog_{a}_{b}")
    for s in (1, 2, 4, 8):
        gx = cv2.Sobel(G[s], cv2.CV_32F, 1, 0)
        gy = cv2.Sobel(G[s], cv2.CV_32F, 0, 1)
        camadas.append(np.sqrt(gx * gx + gy * gy)); nomes.append(f"gradiente_{s}")
    for s in (2, 4, 8):
        H = hessian_matrix(I, sigma=s, order="rc", use_gaussian_derivatives=False)
        l1, l2 = hessian_matrix_eigvals(H)
        camadas += [l1.astype(np.float32), l2.astype(np.float32)]
        nomes += [f"hessiana_{s}_l1", f"hessiana_{s}_l2"]
    for s in (4, 8):
        m2 = cv2.GaussianBlur(I * I, (0, 0), s)
        camadas.append(np.sqrt(np.maximum(m2 - G[s] ** 2, 0))); nomes.append(f"desvio_{s}")
    # Contexto: a lesão fica na ponta de uma raiz, então o brilho alguns pixels
    # acima/abaixo/ao lado (raiz clara? osso? ar?) diz muito sobre o pixel central.
    for s in (4, 8):
        for dy, dx in ((-40, 0), (-20, 0), (20, 0), (40, 0), (0, -25), (0, 25)):
            camadas.append(deslocar(G[s], dy, dx) - G[s]); nomes.append(f"contexto_{s}_{dy}_{dx}")
    camadas.append(G[32] - cv2.GaussianBlur(I, (0, 0), 64)); nomes.append("dog_32_64")
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    camadas += [xx / w, yy / h, np.abs(xx / w - 0.5)]
    nomes += ["pos_x", "pos_y", "dist_centro_x"]
    if not NOMES_ATRIBUTOS:
        NOMES_ATRIBUTOS.extend(nomes)
    return np.stack(camadas, axis=-1)


# ---------------------------------------------------------------- conjuntos

def nossas_imagens():
    """[(id, img_normalizada, mascara_bool)] das 20 panorâmicas marcadas do PDF."""
    saida = []
    for f in sorted((NOSSAS / "imagens").glob("*.png")):
        img, _ = normalizar(ler_cinza(f))
        m = redimensionar_mascara(ler_cinza(NOSSAS / "mascaras" / f.name), img.shape)
        saida.append((f.stem, img, m))
    return saida


def caixas_hanoi(nome):
    r = ET.parse(HANOI / "anotacoes" / "Image Annots" / f"{nome}.xml").getroot()
    caixas = []
    for o in r.iter("object"):
        b = o.find("bndbox")
        caixas.append(tuple(float(b.findtext(t)) for t in ("xmin", "ymin", "xmax", "ymax")))
    return caixas


def imagem_hanoi(nome, largura=None):
    """Imagem normalizada, máscara (elipse dentro de cada caixa) e zona incerta.

    A caixa é justa em torno da lesão, então a elipse inscrita é uma boa
    aproximação. A faixa entre a elipse e a caixa ampliada é marcada como
    incerta e não entra no treino nem como lesão nem como fundo.
    """
    img, esc = normalizar(ler_cinza(HANOI / "imagens" / "Original JPG Images" / f"{nome}.jpg"), largura)
    mascara = np.zeros(img.shape, np.uint8)
    incerta = np.zeros(img.shape, np.uint8)
    for x0, y0, x1, y1 in caixas_hanoi(nome):
        cx, cy = (x0 + x1) / 2 * esc, (y0 + y1) / 2 * esc
        rx, ry = (x1 - x0) / 2 * esc, (y1 - y0) / 2 * esc
        cv2.ellipse(incerta, (round(cx), round(cy)), (round(rx * 1.5), round(ry * 1.5)), 0, 0, 360, 1, -1)
        cv2.ellipse(mascara, (round(cx), round(cy)), (round(rx * 0.85), round(ry * 0.85)), 0, 0, 360, 1, -1)
    return img, mascara > 0, (incerta > 0) & (mascara == 0)


def nomes_hanoi():
    return sorted(p.stem for p in (HANOI / "imagens" / "Original JPG Images").glob("*.jpg"))


# ---------------------------------------------------------------- amostragem

def amostrar(F, mascara, incerta, n_pos, n_neg, rng):
    """Sorteia pixels de lesão e de fundo. Metade do fundo vem da faixa dos dentes,
    onde estão as confusões difíceis (raízes, espaços entre dentes)."""
    h, w = mascara.shape
    pos = np.flatnonzero(mascara)
    neg_ok = ~mascara & ~incerta
    faixa = np.zeros_like(neg_ok)
    faixa[int(0.25 * h):int(0.85 * h), int(0.15 * w):int(0.85 * w)] = True
    neg_todos = np.flatnonzero(neg_ok)
    neg_faixa = np.flatnonzero(neg_ok & faixa)
    escolha = lambda arr, n: rng.choice(arr, min(n, len(arr)), replace=False) if len(arr) else arr
    idx_pos = escolha(pos, n_pos)
    idx_neg = np.concatenate([escolha(neg_todos, n_neg // 2), escolha(neg_faixa, n_neg - n_neg // 2)])
    Fp = F.reshape(-1, F.shape[-1])
    X = np.concatenate([Fp[idx_pos], Fp[idx_neg]])
    y = np.concatenate([np.ones(len(idx_pos), np.uint8), np.zeros(len(idx_neg), np.uint8)])
    return X, y


# ---------------------------------------------------------------- predição

def prever_probabilidade(modelo, img, lote=200_000, devolver_atributos=False):
    F = atributos(img)
    Fp = F.reshape(-1, F.shape[-1])
    p = np.concatenate([modelo.predict_proba(Fp[i:i + lote])[:, 1] for i in range(0, len(Fp), lote)])
    p = p.reshape(img.shape).astype(np.float32)
    return (p, F) if devolver_atributos else p


def segmentar(prob, limiar, area_min):
    """Probabilidade -> máscara: suaviza, corta no limiar e descarta manchas pequenas."""
    p = cv2.GaussianBlur(prob, (0, 0), 2)
    m = (p >= limiar).astype(np.uint8)
    n, rot, st, _ = cv2.connectedComponentsWithStats(m)
    manter = np.zeros(n, bool)
    manter[1:] = st[1:, cv2.CC_STAT_AREA] >= area_min
    return manter[rot]


# ---------------------------------------------------------------- métricas

FRACAO_ACERTO = 0.2


def avaliar(pred, verdade):
    """Por imagem: acertos de pixel e de lesão.

    Uma mancha prevista só é acerto se pelo menos FRACAO_ACERTO dela cair sobre
    lesão de verdade; assim uma mancha enorme que só encosta na lesão não conta.
    Uma lesão verdadeira é achada se alguma mancha correta a toca.
    """
    inter = (pred & verdade).sum()
    nv, rv = cv2.connectedComponents(verdade.astype(np.uint8))
    np_, rp = cv2.connectedComponents(pred.astype(np.uint8))
    area = np.bincount(rp.ravel(), minlength=np_)
    sobre = np.bincount(rp[verdade], minlength=np_)
    corretas = np.flatnonzero(sobre >= FRACAO_ACERTO * np.maximum(area, 1))
    corretas = corretas[corretas > 0]
    achadas = len(set(np.unique(rv[np.isin(rp, corretas)])) - {0})
    return dict(inter=int(inter), soma=int(pred.sum() + verdade.sum()),
                lesoes=nv - 1, lesoes_achadas=achadas,
                previstas=np_ - 1, previstas_corretas=len(corretas))


def resumir(linhas):
    s = {k: sum(l[k] for l in linhas) for k in linhas[0]}
    return dict(
        dice=2 * s["inter"] / max(s["soma"], 1),
        sensibilidade=s["lesoes_achadas"] / max(s["lesoes"], 1),
        precisao=s["previstas_corretas"] / max(s["previstas"], 1),
        falsos_por_imagem=(s["previstas"] - s["previstas_corretas"]) / len(linhas),
    )
