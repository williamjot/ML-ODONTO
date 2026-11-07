"""Equivalência do pipeline C++ completo (radiografia -> lesões) com a referência validada.

Roda `ml-odonto-cli run` nas 20 panorâmicas marcadas e compara com os mapas de probabilidade do
Python (`probs_UNET_V2.npz`) e com a avaliação por lesão já publicada (`lesoes_validacao.csv`).
Usa radiografias de pacientes: roda só no computador local.

Critérios (os mesmos de docs/EQUIVALENCIA_R_PYTHON.md):
  - as mesmas lesões encontradas, uma a uma, que a U-Net v2 em Python (31/45 no limiar 0,5);
  - nenhum falso positivo a mais;
  - correlação das probabilidades >= 0,998;
  - Dice das máscaras C++ x Python >= 0,95.

Uso:
    python cpp/tools/validate_pipeline.py --cli cpp/build/zig-onnx/ml-odonto-cli.exe
"""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import cv2
import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
from compare_reference import dice, reference_mask  # noqa: E402

ROOT = Path(__file__).resolve().parents[2]
HIT_FRACTION = 0.2  # fração da lesão prevista que precisa cair sobre a verdadeira (comum.avaliar)


def read_pfm(path: Path) -> np.ndarray:
    with path.open("rb") as source:
        if source.readline().strip() != b"Pf":
            raise ValueError("probabilidade C++ não é um PFM em cinza")
        width, height = map(int, source.readline().split())
        scale = float(source.readline())
        data = np.fromfile(source, dtype="<f4" if scale < 0 else ">f4", count=width * height)
    return np.flipud(data.reshape(height, width)).astype(np.float32)


def evaluate(pred: np.ndarray, truth: np.ndarray) -> tuple[list[bool], int]:
    """Lesões verdadeiras achadas (na ordem de varredura) e falsos positivos, como avaliar_lesoes()."""
    n_truth, truth_labels = cv2.connectedComponents(truth.astype(np.uint8), connectivity=8)
    n_pred, pred_labels = cv2.connectedComponents(pred.astype(np.uint8), connectivity=8)
    found: set[int] = set()
    false_positives = 0
    for label in range(1, n_pred):
        region = pred_labels == label
        if (region & truth).sum() >= HIT_FRACTION * max(int(region.sum()), 1):
            found.update(int(v) for v in np.unique(truth_labels[region & truth]) if v > 0)
        else:
            false_positives += 1
    return [label in found for label in range(1, n_truth)], false_positives


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument("--model", type=Path,
                        default=ROOT / "software/odontolesao/inst/modelos/unet_lesoes_v2.onnx")
    parser.add_argument("--threshold", type=float, default=0.5)
    parser.add_argument("--output", type=Path, default=None,
                        help="pasta para guardar as saídas da CLI (padrão: temporária)")
    args = parser.parse_args()

    images = ROOT / "Treinamento/extraido/imagens"
    masks = ROOT / "Treinamento/extraido/mascaras"
    with (ROOT / "Resultados/figuras/dados/lesoes_validacao.csv").open(encoding="utf-8") as source:
        rows = list(csv.DictReader(source))
    patients = sorted({row["paciente"] for row in rows})
    expected = {p: [row["achada_UNET_V2"] == "1" for row in
                    sorted((r for r in rows if r["paciente"] == p), key=lambda r: int(r["lesao"]))]
                for p in patients}

    temporary = tempfile.TemporaryDirectory()
    out = args.output or Path(temporary.name)
    out.mkdir(parents=True, exist_ok=True)
    start = time.perf_counter()
    subprocess.run([str(args.cli), "run", str(args.model), str(out),
                    *[str(images / f"{p}.png") for p in patients],
                    "--threshold", str(args.threshold)], check=True)
    elapsed = time.perf_counter() - start

    table = []
    with np.load(ROOT / "Resultados/validacao/probs_UNET_V2.npz") as archive:
        for p in patients:
            prob_cpp = read_pfm(out / f"{p}_probabilidade.pfm")
            prob_py = np.asarray(archive[p], dtype=np.float32)
            if prob_cpp.shape != prob_py.shape:
                raise SystemExit(f"{p}: forma C++ {prob_cpp.shape} != Python {prob_py.shape}")
            mask_cpp = cv2.imread(str(out / f"{p}_mascara.png"), cv2.IMREAD_GRAYSCALE) > 0
            mask_py = reference_mask(prob_py, args.threshold)
            truth = cv2.imread(str(masks / f"{p}.png"), cv2.IMREAD_GRAYSCALE)
            truth = cv2.resize(truth, (prob_cpp.shape[1], prob_cpp.shape[0]),
                               interpolation=cv2.INTER_NEAREST) > 127
            found, false_positives = evaluate(mask_cpp, truth)
            meta = json.loads((out / f"{p}_deteccoes.json").read_text(encoding="utf-8"))
            row = {
                "paciente": p,
                "lesoes": len(found),
                "esperado": sum(expected[p]),
                "achadas_cpp": sum(found),
                "igual": found == expected[p],
                "falsos": false_positives,
                "cor_prob": float(np.corrcoef(prob_cpp.ravel(), prob_py.ravel())[0, 1]),
                "dif_max_prob": float(np.abs(prob_cpp - prob_py).max()),
                "dice_mascaras": dice(mask_cpp, mask_py),
                "segundos": meta["seconds"],
            }
            table.append(row)
            print(f"{p}  esperado {row['esperado']}/{row['lesoes']} | C++ {row['achadas_cpp']} "
                  f"({'igual' if row['igual'] else 'DIFERENTE'}) | falsos {false_positives} | "
                  f"cor {row['cor_prob']:.5f} | dif. máx. {row['dif_max_prob']:.4f} | "
                  f"Dice {row['dice_mascaras']:.3f} | {row['segundos']:.1f} s")

    with (out / "validacao_cpp.csv").open("w", newline="", encoding="utf-8") as target:
        writer = csv.DictWriter(target, fieldnames=list(table[0]))
        writer.writeheader()
        writer.writerows(table)

    total_expected = sum(r["esperado"] for r in table)
    total_found = sum(r["achadas_cpp"] for r in table)
    total = sum(r["lesoes"] for r in table)
    approved = (all(r["igual"] for r in table)
                and min(r["cor_prob"] for r in table) >= 0.998
                and min(r["dice_mascaras"] for r in table) >= 0.95)
    print(f"\nTOTAL esperado {total_expected}/{total} | C++ {total_found}/{total} "
          f"({'IGUAL' if all(r['igual'] for r in table) else 'DIFERENTE'}) | "
          f"falsos {sum(r['falsos'] for r in table)} | "
          f"cor mínima {min(r['cor_prob'] for r in table):.5f} | "
          f"Dice mínimo {min(r['dice_mascaras'] for r in table):.3f} | "
          f"{elapsed:.0f} s no total ({elapsed / len(table):.1f} s/imagem)")
    print("APROVADO:", approved)
    if not approved:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
