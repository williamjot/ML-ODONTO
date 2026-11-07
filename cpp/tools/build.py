"""Compila o motor C++ com ONNX Runtime, roda os testes e instala no aplicativo R.

Não precisa de Visual Studio: usa o compilador Zig (clang) e o CMake/Ninja instalados pelo pip.
    python -m pip install cmake ninja ziglang onnx onnxruntime torch safetensors
    python cpp/tools/build.py

O ONNX Runtime oficial (Microsoft, GitHub) é baixado uma vez para cpp/.deps.
"""

from __future__ import annotations

import argparse
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path

CPP = Path(__file__).resolve().parents[1]
DEPS = CPP / ".deps"
WINDOWS = os.name == "nt"


def run(*command: object) -> None:
    print(">", " ".join(str(c) for c in command), flush=True)
    subprocess.run([str(c) for c in command], check=True)


def zig_wrappers() -> dict[str, Path]:
    try:
        import ziglang
    except ImportError:
        raise SystemExit("instale o compilador: python -m pip install ziglang cmake ninja")
    zig = Path(ziglang.__file__).with_name("zig.exe" if WINDOWS else "zig")
    folder = DEPS / "zig"
    folder.mkdir(parents=True, exist_ok=True)
    tools = {}
    for name, sub in [("cc", "cc"), ("cxx", "c++"), ("ar", "ar"), ("ranlib", "ranlib"), ("rc", "rc")]:
        wrapper = folder / (f"zig{name}.cmd" if WINDOWS else f"zig{name}")
        if WINDOWS:
            wrapper.write_text(f'@"{zig}" {sub} %*\r\n', encoding="ascii")
        else:
            wrapper.write_text(f'#!/bin/sh\nexec "{zig}" {sub} "$@"\n', encoding="ascii")
            wrapper.chmod(0o755)
        tools[name] = wrapper
    return tools


def onnxruntime(version: str) -> Path:
    system = {"Windows": "win", "Linux": "linux", "Darwin": "osx"}[platform.system()]
    name = f"onnxruntime-{system}-x64-{version}"
    root = DEPS / name
    if (root / "include" / "onnxruntime_cxx_api.h").is_file():
        return root
    archive = DEPS / (name + (".zip" if system == "win" else ".tgz"))
    if not archive.is_file():
        url = f"https://github.com/microsoft/onnxruntime/releases/download/v{version}/{archive.name}"
        print("baixando", url, flush=True)
        DEPS.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve(url, archive)
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as source:
            source.extractall(DEPS)
    else:
        with tarfile.open(archive) as source:
            source.extractall(DEPS, filter="data")
    return root


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--onnxruntime", default="1.31.1", help="versão do ONNX Runtime")
    parser.add_argument("--system-compiler", action="store_true",
                        help="usa o compilador do sistema (MSVC, GCC) em vez do Zig")
    parser.add_argument("--no-install", action="store_true", help="não copia para o aplicativo R")
    args = parser.parse_args()

    build = CPP / "build" / ("onnx" if args.system_compiler else "zig-onnx")
    configure = ["cmake", "-S", CPP, "-B", build, "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
                 "-DML_ODONTO_WITH_ONNX=ON", f"-DONNXRUNTIME_ROOT={onnxruntime(args.onnxruntime).as_posix()}"]
    if not args.system_compiler:
        tools = zig_wrappers()
        configure += [f"-DCMAKE_C_COMPILER={tools['cc'].as_posix()}", f"-DCMAKE_CXX_COMPILER={tools['cxx'].as_posix()}",
                      f"-DCMAKE_AR={tools['ar'].as_posix()}", f"-DCMAKE_RANLIB={tools['ranlib'].as_posix()}"]
        if WINDOWS:
            configure.append(f"-DCMAKE_RC_COMPILER={tools['rc'].as_posix()}")
    if shutil.which("ninja") is None:
        raise SystemExit("instale o Ninja: python -m pip install ninja")
    run(*configure)
    run("cmake", "--build", build)
    run("ctest", "--test-dir", build, "--output-on-failure")
    if not args.no_install:
        run(sys.executable, CPP / "tools" / "install_into_app.py", "--build", build)
        print("\nPronto. Reinstale o pacote R: R CMD INSTALL --no-test-load software/odontolesao")


if __name__ == "__main__":
    main()
