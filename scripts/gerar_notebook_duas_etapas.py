"""Gera colab/Treino_Duas_Etapas.ipynb, embutindo os scripts do projeto.

Rode de novo sempre que comum.py, unet.py, treino_unet.py, apices.py, duas_etapas.py
ou treino_duas_etapas.py mudarem, para o Colab usar exatamente o mesmo código.
"""
import json
from pathlib import Path

AQUI = Path(__file__).resolve().parent
DESTINO = AQUI.parent / "colab" / "Treino_Duas_Etapas.ipynb"
PASTA_DRIVE = "ML_Odonto_DuasEtapas"
URL_ORIGINAIS = "https://data.mendeley.com/public-files/datasets/kx52tk2ddj/files/0d016bd4-d5ac-4363-ac35-c74d87ba4022/file_downloaded"
URL_ANOTACOES = "https://data.mendeley.com/public-files/datasets/kx52tk2ddj/files/fa6e54cc-4094-4165-9987-3a8589213696/file_downloaded"
URL_DENTEX = "https://huggingface.co/datasets/ibrahimhamamci/DENTEX/resolve/main/DENTEX/training_data.zip"
SCRIPTS = ["comum.py", "unet.py", "treino_unet.py", "apices.py", "duas_etapas.py", "treino_duas_etapas.py"]


def md(texto):
    return {"cell_type": "markdown", "metadata": {}, "source": texto.strip("\n").splitlines(keepends=True)}


def code(texto):
    return {"cell_type": "code", "metadata": {}, "execution_count": None, "outputs": [],
            "source": texto.strip("\n").splitlines(keepends=True)}


def arquivo(nome):
    return code(f"%%writefile /content/scripts/{nome}\n" + (AQUI / nome).read_text(encoding="utf-8"))


celulas = [
    md(f"""
# Duas etapas — ápices (DENTEX) e lesões em recortes ampliados (Hanói) — ML Odonto

1. Treina um **localizador de ápices radiculares** com os contornos de dentes do DENTEX (634 panorâmicas, uso não comercial).
2. Usa o localizador para achar os ápices nas 3.924 panorâmicas de Hanói (Mendeley, CC BY 4.0).
3. Recorta 256 × 256 px em volta de cada ápice, na resolução original (~2800 px de largura).
4. Treina uma **U-Net de lesão** nesses recortes (perda de Tversky).

Só dados públicos vêm para o Colab; a avaliação nas radiografias de pacientes é feita no computador local.
Tudo fica em `MyDrive/{PASTA_DRIVE}`; se a sessão cair, rode tudo de novo: as fases prontas são puladas.

Tempo esperado (GPU T4): ~15 min de downloads, ~25 min de localizador, ~25 min de preparo dos recortes, ~35 min de segmentador.
"""),
    md("### 1. GPU"),
    code("!nvidia-smi -L\nimport os; print('CPUs:', os.cpu_count())"),
    md("### 2. Google Drive (onde ficam modelos e resultados)"),
    code(f"""
from google.colab import drive
drive.mount('/content/drive')
SAIDA = '/content/drive/MyDrive/{PASTA_DRIVE}'
!mkdir -p "{{SAIDA}}" /content/scripts
"""),
    md("### 3. Código do projeto (gerado a partir de `scripts/`)"),
    *[arquivo(n) for n in SCRIPTS],
    md("### 4. Baixar e extrair o conjunto de Hanói (~850 MB)"),
    code(f"""
%%bash
set -e
mkdir -p /content/Externos/Hanoi/imagens /content/Externos/Hanoi/anotacoes
cd /content/Externos/Hanoi
if [ ! -d "imagens/Original JPG Images" ]; then
  wget -q -O originais.rar "{URL_ORIGINAIS}"
  wget -q -O annots.rar "{URL_ANOTACOES}"
  which unrar > /dev/null || (apt-get -qq update && apt-get -qq install -y unrar > /dev/null)
  unrar x -o+ -inul originais.rar imagens/
  unrar x -o+ -inul annots.rar anotacoes/
  rm originais.rar annots.rar
fi
echo "imagens: $(ls 'imagens/Original JPG Images' | wc -l)  anotações: $(ls 'anotacoes/Image Annots' | wc -l)"
"""),
    md("### 5. Baixar só o conjunto de numeração de dentes do DENTEX (~1,7 GB, lido de dentro do zip remoto)"),
    code(f"""
!pip -q install fsspec aiohttp
import os, zipfile, time, fsspec
D = '/content/Externos/DENTEX'
os.makedirs(D + '/enumeracao', exist_ok=True)
z = zipfile.ZipFile(fsspec.open('{URL_DENTEX}', block_size=2**23).open())
pre = 'training_data/quadrant_enumeration/'
js = D + '/train_quadrant_enumeration.json'
if not os.path.exists(js):
    open(js, 'wb').write(z.read(pre + 'train_quadrant_enumeration.json'))
nomes = [n for n in z.namelist() if n.startswith(pre + 'xrays/') and n.endswith('.png')]
t = time.time()
for k, n in enumerate(nomes, 1):
    dst = D + '/enumeracao/' + n.split('/')[-1]
    if not os.path.exists(dst):
        open(dst, 'wb').write(z.read(n))
    if k % 100 == 0:
        print(k, len(nomes), f'{{time.time() - t:.0f}}s', flush=True)
print('DENTEX:', len(os.listdir(D + '/enumeracao')), 'imagens')
"""),
    md("### 6. Treinar as duas etapas (fases já prontas no Drive são puladas)"),
    code("""
!cd /content/scripts && python -u treino_duas_etapas.py --saida "{SAIDA}" --its1 3000 --its2 6000
"""),
    md("### 7. Baixar os modelos (salve em `ML Odonto/Modelos/duas_etapas/`)"),
    code("""
from google.colab import files
for nome in ('localizador_apices.pt', 'lesoes_recortes.pt', 'apices_hanoi.json'):
    files.download(f'{SAIDA}/{nome}')
"""),
]

nb = {"cells": celulas, "metadata": {"accelerator": "GPU", "colab": {"provenance": []},
                                    "kernelspec": {"name": "python3", "display_name": "Python 3"}},
      "nbformat": 4, "nbformat_minor": 0}
DESTINO.parent.mkdir(exist_ok=True)
DESTINO.write_text(json.dumps(nb, ensure_ascii=False, indent=1), encoding="utf-8")
print(f"Gerado {DESTINO}")
