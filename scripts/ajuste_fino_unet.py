"""Ajuste fino da U-Net (treinada com Hanói) nas 20 panorâmicas marcadas, na CPU.

As máscaras de Hanói são elipses; aqui a rede aprende os contornos reais do
nosso aparelho. Para caber na CPU e não decorar 20 imagens, o codificador fica
congelado (só o decodificador é ajustado) e a BatchNorm não atualiza estatísticas.
A borda de cada marcação (±4 px) é ignorada na perda, porque o traço à mão não é exato.

Validação cruzada de 5 partes (as mesmas do treinar.py): cada paciente é previsto
por um modelo ajustado sem ele. Depois ajusta com as 20 e salva o modelo final.

Se a U-Net base foi treinada noutra largura (v2: 2000 px), o ajuste e a previsão
são feitos nessa largura (com a mesma perda do treino) e o mapa de probabilidade
volta à escala de trabalho (1400 px) para a comparação usar as mesmas máscaras.

Uso: python ajuste_fino_unet.py [--base Modelos/unet_lesoes.pt] [--chave UNET_FT]
                                [--iteracoes 150] [--lote 2] [--lr 3e-4]
Saída: Resultados/validacao/probs_<CHAVE>.npz, Modelos/<base>_ft.pt/.json, Resultados/comparacao.md
"""
import argparse
import json
import time
from pathlib import Path

import cv2
import numpy as np
import torch

import comparar
import comum as c
import deteccao as dt
import unet
from treinar import N_PARTES
from treino_unet import IGNORAR, perda

RECORTE = (384, 576)
BORDA = 4  # px ignorados de cada lado do contorno marcado
P_LESAO = 0.7  # fração dos recortes centrados numa lesão


def rotulo(m, borda_px=BORDA):
    k = np.ones((2 * borda_px + 1,) * 2, np.uint8)
    mu = m.astype(np.uint8)
    borda = (cv2.dilate(mu, k) > 0) & ~(cv2.erode(mu, k) > 0)
    rot = np.zeros(m.shape, np.uint8)
    rot[borda] = IGNORAR
    rot[m & ~borda] = 1
    return rot


def recorte(img, rot, rng):
    ch, cw = RECORTE
    h, w = img.shape
    ys, xs = np.nonzero(rot == 1)
    if len(ys) and rng.random() < P_LESAO:
        k = rng.integers(len(ys))
        y0 = int(np.clip(ys[k] - rng.integers(ch // 4, 3 * ch // 4), 0, h - ch))
        x0 = int(np.clip(xs[k] - rng.integers(cw // 4, 3 * cw // 4), 0, w - cw))
    else:
        y0, x0 = rng.integers(0, h - ch + 1), rng.integers(0, w - cw + 1)
    x = img[y0:y0 + ch, x0:x0 + cw].astype(np.float32) / 255.0
    y = rot[y0:y0 + ch, x0:x0 + cw]
    if rng.random() < 0.5:
        x, y = x[:, ::-1], y[:, ::-1]
    x = np.clip(x ** rng.uniform(0.8, 1.25) * rng.uniform(0.9, 1.1) + rng.uniform(-0.05, 0.05), 0, 1)
    return x.copy(), y.copy()


def ajustar(imgs, rots, args, semente):
    modelo, meta = unet.carregar(args.base)
    for parte in (modelo.stem, modelo.c1, modelo.c2, modelo.c3, modelo.c4):
        for p in parte.parameters():
            p.requires_grad = False
    modelo.train()
    for mod in modelo.modules():
        if isinstance(mod, torch.nn.BatchNorm2d):
            mod.eval()
    opt = torch.optim.AdamW([p for p in modelo.parameters() if p.requires_grad], lr=args.lr, weight_decay=1e-4)
    rng = np.random.default_rng(semente)
    t, acum = time.time(), 0.0
    for it in range(1, args.iteracoes + 1):
        lote = [recorte(imgs[j], rots[j], rng) for j in rng.integers(len(imgs), size=args.lote)]
        x = torch.from_numpy(np.stack([b[0] for b in lote]))[:, None]
        y = torch.from_numpy(np.stack([b[1] for b in lote])).long()
        l = perda(modelo(x), y, meta.get("perda", "dice"))
        opt.zero_grad(set_to_none=True)
        l.backward()
        opt.step()
        acum += l.item()
        if it % 25 == 0:
            print(f"    it {it}/{args.iteracoes} perda {acum / 25:.3f} ({(time.time() - t) / it:.1f}s/it)", flush=True)
            acum = 0.0
    return modelo.eval(), meta


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iteracoes", type=int, default=150)
    ap.add_argument("--lote", type=int, default=2)
    ap.add_argument("--lr", type=float, default=3e-4)
    ap.add_argument("--base", default=str(c.MODELOS / "unet_lesoes.pt"), help="U-Net treinada com Hanói")
    ap.add_argument("--chave", default="UNET_FT", help="nome da variante em comparar.VARIANTES")
    args = ap.parse_args()
    torch.manual_seed(0)
    base = Path(args.base)
    saida = base.with_name(base.stem + "_ft.pt")
    largura = unet.carregar(base)[1].get("largura", c.LARGURA)

    nossas = c.nossas_imagens()
    if largura == c.LARGURA:
        imgs = [img for _, img, _ in nossas]
        mascaras = [m for _, _, m in nossas]
    else:
        imgs = [c.normalizar(c.ler_cinza(c.NOSSAS / "imagens" / f"{nm}.png"), largura)[0] for nm, _, _ in nossas]
        mascaras = [c.redimensionar_mascara(c.ler_cinza(c.NOSSAS / "mascaras" / f"{nm}.png"), img.shape)
                    for (nm, _, _), img in zip(nossas, imgs)]
    rots = [rotulo(m, round(BORDA * largura / c.LARGURA)) for m in mascaras]
    partes = np.arange(len(nossas)) % N_PARTES
    np.random.default_rng(2).shuffle(partes)

    def prever(modelo, i):
        """Probabilidade na escala de trabalho (1400 px), a mesma das máscaras da comparação."""
        p = unet.prever(modelo, imgs[i])
        forma = nossas[i][1].shape
        return p if p.shape == forma else cv2.resize(p, (forma[1], forma[0]), interpolation=cv2.INTER_AREA)

    # cada parte é salva ao terminar, para uma interrupção não custar as anteriores
    sufixo = "" if args.chave == "UNET_FT" else f"_{args.chave}"
    pasta_partes = c.RESULTADOS / "validacao" / f"ajuste_fino_partes_it{args.iteracoes}{sufixo}"
    pasta_partes.mkdir(parents=True, exist_ok=True)
    probs = [None] * len(nossas)
    for p in range(N_PARTES):
        arq = pasta_partes / f"parte_{p}.npz"
        teste = np.flatnonzero(partes == p)
        if arq.exists():
            salvo = np.load(arq)
            for i in teste:
                probs[i] = salvo[nossas[i][0]].astype(np.float32)
            print(f"Parte {p + 1}/{N_PARTES}: já feita, carregada de {arq.name}", flush=True)
            continue
        tr = [i for i in range(len(nossas)) if partes[i] != p]
        print(f"Parte {p + 1}/{N_PARTES}: ajustando com {len(tr)} imagens", flush=True)
        modelo, _ = ajustar([imgs[i] for i in tr], [rots[i] for i in tr], args, semente=p)
        for i in teste:
            probs[i] = prever(modelo, i)
        np.savez_compressed(arq, **{nossas[i][0]: probs[i].astype(np.float16) for i in teste})
    np.savez_compressed(c.RESULTADOS / "validacao" / f"probs_{args.chave}.npz",
                        **{nm: pr.astype(np.float16) for (nm, _, _), pr in zip(nossas, probs)})

    curva = dt.froc([dt.detectar(pr, dt.RAIO_REGIAO_UNET) for pr in probs], [m for _, _, m in nossas])
    melhor, pontos, texto = dt.resumo_froc(curva)
    print(f"U-Net ajustada (validação cruzada): {texto}", flush=True)

    print("Modelo final: ajustando com as 20 imagens", flush=True)
    modelo, meta = ajustar(imgs, rots, args, semente=99)
    meta = dict(meta, ajuste_fino=dict(vars(args), n_imagens=len(imgs), borda=BORDA, recorte=RECORTE))
    torch.save({"modelo": modelo.state_dict(), "meta": meta}, saida)
    json.dump(dict(melhor=melhor, sensibilidade_por_fp={str(f): v for f, v in pontos.items()}, curva=curva,
                   meta=meta), open(saida.with_suffix(".json"), "w", encoding="utf-8"), indent=2, ensure_ascii=False)
    print(f"Salvo {saida}", flush=True)
    comparar.main()


if __name__ == "__main__":
    main()
