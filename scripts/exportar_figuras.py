"""Exporta tabelas organizadas (uma linha por observação) para as figuras em R.

Saída em Resultados/figuras/dados/:
  froc.csv               modelo × limiar: contagens de lesões achadas, previstas, corretas, pixels
  pacientes.csv          modelo × paciente no ponto de operação (para bootstrap por paciente)
  lesoes_validacao.csv   uma linha por lesão marcada: tamanho, posição, se cada modelo a achou
  treino.csv             curvas de treino no Colab (U-Net v1, localizador de ápices, segmentador de recortes)
  aplicacao_lesoes.csv   uma linha por lesão detectada nas 422 imagens (sem nomes de pacientes)
  aplicacao_imagens.csv  uma linha por imagem de aplicação (id anônimo, nº de lesões)

Uso: python exportar_figuras.py
"""
import csv
import re

import cv2
import numpy as np

import comparar
import comum as c
import deteccao as dt

DADOS = c.RESULTADOS / "figuras" / "dados"
APLICACAO = "aplicacao_unet_v2"  # modelo escolhido para a aplicação (U-Net v2)
ROTULOS = {"B": "RF (Hanói)", "C": "RF (Hanói + nossas)", "UNET": "U-Net (Hanói)",
           "UNET_FT": "U-Net + ajuste fino", "UNET_V2": "U-Net v2", "DUAS": "Duas etapas"}


def escrever(nome, linhas):
    with open(DADOS / nome, "w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(linhas[0]))
        w.writeheader()
        w.writerows(linhas)
    print(f"{nome}: {len(linhas)} linhas")


def achadas_por_lesao(pred, verdade):
    """Vetor booleano por lesão marcada (mesma regra de comum.avaliar)."""
    nv, rv = cv2.connectedComponents(verdade.astype(np.uint8))
    np_, rp = cv2.connectedComponents(pred.astype(np.uint8))
    area = np.bincount(rp.ravel(), minlength=np_)
    sobre = np.bincount(rp[verdade], minlength=np_)
    corretas = np.flatnonzero(sobre >= c.FRACAO_ACERTO * np.maximum(area, 1))
    corretas = corretas[corretas > 0]
    tocadas = set(np.unique(rv[np.isin(rp, corretas)])) - {0}
    return [k in tocadas for k in range(1, nv)]


def main():
    DADOS.mkdir(parents=True, exist_ok=True)
    nossas = c.nossas_imagens()
    verdades = [m for _, _, m in nossas]

    # lesões marcadas: geometria
    lesoes = []
    for nm, img, v in nossas:
        n, rot, st, cent = cv2.connectedComponentsWithStats(v.astype(np.uint8))
        h, w = v.shape
        for k in range(1, n):
            lesoes.append(dict(paciente=nm, lesao=k, area_px=int(st[k, cv2.CC_STAT_AREA]),
                               diam_eq_px=round(2 * np.sqrt(st[k, cv2.CC_STAT_AREA] / np.pi), 2),
                               x_rel=round(cent[k][0] / w, 4), y_rel=round(cent[k][1] / h, 4),
                               arcada="superior" if cent[k][1] / h < 0.5 else "inferior"))

    froc, pacientes = [], []
    for chave, _, raio in comparar.VARIANTES:
        arq = c.RESULTADOS / "validacao" / f"probs_{chave}.npz"
        if not arq.exists():
            continue
        P = np.load(arq)
        dets = [dt.detectar(P[nm].astype(np.float32), raio) for nm, _, _ in nossas]
        melhor_f1, limiar_op = -1, None
        for t in np.round(np.arange(0.05, 0.96, 0.025), 3):
            linhas = [c.avaliar(dt.mascara(d, t, v.shape), v) for d, v in zip(dets, verdades)]
            s = {k: sum(l[k] for l in linhas) for k in linhas[0]}
            sens = s["lesoes_achadas"] / s["lesoes"]
            prec = s["previstas_corretas"] / max(s["previstas"], 1)
            f1 = 2 * sens * prec / max(sens + prec, 1e-9)
            froc.append(dict(modelo=ROTULOS[chave], limiar=float(t), lesoes=s["lesoes"], achadas=s["lesoes_achadas"],
                             previstas=s["previstas"], corretas=s["previstas_corretas"], inter_px=s["inter"],
                             soma_px=s["soma"], n_imagens=len(linhas)))
            if f1 > melhor_f1:
                melhor_f1, limiar_op = f1, float(t)
        i_les = 0
        for (nm, _, v), d in zip(nossas, dets):
            m = dt.mascara(d, limiar_op, v.shape)
            a = c.avaliar(m, v)
            pacientes.append(dict(modelo=ROTULOS[chave], limiar=limiar_op, paciente=nm, **a))
            for achou in achadas_por_lesao(m, v):
                lesoes[i_les][f"achada_{chave}"] = int(achou)
                i_les += 1
        print(f"{chave}: limiar de operação {limiar_op} (melhor F1 {melhor_f1:.2f})")
    escrever("froc.csv", froc)
    escrever("pacientes.csv", pacientes)
    escrever("lesoes_validacao.csv", lesoes)

    # treino no Colab: U-Net v1 (log completo) e as duas etapas (log resumido)
    log = (c.MODELOS / "log_colab_unet.txt").read_text(encoding="utf-8")
    treino = [dict(modelo="U-Net (Hanói)", iteracao=int(i), metrica="perda", valor=float(v))
              for i, v in re.findall(r"^it (\d+)/\d+ perda ([\d.]+)", log, re.M)]
    treino += [dict(modelo="U-Net (Hanói)", iteracao=int(i), metrica="dice_validacao", valor=float(v))
               for i, v in re.findall(r"== validação it (\d+): Dice ([\d.]+)", log)]
    nomes_etapa = {"1": "Localizador de ápices", "4": "Segmentador de recortes"}
    for linha in (c.MODELOS / "duas_etapas" / "log_colab_duas_etapas.txt").read_text(encoding="utf-8").splitlines():
        partes = linha.split(",")
        if len(partes) == 4 and partes[0] in nomes_etapa:
            treino.append(dict(modelo=nomes_etapa[partes[0]], iteracao=int(partes[1]), metrica=partes[2],
                               valor=float(partes[3])))
    escrever("treino.csv", treino)

    # aplicação do modelo escolhido: ids anônimos, posições relativas, diâmetro na escala de trabalho (1400 px)
    base = c.RESULTADOS / APLICACAO
    deteccoes = list(csv.DictReader(open(base / "lesoes.csv", encoding="utf-8-sig"), delimiter=";"))
    n_por = {}
    for l in deteccoes:
        n_por[l["arquivo"]] = n_por.get(l["arquivo"], 0) + 1
    arquivos = sorted(p.name for p in (c.RAIZ / "Dados" / "Aplicacao").iterdir())
    imagens = [dict(arquivo=a, n_lesoes=n_por.get(a, 0)) for a in arquivos]
    with open(base / "resumo_por_imagem.csv", "w", newline="", encoding="utf-8-sig") as f:
        w = csv.writer(f, delimiter=";")
        w.writerow(["arquivo", "n_lesoes"])
        for l in sorted(imagens, key=lambda l: (-l["n_lesoes"], l["arquivo"])):
            w.writerow([l["arquivo"], l["n_lesoes"]])
    ids = {l["arquivo"]: f"A{k:03d}" for k, l in enumerate(sorted(imagens, key=lambda l: l["arquivo"]), 1)}
    tamanhos = {}
    for l in imagens:
        img = cv2.imdecode(np.fromfile(str(c.RAIZ / "Dados" / "Aplicacao" / l["arquivo"]), np.uint8),
                           cv2.IMREAD_GRAYSCALE)
        tamanhos[l["arquivo"]] = img.shape
    escrever("aplicacao_imagens.csv", [dict(imagem=ids[l["arquivo"]], n_lesoes=int(l["n_lesoes"]))
                                       for l in imagens])
    les_apl = []
    for l in deteccoes:
        h, w = tamanhos[l["arquivo"]]
        esc = c.LARGURA / w
        les_apl.append(dict(imagem=ids[l["arquivo"]], x_rel=round(int(l["x_centro"]) / w, 4),
                            y_rel=round(int(l["y_centro"]) / h, 4),
                            diam_eq_px=round(2 * np.sqrt(int(l["area_px"]) * esc ** 2 / np.pi), 2),
                            confianca=float(l["confianca"])))
    escrever("aplicacao_lesoes.csv", les_apl)


if __name__ == "__main__":
    main()
