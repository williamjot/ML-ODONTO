"""Monta Resultados/comparacao.md a partir das probabilidades de validação salvas.

Cada Resultados/validacao/probs_<VARIANTE>.npz tem, para cada uma das 20
panorâmicas marcadas, o mapa de probabilidade de um modelo que não viu aquele
paciente no treino. Todas as variantes passam pela mesma detecção e métrica.

Uso: python comparar.py
"""
import numpy as np

import comum as c
import deteccao as dt

VARIANTES = [  # (arquivo, descrição, raio da região)
    ("B", "RF B (só Hanói)", dt.RAIO_REGIAO),
    ("C", "RF C (Hanói + nossas, val. cruzada)", dt.RAIO_REGIAO),
    ("UNET", "U-Net (só Hanói)", dt.RAIO_REGIAO_UNET),
    ("UNET_FT", "U-Net + ajuste fino nas nossas (val. cruzada)", dt.RAIO_REGIAO_UNET),
    ("UNET_V2", "U-Net v2 (2000 px + Tversky, só Hanói)", dt.RAIO_REGIAO_UNET),
    ("UNET_V2_FT", "U-Net v2 + ajuste fino nas nossas (val. cruzada)", dt.RAIO_REGIAO_UNET),
    ("DUAS", "Duas etapas: ápice (DENTEX) + recorte (Hanói)", dt.RAIO_REGIAO_UNET),
]
FP_REF = (0.5, 1, 2, 4)


def avaliar_variante(chave, raio, nossas):
    caminho = c.RESULTADOS / "validacao" / f"probs_{chave}.npz"
    if not caminho.exists():
        return None
    P = np.load(caminho)
    curva = dt.froc([dt.detectar(P[nm].astype(np.float32), raio) for nm, _, _ in nossas],
                    [m for _, _, m in nossas])
    return dt.resumo_froc(curva)


def main():
    nossas = c.nossas_imagens()
    linhas = [
        "# Comparação: Random Forest × U-Net (20 panorâmicas marcadas, 45 lesões)",
        "",
        "Cada paciente é avaliado por um modelo que não o viu no treino. Lesão achada = alguma região "
        "prevista com ≥20% de sobreposição com ela. Sensibilidade em função de falsos positivos (FP) "
        "por imagem; depois, o ponto de melhor F1.",
        "",
        "| Modelo | Sens. 0,5 FP | Sens. 1 FP | Sens. 2 FP | Sens. 4 FP | F1 | Sens. | Precisão | FP/img | Dice | Limiar |",
        "|---|---|---|---|---|---|---|---|---|---|---|",
    ]
    resultados = {}
    for chave, desc, raio in VARIANTES:
        r = avaliar_variante(chave, raio, nossas)
        if r is None:
            continue
        melhor, pontos, texto = r
        resultados[chave] = r
        print(f"{chave}: {texto}", flush=True)
        sens = " | ".join(f"{pontos[f]:.0%}" for f in FP_REF)
        linhas.append(f"| {desc} | {sens} | {melhor['f1']:.2f} | {melhor['sensibilidade']:.0%} | "
                      f"{melhor['precisao']:.0%} | {melhor['falsos_por_imagem']:.1f} | {melhor['dice']:.2f} | "
                      f"{melhor['limiar']} |")
    (c.RESULTADOS / "comparacao.md").write_text("\n".join(linhas) + "\n", encoding="utf-8")
    print("\n".join(linhas))
    return resultados


if __name__ == "__main__":
    main()
