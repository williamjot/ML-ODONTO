"""Servidor local da ferramenta de revisão das detecções da U-Net.

Abre só em 127.0.0.1 (este computador): as radiografias não saem da máquina.
Cada revisão é gravada em Revisao/revisoes/<id>.json assim que o dentista decide algo.

Uso: python revisao_servidor.py [--porta 8770]   e abrir http://127.0.0.1:8770
"""
import argparse
import csv
import json
import re
import threading
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

import cv2
import numpy as np

RAIZ = Path(__file__).resolve().parent.parent
PASTA_IMAGENS = RAIZ / "Dados" / "Aplicacao"
APLIC = RAIZ / "Resultados" / "aplicacao_unet_v2"  # detecções da U-Net v2 (modelo escolhido)
N_AMOSTRA = 60  # imagens sorteadas para estimar a precisão sem viés (revisadas primeiro)
REVISAO = RAIZ / "Revisao"
PAGINA = Path(__file__).resolve().parent / "revisao" / "index.html"
LARGURA_EXIBICAO = 2000  # px; a imagem é enviada reduzida, as coordenadas salvas são as originais
EXTENSOES = {".jpg", ".jpeg", ".png", ".tif", ".tiff", ".bmp"}
TRAVA = threading.Lock()


def ler_cinza(caminho):
    return cv2.imdecode(np.fromfile(str(caminho), np.uint8), cv2.IMREAD_GRAYSCALE)


def carregar_indice():
    """id anônimo (A001…, em ordem alfabética, como em exportar_figuras.py) -> arquivo e detecções do CSV."""
    arquivos = sorted(p.name for p in PASTA_IMAGENS.iterdir() if p.suffix.lower() in EXTENSOES)
    dets = {}
    with open(APLIC / "lesoes.csv", encoding="utf-8-sig") as f:
        for l in csv.DictReader(f, delimiter=";"):
            dets.setdefault(l["arquivo"], []).append((int(l["x_centro"]), int(l["y_centro"]), float(l["confianca"])))
    return {f"A{k:03d}": dict(arquivo=a, conf=dets.get(a, [])) for k, a in enumerate(arquivos, 1)}


INDICE = {}
AMOSTRA = set()


def sortear_amostra(ids, n=N_AMOSTRA):
    """Sorteio fixo (semente 2026): a mesma amostra em todas as sessões e na consolidação."""
    ids = sorted(ids)
    return set(np.random.default_rng(2026).choice(ids, size=min(n, len(ids)), replace=False).tolist())


def deteccoes(id_):
    """Contornos (coordenadas originais) de cada lesão detectada, com a confiança do CSV."""
    e = INDICE[id_]
    m = ler_cinza(APLIC / "mascaras" / f"{Path(e['arquivo']).stem}.png")
    if m is None:
        return []
    n, rot, _, cent = cv2.connectedComponentsWithStats((m > 0).astype(np.uint8))
    saida = []
    for k in range(1, n):
        cnts, _ = cv2.findContours((rot == k).astype(np.uint8), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
        c = max(cnts, key=cv2.contourArea)
        c = cv2.approxPolyDP(c, 1.5, True)[:, 0, :]
        cx, cy = cent[k]
        conf = min(e["conf"], key=lambda t: (t[0] - cx) ** 2 + (t[1] - cy) ** 2)[2] if e["conf"] else None
        saida.append(dict(indice=k, pontos=c.tolist(), centro=[round(cx), round(cy)], confianca=conf))
    return saida


def caminho_revisao(id_):
    return REVISAO / "revisoes" / f"{id_}.json"


def ler_revisao(id_):
    p = caminho_revisao(id_)
    return json.loads(p.read_text(encoding="utf-8")) if p.exists() else None


def prioridade(n, id_=None):
    # primeiro a amostra aleatória (estimativa sem viés); depois onde falsos positivos são mais prováveis
    # (muitas detecções), onde lesões podem ter sido perdidas (nenhuma detecção) e o resto
    if id_ in AMOSTRA:
        return 0
    return 1 if n >= 4 else (2 if n == 0 else 3)


class Manipulador(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def responder(self, corpo, tipo="application/json; charset=utf-8", codigo=200):
        if isinstance(corpo, (dict, list)):
            corpo = json.dumps(corpo, ensure_ascii=False).encode("utf-8")
        self.send_response(codigo)
        self.send_header("Content-Type", tipo)
        self.send_header("Content-Length", str(len(corpo)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(corpo)

    def id_valido(self, id_):
        if id_ not in INDICE:
            self.responder({"erro": "id desconhecido"}, codigo=404)
            return False
        return True

    def do_GET(self):
        rota = self.path.split("?")[0]
        if rota == "/":
            return self.responder(PAGINA.read_bytes(), "text/html; charset=utf-8")
        if rota == "/api/lista":
            itens = []
            for id_, e in INDICE.items():
                r = ler_revisao(id_)
                n = len(e["conf"])
                itens.append(dict(id=id_, arquivo=e["arquivo"], n=n, grupo=prioridade(n, id_),
                                  status="concluida" if r and r.get("concluida") else ("parcial" if r else "pendente")))
            itens.sort(key=lambda i: (i["grupo"], -i["n"], i["arquivo"]))
            return self.responder(itens)
        m = re.fullmatch(r"/api/imagem/(A\d{3})", rota)
        if m and self.id_valido(m.group(1)):
            id_ = m.group(1)
            img = ler_cinza(PASTA_IMAGENS / INDICE[id_]["arquivo"])
            return self.responder(dict(id=id_, arquivo=INDICE[id_]["arquivo"], largura=img.shape[1],
                                       altura=img.shape[0], deteccoes=deteccoes(id_), revisao=ler_revisao(id_)))
        m = re.fullmatch(r"/img/(A\d{3})\.jpg", rota)
        if m and self.id_valido(m.group(1)):
            img = ler_cinza(PASTA_IMAGENS / INDICE[m.group(1)]["arquivo"])
            if img.shape[1] > LARGURA_EXIBICAO:
                f = LARGURA_EXIBICAO / img.shape[1]
                img = cv2.resize(img, (LARGURA_EXIBICAO, round(img.shape[0] * f)), interpolation=cv2.INTER_AREA)
            ok, buf = cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, 92])
            return self.responder(buf.tobytes(), "image/jpeg")
        self.responder({"erro": "não encontrado"}, codigo=404)

    def do_POST(self):
        m = re.fullmatch(r"/api/revisao/(A\d{3})", self.path)
        if not (m and self.id_valido(m.group(1))):
            return self.responder({"erro": "não encontrado"}, codigo=404)
        tam = int(self.headers.get("Content-Length", 0))
        if tam > 5_000_000:
            return self.responder({"erro": "grande demais"}, codigo=413)
        try:
            dados = json.loads(self.rfile.read(tam).decode("utf-8"))
        except ValueError:
            return self.responder({"erro": "JSON inválido"}, codigo=400)
        id_ = m.group(1)
        registro = dict(
            id=id_, arquivo=INDICE[id_]["arquivo"], revisor=str(dados.get("revisor", ""))[:100],
            atualizado=datetime.now().isoformat(timespec="seconds"),
            concluida=bool(dados.get("concluida")), imagem_inadequada=bool(dados.get("imagem_inadequada")),
            observacoes=str(dados.get("observacoes", ""))[:5000],
            decisoes={str(k): v for k, v in dict(dados.get("decisoes", {})).items()
                      if v in ("confirmada", "rejeitada", "duvida")},
            adicionadas=[[[int(x), int(y)] for x, y in p] for p in dados.get("adicionadas", []) if len(p) >= 3],
        )
        with TRAVA:
            caminho_revisao(id_).write_text(json.dumps(registro, ensure_ascii=False, indent=1), encoding="utf-8")
        self.responder({"ok": True, "atualizado": registro["atualizado"]})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--porta", type=int, default=8770)
    args = ap.parse_args()
    (REVISAO / "revisoes").mkdir(parents=True, exist_ok=True)
    INDICE.update(carregar_indice())
    AMOSTRA.update(sortear_amostra(INDICE))
    print(f"{len(INDICE)} imagens. Abra http://127.0.0.1:{args.porta}  (Ctrl+C para encerrar)", flush=True)
    ThreadingHTTPServer(("127.0.0.1", args.porta), Manipulador).serve_forever()


if __name__ == "__main__":
    main()
