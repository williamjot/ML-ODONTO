"""Figuras com radiografias (rótulos, comparação qualitativa, validação, aplicação).

As figuras quantitativas vêm do R (figuras_R/). Aqui ficam as que mostram radiografias.
Pacientes da validação aparecem pelo número do PDF (01-20) e imagens da aplicação por
identificador anônimo (A001-A422, ordem alfabética do arquivo); nenhum nome é desenhado.

Uso: python figuras_radiografias.py      Saída: Resultados/figuras/radiografias/*.png (600 dpi)
"""
import csv
import json

import cv2
import matplotlib
import matplotlib.patches
import numpy as np

matplotlib.use("Agg")
import matplotlib.pyplot as plt  # noqa: E402

import comum as c  # noqa: E402
import deteccao as dt  # noqa: E402

SAIDA = c.RESULTADOS / "figuras" / "radiografias"
plt.rcParams.update({"font.family": "Arial", "font.size": 7, "axes.linewidth": 0.45})
VERDE, AZUL, CIANO = (0.24, 0.78, 0.31), (0.12, 0.44, 0.71), (0.0, 0.85, 1.0)
# probabilidade em azul: transparente onde p = 0, azul opaco onde p = 1
MAPA_AZUL = matplotlib.colors.LinearSegmentedColormap.from_list("azul", [(0.12, 0.44, 0.71, 0.0), (0.12, 0.44, 0.71, 0.85)])
MM = 1 / 25.4


def contorno(ax, m, cor, lw=0.9, ls="-"):
    if m.any():
        ax.contour(m.astype(float), levels=[0.5], colors=[cor], linewidths=lw, linestyles=ls)


def painel(ax, img, titulo=None, tag=None):
    ax.imshow(img, cmap="gray", vmin=0, vmax=255, interpolation="lanczos")
    ax.set_xticks([]); ax.set_yticks([])
    for s in ax.spines.values():
        s.set_linewidth(0.45)
    if titulo:
        ax.set_title(titulo, fontsize=7, pad=2.5)
    if tag:
        ax.text(-0.02, 1.02, tag, transform=ax.transAxes, fontsize=9, fontweight="bold", ha="right", va="bottom")


def recorte(arr, cy, cx, h, w):
    H, W = arr.shape[:2]
    y0 = int(np.clip(cy - h // 2, 0, H - h)); x0 = int(np.clip(cx - w // 2, 0, W - w))
    return arr[y0:y0 + h, x0:x0 + w], (y0, x0)


def salvar(fig, nome):
    fig.savefig(SAIDA / nome, dpi=600, bbox_inches="tight", pad_inches=0.02)
    plt.close(fig)
    print(nome)


# ---------------------------------------------------------------- Fig. 2: rótulos
def fig_rotulos(nossas):
    import apices as ap
    nm, img, m = next(t for t in nossas if t[0] == "16")
    tr = c.redimensionar_mascara(c.ler_cinza(c.NOSSAS / "contornos" / "16.png"), img.shape)
    nome_h = c.nomes_hanoi()[5]
    ih, mh, inc = c.imagem_hanoi(nome_h)
    ys, xs = np.nonzero(mh)
    fig, axs = plt.subplots(1, 5, figsize=(183 * MM, 34 * MM), gridspec_kw=dict(wspace=0.05))
    a, _ = recorte(img, 205, 290, 230, 330)
    mt, _ = recorte(tr, 205, 290, 230, 330); mm, _ = recorte(m, 205, 290, 230, 330)
    painel(axs[0], a, "Traço do dentista (PDF)", "a"); contorno(axs[0], mt, AZUL, 1.0)
    painel(axs[1], a, "Máscara derivada"); axs[1].imshow(np.ma.masked_where(~mm, mm), cmap="Blues", alpha=.6, vmin=0, vmax=1.2)
    contorno(axs[1], mm, AZUL, 0.8)
    b, _ = recorte(ih, int(ys.mean()), int(xs.mean()), 230, 330)
    bm, _ = recorte(mh, int(ys.mean()), int(xs.mean()), 230, 330)
    bi, _ = recorte(inc, int(ys.mean()), int(xs.mean()), 230, 330)
    painel(axs[2], b, "Hanói: radiografia", "b")
    painel(axs[3], b, "Elipse e anel ignorado")
    axs[3].imshow(np.ma.masked_where(~bi, bi), cmap="Greys", alpha=.55, vmin=0, vmax=1)
    axs[3].imshow(np.ma.masked_where(~bm, bm), cmap="Blues", alpha=.7, vmin=0, vmax=1.2)
    # DENTEX: contornos dos dentes e ápices extraídos da ponta da raiz
    imagens, dentes = ap.anotacoes()
    iid = sorted(dentes, key=int)[0]
    dimg, esc = c.normalizar(c.ler_cinza(ap.caminho_imagem(iid)))
    h, w = dimg.shape
    d, (y0, x0) = recorte(dimg, int(.40 * h), int(.36 * w), 230, 330)
    painel(axs[4], d, "DENTEX: ápices", "c")
    for t in dentes[iid]:
        pol = t["poligono"] * esc - [x0, y0]
        axs[4].plot(*np.vstack([pol, pol[:1]]).T, color=VERDE, lw=.5)
    pts = np.array([(x * esc - x0, y * esc - y0) for x, y, _, _ in ap.apices_da_imagem(iid)])
    axs[4].scatter(pts[:, 0], pts[:, 1], s=6, color=CIANO, edgecolors="black", linewidths=.3, zorder=3)
    axs[4].set_xlim(0, 330); axs[4].set_ylim(230, 0)
    salvar(fig, "fig2_rotulos.png")


# ---------------------------------------------------------------- Fig. 4: RF × U-Net v2 × duas etapas
def fig_qualitativa(nossas):
    import duas_etapas as de
    import unet
    P = {k: np.load(c.RESULTADOS / "validacao" / f"probs_{k}.npz") for k in ("C", "UNET_V2", "DUAS")}
    lim = {"C": json.load(open(c.MODELOS / "rf_lesoes.json", encoding="utf-8"))["limiar"],
           "UNET_V2": json.load(open(c.MODELOS / "unet_lesoes_v2.json", encoding="utf-8"))["melhor"]["limiar"],
           "DUAS": json.load(open(c.MODELOS / "duas_etapas" / "avaliacao.json", encoding="utf-8"))["melhor"]["limiar"]}
    raio = {"C": dt.RAIO_REGIAO, "UNET_V2": dt.RAIO_REGIAO_UNET, "DUAS": dt.RAIO_REGIAO_UNET}
    nomes = {"C": "Random Forest C", "UNET_V2": "U-Net v2", "DUAS": "Duas etapas"}
    localizador, _ = unet.carregar(c.MODELOS / "duas_etapas" / "localizador_apices.pt")
    meio = de.RECORTE / 2 * c.LARGURA / de.LARGURA_RECORTE
    casos = ["16", "03", "15"]
    fig, axs = plt.subplots(len(casos), 4, figsize=(183 * MM, 22 * MM * len(casos)), gridspec_kw=dict(wspace=0.03, hspace=0.08))
    for i, nm in enumerate(casos):
        _, img, m = next(t for t in nossas if t[0] == nm)
        h, w = img.shape
        y0, y1, x0, x1 = int(.12 * h), int(.82 * h), int(.1 * w), int(.9 * w)
        cr = lambda a: a[y0:y1, x0:x1]
        t = lambda s: s if i == 0 else None
        painel(axs[i, 0], cr(img), t("Marcação do dentista"), "abc"[i])
        contorno(axs[i, 0], cr(m), VERDE, 1.0)
        n_les = cv2.connectedComponents(m.astype(np.uint8))[0] - 1
        axs[i, 0].text(0.01, 0.02, f"paciente {nm}: {n_les} lesões", transform=axs[i, 0].transAxes,
                       color="white", fontsize=6, va="bottom")
        for j, k in enumerate(("C", "UNET_V2", "DUAS"), 1):
            p = P[k][nm].astype(np.float32)
            mm = dt.mascara(dt.detectar(p, raio[k]), lim[k], img.shape)
            r = c.avaliar(mm, m)
            painel(axs[i, j], cr(img), t(nomes[k]))
            axs[i, j].imshow(cr(p), cmap=MAPA_AZUL, vmin=0, vmax=1)
            if k == "DUAS":
                for x, y in de.detectar_apices(localizador, img):
                    axs[i, j].add_patch(matplotlib.patches.Rectangle((x - meio - x0, y - meio - y0), 2 * meio, 2 * meio,
                                                                     fill=False, lw=.25, ec="white", alpha=.5))
            contorno(axs[i, j], cr(m), VERDE, 0.8)
            contorno(axs[i, j], cr(mm), CIANO, 1.0)
            axs[i, j].set_xlim(-.5, x1 - x0 - .5); axs[i, j].set_ylim(y1 - y0 - .5, -.5)
            axs[i, j].text(0.01, 0.02, f"achou {r['lesoes_achadas']}/{r['lesoes']} · falsos {r['previstas'] - r['previstas_corretas']}",
                           transform=axs[i, j].transAxes, color="white", fontsize=6, va="bottom")
    salvar(fig, "fig4_rf_vs_unet.png")


# ---------------------------------------------------------------- Fig. 5: 20 pacientes (U-Net v2)
def fig_validacao(nossas):
    P = np.load(c.RESULTADOS / "validacao" / "probs_UNET_V2.npz")
    lim = json.load(open(c.MODELOS / "unet_lesoes_v2.json", encoding="utf-8"))["melhor"]["limiar"]
    fig, axs = plt.subplots(5, 4, figsize=(183 * MM, 118 * MM), gridspec_kw=dict(wspace=0.03, hspace=0.22))
    tot = [0, 0]
    for k, (nm, img, m) in enumerate(nossas):
        ax = axs.flat[k]
        h, w = img.shape
        y0, y1, x0, x1 = int(.12 * h), int(.85 * h), int(.08 * w), int(.92 * w)
        mm = dt.mascara(dt.detectar(P[nm].astype(np.float32), dt.RAIO_REGIAO_UNET), lim, img.shape)
        r = c.avaliar(mm, m)
        tot[0] += r["lesoes_achadas"]; tot[1] += r["lesoes"]
        painel(ax, img[y0:y1, x0:x1])
        contorno(ax, m[y0:y1, x0:x1], VERDE, 0.7)
        contorno(ax, mm[y0:y1, x0:x1], CIANO, 0.7)
        fp = r["previstas"] - r["previstas_corretas"]
        ax.set_title(f"{nm}: {r['lesoes_achadas']}/{r['lesoes']} achadas" + (f", {fp} falso(s)" if fp else ""), fontsize=6.5, pad=2)
    print("validação U-Net v2: achadas", tot)
    salvar(fig, "fig5_validacao_20_pacientes.png")


# ---------------------------------------------------------------- Fig. 7: aplicação (U-Net v2)
def fig_aplicacao():
    base = c.RESULTADOS / "aplicacao_unet_v2"
    imgs = list(csv.DictReader(open(base / "resumo_por_imagem.csv", encoding="utf-8-sig"), delimiter=";"))
    ids = {l["arquivo"]: f"A{k:03d}" for k, l in enumerate(sorted(imgs, key=lambda l: l["arquivo"]), 1)}
    n = {l["arquivo"]: int(l["n_lesoes"]) for l in imgs}
    rng = np.random.default_rng(7)
    escolha = []  # um exemplo de cada faixa: 0, 1, 2-3 e 4+ detecções
    for faixa in ((0, 0), (1, 1), (2, 3), (4, 99)):
        cands = sorted(a for a in n if faixa[0] <= n[a] <= faixa[1])
        escolha.append(cands[rng.integers(len(cands))])
    fig, axs = plt.subplots(2, 2, figsize=(183 * MM, 92 * MM), gridspec_kw=dict(wspace=0.03, hspace=0.12))
    for ax, arq, tag in zip(axs.flat, escolha, "abcd"):
        orig = c.ler_cinza(c.RAIZ / "Dados" / "Aplicacao" / arq)
        mask = c.ler_cinza(base / "mascaras" / f"{arq.rsplit('.', 1)[0]}.png") > 0
        h, w = orig.shape
        y0, y1, x0, x1 = int(.12 * h), int(.85 * h), int(.08 * w), int(.92 * w)
        o = cv2.resize(orig[y0:y1, x0:x1], None, fx=.5, fy=.5, interpolation=cv2.INTER_AREA)
        mk = cv2.resize(mask[y0:y1, x0:x1].astype(np.uint8), (o.shape[1], o.shape[0]), interpolation=cv2.INTER_NEAREST) > 0
        painel(ax, o, f"{ids[arq]}: {n[arq]} {'lesão detectada' if n[arq] == 1 else 'lesões detectadas'}", tag)
        contorno(ax, mk, CIANO, 0.9)
    salvar(fig, "fig7_aplicacao_exemplos.png")


def main():
    SAIDA.mkdir(parents=True, exist_ok=True)
    nossas = c.nossas_imagens()
    fig_rotulos(nossas)
    fig_qualitativa(nossas)
    fig_validacao(nossas)
    fig_aplicacao()


if __name__ == "__main__":
    main()
