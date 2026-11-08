"""Compacta a pasta ML ODONTO/ (montada por build_desktop.py) para a Release do GitHub.

Gera cpp/build/release/ML_ODONTO_Windows.zip com a pasta ML ODONTO/ dentro, confere a integridade e mostra o
SHA-256. O nome do arquivo é fixo para o link do README (releases/latest/download/...) continuar válido.

Uso: python cpp/tools/empacotar.py
"""

from __future__ import annotations

import hashlib
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "ML ODONTO"
TARGET = ROOT / "cpp" / "build" / "release" / "ML_ODONTO_Windows.zip"


def main() -> None:
    if not (SOURCE / "ML ODONTO.exe").is_file():
        raise SystemExit("monte o programa antes: python cpp/tools/build_desktop.py")
    if not (SOURCE / "modelos" / "unet_lesoes_v2.onnx").is_file():
        raise SystemExit("falta o modelo em ML ODONTO/modelos")
    TARGET.parent.mkdir(parents=True, exist_ok=True)
    files = sorted(p for p in SOURCE.rglob("*") if p.is_file())
    with zipfile.ZipFile(TARGET, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in files:
            archive.write(path, path.relative_to(ROOT).as_posix())
    with zipfile.ZipFile(TARGET) as archive:
        broken = archive.testzip()
    if broken:
        raise SystemExit(f"zip corrompido: {broken}")
    digest = hashlib.sha256(TARGET.read_bytes()).hexdigest()
    print(f"{TARGET} ({TARGET.stat().st_size / 1e6:.1f} MB, {len(files)} arquivos)\nSHA-256 {digest}")


if __name__ == "__main__":
    main()
