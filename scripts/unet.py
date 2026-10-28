"""U-Net com codificador ResNet34 (torchvision), usada no Colab para treinar e aqui para prever.

Só depende de torch e torchvision, para o mesmo arquivo rodar nos dois lugares.
"""
import numpy as np
import torch
import torch.nn as nn
import torch.nn.functional as F
import torchvision

MEDIA = torch.tensor([0.485, 0.456, 0.406]).view(1, 3, 1, 1)
DESVIO = torch.tensor([0.229, 0.224, 0.225]).view(1, 3, 1, 1)


class Bloco(nn.Sequential):
    def __init__(self, entrada, saida):
        super().__init__(
            nn.Conv2d(entrada, saida, 3, padding=1, bias=False), nn.BatchNorm2d(saida), nn.ReLU(inplace=True),
            nn.Conv2d(saida, saida, 3, padding=1, bias=False), nn.BatchNorm2d(saida), nn.ReLU(inplace=True))


class UNetResNet34(nn.Module):
    def __init__(self, pre_treinada=False):
        super().__init__()
        r = torchvision.models.resnet34(weights="IMAGENET1K_V1" if pre_treinada else None)
        self.stem = nn.Sequential(r.conv1, r.bn1, r.relu)                   # 1/2, 64
        self.pool = r.maxpool
        self.c1, self.c2, self.c3, self.c4 = r.layer1, r.layer2, r.layer3, r.layer4  # 1/4 .. 1/32
        self.d4 = Bloco(512 + 256, 256)
        self.d3 = Bloco(256 + 128, 128)
        self.d2 = Bloco(128 + 64, 64)
        self.d1 = Bloco(64 + 64, 64)
        self.d0 = Bloco(64, 32)
        self.saida = nn.Conv2d(32, 1, 1)
        self.register_buffer("media", MEDIA.clone(), persistent=False)
        self.register_buffer("desvio", DESVIO.clone(), persistent=False)

    def forward(self, x):
        """x: (N, 1, H, W) em [0, 1], H e W múltiplos de 32. Devolve logits (N, 1, H, W)."""
        x = (x.repeat(1, 3, 1, 1) - self.media) / self.desvio
        s1 = self.stem(x)
        s2 = self.c1(self.pool(s1))
        s3 = self.c2(s2)
        s4 = self.c3(s3)
        s5 = self.c4(s4)
        sobe = lambda t: F.interpolate(t, scale_factor=2, mode="bilinear", align_corners=False)
        d = self.d4(torch.cat([sobe(s5), s4], 1))
        d = self.d3(torch.cat([sobe(d), s3], 1))
        d = self.d2(torch.cat([sobe(d), s2], 1))
        d = self.d1(torch.cat([sobe(d), s1], 1))
        return self.saida(self.d0(sobe(d)))


def carregar(caminho, dispositivo="cpu"):
    ck = torch.load(caminho, map_location=dispositivo, weights_only=False)
    m = UNetResNet34()
    m.load_state_dict(ck["modelo"])
    return m.to(dispositivo).eval(), ck.get("meta", {})


@torch.no_grad()
def prever(modelo, img, espelhar=True):
    """Mapa de probabilidade (H, W) para uma imagem uint8 já normalizada (comum.normalizar).

    Com espelhar=True faz a média com a previsão da imagem espelhada (a panorâmica
    é quase simétrica), o que costuma estabilizar o resultado.
    """
    h, w = img.shape
    H, W = -(-h // 32) * 32, -(-w // 32) * 32
    x = np.pad(img, ((0, H - h), (0, W - w)), mode="reflect").astype(np.float32) / 255.0
    disp = next(modelo.parameters()).device
    t = torch.from_numpy(x)[None, None].to(disp)
    p = torch.sigmoid(modelo(t))
    if espelhar:
        p = (p + torch.sigmoid(modelo(t.flip(-1))).flip(-1)) / 2
    return p[0, 0, :h, :w].float().cpu().numpy()
