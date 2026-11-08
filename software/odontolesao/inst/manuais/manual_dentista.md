# Manual do dentista — ML ODONTO

> A versão interativa deste manual (com simulador da revisão, mapa da janela e passos para marcar) está na
> tela **Ajuda** do programa e em [`software/odontolesao/inst/manuais/manual_dentista.html`](manual_dentista.html).
> Este arquivo é o mesmo conteúdo em texto.

O **ML ODONTO** marca nas radiografias panorâmicas as lesões periapicais que uma rede neural encontrou e pede
o seu julgamento. Suas respostas medem quanto o programa acerta de verdade e ensinam a ele as lesões que hoje
escapam, sobretudo as pequenas e as da maxila.

O programa apoia a leitura e **não substitui o diagnóstico**. Julgue sempre o que é visível na panorâmica.
Tudo roda neste computador: nenhuma radiografia é enviada para a internet.

**Roteiro:** 1. abrir · 2. revisar a amostra · 3. decidir e desenhar · 4. auditar · 5. acompanhar os números.

## 1. Abrir o programa

1. Na pasta **ML ODONTO**, dê dois cliques em **ML ODONTO.exe** (ícone azul com um dente). Para ter um atalho,
   clique com o botão direito no arquivo e escolha *Enviar para › Área de trabalho (criar atalho)*.
2. O programa abre na própria janela em poucos segundos. Se ele já estiver aberto, a janela volta para a frente.
3. Na primeira vez, escreva o **seu nome** e clique em OK. Cada revisor tem as próprias respostas; o nome aparece
   no canto superior direito e pode ser trocado em **Configurações**.
4. Para sair, feche a janela. **Tudo já fica salvo**: não existe botão Salvar.

## 2. Conhecendo a tela

| Parte | O que faz |
|---|---|
| Barra de telas | Início, Revisão, Auditoria, Resultados e Aplicar à esquerda; Ajuda e Configurações à direita |
| Barra de ferramentas | **Continuar revisão** (próxima imagem pendente), Auditoria, Resultados, atualizar (↻), backup (disquete) e ajuda (?) |
| Revisor e modelo | À direita da barra de ferramentas: quem está revisando e o modelo ativo |
| Painéis | Cada tela é dividida em painéis; **arraste a divisória** para mudar o tamanho. Alguns têm abas no cabeçalho |
| Barra de status (azul, embaixo) | Amostra revisada, total revisado, hora da última gravação, pasta do banco e versão |

## 3. Revisar as detecções

Abra a tela **Revisão** ou clique em **Continuar revisão**.

### A ordem das imagens

A lista à esquerda é dividida em grupos. **Comece sempre pelo grupo "1º Amostra aleatória"** (60 imagens
sorteadas): só elas medem o programa sem viés. Depois siga para "4 ou mais detecções", "Nenhuma detecção" e
"Demais imagens" (uma a três detecções).

Cada linha mostra o círculo de situação (vazio = pendente, amarelo = começada, verde = concluída), o código da
imagem (A247), **quantas detecções ela tem** e o nome do arquivo. Os filtros Todas, Pendentes e Concluídas
ficam no alto da lista.

### Revisar uma imagem

1. **Olhe primeiro sem a ajuda da máquina.** Aperte **H** para esconder as marcações, procure as lesões e
   aperte **H** de novo. (Em Configurações dá para abrir as imagens não revisadas já com as marcações ocultas.)
2. Para cada contorno numerado: **1** confirma, **2** rejeita (o contorno fica vermelho e tracejado), **3** dúvida.
   Passe de uma detecção a outra com **Tab**, clicando no contorno ou no cartão à direita. Clicar de novo na
   mesma decisão a desfaz.
3. **Desenhe a lesão que o programa não marcou:** aperte **M** (ou *Marcar lesão não detectada*) e contorne a lesão
   segurando o botão do mouse. É o passo mais valioso. Para apagar, clique em *remover*; **Esc** cancela um desenho.
4. Se a radiografia não permite avaliação, marque **Imagem inadequada**, no rodapé.
5. Se quiser, escreva uma observação no rodapé (por exemplo, "lesões 2 e 3 são a mesma lesão").
6. Aperte **Enter** ou **Concluir e ir para a próxima**. Se faltar decidir alguma detecção, o programa pergunta:
   *Concluir* ou *Voltar*.

As teclas funcionam com a radiografia em foco: se não responderem, clique uma vez sobre a imagem.

### Navegar na imagem

| Ação | Como |
|---|---|
| Zoom | Roda do mouse (aproxima no ponto do cursor) |
| Mover | Clicar fora dos contornos e arrastar |
| Ajustar à tela | Tecla **0** ou botão Ajustar |
| Contraste e brilho | Controles na barra escura **abaixo** da imagem |
| Imagem anterior / próxima | Setas **←** e **→**, ou os botões do rodapé |

### Situações comuns

| Situação | O que fazer |
|---|---|
| Duas lesões vizinhas viraram um contorno só | Confirme e anote na observação; se uma ficou de fora, desenhe-a com **M** |
| O contorno pegou a lesão, mas maior ou menor | Confirme: o que importa é se a lesão foi encontrada |
| Quero mudar uma resposta | Abra a imagem pela lista e mude; vale a última resposta |
| Concluí sem querer | Abra a imagem de novo (filtro Concluídas) e ajuste; ela continua concluída |

## 4. Auditoria das lesões não encontradas

Na tela **Auditoria** aparecem as lesões **marcadas por especialista** nas 20 panorâmicas de referência que o
programa **não** encontrou. Escolha o modelo avaliado (padrão: U-Net v2) e use o filtro *Sem resposta* para ver
o que falta. Cada cartão traz quatro imagens: a panorâmica com a posição (retângulo laranja), a região ampliada
sem marcação, o traço e a máscara (laranja: esta lesão; ciano: área usada na avaliação; branco: outras lesões) e
a probabilidade do programa (amarelo = alta). Responda:

- **Lesão visível na panorâmica** — é uma lesão e dá para ver nesta imagem;
- **Duvidosa** — não dá para afirmar só pela panorâmica;
- **Não visível na panorâmica** — pode existir (por exemplo, vista em tomografia), mas não aparece aqui;
- **Não é lesão / erro de marcação** — traço acidental, pedaço de outra lesão ou marcação no lugar errado.

Os avisos em amarelo ("vizinha de uma lesão encontrada", "muito pequena") são pistas, não respostas. As
respostas corrigem a referência usada para medir o programa e aparecem em **Resultados**.

## 5. Acompanhar os números

| Onde | O que mostra |
|---|---|
| Início › Fluxo de trabalho | O caminho dos dados até a revisão, com os números de hoje |
| Início › Ambiente | Acervo, precisão na amostra (IC 95%), decisões, lesões adicionadas e sensibilidade original e corrigida |
| Início › Atividade | O registro do que foi feito e quando |
| Resultados › Validação | Curvas FROC, sensibilidade, precisão e Dice, mapa das 45 lesões e a referência corrigida pela auditoria |
| Resultados › Revisão do acervo | Precisão e sensibilidade estimadas pelas revisões e a concordância entre revisores (kappa) |

Cada gráfico tem o botão **PNG**, que salva a figura em alta resolução.

## 6. Aplicar o programa a novas radiografias

1. Abra a tela **Aplicar**.
2. Clique em **Pasta…** e escolha a pasta com as radiografias (JPG, PNG, TIFF ou BMP). Também dá para colar o
   caminho no campo e apertar Enter, ou usar **Selecionar arquivos…**. Aparece quantas foram encontradas e o
   tempo estimado.
3. Mantenha o **limiar de confiança em 0,50**, o validado. Valores menores encontram mais lesões e mais alarmes falsos.
4. Clique em **Aplicar o modelo**. Cada radiografia leva cerca de **10 a 15 segundos**; o painel Andamento mostra
   quantas faltam e o tempo restante. Pode continuar trabalhando nas outras telas. **Cancelar** para depois da
   radiografia atual e mantém as já processadas.
5. As novas imagens entram na lista da Revisão, **no grupo que corresponde ao número de detecções**. Na tabela
   *Radiografias processadas*, selecione uma linha e clique em **Revisar selecionada**. O botão **CSV** exporta as
   detecções para uma planilha.

Com *Pular radiografias que já estão no banco* marcado, rodar a mesma pasta de novo não repete nada. Imagens já
revisadas nunca têm as detecções substituídas.

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

**Perdi meu trabalho?** Não. Cada clique é gravado na hora, e uma cópia de segurança é feita sozinha todo dia
(as sete mais recentes ficam guardadas). Para uma cópia extra: Configurações › Fazer backup agora, ou o disquete
da barra de ferramentas.

**Duas pessoas podem revisar?** Sim. Cada uma usa o próprio nome (Configurações › Nome do revisor); as respostas
ficam separadas, e a concordância aparece em Resultados › Revisão do acervo.

**Fechei sem querer.** Dê dois cliques de novo em ML ODONTO.exe. Nada se perde; se ele ainda estiver aberto, a
janela volta para a frente.

**As teclas 1, 2, 3 não fazem nada.** Clique uma vez sobre a radiografia: as teclas valem com ela em foco.

**O programa não abre.** Feche o ML ODONTO e tente de novo. Se persistir, envie ao responsável técnico uma foto da
mensagem e o caminho da pasta dos dados (em Configurações e na barra azul, embaixo).

**Onde ficam os dados?** Em `Documentos\OdontoLesao`: banco, cópias reduzidas das imagens e backups.
Configurações › Abrir a pasta leva até lá. Para levar o trabalho a outro computador, copie essa pasta inteira.

---

*ML ODONTO · Laboratório de Estudos Eletromagnéticos (LEMAG), Universidade de Brasília. Uso acadêmico e não comercial.*
