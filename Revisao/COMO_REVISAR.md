> **Ferramenta antiga (Python), mantida só como referência.** Use o programa **ML ODONTO**
> (`Abrir_ML_ODONTO.bat`); o manual atualizado está em [`docs/MANUAL_DENTISTA.md`](../docs/MANUAL_DENTISTA.md).

# Como revisar as detecções

1. Dê dois cliques em **`Revisar_lesoes.bat`** (na pasta *ML Odonto*). O navegador abre a ferramenta.
   Deixe a janela preta aberta enquanto revisa; para terminar, feche-a.
   A ferramenta roda só neste computador: nenhuma imagem é enviada para a internet.
2. Escreva seu nome no campo **Revisor** (fica lembrado).
3. Para cada imagem:
   - Cada contorno laranja numerado é uma lesão que a U-Net encontrou. Para cada uma, escolha
     **✓ Confirma**, **✗ Rejeita** ou **? Dúvida**. Clique no contorno ou use **Tab** para passar de uma à outra
     e as teclas **1 / 2 / 3**.
   - Se houver lesão que a U-Net **não** marcou: clique em **✏️ Marcar lesão não detectada** (ou tecla **M**)
     e desenhe o contorno segurando o botão do mouse.
   - Se a radiografia não permitir avaliação, marque **Imagem inadequada**.
   - Clique em **✓ Concluir** (ou **Enter**) para ir à próxima.
4. Zoom com a roda do mouse, arrastar para mover, **H** esconde as marcações para ver a imagem limpa,
   **0** volta ao tamanho inteiro; contraste e brilho no painel direito.

Tudo é salvo automaticamente em `Revisao/revisoes/`. Pode parar e continuar depois.

**Ordem:** a lista começa pelas **60 imagens da amostra aleatória** (sorteio fixo): revise todas primeiro,
porque só elas dão a precisão real sem viés. Depois vêm as imagens com 4 ou mais detecções (onde alarmes
falsos são mais prováveis), as sem nenhuma detecção (onde lesões podem ter escapado) e as demais.
As detecções mostradas são as da U-Net v2 (`Resultados/aplicacao_unet_v2`). Guia ilustrado para o
revisor: `Revisao/GUIA_DO_DENTISTA.html`.

**Dica para não ser influenciado pela máquina:** aperte **H** ao abrir cada imagem, procure as lesões
você mesmo e só depois mostre as marcações da U-Net.

## Depois da revisão

```
python scripts/consolidar_revisao.py
```

gera `Revisao/estatisticas.md` (precisão real, sensibilidade estimada, falsos positivos por imagem, com
IC 95%), as tabelas e as novas máscaras de treino (`Revisao/mascaras/`, `Revisao/negativos/`).
