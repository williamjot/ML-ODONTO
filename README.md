<div align="center">

<img src="software/odontolesao/inst/app/www/logo.svg" width="72" alt="">

# ML ODONTO

**Detecção de lesões periapicais em radiografias panorâmicas com poucos exemplos locais**

Redes neurais treinadas em dados públicos · revisão por especialistas · software local em R

![R](https://img.shields.io/badge/R-4.6-276DC3?logo=r&logoColor=white)
![Python](https://img.shields.io/badge/Python-3.11-3776AB?logo=python&logoColor=white)
![PyTorch](https://img.shields.io/badge/PyTorch%20%7C%20R%20torch-EE4C2C?logo=pytorch&logoColor=white)
![Licença](https://img.shields.io/badge/licen%C3%A7a-acad%C3%AAmica%2C%20n%C3%A3o%20comercial-lightgrey)

</div>

---

## Sumário

- [Sobre o projeto](#sobre-o-projeto)
- [Principais resultados](#principais-resultados)
- [O software ML ODONTO](#o-software-ml-odonto)
- [Migração incremental para C++](#migração-incremental-para-c)
- [Estrutura do repositório](#estrutura-do-repositório)
- [Instalação](#instalação)
- [Uso](#uso)
- [Pipeline de pesquisa (Python)](#pipeline-de-pesquisa-python)
- [Testes e reprodutibilidade](#testes-e-reprodutibilidade)
- [Dados, privacidade e ética](#dados-privacidade-e-ética)
- [Licença](#licença)

---

## Sobre o projeto

Lesões periapicais aparecem na radiografia panorâmica como áreas radiolúcidas pequenas e de contraste
baixo, e parte delas passa despercebida na leitura de rotina. Este projeto investiga se um detector
automático pode ser construído para o acervo de um serviço que dispõe de **apenas 20 panorâmicas com
lesões contornadas por especialista (45 lesões)** e de **422 panorâmicas sem marcação**.

Todos os modelos foram treinados apenas com **dados públicos** — 3.924 panorâmicas de Hanói com lesões
delimitadas por caixas e 705 panorâmicas do DENTEX — e avaliados nas radiografias locais, que nunca saem
do computador. Foram comparados:

| Abordagem | Ideia |
|---|---|
| Random Forest por pixel | 38 atributos de intensidade, textura e contexto (linha de base) |
| U-Net v1 | codificador ResNet34, 1.400 px, BCE + Dice |
| **U-Net v2** | 2.000 px, perda de Tversky (pesa mais a lesão perdida) — **modelo escolhido** |
| Duas etapas | localizador de ápices (DENTEX) + segmentação de recortes ampliados |
| Classificador por dente | um recorte por ápice, rótulos de Hanói e dos diagnósticos do DENTEX |

Para transformar o modelo em ferramenta de trabalho, o projeto inclui o **ML ODONTO**, um programa local
em R no qual o dentista revisa as detecções, audita as lesões não encontradas e acompanha os resultados.

<div align="center"><img src="docs/imagens/fluxo_trabalho.png" width="820" alt="Fluxo de trabalho do projeto"></div>

## Principais resultados

Validação nas 20 panorâmicas marcadas (45 lesões; cada paciente avaliado por um modelo que não o viu):

| Modelo | Sensibilidade (≤ 0,5 FP/img) | Sensibilidade no limiar | Precisão | Dice |
|---|---|---|---|---|
| Random Forest (melhor) | 4% | 16% | 6% | 0,04 |
| U-Net v1 | 69% | 64% | 100% | 0,57 |
| **U-Net v2** | **76%** | **69%** (IC 95% 54–80%) | **100%** | 0,58 |
| Duas etapas | 64% | 64% | 76% | 0,53 |
| Classificador por dente | — | 56% (critério por ponto) | 54% | — |

- A U-Net v2 encontrou 31/45 lesões **sem nenhum falso positivo** e 34/45 com até 0,5 falso positivo por imagem.
- **13 das 45 lesões não foram encontradas por nenhum modelo**: são pequenas e, em sua maioria, maxilares. Os
  testes indicam que o limite é a representação desse tipo de lesão nos dados de treinamento, e não a
  arquitetura ou a resolução.
- Aplicada às 422 panorâmicas sem marcação, a U-Net v2 assinalou 807 lesões em 407 imagens; a precisão nesse
  acervo está sendo medida pela revisão especializada no próprio software.

<div align="center"><img src="docs/imagens/validacao_modelos.png" width="820" alt="Validação dos modelos"></div>

## O software ML ODONTO

Programa de computador **nativo** (C++20 e Qt 6), com aparência de ambiente de trabalho: painéis com abas e
divisórias arrastáveis, barra de ferramentas e barra de status. Abre na própria janela, não precisa de R
nem de Python e não faz nenhuma chamada à internet. É uma pasta (`ML ODONTO/`, ~150 MB) que se copia para
qualquer Windows 10/11; os dados ficam em `Documentos\OdontoLesao`.

| Tela | Para quê |
|---|---|
| **Início** | Fluxo de trabalho com os números atuais, painel "Ambiente" com as métricas, histórico de atividades |
| **Revisão** | Visualizador com zoom, contraste e modo cego; confirmar / rejeitar / dúvida (teclas 1/2/3); desenhar lesões não detectadas (M); amostra aleatória de 60 imagens para estimar o desempenho sem viés |
| **Auditoria** | Cada lesão de referência que o modelo não encontrou, ampliada, para o especialista julgar se é visível |
| **Resultados** | Curvas FROC, métricas com IC 95%, mapa de lesões por modelo, sensibilidade corrigida pela auditoria, concordância entre revisores (kappa) |
| **Aplicar** | Roda a U-Net v2 em novas radiografias, em segundo plano, com barra de progresso; as detecções vão direto para a revisão |
| **Ajuda** | Manual do dentista e atalhos |

A rede roda com o ONNX Runtime: o pré-processamento (redução por área e CLAHE), a U-Net com espelhamento
e a detecção reproduzem o pipeline Python **lesão a lesão** nas 20 panorâmicas marcadas (31/45, 0 falso
positivo, correlação ≥ 0,998, Dice ≥ 0,988), a ~11 s por radiografia num PC de 8 GB. Detalhes em
[`docs/EQUIVALENCIA_CPP.md`](docs/EQUIVALENCIA_CPP.md) e [`docs/MIGRACAO_CPP.md`](docs/MIGRACAO_CPP.md).

### Versão em R (referência)

O mesmo programa existe como pacote R (Shiny, [`software/odontolesao`](software/odontolesao)), que foi a
primeira versão e continua como implementação de referência: usa **o mesmo banco** e pode ser aberto no
lugar do nativo (`Rscript -e "odontolesao::run_app()"`). A equivalência R × Python está em
[`docs/EQUIVALENCIA_R_PYTHON.md`](docs/EQUIVALENCIA_R_PYTHON.md).

## Estrutura do repositório

```
ML-ODONTO/
├── README.md                     este arquivo
├── LICENSE                       uso acadêmico e não comercial
├── ML ODONTO/                    o programa pronto (montado por cpp/tools/build_desktop.py, fora do git)
├── Revisar_lesoes.bat           ferramenta de revisão antiga (Python), substituída pelo ML ODONTO
│
├── software/                     ── versão em R do programa (referência)
│   ├── instalar_dependencias.R   instala pacotes, libtorch e o programa
│   └── odontolesao/              pacote R
│       ├── R/                    módulos das telas (mod_*.R) e núcleo (fct_*.R)
│       ├── inst/app/www/         visor de radiografias (JS), estilos, logotipo
│       ├── inst/manuais/         manual do dentista (Markdown)
│       ├── inst/modelos/         registro dos modelos (pesos à parte, ver LEIAME.md)
│       ├── inst/sql/             migrações do banco SQLite
│       └── tests/                testes unitários e teste de equivalência R × Python
│
├── cpp/                          ── programa nativo (C++20, Qt 6, ONNX Runtime)
│   ├── desktop/                  as telas: Início, Revisão, Auditoria, Resultados, Aplicar, Ajuda
│   ├── include/, src/            núcleo: pré-processamento, detecção, pipeline, CLI
│   ├── app/                      ícone, manifesto e versão do .exe
│   ├── tests/                    testes unitários
│   ├── third_party/stb/          leitura e gravação de PNG/JPEG (domínio público)
│   └── tools/                    build, exportação ONNX, instalação no app e validação
│
├── scripts/                      ── pipeline de pesquisa (Python)
│   ├── requirements.txt
│   ├── extrair_pdf.py            marcações do PDF → máscaras
│   ├── comum.py, unet.py, deteccao.py, apices.py, duas_etapas.py, dentes.py
│   ├── treinar.py, treino_unet.py, treino_duas_etapas.py, treino_dentes.py
│   ├── avaliar_unet.py, avaliar_duas_etapas.py, avaliar_dentes.py, comparar.py
│   ├── aplicar.py                aplicação em lote (versão Python)
│   ├── exportar_figuras.py, figuras_radiografias.py
│   └── gerar_notebook*.py        geram os notebooks do Colab a partir dos scripts
├── colab/                        notebooks de treino no Google Colab (GPU)
├── figuras_R/                    figuras quantitativas em R (estilo Nature)
├── docs/                         manual do dentista, plano do software, equivalência, imagens
├── Modelos/                      métricas e registros de treino (pesos fora do repositório)
├── Resultados/                   comparação e gráficos agregados (dados de pacientes fora)
├── Externos/                     conjuntos públicos (baixar da fonte, ver LEIAME.md)
└── .claude/skills/               guias de desenvolvimento do software (Claude Code)
```

Pastas **não versionadas** (dados de pacientes): `Dados/`, `HUB/`, `Treinamento/`, `Revisao/` (respostas),
partes de `Resultados/`. Veja [Dados, privacidade e ética](#dados-privacidade-e-ética).

## Instalação

**Para usar:** Windows 10/11 e 8 GB de RAM. Copie a pasta `ML ODONTO/` para o computador e dê dois cliques
em `ML ODONTO.exe` — não é preciso instalar mais nada.

**Para gerar a pasta a partir do código** (uma vez; baixa o Qt 6.8 e o ONNX Runtime, ~2 GB):

1. Clone o repositório:
   ```bash
   git clone https://github.com/williamjot/ML-ODONTO.git
   cd ML-ODONTO
   ```
2. Coloque os **pesos dos modelos** (`unet_lesoes_v2.safetensors` e demais, distribuídos à parte — veja
   [`software/odontolesao/inst/modelos/LEIAME.md`](software/odontolesao/inst/modelos/LEIAME.md)) em
   `software/odontolesao/inst/modelos/`.
3. Monte o programa:
   ```bash
   python -m pip install aqtinstall cmake ninja onnx onnxruntime torch safetensors
   python cpp/tools/build_desktop.py
   ```
   O resultado é a pasta **`ML ODONTO/`** na raiz do projeto.
4. Abra com dois cliques em **`ML ODONTO/ML ODONTO.exe`**.

Para a versão em R: `Rscript software/instalar_dependencias.R` e depois `Rscript -e "odontolesao::run_app()"`.

Os dados ficam em `Documentos\OdontoLesao` — banco SQLite, cópias reduzidas das imagens e backups diários.
Num computador novo, o banco é criado vazio e as radiografias entram pela tela **Aplicar**. A importação do
acervo do projeto (422 panorâmicas e a referência das 20 marcadas) é feita uma vez pela versão em R.

## Uso

- **Dentistas:** leia o [**Manual do dentista**](docs/MANUAL_DENTISTA.md) (também disponível no programa, aba
  Ajuda). Em resumo: revise primeiro as 60 imagens da amostra aleatória, use **H** para olhar sem as
  marcações, decida cada detecção com **1/2/3** e desenhe com **M** as lesões que o programa não marcou.
- **Pesquisadores:** a tela **Resultados** mostra as métricas atualizadas a cada revisão; os gráficos podem
  ser baixados em PNG. O plano completo do software está em [`docs/PLANO_SOFTWARE.md`](docs/PLANO_SOFTWARE.md).

## Pipeline de pesquisa (Python)

```bash
pip install -r scripts/requirements.txt
```

| Etapa | Script | Observação |
|---|---|---|
| Marcações do PDF → máscaras | `extrair_pdf.py` | 45 lesões em 20 pacientes |
| Random Forest por pixel | `treinar.py` | linha de base, CPU |
| U-Net v1 / v2 | `treino_unet.py` (via `colab/Treino_UNet*_Lesoes.ipynb`) | GPU no Google Colab, só dados públicos |
| Duas etapas | `treino_duas_etapas.py` (via `colab/Treino_Duas_Etapas.ipynb`) | localizador de ápices + recortes |
| Classificador por dente | `treino_dentes.py` (via `colab/Treino_Classificador_Dentes.ipynb`) | Hanói + diagnósticos do DENTEX |
| Avaliação local | `avaliar_unet.py`, `avaliar_duas_etapas.py`, `avaliar_dentes.py`, `comparar.py` | nas 20 panorâmicas marcadas |
| Figuras | `exportar_figuras.py` → `figuras_R/*.R`; `figuras_radiografias.py` | gráficos em R, figuras com radiografias em Python |

Os notebooks do Colab são **gerados** pelos `gerar_notebook*.py`, que embutem o código de `scripts/`,
garantindo que o treino na nuvem use exatamente o mesmo código. Conjuntos externos: veja
[`Externos/LEIAME.md`](Externos/LEIAME.md).

## Testes e reprodutibilidade

```bash
# testes unitários do pacote (35 testes)
Rscript -e "testthat::test_local('software/odontolesao')"

# equivalência R × Python nas 20 panorâmicas marcadas (~10 min; exige os dados locais)
Rscript software/odontolesao/tests/equivalencia.R

# equivalência do motor C++ nas mesmas 20 panorâmicas (~5 min; exige os dados locais)
python cpp/tools/validate_pipeline.py --cli cpp/build/zig-onnx/ml-odonto-cli.exe
```

Os testes unitários do C++ rodam a cada `python cpp/tools/build.py` e na integração contínua do GitHub
(Linux e Windows).

## Dados, privacidade e ética

- As radiografias de pacientes **nunca** são enviadas à nuvem: só os conjuntos públicos vão ao Colab; a
  avaliação, a aplicação e a revisão rodam no computador local.
- Nada que identifique pacientes é versionado (veja o `.gitignore`). Gráficos e relatórios usam
  identificadores anônimos (A001…, 01–20).
- Conjuntos públicos: **Hanói** — Panoramic radiographs with periapical lesions, Mendeley Data v3,
  [doi:10.17632/kx52tk2ddj.3](https://doi.org/10.17632/kx52tk2ddj.3) (CC BY 4.0); **DENTEX** — Hamamci et al.,
  arXiv:2305.19112 (CC BY-NC-SA 4.0).
- O software apoia a revisão por especialistas e **não substitui o diagnóstico clínico**.

## Licença

Uso acadêmico e não comercial — veja [`LICENSE`](LICENSE). Modelos derivados do DENTEX seguem a licença
CC BY-NC-SA 4.0.

---

<div align="center">
<sub>William Jota · Laboratório de Estudos Eletromagnéticos (LEMAG) · Universidade de Brasília</sub>
</div>
