"""Instala o motor C++ no aplicativo R (pacote odontolesao).

Copia o executável e as DLLs do ONNX Runtime para software/odontolesao/inst/bin e exporta o modelo
ONNX ao lado dos pesos, se ainda não existir. Depois reinstale o pacote R:
    R CMD INSTALL --no-test-load software/odontolesao

Uso:
    python cpp/tools/install_into_app.py --build cpp/build/zig-onnx
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
APP = ROOT / "software" / "odontolesao" / "inst"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--build", type=Path, required=True, help="pasta de build com ML_ODONTO_WITH_ONNX=ON")
    parser.add_argument("--model", default="unet_lesoes_v2")
    args = parser.parse_args()

    executable = next((args.build / name for name in ("ml-odonto-cli.exe", "ml-odonto-cli")
                       if (args.build / name).is_file()), None)
    if executable is None:
        raise SystemExit(f"executável não encontrado em {args.build}")
    destination = APP / "bin"
    destination.mkdir(exist_ok=True)
    for source in [executable, *args.build.glob("*.dll"), *args.build.glob("libonnxruntime*.so*")]:
        shutil.copy2(source, destination / source.name)
        print("copiado", destination / source.name)

    weights = APP / "modelos" / f"{args.model}.safetensors"
    onnx = weights.with_suffix(".onnx")
    if not onnx.is_file():
        if not weights.is_file():
            raise SystemExit(f"pesos não encontrados: {weights}")
        subprocess.run([sys.executable, str(Path(__file__).with_name("export_onnx.py")),
                        str(weights), str(onnx), "--verify"], check=True)
    print("modelo", onnx)


if __name__ == "__main__":
    main()
