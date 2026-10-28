"""Treina o Random Forest por pixel e mede a detecção de lesões nas 20 panorâmicas marcadas.

Passos:
  1. B1: RF treinado com pixels sorteados de N imagens de Hanói.
  2. Mineração: B1 roda em outras imagens de Hanói; os pixels de fundo onde ele
     se engana (probabilidade alta) entram no treino como negativos difíceis.
  3. B: RF retreinado com Hanói + negativos difíceis.
  4. C: como B, mais as nossas imagens (validação cruzada de 5 partes; cada
     paciente fica fora do treino da parte em que é testado).
Avalia B1, B e C nas nossas 20 imagens (curva FROC), salva a melhor variante
treinada com todos os dados em Modelos/rf_lesoes.joblib e a configuração em .json.

Uso: python treinar.py [--n-hanoi 800] [--n-mineracao 300]
"""
import argparse
import json
import time

import joblib
import numpy as np
from joblib import Parallel, delayed
from sklearn.ensemble import RandomForestClassifier

import comum as c
import deteccao as dt

RF = dict(n_estimators=100, max_depth=20, min_samples_leaf=10, max_features="sqrt",
          max_samples=200_000, n_jobs=4)  # cada árvore vê 200 mil pixels, para o treino caber no tempo
N_PARTES = 5
PROB_DIFICIL = 0.3


def nomes_divididos(n1, n2):
    todos = c.nomes_hanoi()
    h1 = list(np.random.default_rng(0).choice(todos, n1, replace=False))
    resto = sorted(set(todos) - set(h1))
    h2 = list(np.random.default_rng(3).choice(resto, n2, replace=False))
    return h1, h2


def amostra_hanoi(nome, semente):
    img, m, inc = c.imagem_hanoi(nome)
    return c.amostrar(c.atributos(img), m, inc, 200, 1000, np.random.default_rng(semente))


def carregar_cache(caminho, chave):
    if caminho.exists():
        d = np.load(caminho)
        if "chave" in d and str(d["chave"]) == chave:
            return d["X"], d["y"]
    return None


def amostras_hanoi(nomes, cache):
    chave = f"v{c.VERSAO_ATRIBUTOS}-{len(nomes)}"
    r = carregar_cache(cache, chave)
    if r is not None:
        return r
    t = time.time()
    partes = Parallel(n_jobs=4)(delayed(amostra_hanoi)(nm, i) for i, nm in enumerate(nomes))
    X = np.concatenate([p[0] for p in partes]).astype(np.float32)
    y = np.concatenate([p[1] for p in partes])
    np.savez(cache, X=X, y=y, chave=chave)
    print(f"Hanói: {len(nomes)} imagens, {len(y)} pixels ({y.mean():.1%} lesão) em {time.time() - t:.0f}s", flush=True)
    return X, y


def minerar(modelo, nomes, cache):
    """Pixels de fundo onde o modelo erra, mais alguns de lesão e de fundo comum."""
    chave = f"v{c.VERSAO_ATRIBUTOS}-{len(nomes)}"
    r = carregar_cache(cache, chave)
    if r is not None:
        return r
    rng = np.random.default_rng(4)
    Xs, ys, t, n_dif = [], [], time.time(), 0
    for k, nm in enumerate(nomes, 1):
        img, m, inc = c.imagem_hanoi(nm)
        prob, F = c.prever_probabilidade(modelo, img, devolver_atributos=True)
        X, y = c.amostrar(F, m, inc, 200, 200, rng)
        dificeis = np.flatnonzero((prob >= PROB_DIFICIL) & ~m & ~inc)
        dificeis = rng.choice(dificeis, min(600, len(dificeis)), replace=False)
        n_dif += len(dificeis)
        Xs += [X, F.reshape(-1, F.shape[-1])[dificeis]]
        ys += [y, np.zeros(len(dificeis), np.uint8)]
        if k % 25 == 0:
            print(f"  mineração {k}/{len(nomes)} ({(time.time() - t) / k:.1f}s/imagem)", flush=True)
    X, y = np.concatenate(Xs).astype(np.float32), np.concatenate(ys)
    np.savez(cache, X=X, y=y, chave=chave)
    print(f"Mineração: {n_dif} negativos difíceis de {len(nomes)} imagens", flush=True)
    return X, y


def treinar(X, y):
    t = time.time()
    params = dict(RF, max_samples=min(RF["max_samples"], len(y)))
    m = RandomForestClassifier(random_state=0, **params).fit(X, y)
    print(f"    RF treinado com {len(y)} pixels em {time.time() - t:.0f}s", flush=True)
    return m


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n-hanoi", type=int, default=800)
    ap.add_argument("--n-mineracao", type=int, default=300)
    args = ap.parse_args()
    c.MODELOS.mkdir(exist_ok=True)
    val_dir = c.RESULTADOS / "validacao"
    val_dir.mkdir(parents=True, exist_ok=True)

    h1, h2 = nomes_divididos(args.n_hanoi, args.n_mineracao)
    Xh, yh = amostras_hanoi(h1, c.MODELOS / "amostras_hanoi.npz")
    print("B1: Hanói", flush=True)
    modelo_b1 = treinar(Xh, yh)
    Xm, ym = minerar(modelo_b1, h2, c.MODELOS / "amostras_mineracao.npz")
    Xh, yh = np.concatenate([Xh, Xm]), np.concatenate([yh, ym])
    print("B: Hanói + negativos difíceis", flush=True)
    modelo_b = treinar(Xh, yh)

    nossas = c.nossas_imagens()
    rng = np.random.default_rng(1)
    am_nossas = [c.amostrar(c.atributos(img), m, np.zeros_like(m), 2000, 8000, rng) for _, img, m in nossas]
    partes = np.arange(len(nossas)) % N_PARTES
    np.random.default_rng(2).shuffle(partes)

    probs = {k: [None] * len(nossas) for k in ("B1", "B", "C")}
    for p in range(N_PARTES):
        print(f"Parte {p + 1}/{N_PARTES}", flush=True)
        tr = [i for i in range(len(nossas)) if partes[i] != p]
        modelo_c = treinar(np.concatenate([Xh] + [am_nossas[i][0] for i in tr]),
                           np.concatenate([yh] + [am_nossas[i][1] for i in tr]))
        for i in np.flatnonzero(partes == p):
            for k, mod in (("B1", modelo_b1), ("B", modelo_b), ("C", modelo_c)):
                probs[k][i] = c.prever_probabilidade(mod, nossas[i][1])

    verdades = [m for _, _, m in nossas]
    relatorio = {}
    for k in probs:
        curva = dt.froc([dt.detectar(p) for p in probs[k]], verdades)
        melhor, pontos, texto = dt.resumo_froc(curva)
        relatorio[k] = dict(melhor=melhor, sensibilidade_por_fp={str(f): v for f, v in pontos.items()}, curva=curva)
        print(f"{k}: {texto}", flush=True)
        np.savez_compressed(val_dir / f"probs_{k}.npz",
                            **{nossas[i][0]: probs[k][i].astype(np.float16) for i in range(len(nossas))})

    # critério: sensibilidade com até 2 falsos positivos por imagem
    escolhido = max(relatorio, key=lambda k: (relatorio[k]["sensibilidade_por_fp"]["2"], relatorio[k]["melhor"]["f1"]))
    print(f"Melhor variante: {escolhido}", flush=True)
    if escolhido == "C":
        Xn = np.concatenate([a[0] for a in am_nossas])
        yn = np.concatenate([a[1] for a in am_nossas])
        final = treinar(np.concatenate([Xh, Xn]), np.concatenate([yh, yn]))
    else:
        final = {"B1": modelo_b1, "B": modelo_b}[escolhido]
    joblib.dump(final, c.MODELOS / "rf_lesoes.joblib", compress=3)
    imp = sorted(zip(c.NOMES_ATRIBUTOS, final.feature_importances_), key=lambda t: -t[1])
    json.dump(dict(variante=escolhido, limiar=relatorio[escolhido]["melhor"]["limiar"], validacao=relatorio,
                   importancia_atributos={n: round(float(v), 4) for n, v in imp},
                   n_hanoi=args.n_hanoi, n_mineracao=args.n_mineracao, rf=RF,
                   versao_atributos=c.VERSAO_ATRIBUTOS),
              open(c.MODELOS / "rf_lesoes.json", "w", encoding="utf-8"), indent=2, ensure_ascii=False)
    print("Salvo em Modelos/rf_lesoes.joblib e rf_lesoes.json")


if __name__ == "__main__":
    main()
