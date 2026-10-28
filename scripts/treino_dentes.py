"""Treina no Colab (GPU) o classificador por dente (dentes.py).

Reaproveita o localizador de ápices e os ápices de Hanói da rodada das duas etapas (--duas-etapas).
Fases, cada uma pulada se o resultado já estiver em --saida (permite retomar):
  1. apices_dentex_doenca.json   ápices detectados nas 705 panorâmicas do DENTEX com diagnósticos
  2. recortes/                   recortes em volta dos ápices, com rótulo (lesão / sem lesão)
  3. classificador_dentes.pt     ResNet34 que classifica cada recorte

Rótulos:
  Hanói: caixa de lesão a até R_POS px do ápice = positivo; a mais de R_NEG px de todas = negativo.
  DENTEX: ápices do dente diagnosticado com lesão periapical (polígono) = positivo; os demais
          ápices da imagem, longe deles, = negativo (dentes sem esse diagnóstico).

Uso: python treino_dentes.py --saida PASTA --duas-etapas PASTA_DUAS_ETAPAS [--its 5000]
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
import dentes as dn
from unet import UNetResNet34

TOTAL = dn.RECORTE + 2 * dn.MARGEM
N_NEG_HANOI = 6     # negativos guardados por imagem de Hanói
N_NEG_DENTEX = 12   # negativos guardados por imagem do DENTEX (outro aparelho: mais valiosos)
PEQUENA = 35        # px (escala de trabalho): lesão com diâmetro equivalente menor que isso é "pequena"
DENTEX_DOENCA = c.RAIZ / "Externos" / "DENTEX"
JSON_DOENCA = DENTEX_DOENCA / "train_quadrant_enumeration_disease.json"
PASTA_DOENCA = DENTEX_DOENCA / "doenca"
if not PASTA_DOENCA.exists():  # no computador local só há as 116 panorâmicas com lesão periapical
    PASTA_DOENCA = DENTEX_DOENCA / "imagens"
LESAO_PERIAPICAL = 2  # category_id_3 no DENTEX


def log(msg):
    print(msg, flush=True)


def lesoes_dentex():
    """{arquivo: (largura, [(x, y) dos ápices dos dentes com lesão periapical], todos os ápices anotados)}."""
    d = json.load(open(JSON_DOENCA, encoding="utf-8"))
    imgs = {i["id"]: i for i in d["images"]}
    saida = {i["file_name"]: (i["width"], []) for i in d["images"]}
    for a in d["annotations"]:
        if a["category_id_3"] != LESAO_PERIAPICAL:
            continue
        pol = np.array(a["segmentation"][0], float).reshape(-1, 2)
        pts = ap.apices_do_dente(pol, int(a["category_id_1"]) + 1, int(a["category_id_2"]) + 1)
        saida[imgs[a["image_id"]]["file_name"]][1].extend(pts)
    return {k: v for k, v in saida.items() if (PASTA_DOENCA / k).exists()}


# ------------------------------------------------------------------ fase 1
def fase_apices_dentex(args, disp, saida):
    arq = saida / "apices_dentex_doenca.json"
    if arq.exists():
        log("Fase 1: ápices do DENTEX já detectados")
        return json.load(open(arq))
    ck = torch.load(Path(args.duas_etapas) / "localizador_apices.pt", map_location=disp, weights_only=False)
    loc = UNetResNet34().to(disp)
    loc.load_state_dict(ck["modelo"]); loc.eval()
    import duas_etapas as de
    res, t = {}, time.time()
    nomes = sorted(lesoes_dentex())[:args.limite or None]
    for k, nm in enumerate(nomes, 1):
        img, _ = c.normalizar(c.ler_cinza(PASTA_DOENCA / nm))
        res[nm] = de.detectar_apices(loc, img).round(1).tolist()
        if k % 100 == 0:
            log(f"  [1] {k}/{len(nomes)} ({(time.time() - t) / k:.2f}s/imagem)")
    json.dump(res, open(arq, "w"))
    log(f"Fase 1: {sum(len(v) for v in res.values())} ápices em {len(res)} panorâmicas do DENTEX")
    return res


# ------------------------------------------------------------------ fase 2
def recortes_hanoi(nome, pts, semente):
    orig = c.ler_cinza(c.HANOI / "imagens" / "Original JPG Images" / f"{nome}.jpg")
    img_t, esc = c.normalizar(orig)
    img_r, _ = c.normalizar(orig, dn.LARGURA_RECORTE)
    caixas = [(x0 * esc, y0 * esc, x1 * esc, y1 * esc) for x0, y0, x1, y1 in c.caixas_hanoi(nome)]
    rot = dn.rotular_apices(pts, caixas)
    diam = []
    for x, y in pts:  # diâmetro equivalente da caixa mais próxima (para amostrar lesões pequenas)
        if caixas:
            b = min(caixas, key=lambda b: (x - (b[0] + b[2]) / 2) ** 2 + (y - (b[1] + b[3]) / 2) ** 2)
            diam.append(2 * math.sqrt((b[2] - b[0]) * (b[3] - b[1]) * math.pi / 4 / math.pi))
        else:
            diam.append(0)
    rng = np.random.default_rng(semente)
    pos = [i for i in range(len(pts)) if rot[i] == 1]
    neg = [i for i in rng.permutation(len(pts)) if rot[i] == 0][:N_NEG_HANOI]
    sel = pos + neg
    if not sel:
        return None
    rec = dn.recortes_apices(img_r, [pts[i] for i in sel], img_t.shape[0], TOTAL)
    return np.stack(rec), np.array([1] * len(pos) + [0] * len(neg), np.uint8), np.array([diam[i] for i in sel], np.float32)


def recortes_dentex(nome, pts, largura, apices_lesao, semente):
    orig = c.ler_cinza(PASTA_DOENCA / nome)
    img_t, esc = c.normalizar(orig)
    img_r, _ = c.normalizar(orig, dn.LARGURA_RECORTE)
    caixas = [(x * esc - 20, y * esc - 20, x * esc + 20, y * esc + 20) for x, y in apices_lesao]
    rot = dn.rotular_apices(pts, caixas)
    rng = np.random.default_rng(semente)
    pos = [i for i in range(len(pts)) if rot[i] == 1]
    neg = [i for i in rng.permutation(len(pts)) if rot[i] == 0][:N_NEG_DENTEX]
    sel = pos + neg
    if not sel:
        return None
    rec = dn.recortes_apices(img_r, [pts[i] for i in sel], img_t.shape[0], TOTAL)
    return np.stack(rec), np.array([1] * len(pos) + [0] * len(neg), np.uint8), np.zeros(len(sel), np.float32)


def fase_recortes(args, apices_hanoi, apices_dentex):
    pasta = Path(args.pasta_recortes)
    pasta.mkdir(parents=True, exist_ok=True)
    pronto = pasta / "pronto.json"
    if pronto.exists():
        log("Fase 2: recortes já preparados")
        return json.load(open(pronto))
    les = lesoes_dentex()
    tarefas = [("hanoi", nm, k) for k, nm in enumerate(sorted(apices_hanoi))]
    tarefas += [("dentex", nm, k) for k, nm in enumerate(sorted(apices_dentex))]
    t, partes, bloco = time.time(), [], 300

    def um(fonte, nm, k):
        if fonte == "hanoi":
            return recortes_hanoi(nm, apices_hanoi[nm], k)
        return recortes_dentex(nm, apices_dentex[nm], *les[nm], k)

    for b in range(0, len(tarefas), bloco):
        lote = tarefas[b:b + bloco]
        res = Parallel(n_jobs=max(1, args.workers))(delayed(um)(*tf) for tf in lote)
        res = [(tf, r) for tf, r in zip(lote, res) if r is not None]
        if res:
            p = f"{b:05d}"
            np.save(pasta / f"img_{p}.npy", np.concatenate([r[0] for _, r in res]))
            np.save(pasta / f"rot_{p}.npy", np.concatenate([r[1] for _, r in res]))
            np.save(pasta / f"diam_{p}.npy", np.concatenate([r[2] for _, r in res]))
            partes.append(dict(parte=p, fonte=[tf[0] for tf, r in res for _ in range(len(r[1]))],
                               nomes=[tf[1] for tf, r in res for _ in range(len(r[1]))]))
        log(f"  [2] {min(b + bloco, len(tarefas))}/{len(tarefas)} imagens ({time.time() - t:.0f}s)")
    json.dump(partes, open(pronto, "w"))
    return partes


# ------------------------------------------------------------------ fase 3
class Recortes(Dataset):
    """Sorteio balanceado: metade com lesão; entre elas, DENTEX e lesões pequenas de Hanói reforçadas."""

    GRUPOS = (("pos_dentex", .25), ("pos_pequena", .35), ("pos_hanoi", .40), ("neg_dentex", .3), ("neg_hanoi", .7))

    def __init__(self, pasta, partes, nomes_ok, n, semente=0):
        self.img, self.idx = [], {g: [] for g, _ in self.GRUPOS}
        self.todos = []
        for k, p in enumerate(partes):
            self.img.append(np.load(Path(pasta) / f"img_{p['parte']}.npy", mmap_mode="r"))
            rot = np.load(Path(pasta) / f"rot_{p['parte']}.npy")
            diam = np.load(Path(pasta) / f"diam_{p['parte']}.npy")
            for j, (fonte, nm) in enumerate(zip(p["fonte"], p["nomes"])):
                if nm not in nomes_ok:
                    continue
                if rot[j]:
                    g = "pos_dentex" if fonte == "dentex" else ("pos_pequena" if diam[j] < PEQUENA else "pos_hanoi")
                else:
                    g = "neg_dentex" if fonte == "dentex" else "neg_hanoi"
                self.idx[g].append((k, j))
                self.todos.append((k, j, int(rot[j]), g))
        self.n, self.semente = n, semente

    def resumo(self):
        return ", ".join(f"{g} {len(v)}" for g, v in self.idx.items())

    def __len__(self):
        return self.n

    def __getitem__(self, i):
        rng = np.random.default_rng(self.semente * 1_000_003 + i)
        positivo = rng.random() < .5
        grupos = [(g, w) for g, w in self.GRUPOS if g.startswith("pos" if positivo else "neg") and self.idx[g]]
        pesos = np.array([w for _, w in grupos]); pesos /= pesos.sum()
        g = grupos[rng.choice(len(grupos), p=pesos)][0]
        k, j = self.idx[g][rng.integers(len(self.idx[g]))]
        dy, dx = rng.integers(0, 2 * dn.MARGEM + 1, size=2)
        a = self.img[k][j, dy:dy + dn.RECORTE, dx:dx + dn.RECORTE].astype(np.float32) / 255
        if rng.random() < .5:
            a = a[:, ::-1]
        a = np.clip(a ** rng.uniform(0.7, 1.4) * rng.uniform(0.85, 1.15) + rng.uniform(-0.08, 0.08), 0, 1)
        return torch.from_numpy(a.copy())[None], torch.tensor(float(g.startswith("pos")))


def auc(y, p):
    y, p = np.asarray(y), np.asarray(p)
    if y.min() == y.max():
        return float("nan")
    ordem = np.argsort(p)
    r = np.empty(len(p)); r[ordem] = np.arange(1, len(p) + 1)
    n1 = y.sum(); n0 = len(y) - n1
    return float((r[y == 1].sum() - n1 * (n1 + 1) / 2) / (n1 * n0))


def avaliar(modelo, ds, disp, lote=96):
    modelo.eval()
    m = dn.MARGEM
    ps = []
    with torch.no_grad():
        for b in range(0, len(ds.todos), lote):
            x = np.stack([ds.img[k][j, m:m + dn.RECORTE, m:m + dn.RECORTE] for k, j, _, _ in ds.todos[b:b + lote]])
            x = torch.from_numpy(x.astype(np.float32) / 255)[:, None].to(disp)
            with torch.autocast(device_type=disp, dtype=torch.float16, enabled=disp == "cuda"):
                ps.append(torch.sigmoid(modelo(x)).float().cpu().numpy())
    modelo.train()
    p = np.concatenate(ps)
    y = np.array([t[2] for t in ds.todos]); g = np.array([t[3] for t in ds.todos])
    tudo = auc(y, p)
    dentex = auc(y[np.char.endswith(g, "dentex")], p[np.char.endswith(g, "dentex")])
    sel = (g == "pos_pequena") | (g == "neg_hanoi")
    pequenas = auc(y[sel], p[sel])
    return tudo, dentex, pequenas


def fase_classificador(args, disp, saida, partes):
    arq = saida / "classificador_dentes.pt"
    if arq.exists():
        log("Fase 3: classificador já treinado")
        return
    nomes = sorted({nm for p in partes for nm in p["nomes"]})
    rng = np.random.default_rng(3)
    hanoi = [n for n in nomes if not n.endswith(".png")]
    dentex = [n for n in nomes if n.endswith(".png")]
    val = set(rng.permutation(hanoi)[:min(200, len(hanoi) // 5)]) | set(rng.permutation(dentex)[:min(60, len(dentex) // 5)])
    ds_tr = Recortes(args.pasta_recortes, partes, set(nomes) - val, args.its * args.lote)
    ds_val = Recortes(args.pasta_recortes, partes, val, 1)
    log(f"Fase 3: treino [{ds_tr.resumo()}]")
    log(f"        validação [{ds_val.resumo()}]")
    modelo = dn.ClassificadorDente(pre_treinada=not args.sem_pre_treino).to(disp)
    opt = torch.optim.AdamW(modelo.parameters(), lr=3e-4, weight_decay=1e-4)
    agenda = torch.optim.lr_scheduler.LambdaLR(
        opt, lambda it: min(1, (it + 1) / 200) * 0.5 * (1 + math.cos(math.pi * min(it, args.its) / args.its)))
    escal = torch.amp.GradScaler(enabled=disp == "cuda")
    carga = DataLoader(ds_tr, batch_size=args.lote, num_workers=args.workers, pin_memory=disp == "cuda")
    melhor, t, acum = -1.0, time.time(), 0.0
    modelo.train()
    for it, (x, y) in enumerate(carga, 1):
        x, y = x.to(disp), y.to(disp)
        with torch.autocast(device_type=disp, dtype=torch.float16, enabled=disp == "cuda"):
            logit = modelo(x)
        l = F.binary_cross_entropy_with_logits(logit.float(), y)
        opt.zero_grad(set_to_none=True); escal.scale(l).backward(); escal.step(opt); escal.update(); agenda.step()
        acum += l.item()
        if it % 50 == 0:
            log(f"  [3] it {it}/{args.its} perda {acum / 50:.4f} ({(time.time() - t) / 50:.2f}s/it)")
            t, acum = time.time(), 0.0
        if it % args.validar_cada == 0 or it == args.its:
            a, a_d, a_p = avaliar(modelo, ds_val, disp)
            nota = (a + a_d + a_p) / 3
            if nota > melhor:
                melhor = nota
                torch.save({"modelo": modelo.state_dict(),
                            "meta": dict(iteracao=it, auc=a, auc_dentex=a_d, auc_pequenas=a_p,
                                         recorte=dn.RECORTE, largura_recorte=dn.LARGURA_RECORTE)}, arq)
            log(f"  == [3] validação it {it}: AUC {a:.3f} | DENTEX {a_d:.3f} | lesões pequenas {a_p:.3f} (melhor média {melhor:.3f})")


def main():
    a = argparse.ArgumentParser()
    a.add_argument("--saida", required=True)
    a.add_argument("--duas-etapas", required=True, help="pasta com localizador_apices.pt e apices_hanoi.json")
    a.add_argument("--pasta-recortes", default="/content/recortes_dentes")
    a.add_argument("--its", type=int, default=5000)
    a.add_argument("--lote", type=int, default=48)
    a.add_argument("--validar-cada", type=int, default=500)
    a.add_argument("--workers", type=int, default=min(4, os.cpu_count() or 2))
    a.add_argument("--limite", type=int, default=0, help="só para testes: n imagens de cada conjunto")
    a.add_argument("--sem-pre-treino", action="store_true")
    args = a.parse_args()
    disp = "cuda" if torch.cuda.is_available() else "cpu"
    saida = Path(args.saida)
    saida.mkdir(parents=True, exist_ok=True)
    log(f"Dispositivo: {disp} | CPUs: {os.cpu_count()} | workers: {args.workers}")
    apices_hanoi = json.load(open(Path(args.duas_etapas) / "apices_hanoi.json"))
    disponiveis = set(c.nomes_hanoi())
    apices_hanoi = {k: v for k, v in apices_hanoi.items() if k in disponiveis}
    apices_dentex = fase_apices_dentex(args, disp, saida)
    if args.limite:
        apices_hanoi = dict(sorted(apices_hanoi.items())[:args.limite])
        apices_dentex = dict(sorted(apices_dentex.items())[:args.limite])
    partes = fase_recortes(args, apices_hanoi, apices_dentex)
    fase_classificador(args, disp, saida, partes)
    log(f"Fim. Modelo em {saida / 'classificador_dentes.pt'}")


if __name__ == "__main__":
    main()
