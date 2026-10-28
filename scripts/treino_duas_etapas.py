"""Treina as duas etapas no Colab (GPU): localizador de ápices (DENTEX) e U-Net de
lesão em recortes ampliados em volta dos ápices (Hanói).

Fases, cada uma pulada se o resultado já estiver em --saida (permite retomar):
  1. localizador_apices.pt   U-Net de mapa de calor treinada nos ápices do DENTEX
  2. apices_hanoi.json       ápices detectados pelo localizador nas panorâmicas de Hanói
  3. recortes/               recortes (imagem + rótulo) em volta desses ápices
  4. lesoes_recortes.pt      U-Net de lesão treinada nos recortes

Uso: python treino_duas_etapas.py --saida PASTA [--its1 3000] [--its2 6000]
     [--perda tversky] [--limite-dentex N --limite-hanoi N --sem-pre-treino]  (testes)
"""
import argparse
import json
import math
import os
import time
from pathlib import Path

import cv2
import numpy as np
import torch
import torch.nn.functional as F
from joblib import Parallel, delayed
from torch.utils.data import DataLoader, Dataset

import apices as ap
import comum as c
import duas_etapas as de
from treino_unet import IGNORAR, perda
from unet import UNetResNet34, prever

TOTAL = de.RECORTE + 2 * de.MARGEM_AUMENTO
N_NEGATIVOS = 4      # recortes sem lesão guardados por imagem
MAX_POSITIVOS = 8    # recortes com lesão guardados por imagem (os ápices mais próximos da lesão)
MIN_LESAO_PX = 20    # pixels de lesão no recorte central para contar como positivo


def log(msg):
    print(msg, flush=True)


def agenda_cos(opt, total, aquecimento=200):
    return torch.optim.lr_scheduler.LambdaLR(
        opt, lambda it: min(1, (it + 1) / aquecimento) * 0.5 * (1 + math.cos(math.pi * min(it, total) / total)))


# ------------------------------------------------------------------ fase 1: localizador
class RecortesApices(Dataset):
    def __init__(self, dados, n, semente=0, tam=(512, 768)):
        self.dados, self.n, self.semente, self.tam = dados, n, semente, tam

    def __len__(self):
        return self.n

    def __getitem__(self, i):
        rng = np.random.default_rng(self.semente * 1_000_003 + i)
        img, pts = self.dados[rng.integers(len(self.dados))]
        ch, cw = self.tam
        h, w = img.shape
        y0, x0 = rng.integers(0, h - ch + 1), rng.integers(0, w - cw + 1)
        a = img[y0:y0 + ch, x0:x0 + cw].astype(np.float32) / 255
        p = pts - np.array([x0, y0], np.float32)
        if rng.random() < .5:
            a = a[:, ::-1]
            p = p.copy(); p[:, 0] = cw - 1 - p[:, 0]
        alvo = de.mapa_alvo((ch, cw), p)
        a = np.clip(a ** rng.uniform(0.7, 1.4) * rng.uniform(0.85, 1.15) + rng.uniform(-0.08, 0.08), 0, 1)
        return torch.from_numpy(a.copy())[None], torch.from_numpy(alvo)[None]


def avaliar_localizador(modelo, val):
    modelo.eval()
    vp = fn = fp = 0
    for img, pts in val:
        det = de.picos(prever(modelo, img, espelhar=False))
        if len(det) and len(pts):
            d = np.sqrt(((pts[:, None] - det[None]) ** 2).sum(-1))
            vp_i = int((d.min(1) <= 20).sum())
            fp += int((d.min(0) > 20).sum())
        else:
            vp_i = 0
            fp += len(det)
        vp += vp_i; fn += len(pts) - vp_i
    modelo.train()
    rec, prec = vp / max(vp + fn, 1), vp / max(vp + fp, 1)
    return rec, prec, 2 * rec * prec / max(rec + prec, 1e-9)


def fase_localizador(args, disp, saida):
    arq = saida / "localizador_apices.pt"
    if arq.exists():
        log("Fase 1: localizador já treinado, carregando")
        return
    imagens, _ = ap.anotacoes()
    ids = sorted(imagens, key=int)
    np.random.default_rng(0).shuffle(ids)
    if args.limite_dentex:
        ids = ids[:args.limite_dentex]
    n_val = min(50, len(ids) // 5)

    def carregar(i):
        img, esc = c.normalizar(c.ler_cinza(ap.caminho_imagem(i)))
        pts = np.array([(x * esc, y * esc) for x, y, _, _ in ap.apices_da_imagem(i)], np.float32).reshape(-1, 2)
        return img, pts

    t = time.time()
    dados = Parallel(n_jobs=max(1, args.workers))(delayed(carregar)(i) for i in ids)
    val, tr = dados[:n_val], dados[n_val:]
    log(f"Fase 1: DENTEX {len(tr)} treino / {len(val)} validação ({time.time() - t:.0f}s)")

    modelo = UNetResNet34(pre_treinada=not args.sem_pre_treino).to(disp)
    opt = torch.optim.AdamW(modelo.parameters(), lr=3e-4, weight_decay=1e-4)
    agenda = agenda_cos(opt, args.its1)
    escal = torch.amp.GradScaler(enabled=disp == "cuda")
    carga = DataLoader(RecortesApices(tr, args.its1 * args.lote1), batch_size=args.lote1,
                       num_workers=args.workers, pin_memory=disp == "cuda")
    melhor, t, acum = -1.0, time.time(), 0.0
    modelo.train()
    for it, (x, y) in enumerate(carga, 1):
        x, y = x.to(disp), y.to(disp)
        with torch.autocast(device_type=disp, dtype=torch.float16, enabled=disp == "cuda"):
            logit = modelo(x)
        l = (F.binary_cross_entropy_with_logits(logit.float(), y, reduction="none") * (1 + 20 * y)).mean()
        opt.zero_grad(set_to_none=True); escal.scale(l).backward(); escal.step(opt); escal.update(); agenda.step()
        acum += l.item()
        if it % 50 == 0:
            log(f"  [1] it {it}/{args.its1} perda {acum / 50:.4f} ({(time.time() - t) / 50:.2f}s/it)")
            t, acum = time.time(), 0.0
        if it % args.validar_cada == 0 or it == args.its1:
            rec, prec, f1 = avaliar_localizador(modelo, val)
            if f1 > melhor:
                melhor = f1
                torch.save({"modelo": modelo.state_dict(),
                            "meta": dict(iteracao=it, recall_20px=rec, precisao_20px=prec, f1=f1,
                                         sigma=de.SIGMA_APICE, largura=c.LARGURA)}, arq)
            log(f"  == [1] validação it {it}: ápices a ≤20 px recall {rec:.3f} precisão {prec:.3f} F1 {f1:.3f} (melhor {melhor:.3f})")


# ------------------------------------------------------------------ fase 2: ápices em Hanói
class ImagensHanoi(Dataset):
    def __init__(self, nomes):
        self.nomes = nomes

    def __len__(self):
        return len(self.nomes)

    def __getitem__(self, i):
        img, _ = c.normalizar(c.ler_cinza(c.HANOI / "imagens" / "Original JPG Images" / f"{self.nomes[i]}.jpg"))
        return self.nomes[i], img


def fase_apices_hanoi(args, disp, saida, nomes):
    arq = saida / "apices_hanoi.json"
    if arq.exists():
        log("Fase 2: ápices de Hanói já detectados")
        return json.load(open(arq))
    ck = torch.load(saida / "localizador_apices.pt", map_location=disp, weights_only=False)
    modelo = UNetResNet34().to(disp)
    modelo.load_state_dict(ck["modelo"]); modelo.eval()
    res, t = {}, time.time()
    carga = DataLoader(ImagensHanoi(nomes), batch_size=None, num_workers=args.workers)
    for k, (nm, img) in enumerate(carga, 1):
        res[nm] = de.detectar_apices(modelo, np.asarray(img)).round(1).tolist()
        if k % 250 == 0:
            log(f"  [2] {k}/{len(nomes)} ({(time.time() - t) / k:.2f}s/imagem)")
    json.dump(res, open(arq, "w"))
    n = [len(v) for v in res.values()]
    log(f"Fase 2: {sum(n)} ápices em {len(n)} imagens (mediana {np.median(n):.0f} por imagem)")
    return res


# ------------------------------------------------------------------ fase 3: recortes
def recortes_da_imagem(nome, pts, semente):
    """Recortes TOTAL x TOTAL (imagem e rótulo 0/1/2) em volta dos ápices de uma imagem de Hanói."""
    img, m, inc = c.imagem_hanoi(nome, de.LARGURA_RECORTE)
    rot = np.zeros(img.shape, np.uint8)
    rot[inc] = IGNORAR
    rot[m] = 1
    fator = de.LARGURA_RECORTE / c.LARGURA
    pos, neg = [], []
    for x, y in pts:
        ri, _ = de.recorte_em(img, x * fator, y * fator, TOTAL, 0)
        rr, _ = de.recorte_em(rot, x * fator, y * fator, TOTAL, IGNORAR)
        centro = rr[de.MARGEM_AUMENTO:-de.MARGEM_AUMENTO, de.MARGEM_AUMENTO:-de.MARGEM_AUMENTO]
        n_les = int((centro == 1).sum())
        (pos if n_les >= MIN_LESAO_PX else neg).append((n_les, ri, rr))
    rng = np.random.default_rng(semente)
    pos = sorted(pos, key=lambda t: -t[0])[:MAX_POSITIVOS]
    neg = [neg[k] for k in rng.permutation(len(neg))[:N_NEGATIVOS]]
    sel = pos + neg
    if not sel:
        return None
    return (np.stack([s[1] for s in sel]), np.stack([s[2] for s in sel]),
            np.array([1] * len(pos) + [0] * len(neg), np.uint8))


def fase_recortes(args, saida, apices_hanoi, nomes):
    pasta = Path(args.pasta_recortes)
    pasta.mkdir(parents=True, exist_ok=True)
    pronto = pasta / "pronto.json"
    if pronto.exists():
        log("Fase 3: recortes já preparados")
        return json.load(open(pronto))
    t, partes, bloco = time.time(), [], 200
    for b in range(0, len(nomes), bloco):
        lote = nomes[b:b + bloco]
        res = Parallel(n_jobs=max(1, args.workers))(delayed(recortes_da_imagem)(nm, apices_hanoi.get(nm, []), k)
                                            for k, nm in enumerate(lote, b))
        res = [(nm, r) for nm, r in zip(lote, res) if r is not None]
        if res:
            np.save(pasta / f"img_{b:05d}.npy", np.concatenate([r[0] for _, r in res]))
            np.save(pasta / f"rot_{b:05d}.npy", np.concatenate([r[1] for _, r in res]))
            np.save(pasta / f"pos_{b:05d}.npy", np.concatenate([r[2] for _, r in res]))
            partes.append(dict(parte=f"{b:05d}", nomes=[nm for nm, r in res for _ in range(len(r[2]))]))
        log(f"  [3] {min(b + bloco, len(nomes))}/{len(nomes)} imagens ({time.time() - t:.0f}s)")
    json.dump(partes, open(pronto, "w"))
    return partes


class Recortes(Dataset):
    def __init__(self, pasta, partes, nomes_ok, n, semente=0):
        self.img, self.rot, idx_pos, idx_neg = [], [], [], []
        for k, p in enumerate(partes):
            self.img.append(np.load(Path(pasta) / f"img_{p['parte']}.npy", mmap_mode="r"))
            self.rot.append(np.load(Path(pasta) / f"rot_{p['parte']}.npy", mmap_mode="r"))
            pos = np.load(Path(pasta) / f"pos_{p['parte']}.npy")
            for j, nm in enumerate(p["nomes"]):
                if nm in nomes_ok:
                    (idx_pos if pos[j] else idx_neg).append((k, j))
        self.pos, self.neg, self.n, self.semente = idx_pos, idx_neg, n, semente

    def __len__(self):
        return self.n

    def __getitem__(self, i):
        rng = np.random.default_rng(self.semente * 1_000_003 + i)
        fonte = self.pos if (rng.random() < 0.5 and self.pos) or not self.neg else self.neg
        k, j = fonte[rng.integers(len(fonte))]
        dy, dx = rng.integers(0, 2 * de.MARGEM_AUMENTO + 1, size=2)
        a = self.img[k][j, dy:dy + de.RECORTE, dx:dx + de.RECORTE].astype(np.float32) / 255
        r = np.array(self.rot[k][j, dy:dy + de.RECORTE, dx:dx + de.RECORTE])
        if rng.random() < .5:
            a, r = a[:, ::-1], r[:, ::-1]
        a = np.clip(a ** rng.uniform(0.7, 1.4) * rng.uniform(0.85, 1.15) + rng.uniform(-0.08, 0.08), 0, 1)
        return torch.from_numpy(a.copy())[None], torch.from_numpy(r.copy()).long()


def avaliar_segmentador(modelo, ds, disp, lote=64):
    """Dice nos recortes de validação (positivos e negativos, sem deslocamento)."""
    modelo.eval()
    inter = soma = 0
    m = de.MARGEM_AUMENTO
    with torch.no_grad():
        for fonte in (ds.pos, ds.neg):
            for b in range(0, len(fonte), lote):
                xs, rs = [], []
                for k, j in fonte[b:b + lote]:
                    xs.append(ds.img[k][j, m:m + de.RECORTE, m:m + de.RECORTE].astype(np.float32) / 255)
                    rs.append(np.array(ds.rot[k][j, m:m + de.RECORTE, m:m + de.RECORTE]))
                x = torch.from_numpy(np.stack(xs))[:, None].to(disp)
                p = (torch.sigmoid(modelo(x))[:, 0] >= 0.5).cpu().numpy()
                r = np.stack(rs)
                inter += (p & (r == 1)).sum()
                soma += (p & (r != IGNORAR)).sum() + (r == 1).sum()
    modelo.train()
    return 2 * inter / max(soma, 1)


def fase_segmentador(args, disp, saida, partes, nomes):
    arq = saida / "lesoes_recortes.pt"
    if arq.exists():
        log("Fase 4: segmentador já treinado")
        return
    val_nomes = set(nomes[:max(1, min(200, len(nomes) // 5))])
    tr_nomes = set(nomes) - val_nomes
    ds_tr = Recortes(args.pasta_recortes, partes, tr_nomes, args.its2 * args.lote2)
    ds_val = Recortes(args.pasta_recortes, partes, val_nomes, 1)
    log(f"Fase 4: recortes de treino {len(ds_tr.pos)} com lesão / {len(ds_tr.neg)} sem; "
        f"validação {len(ds_val.pos)} / {len(ds_val.neg)}")
    modelo = UNetResNet34(pre_treinada=not args.sem_pre_treino).to(disp)
    opt = torch.optim.AdamW(modelo.parameters(), lr=3e-4, weight_decay=1e-4)
    agenda = agenda_cos(opt, args.its2)
    escal = torch.amp.GradScaler(enabled=disp == "cuda")
    carga = DataLoader(ds_tr, batch_size=args.lote2, num_workers=args.workers, pin_memory=disp == "cuda")
    melhor, t, acum = -1.0, time.time(), 0.0
    modelo.train()
    for it, (x, y) in enumerate(carga, 1):
        x, y = x.to(disp), y.to(disp)
        with torch.autocast(device_type=disp, dtype=torch.float16, enabled=disp == "cuda"):
            logit = modelo(x)
        l = perda(logit.float(), y, args.perda)
        opt.zero_grad(set_to_none=True); escal.scale(l).backward(); escal.step(opt); escal.update(); agenda.step()
        acum += l.item()
        if it % 50 == 0:
            log(f"  [4] it {it}/{args.its2} perda {acum / 50:.3f} ({(time.time() - t) / 50:.2f}s/it)")
            t, acum = time.time(), 0.0
        if it % args.validar_cada == 0 or it == args.its2:
            d = avaliar_segmentador(modelo, ds_val, disp)
            if d > melhor:
                melhor = d
                torch.save({"modelo": modelo.state_dict(),
                            "meta": dict(iteracao=it, dice_validacao=float(d), perda=args.perda,
                                         recorte=de.RECORTE, largura_recorte=de.LARGURA_RECORTE)}, arq)
            log(f"  == [4] validação it {it}: Dice {d:.3f} (melhor {melhor:.3f})")


def main():
    a = argparse.ArgumentParser()
    a.add_argument("--saida", required=True)
    a.add_argument("--pasta-recortes", default=str(c.HANOI / "recortes_apices"))
    a.add_argument("--its1", type=int, default=3000)
    a.add_argument("--its2", type=int, default=6000)
    a.add_argument("--lote1", type=int, default=8)
    a.add_argument("--lote2", type=int, default=32)
    a.add_argument("--validar-cada", type=int, default=500)
    a.add_argument("--perda", choices=("dice", "tversky"), default="tversky")
    a.add_argument("--workers", type=int, default=min(4, os.cpu_count() or 2))
    a.add_argument("--limite-dentex", type=int, default=0)
    a.add_argument("--limite-hanoi", type=int, default=0)
    a.add_argument("--sem-pre-treino", action="store_true")
    args = a.parse_args()
    disp = "cuda" if torch.cuda.is_available() else "cpu"
    saida = Path(args.saida)
    saida.mkdir(parents=True, exist_ok=True)
    log(f"Dispositivo: {disp} | CPUs: {os.cpu_count()} | workers: {args.workers}")

    fase_localizador(args, disp, saida)
    nomes = c.nomes_hanoi()
    np.random.default_rng(1).shuffle(nomes)
    if args.limite_hanoi:
        nomes = nomes[:args.limite_hanoi]
    apices_hanoi = fase_apices_hanoi(args, disp, saida, nomes)
    partes = fase_recortes(args, saida, apices_hanoi, nomes)
    fase_segmentador(args, disp, saida, partes, nomes)
    log(f"Fim. Modelos em {saida}: localizador_apices.pt e lesoes_recortes.pt")


if __name__ == "__main__":
    main()
