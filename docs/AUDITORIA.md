# Auditoria antes da entrega à equipe

Data: 08/11/2026 · Escopo: repositório inteiro, programa nativo (`cpp/desktop`), núcleo C++, versão em R,
documentação e dados.

## Resumo

| Área | Situação |
|---|---|
| Dados de pacientes no repositório | nenhum, em nenhum dos 9 commits de nenhum ramo |
| Backup diário do banco | **estava quebrado** (cópias vazias); corrigido nas duas versões e coberto por testes |
| Testes | todos passando; o programa nativo ganhou a sua própria suíte |
| Equivalência com a referência | aprovada: 31/45 lesões iguais, uma a uma, 0 falso positivo |
| Documentação | conferida contra o programa; o que não batia foi corrigido |

## Privacidade

- Os 188 arquivos versionados e o conteúdo de todos os commits (inclusive os antigos do GitHub) foram
  comparados com os nomes dos arquivos das 422 radiografias, que são nomes de pacientes: nenhuma ocorrência.
- As imagens versionadas (`docs/imagens`, `Resultados/figuras/R`) são gráficos agregados, sem radiografias.
- A lista da amostra aleatória (`inst/extdata`) tem só códigos anônimos (A006, A010…).
- `.gitignore` exclui `Dados/`, `HUB/`, `Treinamento/`, as respostas de `Revisao/`, os mapas de `Resultados/`,
  pesos, modelos ONNX, bancos SQLite e o programa montado.
- Observação: os commits levam o e-mail institucional do autor (número de matrícula). Se não quiser que ele
  fique público, configure um e-mail `noreply` do GitHub antes dos próximos commits.

## Defeitos encontrados e corrigidos

| Defeito | Efeito | Correção |
|---|---|---|
| Backup diário copiava o arquivo `.sqlite` sem o `-wal` | as cópias saíam vazias (4 KB): os dois backups existentes do banco real não tinham nenhuma tabela | `VACUUM INTO` com o banco aberto; a cópia de hoje é refeita se estiver vazia; só as diárias entram no rodízio de 7. Feita uma cópia correta do banco real (`backups/odontolesao_2026-11-08_auditoria.sqlite`) |
| Observação digitada logo antes de trocar de imagem, concluir ou fechar | podia se perder | gravada na hora nesses três momentos |
| Caixas de diálogo do Qt em inglês (Yes/No, OK/Cancel) | interface misturada | traduções do Qt em português e botões próprios (Concluir/Voltar) |
| Segunda execução do programa | mostrava só um aviso | traz a janela aberta para a frente |
| Porcentagens arredondadas para longe do zero | podiam diferir em 1 ponto do app R | metade para o par, como `round()` do R |
| Sinais da thread da Aplicar com os nomes dos sinais do `QThread` | risco de ligar o sinal errado numa mudança futura | renomeados |
| `Revisar_lesoes.bat` (ferramenta antiga) na raiz | revisões feitas nela não entram no banco do programa | retirado; o script fica em `scripts/` como histórico |
| Banco sem `busy_timeout` no app R | erro se o programa nativo gravasse ao mesmo tempo | `busy_timeout` de 5 s nas duas versões |
| Auditoria sem sinal de espera na primeira abertura | parecia travada por alguns segundos | cursor de espera |

## Testes

| Conjunto | Resultado |
|---|---|
| Núcleo C++ (`ml_odonto_core_tests`), Zig/clang e GCC | passa |
| Programa nativo (`ml_odonto_desktop_tests`, novo): banco, migrações, modelos, lista de revisão, decisões, estatísticas, kappa, backup com WAL, aplicação, Wilson, FROC, bootstrap, contornos | passa; os cenários e valores esperados repetem os testes do R |
| Pacote R (39 verificações, 2 novas de backup) | passa |
| Motor C++ nas 20 panorâmicas (`validate_pipeline.py`) | aprovado: 31/45 iguais, 0 falso positivo, correlação ≥ 0,998, Dice ≥ 0,988, 11,4 s/imagem |
| Detecção C++ isolada (`compare_reference.py`) | Dice 1,000 nos 20 casos |
| Equivalência R × Python (`equivalencia.R`) | aprovado: 31/45 iguais, 0 falso positivo, correlação ≥ 0,998, Dice ≥ 0,988 |
| Scripts Python (sintaxe) | todos ok |
| Fluxo real do programa (UI Automation, cópia do banco): confirmar, rejeitar, concluir com aviso, abrir a próxima | ok |
| Aplicar em banco vazio e na cópia do banco real | ok; mesmas 7 lesões do app R |

A integração contínua do GitHub passa a compilar e testar também o programa nativo (Windows, Qt 6 + MinGW).

## O que não foi verificado

- O desenho de lesões à mão e os atalhos de teclado do visor: o teste automático aciona botões, não o mouse
  nem o teclado. Teste manual recomendado antes de começar: desenhar uma lesão com **M**, removê-la, usar
  **1/2/3**, **Tab** e **Enter** numa imagem qualquer.
- A compilação com o MSVC (Visual Studio), que só a integração contínua do GitHub faz.
- A importação do acervo do projeto pelo programa nativo, que continua sendo feita pela versão em R.
