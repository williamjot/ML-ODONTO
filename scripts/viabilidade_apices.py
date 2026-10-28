"""Passo 1 da abordagem em duas etapas: as lesões ficam perto de ápices detectáveis?

Treina na CPU um localizador rápido de ápices (U-Net com codificador ImageNet
congelado que prevê um mapa de calor) com os contornos de dentes do DENTEX, e mede:
  1. nas 50 imagens reservadas do DENTEX, quantos ápices anotados têm um ápice
     detectado por perto (qualidade do localizador);
  2. nas 20 panorâmicas locais, quantas das 45 lesões marcadas ficam a até R pixels
     de algum ápice detectado (teto da segunda etapa) e quantos recortes isso gera.

É um teste de viabilidade, não o modelo final (que seria treinado no Colab).

Uso: python viabilidade_apices.py [--iteracoes 500]
Saída: Resultados/viabilidade_apices/ (modelo, tabela, figuras de conferência)
"""
import argparse
import json
import time

import cv2
import numpy as np
import torch
import torch.nn.functional as F
from scipy import ndimage as ndi

import apices as ap
import comum as c
import unet

SAIDA = c.RESULTADOS / "viabilidade_apices"
SIGMA = 6          # px (escala de trabalho) do alvo gaussiano em cada ápice
RECORTE = (384, 576)
RAIOS = (30, 45, 60, 80)  # px: distância lesão-ápice considerada "coberta"


def preparar(ids):
    """Imagens normalizadas (largura 1400) e ápices na mesma escala."""
    dados = []
    for i in ids:
        img, esc = c.normalizar(c.ler_cinza(ap.caminho_imagem(i)))
        pts = np.array([(x * esc, y * esc) for x, y, _, _ in ap.apices_da_imagem(i)], np.float32)
        dados.append((img, pts))
    return dados


def mapa_alvo(forma, pts):
    m = np.zeros(forma, np.float32)
    yy, xx = np.mgrid[0:forma[0], 0:forma[1]]
    for x, y in pts:
        x0, x1 = int(max(0, x - 4 * SIGMA)), int(min(forma[1], x + 4 * SIGMA + 1))
        y0, y1 = int(max(0, y - 4 * SIGMA)), int(min(forma[0], y + 4 * SIGMA + 1))
        g = np.exp(-((xx[y0:y1, x0:x1] - x) ** 2 + (yy[y0:y1, x0:x1] - y) ** 2) / (2 * SIGMA ** 2))
        m[y0:y1, x0:x1] = np.maximum(m[y0:y1, x0:x1], g)
    return m


def lote(dados, alvos, rng, n=2):
    xs, ys = [], []
    ch, cw = RECORTE
    for _ in range(n):
        k = rng.integers(len(dados))
        img, alvo = dados[k][0], alvos[k]
        h, w = img.shape
        y0, x0 = rng.integers(0, h - ch + 1), rng.integers(0, w - cw + 1)
        a, b = img[y0:y0 + ch, x0:x0 + cw].astype(np.float32) / 255, alvo[y0:y0 + ch, x0:x0 + cw]
        if rng.random() < .5:
            a, b = a[:, ::-1], b[:, ::-1]
        xs.append(a.copy()); ys.append(b.copy())
    return torch.from_numpy(np.stack(xs))[:, None], torch.from_numpy(np.stack(ys))[:, None]


def picos(p, limiar=0.3, raio=10):
    ps = cv2.GaussianBlur(p, (0, 0), 1.5)
    m = (ps == ndi.maximum_filter(ps, size=2 * raio + 1)) & (ps >= limiar)
    ys, xs = np.nonzero(m)
    return np.column_stack([xs, ys]).astype(np.float32)


def dist_min(a, b):
    if len(a) == 0 or len(b) == 0:
        return np.full(len(a), np.inf)
    return np.sqrt(((a[:, None, :] - b[None, :, :]) ** 2).sum(-1)).min(1)


def main():
    ap_ = argparse.ArgumentParser()
    ap_.add_argument("--iteracoes", type=int, default=500)
    args = ap_.parse_args()
    SAIDA.mkdir(parents=True, exist_ok=True)
    torch.manual_seed(0)
    rng = np.random.default_rng(0)

    imagens, _ = ap.anotacoes()
    ids = sorted(imagens, key=int)
    rng.shuffle(ids)
    val_ids, tr_ids = ids[:50], ids[50:]
    t = time.time()
    tr, val = preparar(tr_ids), preparar(val_ids)
    alvos = [mapa_alvo(img.shape, pts) for img, pts in tr]
    print(f"DENTEX: {len(tr)} treino, {len(val)} conferência ({time.time() - t:.0f}s)", flush=True)

    modelo = unet.UNetResNet34(pre_treinada=True)
    for parte in (modelo.stem, modelo.c1, modelo.c2, modelo.c3, modelo.c4):
        for p in parte.parameters():
            p.requires_grad = False
    modelo.train()
    for mod in modelo.modules():
        if isinstance(mod, torch.nn.BatchNorm2d):
            mod.eval()
    opt = torch.optim.AdamW([p for p in modelo.parameters() if p.requires_grad], lr=1e-3, weight_decay=1e-4)
    t, acum = time.time(), 0.0
    for it in range(1, args.iteracoes + 1):
        x, y = lote(tr, alvos, rng)
        logit = modelo(x)
        # entropia cruzada com alvo gaussiano, pesando mais os pixels perto de ápices
        peso = 1 + 20 * y
        l = (F.binary_cross_entropy_with_logits(logit, y, reduction="none") * peso).mean()
        opt.zero_grad(); l.backward(); opt.step()
        acum += l.item()
        if it % 25 == 0:
            print(f"  it {it}/{args.iteracoes} perda {acum / 25:.4f} ({(time.time() - t) / it:.1f}s/it)", flush=True)
            acum = 0.0
    modelo.eval()
    torch.save({"modelo": modelo.state_dict(), "meta": dict(iteracoes=args.iteracoes, sigma=SIGMA)},
               SAIDA / "localizador_apices.pt")

    # 1. qualidade do localizador nas imagens reservadas do DENTEX
    rec, prec, n_det = [], [], []
    for img, pts in val:
        det = picos(unet.prever(modelo, img))
        n_det.append(len(det))
        rec.append((dist_min(pts, det) <= 20).mean())
        prec.append((dist_min(det, pts) <= 20).mean() if len(det) else 0)
    q = dict(recall_20px=float(np.mean(rec)), precisao_20px=float(np.mean(prec)),
             apices_por_imagem=float(np.mean(n_det)), apices_anotados_por_imagem=float(np.mean([len(p) for _, p in val])))
    print(f"DENTEX reservadas: {q}", flush=True)

    # 2. cobertura das lesões locais
    nossas = c.nossas_imagens()
    linhas, cobertas = [], {r: 0 for r in RAIOS}
    total = 0
    for nm, img, m in nossas:
        p = unet.prever(modelo, img)
        det = picos(p)
        n, rot = cv2.connectedComponents(m.astype(np.uint8))
        for k in range(1, n):
            ys, xs = np.nonzero(rot == k)
            # distância do ápice mais próximo a QUALQUER pixel da lesão
            d = dist_min(det, np.column_stack([xs, ys]).astype(np.float32)).min() if len(det) else np.inf
            total += 1
            for r in RAIOS:
                cobertas[r] += d <= r
            linhas.append(dict(paciente=nm, lesao=k, dist_apice_px=round(float(d), 1),
                               metade="superior" if ys.mean() / m.shape[0] < .5 else "inferior"))
        # figura de conferência
        o = cv2.cvtColor(img, cv2.COLOR_GRAY2BGR)
        cn, _ = cv2.findContours(m.astype(np.uint8), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        cv2.drawContours(o, cn, -1, (60, 200, 60), 2)
        for x, y in det:
            cv2.circle(o, (int(x), int(y)), 5, (255, 120, 0), -1)
        cv2.imwrite(str(SAIDA / f"conferencia_{nm}.jpg"), o)
    cob = {r: cobertas[r] / total for r in RAIOS}
    print("Lesões locais a até R px de um ápice detectado: " +
          " | ".join(f"R={r}: {cobertas[r]}/{total} ({cob[r]:.0%})" for r in RAIOS), flush=True)
    sup = [l for l in linhas if l["metade"] == "superior"]
    inf = [l for l in linhas if l["metade"] == "inferior"]
    for nome, grupo in (("superior", sup), ("inferior", inf)):
        print(f"  metade {nome}: " + " | ".join(
            f"R={r}: {sum(l['dist_apice_px'] <= r for l in grupo)}/{len(grupo)}" for r in RAIOS), flush=True)
    json.dump(dict(dentex=q, cobertura={str(r): v for r, v in cob.items()}, lesoes=linhas),
              open(SAIDA / "resultado.json", "w", encoding="utf-8"), indent=2, ensure_ascii=False)


if __name__ == "__main__":
    main()
