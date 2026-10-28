"""Treina a U-Net com as panorâmicas de Hanói (feito para o Google Colab com GPU).

Usa o mesmo pré-processamento do Random Forest (comum.normalizar) e as mesmas
máscaras: elipse dentro de cada caixa = lesão, anel em volta = ignorado na perda.
Salva checkpoints em --saida a cada validação; com --continuar retoma de lá.

Versão 2 (lesões pequenas): --largura 2000 treina em resolução maior e --perda tversky
penaliza mais deixar lesão passar do que marcar a mais.

Uso: python treino_unet.py --saida PASTA [--iteracoes 6000] [--lote 8] [--largura 1400]
                           [--perda dice|tversky] [--continuar]
"""
import argparse
import math
import time
from pathlib import Path

import cv2
import numpy as np
import torch
import torch.nn.functional as F
from joblib import Parallel, delayed
from torch.utils.data import DataLoader, Dataset

import comum as c
from unet import UNetResNet34, prever

RECORTE = (512, 768)  # altura, largura dos recortes de treino
IGNORAR = 2


def preparar_um(nome, pasta, largura):
    destino = pasta / f"{nome}_img.png"
    if destino.exists():
        return
    img, m, inc = c.imagem_hanoi(nome, largura)
    rot = np.zeros(img.shape, np.uint8)
    rot[inc] = IGNORAR
    rot[m] = 1
    cv2.imwrite(str(pasta / f"{nome}_rot.png"), rot)
    cv2.imwrite(str(destino), img)


def preparar(nomes, pasta, workers, largura):
    pasta.mkdir(parents=True, exist_ok=True)
    t = time.time()
    Parallel(n_jobs=workers)(delayed(preparar_um)(nm, pasta, largura) for nm in nomes)
    print(f"Pré-processamento: {len(nomes)} imagens em {time.time() - t:.0f}s", flush=True)


class Recortes(Dataset):
    """Recortes aleatórios; metade centrada numa lesão, para ela não ser rara demais."""

    def __init__(self, nomes, pasta, n_amostras, semente=0):
        self.nomes, self.pasta, self.n = nomes, pasta, n_amostras
        self.semente = semente

    def __len__(self):
        return self.n

    def __getitem__(self, i):
        rng = np.random.default_rng(self.semente * 1_000_003 + i)
        nm = self.nomes[rng.integers(len(self.nomes))]
        img = cv2.imread(str(self.pasta / f"{nm}_img.png"), cv2.IMREAD_GRAYSCALE)
        rot = cv2.imread(str(self.pasta / f"{nm}_rot.png"), cv2.IMREAD_GRAYSCALE)
        ch, cw = RECORTE
        if img.shape[0] < ch or img.shape[1] < cw:
            py, px = max(0, ch - img.shape[0]), max(0, cw - img.shape[1])
            img = np.pad(img, ((0, py), (0, px)), mode="reflect")
            rot = np.pad(rot, ((0, py), (0, px)), constant_values=IGNORAR)
        h, w = img.shape
        ys, xs = np.nonzero(rot == 1)
        if len(ys) and rng.random() < 0.5:
            k = rng.integers(len(ys))
            y0 = np.clip(ys[k] - rng.integers(ch // 4, 3 * ch // 4), 0, h - ch)
            x0 = np.clip(xs[k] - rng.integers(cw // 4, 3 * cw // 4), 0, w - cw)
        else:
            y0, x0 = rng.integers(0, h - ch + 1), rng.integers(0, w - cw + 1)
        img = img[y0:y0 + ch, x0:x0 + cw].astype(np.float32) / 255.0
        rot = rot[y0:y0 + ch, x0:x0 + cw]
        if rng.random() < 0.5:
            img, rot = img[:, ::-1], rot[:, ::-1]
        img = np.clip(img ** rng.uniform(0.7, 1.4) * rng.uniform(0.85, 1.15) + rng.uniform(-0.08, 0.08), 0, 1)
        return torch.from_numpy(img.copy())[None], torch.from_numpy(rot.copy()).long()


TVERSKY_FP, TVERSKY_FN = 0.3, 0.7  # peso de marcar a mais × peso de deixar lesão passar


def perda(logits, rot, tipo="dice"):
    """BCE + Dice (ou Tversky), só nos pixels que não estão na zona ignorada."""
    logits = logits[:, 0]
    valido = (rot != IGNORAR).float()
    alvo = (rot == 1).float()
    bce = (F.binary_cross_entropy_with_logits(logits, alvo, reduction="none") * valido).sum() / valido.sum()
    p = torch.sigmoid(logits) * valido
    vp = (p * alvo).sum()
    if tipo == "tversky":
        fp, fn = (p * (1 - alvo)).sum(), ((1 - p) * alvo).sum()
        return bce + 1 - (vp + 1) / (vp + TVERSKY_FP * fp + TVERSKY_FN * fn + 1)
    dice = 1 - (2 * vp + 1) / (p.sum() + alvo.sum() + 1)
    return bce + dice


def validar(modelo, nomes, pasta):
    modelo.eval()
    inter = soma = 0
    for nm in nomes:
        img = cv2.imread(str(pasta / f"{nm}_img.png"), cv2.IMREAD_GRAYSCALE)
        rot = cv2.imread(str(pasta / f"{nm}_rot.png"), cv2.IMREAD_GRAYSCALE)
        pred = prever(modelo, img, espelhar=False) >= 0.5
        valido = rot != IGNORAR
        inter += (pred & (rot == 1)).sum()
        soma += (pred & valido).sum() + (rot == 1).sum()
    modelo.train()
    return 2 * inter / max(soma, 1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--saida", required=True)
    ap.add_argument("--iteracoes", type=int, default=6000)
    ap.add_argument("--lote", type=int, default=8)
    ap.add_argument("--validar-cada", type=int, default=500)
    ap.add_argument("--n-val", type=int, default=200)
    ap.add_argument("--limite-imagens", type=int, default=0, help="só para testes rápidos")
    ap.add_argument("--workers", type=int, default=2)
    ap.add_argument("--continuar", action="store_true")
    ap.add_argument("--largura", type=int, default=c.LARGURA, help="largura (px) em que as panorâmicas são treinadas")
    ap.add_argument("--perda", choices=("dice", "tversky"), default="dice")
    ap.add_argument("--sem-pre-treino", action="store_true", help="não baixa pesos ImageNet (testes)")
    args = ap.parse_args()

    disp = "cuda" if torch.cuda.is_available() else "cpu"
    saida = Path(args.saida)
    saida.mkdir(parents=True, exist_ok=True)
    nomes = c.nomes_hanoi()
    np.random.default_rng(0).shuffle(nomes)
    if args.limite_imagens:
        nomes = nomes[:args.limite_imagens]
    n_val = min(args.n_val, len(nomes) // 5)
    val, treino = nomes[:n_val], nomes[n_val:]
    pasta = c.HANOI / f"preparadas_{args.largura}"
    preparar(nomes, pasta, max(args.workers, 1), args.largura)
    print(f"Treino: {len(treino)} imagens | validação: {len(val)} | dispositivo: {disp}", flush=True)

    modelo = UNetResNet34(pre_treinada=not args.sem_pre_treino).to(disp)
    opt = torch.optim.AdamW(modelo.parameters(), lr=3e-4, weight_decay=1e-4)
    agenda = torch.optim.lr_scheduler.LambdaLR(
        opt, lambda it: min(1, (it + 1) / 200) * 0.5 * (1 + math.cos(math.pi * it / args.iteracoes)))
    escalador = torch.amp.GradScaler(enabled=disp == "cuda")
    inicio, melhor = 0, -1.0
    ck_path = saida / "checkpoint.pt"
    if args.continuar and ck_path.exists():
        ck = torch.load(ck_path, map_location=disp, weights_only=False)
        modelo.load_state_dict(ck["modelo"]); opt.load_state_dict(ck["opt"]); agenda.load_state_dict(ck["agenda"])
        inicio, melhor = ck["iteracao"], ck["melhor"]
        print(f"Retomando da iteração {inicio} (melhor Dice de validação {melhor:.3f})", flush=True)

    dados = DataLoader(Recortes(treino, pasta, (args.iteracoes - inicio) * args.lote, semente=inicio),
                       batch_size=args.lote, num_workers=args.workers, pin_memory=disp == "cuda")
    modelo.train()
    t, acum = time.time(), 0.0
    for it, (x, y) in enumerate(dados, start=inicio + 1):
        x, y = x.to(disp, non_blocking=True), y.to(disp, non_blocking=True)
        with torch.autocast(device_type=disp, dtype=torch.float16, enabled=disp == "cuda"):
            logits = modelo(x)
        l = perda(logits.float(), y, args.perda)
        opt.zero_grad(set_to_none=True)
        escalador.scale(l).backward()
        escalador.step(opt)
        escalador.update()
        agenda.step()
        acum += l.item()
        if it % 50 == 0:
            print(f"it {it}/{args.iteracoes} perda {acum / 50:.3f} ({(time.time() - t) / 50:.2f}s/it)", flush=True)
            t, acum = time.time(), 0.0
        if it % args.validar_cada == 0 or it == args.iteracoes:
            d = validar(modelo, val, pasta)
            meta = dict(iteracao=it, dice_validacao=float(d), versao_atributos=c.VERSAO_ATRIBUTOS,
                        largura=args.largura, perda=args.perda, n_treino=len(treino))
            if d > melhor:
                melhor = d
                torch.save({"modelo": modelo.state_dict(), "meta": meta}, saida / "unet_lesoes.pt")
            torch.save({"modelo": modelo.state_dict(), "opt": opt.state_dict(), "agenda": agenda.state_dict(),
                        "iteracao": it, "melhor": melhor}, ck_path)
            print(f"== validação it {it}: Dice {d:.3f} (melhor {melhor:.3f})", flush=True)
    print(f"Fim. Melhor modelo em {saida / 'unet_lesoes.pt'}")


if __name__ == "__main__":
    main()
