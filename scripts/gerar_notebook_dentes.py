"""Gera colab/Treino_Classificador_Dentes.ipynb, embutindo os scripts do projeto.

Rode de novo sempre que comum.py, unet.py, apices.py, duas_etapas.py, dentes.py ou
treino_dentes.py mudarem, para o Colab usar exatamente o mesmo código.
"""
import json
from pathlib import Path

AQUI = Path(__file__).resolve().parent
DESTINO = AQUI.parent / "colab" / "Treino_Classificador_Dentes.ipynb"
PASTA_DRIVE = "ML_Odonto_Dentes"
PASTA_DUAS = "ML_Odonto_DuasEtapas"
URL_ORIGINAIS = "https://data.mendeley.com/public-files/datasets/kx52tk2ddj/files/0d016bd4-d5ac-4363-ac35-c74d87ba4022/file_downloaded"
URL_ANOTACOES = "https://data.mendeley.com/public-files/datasets/kx52tk2ddj/files/fa6e54cc-4094-4165-9987-3a8589213696/file_downloaded"
URL_DENTEX = "https://huggingface.co/datasets/ibrahimhamamci/DENTEX/resolve/main/DENTEX/training_data.zip"
SCRIPTS = ["comum.py", "unet.py", "apices.py", "duas_etapas.py", "dentes.py", "treino_dentes.py"]


def md(texto):
    return {"cell_type": "markdown", "metadata": {}, "source": texto.strip("\n").splitlines(keepends=True)}


def code(texto):
    return {"cell_type": "code", "metadata": {}, "execution_count": None, "outputs": [],
            "source": texto.strip("\n").splitlines(keepends=True)}


def arquivo(nome):
    return code(f"%%writefile /content/scripts/{nome}\n" + (AQUI / nome).read_text(encoding="utf-8"))


celulas = [
    md(f"""
# Classificador por dente — "este ápice tem lesão periapical?" — ML Odonto

1. Reaproveita o localizador de ápices e os ápices de Hanói da rodada anterior (`MyDrive/{PASTA_DUAS}`).
2. Detecta os ápices nas 705 panorâmicas do DENTEX com diagnósticos (cárie, lesão periapical, dente incluso).
3. Recorta 384 × 384 px em volta de cada ápice (a 2.800 px de largura; raiz sempre para cima) e rotula:
   lesão de Hanói ou dente com lesão periapical no DENTEX = positivo; demais ápices = negativo.
4. Treina uma ResNet34 que classifica cada recorte, reforçando lesões pequenas e o DENTEX.

Só dados públicos vêm para o Colab. Tudo fica em `MyDrive/{PASTA_DRIVE}`; se a sessão cair, rode tudo de novo.
Tempo esperado (GPU T4): ~15 min de downloads, ~5 min de ápices, ~15 min de recortes, ~40 min de treino.
"""),
    md("### 1. GPU"),
    code("!nvidia-smi -L\nimport os; print('CPUs:', os.cpu_count())"),
    md("### 2. Google Drive (onde ficam modelos e resultados)"),
    code(f"""
from google.colab import drive
drive.mount('/content/drive')
SAIDA = '/content/drive/MyDrive/{PASTA_DRIVE}'
DUAS = '/content/drive/MyDrive/{PASTA_DUAS}'
!mkdir -p "{{SAIDA}}" /content/scripts
!ls -la "{{DUAS}}"
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
    md("### 5. Baixar o conjunto de diagnósticos do DENTEX (705 panorâmicas, lido de dentro do zip remoto)"),
    code(f"""
!pip -q install fsspec aiohttp
import os, zipfile, time, fsspec
D = '/content/Externos/DENTEX'
os.makedirs(D + '/doenca', exist_ok=True)
z = zipfile.ZipFile(fsspec.open('{URL_DENTEX}', block_size=2**23).open())
todos = z.namelist()
js = [n for n in todos if n.endswith('train_quadrant_enumeration_disease.json')][0]
pre = js.rsplit('/', 1)[0] + '/'
open(D + '/train_quadrant_enumeration_disease.json', 'wb').write(z.read(js))
nomes = [n for n in todos if n.startswith(pre) and '/xrays/' in n and n.endswith('.png')]
t = time.time()
for k, n in enumerate(nomes, 1):
    dst = D + '/doenca/' + n.split('/')[-1]
    if not os.path.exists(dst):
        open(dst, 'wb').write(z.read(n))
    if k % 100 == 0:
        print(k, len(nomes), f'{{time.time() - t:.0f}}s', flush=True)
print('DENTEX diagnósticos:', len(os.listdir(D + '/doenca')), 'imagens')
"""),
    md("### 6. Treinar o classificador (fases já prontas no Drive são puladas)"),
    code("""
!cd /content/scripts && python -u treino_dentes.py --saida "{SAIDA}" --duas-etapas "{DUAS}" --its 5000
"""),
]
nb = {"cells": celulas, "metadata": {"accelerator": "GPU", "colab": {"provenance": []},
                                    "kernelspec": {"name": "python3", "display_name": "Python 3"}},
      "nbformat": 4, "nbformat_minor": 0}
DESTINO.parent.mkdir(exist_ok=True)
DESTINO.write_text(json.dumps(nb, ensure_ascii=False, indent=1), encoding="utf-8")
print(f"Gerado {DESTINO}")
