"""Gera o notebook do Colab embutindo comum.py, unet.py e treino_unet.py.

Rode de novo sempre que um desses arquivos mudar, para o Colab usar o mesmo código.
  python gerar_notebook.py        -> colab/Treino_UNet_Lesoes.ipynb (v1: 1400 px, Dice, 6.000 iterações)
  python gerar_notebook.py --v2   -> colab/Treino_UNet_v2_Lesoes.ipynb (2000 px, Tversky, 12.000 iterações)
"""
import json
import sys
from pathlib import Path

AQUI = Path(__file__).resolve().parent
V2 = "--v2" in sys.argv
if V2:
    DESTINO = AQUI.parent / "colab" / "Treino_UNet_v2_Lesoes.ipynb"
    PASTA_DRIVE, ITERACOES, EXTRA, TEMPO = "ML_Odonto_UNet_v2", 12000, "--largura 2000 --perda tversky --validar-cada 1000", "~1h30-2h"
    TITULO, ARQ_LOCAL = "Treino da U-Net v2 (lesões pequenas: 2000 px + perda de Tversky)", "unet_lesoes_v2.pt"
else:
    DESTINO = AQUI.parent / "colab" / "Treino_UNet_Lesoes.ipynb"
    PASTA_DRIVE, ITERACOES, EXTRA, TEMPO = "ML_Odonto_UNet", 6000, "", "~40-60 min"
    TITULO, ARQ_LOCAL = "Treino da U-Net", "unet_lesoes.pt"
URL_ORIGINAIS = "https://data.mendeley.com/public-files/datasets/kx52tk2ddj/files/0d016bd4-d5ac-4363-ac35-c74d87ba4022/file_downloaded"
URL_ANOTACOES = "https://data.mendeley.com/public-files/datasets/kx52tk2ddj/files/fa6e54cc-4094-4165-9987-3a8589213696/file_downloaded"


def md(texto):
    return {"cell_type": "markdown", "metadata": {}, "source": texto.strip("\n").splitlines(keepends=True)}


def code(texto):
    return {"cell_type": "code", "metadata": {}, "execution_count": None, "outputs": [],
            "source": texto.strip("\n").splitlines(keepends=True)}


def arquivo(nome):
    return code(f"%%writefile /content/scripts/{nome}\n" + (AQUI / nome).read_text(encoding="utf-8"))


celulas = [
    md(f"""
# {TITULO} — lesões periapicais (ML Odonto)

Treina a U-Net **só com o conjunto público de Hanói** (Mendeley, CC BY 4.0). Nenhuma radiografia
de paciente vem para o Colab: a avaliação e a aplicação rodam no computador local.

**Antes de começar:** menu *Ambiente de execução → Alterar o tipo de ambiente de execução → GPU T4*.
Depois *Ambiente de execução → Executar tudo*. O Google vai pedir permissão para acessar o Drive
(os checkpoints ficam em `MyDrive/{PASTA_DRIVE}`, para não perder o treino se a sessão cair).

Tempo esperado: ~10 min de download e preparação + {TEMPO} de treino.

**Se a sessão cair:** reconecte, rode as células de 1 a 6 de novo e, na célula 7, troque
`CONTINUAR = False` por `CONTINUAR = True`.

No fim, a última célula baixa `unet_lesoes.pt`: salve em `ML Odonto/Modelos/` com o nome `{ARQ_LOCAL}`.
"""),
    md("### 1. GPU"),
    code("!nvidia-smi -L"),
    md("### 2. Google Drive (onde ficam os checkpoints)"),
    code(f"""
from google.colab import drive
drive.mount('/content/drive')
SAIDA = '/content/drive/MyDrive/{PASTA_DRIVE}'
!mkdir -p "{{SAIDA}}" /content/scripts
"""),
    md("### 3–5. Código do projeto (gerado a partir de `scripts/`)"),
    arquivo("comum.py"),
    arquivo("unet.py"),
    arquivo("treino_unet.py"),
    md("### 6. Baixar e extrair o conjunto de Hanói (~850 MB)"),
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
    md(f"### 7. Treinar (o log mostra a perda e, a cada {1000 if V2 else 500} iterações, o Dice na validação)"),
    code(f"""
CONTINUAR = False
ITERACOES = {ITERACOES}
flag = '--continuar' if CONTINUAR else ''
!cd /content/scripts && python -u treino_unet.py --saida "{{SAIDA}}" --iteracoes {{ITERACOES}} --lote 8 --workers 2 {EXTRA} {{flag}}
"""),
    md(f"### 8. Baixar o modelo treinado (salve em `ML Odonto/Modelos/{ARQ_LOCAL}`)"),
    code("""
from google.colab import files
files.download(f'{SAIDA}/unet_lesoes.pt')
"""),
]

nb = {"cells": celulas, "metadata": {"accelerator": "GPU", "colab": {"provenance": []},
                                    "kernelspec": {"name": "python3", "display_name": "Python 3"}},
      "nbformat": 4, "nbformat_minor": 0}
DESTINO.parent.mkdir(exist_ok=True)
DESTINO.write_text(json.dumps(nb, ensure_ascii=False, indent=1), encoding="utf-8")
print(f"Gerado {DESTINO}")
