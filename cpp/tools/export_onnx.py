"""Exporta a U-Net v2 em safetensors para ONNX, incluindo sigmoid e espelhamento.

Uso:
    python cpp/tools/export_onnx.py \
      software/odontolesao/inst/modelos/unet_lesoes_v2.safetensors \
      software/odontolesao/inst/modelos/unet_lesoes_v2.onnx
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

import numpy as np
import torch
from safetensors.torch import load_file

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from unet import UNetResNet34  # noqa: E402


class InferenceGraph(torch.nn.Module):
    """Grafo implantável: probabilidade média da imagem e de seu espelhamento."""

    def __init__(self, model: torch.nn.Module) -> None:
        super().__init__()
        self.model = model

    def forward(self, image: torch.Tensor) -> torch.Tensor:
        direct = torch.sigmoid(self.model(image))
        mirrored = torch.sigmoid(self.model(torch.flip(image, dims=(-1,))))
        return (direct + torch.flip(mirrored, dims=(-1,))) / 2


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("weights", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--opset", type=int, default=18)
    parser.add_argument("--verify", action="store_true")
    args = parser.parse_args()

    model = UNetResNet34()
    model.load_state_dict(load_file(str(args.weights), device="cpu"), strict=True)
    graph = InferenceGraph(model.eval()).eval()
    example = torch.linspace(0, 1, 256 * 320, dtype=torch.float32).reshape(1, 1, 256, 320)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with torch.inference_mode():
        torch.onnx.export(
            graph,
            (example,),
            str(args.output),
            input_names=["image"],
            output_names=["probability"],
            dynamic_axes={
                "image": {2: "height", 3: "width"},
                "probability": {2: "height", 3: "width"},
            },
            opset_version=args.opset,
            do_constant_folding=True,
        )

    if args.verify:
        import onnxruntime as ort

        with torch.inference_mode():
            expected = graph(example).numpy()
        session = ort.InferenceSession(str(args.output), providers=["CPUExecutionProvider"])
        actual = session.run(["probability"], {"image": example.numpy()})[0]
        maximum_error = float(np.max(np.abs(expected - actual)))
        print(f"erro máximo PyTorch × ONNX: {maximum_error:.8g}")
        if maximum_error > 1e-4:
            raise SystemExit("exportação ONNX fora da tolerância")

    print(args.output)


if __name__ == "__main__":
    main()
