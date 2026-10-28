"""Página local para o dentista julgar as lesões marcadas que a U-Net v2 não encontrou.

Para cada lesão: panorâmica inteira com a posição, recorte ampliado sem marcação, recorte com o
traço original do PDF e a máscara derivada, e a probabilidade da U-Net v2. O dentista responde se a
lesão é visível na panorâmica; as respostas ficam no navegador e são baixadas como CSV.

Uso: python galeria_perdidas.py      Saída: Revisao/lesoes_perdidas/index.html (abrir no navegador)
"""
import base64
import csv
import html

import cv2
import numpy as np

import comum as c

SAIDA = c.RAIZ / "Revisao" / "lesoes_perdidas"
JANELA = 150  # meia-janela do recorte, px na escala de trabalho (1.400 px)


def jpg(img, q=90):
    return "data:image/jpeg;base64," + base64.b64encode(cv2.imencode(".jpg", img, [cv2.IMWRITE_JPEG_QUALITY, q])[1]).decode()


def contorno(img, m, cor, esp=1):
    cs, _ = cv2.findContours(m.astype(np.uint8), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_NONE)
    cv2.drawContours(img, cs, -1, cor, esp, cv2.LINE_AA)


def main():
    SAIDA.mkdir(parents=True, exist_ok=True)
    lesoes = list(csv.DictReader(open(c.RESULTADOS / "figuras" / "dados" / "lesoes_validacao.csv", encoding="utf-8")))
    perdidas = [l for l in lesoes if l["achada_UNET_V2"] == "0"]
    P = np.load(c.RESULTADOS / "validacao" / "probs_UNET_V2.npz")
    cartoes = []
    for k, l in enumerate(perdidas, 1):
        nm = l["paciente"]
        orig = c.ler_cinza(c.NOSSAS / "imagens" / f"{nm}.png")
        img, esc = c.normalizar(orig)
        m = c.redimensionar_mascara(c.ler_cinza(c.NOSSAS / "mascaras" / f"{nm}.png"), img.shape)
        n, rot = cv2.connectedComponents(m.astype(np.uint8))
        alvo = rot == int(l["lesao"])
        tr = c.ler_cinza(c.NOSSAS / "contornos" / f"{nm}.png") > 0
        ys, xs = np.nonzero(alvo)
        cy, cx = int(ys.mean()), int(xs.mean())
        h, w = img.shape
        y0, y1 = max(0, cy - JANELA), min(h, cy + JANELA)
        x0, x1 = max(0, cx - JANELA), min(w, cx + JANELA)
        # recorte da imagem original (mais nítida), com o mesmo enquadramento
        f = orig.shape[1] / w
        o = orig[int(y0 * f):int(y1 * f), int(x0 * f):int(x1 * f)]
        o = cv2.resize(o, ((x1 - x0) * 2, (y1 - y0) * 2), interpolation=cv2.INTER_CUBIC)
        limpo = cv2.cvtColor(o, cv2.COLOR_GRAY2BGR)
        marcado = limpo.copy()
        amp = lambda a: cv2.resize(a[y0:y1, x0:x1].astype(np.uint8), (o.shape[1], o.shape[0]), interpolation=cv2.INTER_NEAREST) > 0
        trc = cv2.resize(tr.astype(np.uint8), (w, h), interpolation=cv2.INTER_NEAREST) > 0
        perto = cv2.dilate(alvo.astype(np.uint8), np.ones((9, 9), np.uint8)) > 0
        marcado[amp(trc & ~perto)] = (235, 235, 235)        # traços de outras lesões (branco)
        contorno(marcado, amp(alvo), (255, 200, 0), 1)      # máscara usada (ciano)
        marcado[amp(trc & perto)] = (0, 140, 255)           # traço do PDF desta lesão (laranja)
        p = P[nm].astype(np.float32)[y0:y1, x0:x1]
        mapa = cv2.applyColorMap((np.clip(p, 0, 1) * 255).astype(np.uint8), cv2.COLORMAP_VIRIDIS)
        mapa = cv2.resize(mapa, (o.shape[1], o.shape[0]))
        prob = cv2.addWeighted(limpo, 0.55, mapa, 0.45, 0)
        contorno(prob, amp(alvo), (255, 255, 255), 1)
        pano = cv2.cvtColor(cv2.resize(img, (700, int(700 * h / w))), cv2.COLOR_GRAY2BGR)
        e = 700 / w
        cv2.rectangle(pano, (int(x0 * e), int(y0 * e)), (int(x1 * e), int(y1 * e)), (0, 140, 255), 2)
        info = (f"Paciente {nm}, lesão {l['lesao']} · {'maxila' if l['arcada'] == 'superior' else 'mandíbula'} · "
                f"diâmetro {float(l['diam_eq_px']):.0f} px (escala 1.400) · área {l['area_px']} px · "
                f"prob. máxima U-Net v2 na lesão {float(P[nm][alvo].max()):.3f} · "
                f"duas etapas: {'achou' if l['achada_DUAS'] == '1' else 'não achou'}")
        cid = f"{nm}_{l['lesao']}"
        cartoes.append(f"""
<section class="card" data-id="{cid}">
  <h2>{k}. {html.escape(info)}</h2>
  <div class="imgs">
    <figure><img src="{jpg(pano, 80)}"><figcaption>Panorâmica: posição</figcaption></figure>
    <figure><img src="{jpg(limpo)}"><figcaption>Ampliado, sem marcação</figcaption></figure>
    <figure><img src="{jpg(marcado)}"><figcaption>Laranja: traço desta lesão · ciano: máscara usada · branco: outras lesões marcadas</figcaption></figure>
    <figure><img src="{jpg(prob)}"><figcaption>Probabilidade U-Net v2 (amarelo = alta)</figcaption></figure>
  </div>
  <div class="resp">
    <label><input type="radio" name="{cid}" value="visivel"> Lesão visível na panorâmica</label>
    <label><input type="radio" name="{cid}" value="duvidosa"> Duvidosa</label>
    <label><input type="radio" name="{cid}" value="nao_visivel"> Não visível na panorâmica</label>
    <label><input type="radio" name="{cid}" value="erro_marcacao"> Não é lesão / erro de marcação</label>
    <input type="text" class="obs" placeholder="Observação (opcional)">
  </div>
</section>""")
    pagina = PAGINA.replace("{{N}}", str(len(perdidas))).replace("{{CARTOES}}", "".join(cartoes))
    (SAIDA / "index.html").write_text(pagina, encoding="utf-8")
    print(SAIDA / "index.html", len(perdidas), "lesões")


PAGINA = """<!doctype html><html lang="pt-BR"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>Lesões não encontradas</title>
<style>
body{font-family:Arial,sans-serif;margin:0;background:#f4f6f8;color:#1b2430}
header{position:sticky;top:0;background:#0b2e59;color:#fff;padding:12px 20px;display:flex;gap:16px;align-items:center;z-index:2}
header h1{font-size:18px;margin:0;flex:1} button{background:#1f6fb4;color:#fff;border:0;padding:8px 14px;border-radius:4px;cursor:pointer;font-size:14px}
main{max-width:1500px;margin:0 auto;padding:16px} .intro{background:#fff;padding:12px 16px;border-left:4px solid #1f6fb4;margin-bottom:16px;line-height:1.5}
.card{background:#fff;margin-bottom:18px;padding:12px 16px;border-radius:6px;box-shadow:0 1px 3px #0002}
.card.ok{border-left:6px solid #1f6fb4} h2{font-size:14px;margin:0 0 10px}
.imgs{display:grid;grid-template-columns:repeat(4,1fr);gap:8px} figure{margin:0} img{width:100%;display:block;border:1px solid #ccd}
figcaption{font-size:12px;color:#556;margin-top:3px}.resp{display:flex;flex-wrap:wrap;gap:14px;margin-top:10px;align-items:center;font-size:14px}
.obs{flex:1;min-width:220px;padding:6px}@media(max-width:900px){.imgs{grid-template-columns:1fr 1fr}}
</style></head><body>
<header><h1>Lesões marcadas que a U-Net v2 não encontrou ({{N}})</h1><span id="cont"></span><button onclick="baixar()">Baixar respostas (CSV)</button></header>
<main><div class="intro">Para cada lesão, diga se ela é visível <b>nesta panorâmica</b>, independentemente do que a rede fez.
As imagens são do arquivo local; nada é enviado pela internet. As respostas ficam salvas neste navegador; ao terminar,
clique em <b>Baixar respostas</b> e coloque o CSV em <code>Revisao/lesoes_perdidas/</code>.</div>
{{CARTOES}}</main>
<script>
const K='revisao_perdidas_v1'; let R={}; try{R=JSON.parse(localStorage.getItem(K)||'{}')}catch(e){}
function salvar(){try{localStorage.setItem(K,JSON.stringify(R))}catch(e){} atualizar()}
function atualizar(){let n=0;document.querySelectorAll('.card').forEach(c=>{const r=R[c.dataset.id];c.classList.toggle('ok',!!(r&&r.v));if(r&&r.v)n++});
document.getElementById('cont').textContent=n+' de '+document.querySelectorAll('.card').length+' respondidas'}
document.querySelectorAll('.card').forEach(c=>{const id=c.dataset.id,r=R[id]||{};
 c.querySelectorAll('input[type=radio]').forEach(i=>{if(i.value===r.v)i.checked=true;i.onchange=()=>{R[id]={...(R[id]||{}),v:i.value};salvar()}});
 const o=c.querySelector('.obs');o.value=r.o||'';o.oninput=()=>{R[id]={...(R[id]||{}),o:o.value};salvar()}});
function baixar(){const l=['paciente;lesao;resposta;observacao'];document.querySelectorAll('.card').forEach(c=>{const[p,s]=c.dataset.id.split('_'),r=R[c.dataset.id]||{};
 l.push([p,s,r.v||'',(r.o||'').replace(/;/g,',')].join(';'))});const a=document.createElement('a');
 a.href=URL.createObjectURL(new Blob(['\\ufeff'+l.join('\\n')],{type:'text/csv'}));a.download='revisao_lesoes_perdidas.csv';a.click()}
atualizar();
</script></body></html>"""

if __name__ == "__main__":
    main()
