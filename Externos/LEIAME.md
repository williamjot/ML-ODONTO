# Conjuntos externos

## Hanoi/ — Panoramic radiographs with periapical lesions
- Fonte: https://data.mendeley.com/datasets/kx52tk2ddj (v3, DOI 10.17632/kx52tk2ddj.3), Hanoi Medical University. Licença CC BY 4.0.
- `imagens/Original JPG Images/`: 3920 panorâmicas originais (das 3924 do pacote, 4 não puderam
  ser extraídas pelo tar do Windows: 18711, 21921, 22341, 35451).
- `anotacoes/Image Annots/`: XML Pascal VOC, 17004 arquivos (inclui as cópias aumentadas, que não baixamos).
  Para as 3920 originais: 6733 lesões, caixas justas em torno da lesão no ápice (largura mediana 60 px).
- Classe (`<name>`) = 3, 4 ou 5; provavelmente o escore PAI (Periapical Index). Não confirmado.
- `originais.rar` e `annots.rar` são os pacotes baixados; podem ser apagados.

## DENTEX/ — DENTEX challenge (MICCAI 2023)
- Fonte: https://huggingface.co/datasets/ibrahimhamamci/DENTEX. Licença CC BY-NC-SA 4.0 (uso não comercial).
- `train_quadrant_enumeration_disease.json`: COCO, 705 imagens e 3529 anotações (cárie, cárie profunda,
  lesão periapical, dente incluso).
- `imagens/`: só as 116 imagens que têm lesão periapical (158 lesões).
- Atenção: o polígono contorna o DENTE inteiro, não a lesão. `category_id_1` é o quadrante
  (0-1 superior, ápice para cima; 2-3 inferior, ápice para baixo).
