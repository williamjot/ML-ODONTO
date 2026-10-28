"""Consolida as revisões do dentista: desempenho real da U-Net e novas máscaras de treino.

Considera só imagens marcadas como concluídas (ou todas com --todas) e ignora as
marcadas como inadequadas. Detecções com "dúvida" ficam fora da precisão, mas a
estatística também é dada no cenário pessimista (dúvida = rejeitada).

Saída em Revisao/:
  estatisticas.md            precisão, sensibilidade estimada, falsos positivos/imagem (IC 95% de Wilson)
  lesoes_revisadas.csv       uma linha por detecção revisada e por lesão adicionada
  resumo_por_imagem.csv      contagens por imagem
  mascaras/<nome>.png        lesões confirmadas + adicionadas (resolução original): novos rótulos de treino
  negativos/<nome>.png       detecções rejeitadas (falsos positivos para treino)
  conferencia/<nome>.jpg     radiografia com o resultado da revisão desenhado

Uso: python consolidar_revisao.py [--todas]
"""
import argparse
import csv
import json
from math import sqrt
from pathlib import Path

import cv2
import numpy as np

import revisao_servidor as rs

SAIDA = rs.REVISAO
CORES = {"confirmada": (60, 200, 60), "rejeitada": (60, 60, 255), "duvida": (40, 210, 240), None: (31, 154, 255)}


def wilson(x, n, z=1.96):
    if n == 0:
        return float("nan"), float("nan"), float("nan")
    p = x / n
    d = 1 + z * z / n
    c = (p + z * z / (2 * n)) / d
    h = z * sqrt(p * (1 - p) / n + z * z / (4 * n * n)) / d
    return p, max(0, c - h), min(1, c + h)


def fmt(x, n):
    p, lo, hi = wilson(x, n)
    return "—" if n == 0 else f"{100 * p:.0f}% ({x}/{n}; IC 95% {100 * lo:.0f}–{100 * hi:.0f}%)"


def salvar_png(caminho, img):
    ok, buf = cv2.imencode(caminho.suffix, img)
    buf.tofile(str(caminho))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--todas", action="store_true", help="inclui revisões ainda não concluídas")
    args = ap.parse_args()
    rs.INDICE.update(rs.carregar_indice())
    rs.AMOSTRA.update(rs.sortear_amostra(rs.INDICE))
    for sub in ("mascaras", "negativos", "conferencia"):
        (SAIDA / sub).mkdir(parents=True, exist_ok=True)

    linhas, resumo = [], []
    tot = dict(imagens=0, inadequadas=0, confirmadas=0, rejeitadas=0, duvidas=0, sem_decisao=0, adicionadas=0)
    tot_am = dict(tot)  # só a amostra aleatória
    for arq in sorted((SAIDA / "revisoes").glob("A*.json")):
        r = json.loads(arq.read_text(encoding="utf-8"))
        if not (r.get("concluida") or args.todas):
            continue
        if r.get("imagem_inadequada"):
            tot["inadequadas"] += 1
            if r["id"] in rs.AMOSTRA:
                tot_am["inadequadas"] += 1
            continue
        id_ = r["id"]
        dets = rs.deteccoes(id_)
        img = rs.ler_cinza(rs.PASTA_IMAGENS / r["arquivo"])
        stem = Path(r["arquivo"]).stem
        positivo = np.zeros(img.shape, np.uint8)
        negativo = np.zeros(img.shape, np.uint8)
        conf = cv2.cvtColor(img, cv2.COLOR_GRAY2BGR)
        cont = dict(confirmadas=0, rejeitadas=0, duvidas=0, sem_decisao=0)
        for d in dets:
            dec = r["decisoes"].get(str(d["indice"]))
            pts = np.array(d["pontos"], np.int32)
            chave = {"confirmada": "confirmadas", "rejeitada": "rejeitadas", "duvida": "duvidas"}.get(dec, "sem_decisao")
            cont[chave] += 1
            if dec == "confirmada":
                cv2.fillPoly(positivo, [pts], 255)
            elif dec == "rejeitada":
                cv2.fillPoly(negativo, [pts], 255)
            cv2.polylines(conf, [pts], True, CORES[dec], 4)
            linhas.append(dict(id=id_, arquivo=r["arquivo"], origem="U-Net", indice=d["indice"],
                               x=d["centro"][0], y=d["centro"][1], confianca=d["confianca"], decisao=dec or ""))
        for k, p in enumerate(r["adicionadas"], 1):
            pts = np.array(p, np.int32)
            cv2.fillPoly(positivo, [pts], 255)
            cv2.polylines(conf, [pts], True, (255, 170, 60), 4)
            cx, cy = pts.mean(0)
            linhas.append(dict(id=id_, arquivo=r["arquivo"], origem="adicionada pelo revisor", indice=k,
                               x=int(cx), y=int(cy), confianca="", decisao="adicionada"))
        salvar_png(SAIDA / "mascaras" / f"{stem}.png", positivo)
        salvar_png(SAIDA / "negativos" / f"{stem}.png", negativo)
        salvar_png(SAIDA / "conferencia" / f"{stem}.jpg", conf)
        resumo.append(dict(id=id_, arquivo=r["arquivo"], revisor=r.get("revisor", ""), concluida=r.get("concluida"),
                           deteccoes=len(dets), **cont, adicionadas=len(r["adicionadas"]),
                           observacoes=r.get("observacoes", "")))
        for t in ([tot, tot_am] if id_ in rs.AMOSTRA else [tot]):
            t["imagens"] += 1
            t["adicionadas"] += len(r["adicionadas"])
            for k in cont:
                t[k] += cont[k]

    if not resumo:
        print("Nenhuma revisão concluída ainda (use --todas para incluir as parciais).")
        return
    for nome, dados in (("lesoes_revisadas.csv", linhas), ("resumo_por_imagem.csv", resumo)):
        with open(SAIDA / nome, "w", newline="", encoding="utf-8-sig") as f:
            w = csv.DictWriter(f, fieldnames=list(dados[0]), delimiter=";")
            w.writeheader()
            w.writerows(dados)

    def tabela(t, titulo):
        C, R, D, A, n = t["confirmadas"], t["rejeitadas"], t["duvidas"], t["adicionadas"], t["imagens"]
        if n == 0:
            return [f"## {titulo}", "", "Nenhuma imagem revisada neste grupo ainda.", ""]
        return [
            f"## {titulo}",
            "",
            f"Imagens revisadas: **{n}** ({'incluindo parciais' if args.todas else 'só concluídas'}); "
            f"inadequadas excluídas: {t['inadequadas']}.",
            f"Detecções: {C} confirmadas, {R} rejeitadas, {D} com dúvida, {t['sem_decisao']} sem decisão. "
            f"Lesões adicionadas pelo revisor (não detectadas): {A}.",
            "",
            "| Medida | Estimativa |",
            "|---|---|",
            f"| Precisão (confirmadas ÷ confirmadas + rejeitadas) | {fmt(C, C + R)} |",
            f"| Precisão pessimista (dúvida conta como rejeitada) | {fmt(C, C + R + D)} |",
            f"| Sensibilidade estimada (confirmadas ÷ confirmadas + adicionadas) | {fmt(C, C + A)} |",
            f"| Falsos positivos por imagem | {R / n:.2f}".replace(".", ",") + " |",
            "",
        ]

    texto = ["# Revisão das detecções da U-Net v2", ""]
    texto += tabela(tot_am, f"Amostra aleatória ({len(rs.AMOSTRA)} imagens sorteadas): estimativa sem viés")
    texto += tabela(tot, "Todas as imagens revisadas")
    texto += [
        "A sensibilidade pressupõe que o revisor marcou todas as lesões que a U-Net não achou. "
        "Use a amostra aleatória para relatar o desempenho; o conjunto de todas as revisadas segue a ordem de "
        "prioridade (muitas detecções e nenhuma detecção primeiro) e não é representativo do acervo.",
    ]
    (SAIDA / "estatisticas.md").write_text("\n".join(texto) + "\n", encoding="utf-8")
    print("\n".join(texto))


if __name__ == "__main__":
    main()
