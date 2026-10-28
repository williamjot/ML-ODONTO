# Manual do dentista — ML ODONTO

O **ML ODONTO** mostra, em radiografias panorâmicas, as lesões periapicais que uma rede neural encontrou e
pede o seu julgamento clínico. Suas respostas servem para duas coisas: **medir quanto o programa acerta de
verdade** e **ensiná-lo a encontrar as lesões que hoje escapam**, sobretudo as pequenas e as da maxila.

> O programa apoia a leitura e **não substitui o diagnóstico**. Julgue sempre o que é visível na panorâmica.
> Tudo roda neste computador: nenhuma radiografia é enviada para a internet.

---

## 1. Abrir o programa

1. Na pasta do projeto, dê dois cliques em **`Abrir_ML_ODONTO.bat`**.
2. Abre-se uma janela preta (deixe-a aberta enquanto usa o programa) e, em seguida, o navegador com o ML ODONTO.
3. Na primeira vez, informe o **seu nome**. Cada revisor tem as próprias respostas; o nome aparece no canto
   superior direito e pode ser trocado em **Configurações**.
4. Para sair, feche a aba do navegador e a janela preta. **Tudo já fica salvo** — não há botão "Salvar".

## 2. Conhecendo a tela

| Área | O que tem |
|---|---|
| Barra superior | As telas do programa: Início, Revisão, Auditoria, Resultados, Aplicar, Ajuda, Configurações |
| Barra de ferramentas | Atalhos: **Continuar revisão**, Auditoria, Resultados, atualizar, backup e ajuda |
| Painéis | Cada tela é dividida em painéis. **Arraste a divisória** entre dois painéis para mudar o tamanho |
| Barra de status (rodapé) | Quantas imagens da amostra já foram revisadas, total revisado e hora da última gravação |

## 3. Tarefa principal: revisar as detecções

Abra a tela **Revisão** (ou clique em **Continuar revisão**).

### 3.1 A ordem das imagens

A lista à esquerda é dividida em grupos. **Comece sempre pelo grupo "1º Amostra aleatória"** (60 imagens
sorteadas): só elas permitem calcular o desempenho real do programa sem viés. Depois, se houver tempo, siga
para "4 ou mais detecções", "Nenhuma detecção" e "Demais imagens".

Os círculos à esquerda de cada imagem indicam a situação: vazio = pendente, amarelo = começada,
verde = concluída. O número à direita é a quantidade de detecções do programa.

### 3.2 Revisar uma imagem

1. **Olhe primeiro sem a ajuda da máquina.** Aperte **H** para esconder as marcações, procure as lesões e
   aperte **H** de novo para ver o que o programa marcou.
2. Cada contorno numerado é uma detecção. Para cada uma, decida:
   - **1 — Confirma**: é lesão periapical;
   - **2 — Rejeita**: não é lesão (o contorno fica tracejado em vermelho);
   - **3 — Dúvida**: não dá para afirmar pela panorâmica.

   Use **Tab** para passar de uma detecção a outra, ou clique no contorno ou no cartão à direita.
3. **Lesão que o programa não marcou:** aperte **M** (ou "Marcar lesão não detectada") e desenhe o contorno
   segurando o botão do mouse. **Este é o passo mais valioso** — são exatamente os exemplos que faltam ao
   programa. Para apagar, use "remover" ao lado da lesão adicionada.
4. Se a radiografia não permite avaliação, marque **Imagem inadequada**.
5. Se quiser, escreva uma observação (por exemplo, "lesões 2 e 3 são a mesma lesão").
6. Aperte **Enter** (ou "Concluir e ir para a próxima"). Se faltar decidir alguma detecção, o programa avisa.

### 3.3 Navegar na imagem

| Ação | Como |
|---|---|
| Zoom | Roda do mouse (aproxima no ponto do cursor) |
| Mover | Clicar e arrastar |
| Ajustar à tela | Tecla **0** ou botão "Ajustar" |
| Contraste e brilho | Controles no alto da imagem |
| Imagem anterior / próxima | Setas **←** e **→** |

### 3.4 Situações comuns

| Situação | O que fazer |
|---|---|
| Duas lesões vizinhas viraram um contorno só | Confirme e anote na observação; se uma ficou de fora, desenhe-a com **M** |
| O contorno pegou a lesão, mas maior ou menor | Confirme: o que importa é se a lesão foi encontrada |
| Quero mudar uma resposta | Abra a imagem pela lista e mude; vale a última resposta |
| Concluí sem querer | Abra a imagem de novo pela lista e ajuste |

## 4. Auditoria das lesões não encontradas

Na tela **Auditoria** aparecem as lesões **marcadas por especialista** que o programa **não** encontrou. Cada
cartão mostra a panorâmica com a posição, a região ampliada sem marcação, o traço original (laranja; outras
lesões marcadas em branco) e o que o programa "viu" (amarelo = alta probabilidade). Responda:

- **Lesão visível na panorâmica** — é uma lesão e dá para ver nesta imagem;
- **Duvidosa** — não dá para afirmar só pela panorâmica;
- **Não visível na panorâmica** — pode existir (por exemplo, vista em tomografia), mas não aparece aqui;
- **Não é lesão / erro de marcação** — traço acidental, pedaço de outra lesão ou marcação no lugar errado.

Os avisos em amarelo ("vizinha de uma lesão encontrada", "muito pequena") são pistas, não respostas.
Suas respostas corrigem a referência usada para medir o programa (veja a tela **Resultados**).

## 5. Acompanhar os números

- **Início**: o painel **Ambiente** resume o acervo, a revisão (precisão na amostra aleatória, com intervalo de
  confiança) e a sensibilidade do modelo, original e corrigida pela auditoria. O painel **Atividade** registra
  o que foi feito e quando.
- **Resultados → Validação**: desempenho dos modelos nas 20 panorâmicas marcadas.
- **Resultados → Revisão do acervo**: precisão e sensibilidade estimadas a partir das suas revisões e,
  quando houver dois revisores nas mesmas imagens, a concordância entre vocês.

## 6. Aplicar o programa a novas radiografias

1. Abra a tela **Aplicar**.
2. Escreva o caminho de uma pasta com radiografias (JPG, PNG ou TIFF) e clique em **Procurar imagens na
   pasta**, ou use **Selecionar arquivos**.
3. Mantenha o **limiar 0,5** (o validado). Valores menores encontram mais lesões e mais alarmes falsos.
4. Clique em **Aplicar o modelo**. Cada radiografia leva cerca de 20 a 30 segundos; a barra mostra o
   andamento e o tempo restante. Pode continuar revisando enquanto isso.
5. Ao terminar, as novas imagens aparecem na lista da **Revisão** (grupo "Demais imagens"). Na tabela da
   tela Aplicar, selecione uma linha e clique em **Revisar selecionada** para abri-la direto.

## 7. Atalhos de teclado

| Tecla | Ação |
|---|---|
| **1** / **2** / **3** | Confirma / rejeita / dúvida na detecção selecionada |
| **Tab** | Próxima detecção (**Shift+Tab**: anterior) |
| **M** | Desenhar lesão não detectada (**Esc** cancela) |
| **H** | Esconder / mostrar as marcações |
| **0** | Ajustar a imagem à tela |
| **Enter** | Concluir a imagem e ir para a próxima |
| **←** / **→** | Imagem anterior / próxima |

## 8. Perguntas frequentes

**Perdi meu trabalho?** Não. Cada clique é gravado na hora no banco de dados, e uma cópia de segurança é feita
automaticamente todos os dias (Configurações → Fazer backup agora, para uma cópia extra).

**Duas pessoas podem revisar?** Sim. Cada uma usa o próprio nome; as respostas ficam separadas, e o programa
calcula a concordância entre os revisores.

**O navegador fechou.** Dê dois cliques de novo em `Abrir_ML_ODONTO.bat`. Se a janela preta ainda estiver
aberta, feche-a antes.

**O programa não abre.** Feche todas as janelas pretas do ML ODONTO e tente de novo. Se persistir, anote a
mensagem da janela preta e envie ao responsável técnico.

**Onde ficam os dados?** Em `Documentos\OdontoLesao` (banco, cópias reduzidas das imagens e backups).

---

*ML ODONTO · Laboratório de Estudos Eletromagnéticos (LEMAG), Universidade de Brasília. Uso acadêmico e
não comercial.*
