# Pesos dos modelos

Os pesos não ficam no repositório (cada arquivo tem ~94 MB e há restrições de licença do DENTEX).
Coloque nesta pasta, **antes de instalar o pacote**, os arquivos:

| Arquivo | Modelo | Uso no programa |
|---|---|---|
| `unet_lesoes_v2.safetensors` | U-Net v2 (2.000 px, perda de Tversky) | modelo ativo: tela Aplicar |
| `unet_lesoes.safetensors` | U-Net v1 (1.400 px) | comparação |
| `localizador_apices.safetensors` | localizador de ápices (DENTEX) | reservado (classificador por dente) |

O registro `modelos.yml` descreve cada um (largura, limiar de operação, métricas).

Para gerar os `.safetensors` a partir dos checkpoints do PyTorch (`Modelos/*.pt`), veja a seção
"Converter pesos" da skill `.claude/skills/odonto-torch-r/SKILL.md`. Uso acadêmico e não comercial
(o DENTEX é licenciado sob CC BY-NC-SA 4.0).
