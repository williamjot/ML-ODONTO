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


def fetch(url: str, archive: Path, destination: Path) -> None:
    """Baixa (uma vez) e extrai um .zip/.nupkg/.tgz/.tar.gz em `destination`."""
    if not archive.is_file():
        print("baixando", url, flush=True)
        DEPS.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve(url, archive)
    if archive.suffix in (".zip", ".nupkg"):
        with zipfile.ZipFile(archive) as source:
            source.extractall(destination)
    else:
        with tarfile.open(archive) as source:
            source.extractall(destination, filter="data")


def onnxruntime(version: str) -> Path:
    system = {"Windows": "win", "Linux": "linux", "Darwin": "osx"}[platform.system()]
    name = f"onnxruntime-{system}-x64-{version}"
    root = DEPS / name
    if not (root / "include" / "onnxruntime_cxx_api.h").is_file():
        archive = DEPS / (name + (".zip" if system == "win" else ".tgz"))
        fetch(f"https://github.com/microsoft/onnxruntime/releases/download/v{version}/{archive.name}",
              archive, DEPS)
    return root


def webview(version: str, sdk_version: str) -> tuple[Path, Path]:
    """Biblioteca webview (MIT) e cabeçalhos do SDK do WebView2 (Microsoft, NuGet)."""
    include = DEPS / f"webview-{version}" / "core" / "include"
    if not (include / "webview" / "webview.h").is_file():
        fetch(f"https://github.com/webview/webview/archive/refs/tags/{version}.tar.gz",
              DEPS / f"webview-{version}.tar.gz", DEPS)
    sdk = DEPS / f"webview2-sdk-{sdk_version}"
    if not (sdk / "build" / "native" / "include" / "WebView2.h").is_file():
        fetch("https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/"
              f"{sdk_version}/microsoft.web.webview2.{sdk_version}.nupkg",
              DEPS / f"webview2-sdk-{sdk_version}.nupkg", sdk)
    return include, sdk / "build" / "native" / "include"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--onnxruntime", default="1.31.1", help="versão do ONNX Runtime")
    parser.add_argument("--system-compiler", action="store_true",
                        help="usa o compilador do sistema (MSVC, GCC) em vez do Zig")
    parser.add_argument("--webview", default="0.12.0", help="versão da biblioteca webview")
    parser.add_argument("--webview2-sdk", default="1.0.4258.31", help="versão do SDK do WebView2")
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
    if WINDOWS:
        webview_include, webview2_include = webview(args.webview, args.webview2_sdk)
        configure += ["-DML_ODONTO_BUILD_APP=ON", f"-DWEBVIEW_INCLUDE={webview_include.as_posix()}",
                      f"-DWEBVIEW2_INCLUDE={webview2_include.as_posix()}"]
    if shutil.which("ninja") is None:
        raise SystemExit("instale o Ninja: python -m pip install ninja")
    run(*configure)
    run("cmake", "--build", build)
    run("ctest", "--test-dir", build, "--output-on-failure")
    if not args.no_install:
        run(sys.executable, CPP / "tools" / "install_into_app.py", "--build", build)
        app = build / "ML ODONTO.exe"
        if app.is_file():
            shutil.copy2(app, CPP.parent / app.name)
            print("programa:", CPP.parent / app.name)
        print("\nPronto. Reinstale o pacote R: R CMD INSTALL --no-test-load software/odontolesao")


if __name__ == "__main__":
    main()
