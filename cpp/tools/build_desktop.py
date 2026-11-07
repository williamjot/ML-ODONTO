"""Compila o ML ODONTO nativo (Qt 6, sem R) e monta a pasta distribuível.

Baixa uma vez para cpp/.deps o Qt 6.8 LTS com MinGW (só módulos LGPL) pelo aqtinstall, o ONNX Runtime
oficial e compila com o GCC do Qt. O resultado é a pasta `ML ODONTO/` na raiz do projeto, com o programa,
as bibliotecas e o modelo: copie a pasta para qualquer Windows 10/11 e abra `ML ODONTO.exe`.

    python -m pip install aqtinstall cmake ninja onnx onnxruntime torch safetensors
    python cpp/tools/build_desktop.py
"""

from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build import DEPS, onnxruntime, run  # noqa: E402

CPP = Path(__file__).resolve().parents[1]
ROOT = CPP.parent
QT_VERSION = "6.8.3"


def short(path: Path) -> str:
    """Nome curto 8.3 no Windows: o windres do MinGW não aceita espaços no próprio caminho."""
    import ctypes

    buffer = ctypes.create_unicode_buffer(1024)
    ctypes.windll.kernel32.GetShortPathNameW(str(path), buffer, 1024)
    return buffer.value or str(path)


def qt() -> Path:
    root = DEPS / "Qt"
    prefix = root / QT_VERSION / "mingw_64"
    if not (prefix / "bin" / "windeployqt.exe").is_file():
        run(sys.executable, "-m", "aqt", "install-qt", "windows", "desktop", QT_VERSION, "win64_mingw",
            "-m", "qtimageformats", "--outputdir", root)
    if not (root / "Tools" / "mingw1310_64" / "bin" / "g++.exe").is_file():
        run(sys.executable, "-m", "aqt", "install-tool", "windows", "desktop", "tools_mingw1310",
            "qt.tools.win64_mingw1310", "--outputdir", root)
    return root


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--onnxruntime", default="1.31.1")
    parser.add_argument("--output", type=Path, default=ROOT / "ML ODONTO")
    args = parser.parse_args()

    root = qt()
    prefix = root / QT_VERSION / "mingw_64"
    gcc = root / "Tools" / "mingw1310_64" / "bin"
    ort = onnxruntime(args.onnxruntime)
    build = CPP / "build" / "desktop"
    if not (build / "build.ninja").is_file():
        run("cmake", "-S", CPP, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
            f"-DCMAKE_C_COMPILER={(gcc / 'gcc.exe').as_posix()}", f"-DCMAKE_CXX_COMPILER={(gcc / 'g++.exe').as_posix()}",
            f"-DCMAKE_RC_COMPILER={Path(short(gcc / 'windres.exe')).as_posix()}",
            f"-DCMAKE_PREFIX_PATH={prefix.as_posix()}", "-DML_ODONTO_BUILD_DESKTOP=ON",
            "-DML_ODONTO_WITH_ONNX=ON", f"-DONNXRUNTIME_ROOT={ort.as_posix()}")
    run("cmake", "--build", build)

    # pasta distribuível
    out = args.output
    if out.exists():
        shutil.rmtree(out)
    (out / "modelos").mkdir(parents=True)
    shutil.copy2(build / "desktop" / "ML ODONTO.exe", out)
    for dll in (ort / "lib").glob("*.dll"):
        shutil.copy2(dll, out)
    env_path = f"{gcc};{prefix / 'bin'}"
    subprocess.run([str(prefix / "bin" / "windeployqt.exe"), "--release", "--compiler-runtime", "--no-translations",
                    "--no-system-d3d-compiler", "--no-opengl-sw", str(out / "ML ODONTO.exe")],
                   check=True, env={**__import__("os").environ, "PATH": env_path})
    model = ROOT / "software" / "odontolesao" / "inst" / "modelos" / "unet_lesoes_v2.onnx"
    if not model.is_file():
        run(sys.executable, CPP / "tools" / "export_onnx.py", model.with_suffix(".safetensors"), model, "--verify")
    shutil.copy2(model, out / "modelos")
    size = sum(f.stat().st_size for f in out.rglob("*") if f.is_file()) / 1e6
    print(f"\nPronto: {out} ({size:.0f} MB). Abra 'ML ODONTO.exe'.")


if __name__ == "__main__":
    main()
