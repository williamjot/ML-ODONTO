---
name: odonto-standalone-windows
description: Empacotar o OdontoLesão como programa standalone para Windows (R portátil, renv, libtorch embutido, lançador sem janela de console, instalador Inno Setup, atualização sem perder dados). Use ao preparar uma versão para distribuir ou ao mexer em software/distribuicao/.
---

# Distribuição standalone (Windows)

## Estrutura da distribuição

```
software/distribuicao/
  build.R                    monta tudo a partir do pacote e do renv.lock
  OdontoLesao.iss            script do Inno Setup
  lancador/
    OdontoLesao.vbs          abre R portátil sem janela, chama iniciar.R
    iniciar.R                .libPaths(lib local); odontolesao::run_app(abrir = TRUE)
    icone.ico
  saida/OdontoLesao/         pasta pronta (vai para o instalador)
    R/                       R portátil 4.6.x (sem instalação)
    library/                 pacotes do renv.lock + odontolesao + torch/libtorch (lantern)
    quarto/                  opcional, para relatórios PDF
    OdontoLesao.vbs, iniciar.R, icone.ico, VERSAO
```

## Passos do build.R

1. `renv::restore()` no projeto do pacote; `renv::snapshot()` só quando dependências mudarem.
2. Baixar/copiar R portátil (instalador oficial do R com `/DIR=` numa pasta local e `/COMPONENTS=main,x64`).
3. Instalar os pacotes do lock em `saida/OdontoLesao/library` com o R portátil
   (`install.packages(..., lib = )` ou `renv::restore(library = )`), incluindo Bioconductor (`EBImage`).
4. `torch::install_torch()` apontando `TORCH_HOME` para dentro da library, para o libtorch ir junto
   (≈200 MB); validar com `torch::torch_tensor(1)` no R portátil.
5. Instalar o pacote `odontolesao` a partir de `software/odontolesao`.
6. Renderizar os manuais (Quarto) para `inst/manuais/html` antes de instalar o pacote.
7. Copiar lançador, ícone e escrever `VERSAO` (versão do DESCRIPTION + data).
8. Rodar o teste de fumaça: abrir o app com `abrir = FALSE`, fazer GET em `/` e encerrar.
9. Compilar o instalador: `ISCC.exe OdontoLesao.iss`.

## Lançador

- `.vbs` com `WshShell.Run """R\bin\Rscript.exe"" iniciar.R", 0, False` (0 = sem janela).
- `iniciar.R`: escolhe porta livre (`httpuv::randomPort()`), escreve log em
  `Documentos/OdontoLesao/logs/`, abre o navegador padrão em modo app quando possível
  (`msedge --app=URL` ou `chrome --app=URL`, com recuo para `browseURL`).
- Impedir duas instâncias: arquivo de trava com a porta; se existir e responder, só abre o navegador.
- Encerrar o R quando a última aba fechar (`session$onSessionEnded` + contador).

## Instalador (Inno Setup)

- `PrivilegesRequired=lowest`, instala em `{localappdata}\OdontoLesao` (sem administrador).
- Atalhos no Menu Iniciar e na Área de Trabalho com o ícone; desinstalador.
- **Nunca** apagar `Documentos\OdontoLesao` (dados do usuário) na atualização ou na desinstalação;
  perguntar antes.
- Nome do arquivo: `OdontoLesao_<versao>_setup.exe`. Tamanho esperado: 400–600 MB por causa do libtorch.

## Antes de distribuir

- Testar num Windows limpo (máquina virtual ou outro PC) sem R instalado.
- Conferir que nada acessa a rede (desligar a internet e usar o programa inteiro).
- Pesos dos modelos e manuais dentro do pacote; nenhuma radiografia de paciente dentro do instalador.
- O DENTEX é CC BY-NC-SA: o programa e os modelos derivados não podem ter uso comercial; deixar isso em
  "Sobre" e na licença.
