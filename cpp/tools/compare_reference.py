"""Compara a detecção C++ com os mapas de probabilidade validados do Python.

O script não lê radiografias: ele usa apenas `probs_UNET_V2.npz`, executa a CLI C++ e compara a máscara
resultante com `scripts/deteccao.py`. Assim, isola exatamente a migração do pós-processamento.

Uso:
    python cpp/tools/compare_reference.py \
      --cli cpp/build/default/ml-odonto-cli.exe \
      --probabilities Resultados/validacao/probs_UNET_V2.npz
"""

from __future__ import annotations

import argparse
import json
import subprocess
import tempfile
from pathlib import Path

import cv2
import numpy as np
from scipy import ndimage as ndi

ROOT = Path(__file__).resolve().parents[2]
PEAK_RADIUS = 15
REGION_RADIUS = 100
SMOOTHING = 4
MINIMUM_PEAK = 0.05


def write_pfm(path: Path, image: np.ndarray) -> None:
    array = np.asarray(image, dtype="<f4")
    if array.ndim != 2:
        raise ValueError("o mapa de probabilidade deve ter duas dimensões")
    with path.open("wb") as output:
        output.write(f"Pf\n{array.shape[1]} {array.shape[0]}\n-1.0\n".encode("ascii"))
        np.flipud(array).tofile(output)


def read_pgm(path: Path) -> np.ndarray:
    with path.open("rb") as source:
        if source.readline().strip() != b"P5":
            raise ValueError("máscara C++ não é um PGM binário")
        line = source.readline()
        while line.startswith(b"#"):
            line = source.readline()
        width, height = map(int, line.split())
        if int(source.readline()) != 255:
            raise ValueError("máscara C++ não usa 8 bits")
        return np.frombuffer(source.read(), dtype=np.uint8).reshape(height, width)


def dice(left: np.ndarray, right: np.ndarray) -> float:
    denominator = int(left.sum()) + int(right.sum())
    return 1.0 if denominator == 0 else 2.0 * int(np.logical_and(left, right).sum()) / denominator


def reference_mask(probability: np.ndarray, threshold: float) -> np.ndarray:
    """Tradução local de `scripts/deteccao.py`, sem importar o restante do pipeline de pesquisa."""

    height, width = probability.shape
    smoothed = cv2.GaussianBlur(probability, (0, 0), SMOOTHING)
    peaks = (smoothed == ndi.maximum_filter(smoothed, size=2 * PEAK_RADIUS + 1)) & (
        smoothed >= MINIMUM_PEAK
    )
    ys, xs = np.nonzero(peaks)
    detections: list[tuple[np.ndarray, int, int, float]] = []
    for index in np.argsort(-smoothed[ys, xs]):
        y, x = int(ys[index]), int(xs[index])
        confidence = float(smoothed[y, x])
        y0, y1 = max(0, y - REGION_RADIUS), min(height, y + REGION_RADIUS + 1)
        x0, x1 = max(0, x - REGION_RADIUS), min(width, x + REGION_RADIUS + 1)
        _, labels = cv2.connectedComponents(
            (smoothed[y0:y1, x0:x1] >= 0.5 * confidence).astype(np.uint8)
        )
        detections.append((labels == labels[y - y0, x - x0], y0, x0, confidence))

    result = np.zeros(probability.shape, dtype=bool)
    for region, y0, x0, confidence in detections:
        if confidence >= threshold:
            result[y0 : y0 + region.shape[0], x0 : x0 + region.shape[1]] |= region
    return result


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--cli", type=Path, required=True)
    parser.add_argument(
        "--probabilities",
        type=Path,
        default=ROOT / "Resultados" / "validacao" / "probs_UNET_V2.npz",
    )
    parser.add_argument("--threshold", type=float, default=0.5)
    parser.add_argument("--minimum-dice", type=float, default=0.95)
    args = parser.parse_args()

    if not args.cli.is_file():
        raise SystemExit(f"CLI não encontrada: {args.cli}")
    if not args.probabilities.is_file():
        raise SystemExit(f"mapas de referência não encontrados: {args.probabilities}")

    failures: list[str] = []
    values: list[float] = []
    with np.load(args.probabilities) as archive, tempfile.TemporaryDirectory() as temporary:
        directory = Path(temporary)
        for patient in sorted(archive.files):
            probability = np.asarray(archive[patient], dtype=np.float32)
            reference = reference_mask(probability, args.threshold)
            pfm = directory / f"{patient}.pfm"
            pgm = directory / f"{patient}.pgm"
            metadata = directory / f"{patient}.json"
            write_pfm(pfm, probability)
            subprocess.run(
                [str(args.cli), "detect", str(pfm), str(pgm), str(metadata), str(args.threshold)],
                check=True,
                capture_output=True,
                text=True,
            )
            actual = read_pgm(pgm) != 0
            score = dice(reference, actual)
            values.append(score)
            component_count = len(json.loads(metadata.read_text(encoding="utf-8"))["components"])
            print(f"{patient}: Dice={score:.6f} componentes={component_count}")
            if score < args.minimum_dice:
                failures.append(patient)

    print(f"mínimo={min(values):.6f} média={np.mean(values):.6f} casos={len(values)}")
    if failures:
        raise SystemExit("equivalência reprovada: " + ", ".join(failures))


if __name__ == "__main__":
    main()
