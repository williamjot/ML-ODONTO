/* Visor de radiografias do OdontoLesão.
 * R -> JS: mensagem "visor:carregar" {id, url, largura, altura, deteccoes, decisoes, adicionadas, cego}
 *          mensagem "visor:adicionada" {id, k, lesao_id}  (confirma o id da lesão gravada no banco)
 * JS -> R: input <id>_evento = {tipo: 'decisao'|'adicionar'|'remover'|'concluir'|'navegar', ...}
 * Coordenadas trocadas com o R estão sempre em pixels da imagem ORIGINAL.
 */
(function () {
  const COR = {pend: '#ffffff', confirmada: '#2fb36b', rejeitada: '#e5484d', duvida: '#e8c33a', add: '#3fa9ff'};
  const ROTULO = {confirmada: 'Confirmada', rejeitada: 'Rejeitada', duvida: 'Dúvida'};
  const visores = {};

  function Visor(raiz) {
    const id = raiz.id;
    raiz.innerHTML = `
      <div class="visor-palco">
        <canvas tabindex="0" aria-label="Radiografia"></canvas>
        <div class="visor-titulo">—</div>
        <div class="visor-dica">Roda do mouse: zoom · arrastar: mover · <kbd>H</kbd> ocultar marcações · <kbd>0</kbd> ajustar</div>
        <div class="visor-ferramentas">
          <label>Contraste <input type="range" class="v-contraste" min="60" max="220" value="100"></label>
          <label>Brilho <input type="range" class="v-brilho" min="60" max="160" value="100"></label>
          <button type="button" class="btn btn-sm btn-outline-light v-cego" title="Ocultar/mostrar marcações (H)">Ocultar marcações</button>
          <button type="button" class="btn btn-sm btn-outline-light v-ajustar" title="Ajustar à tela (0)">Ajustar</button>
        </div>
      </div>
      <div class="visor-painel">
        <div class="visor-legenda">
          <span><i style="background:${COR.pend};border:1px solid #8fa3b8"></i>a decidir</span><span><i style="background:${COR.confirmada}"></i>confirmada</span>
          <span><i style="background:${COR.rejeitada}"></i>rejeitada</span><span><i style="background:${COR.duvida}"></i>dúvida</span>
          <span><i style="background:${COR.add}"></i>adicionada</span>
        </div>
        <h6>Detecções do modelo</h6>
        <div class="v-dets"></div>
        <div class="v-semdet text-muted small">Nenhuma detecção nesta imagem. Se houver lesão, marque abaixo.</div>
        <h6 class="mt-3">Lesões não detectadas</h6>
        <button type="button" class="btn btn-outline-primary w-100 v-marcar">Marcar lesão não detectada <kbd>M</kbd></button>
        <div class="v-adds mt-2"></div>
        <div class="text-muted small mt-3">Na lesão selecionada: <kbd>1</kbd> confirma · <kbd>2</kbd> rejeita · <kbd>3</kbd> dúvida ·
          <kbd>Tab</kbd> próxima · <kbd>Enter</kbd> concluir · <kbd>←</kbd>/<kbd>→</kbd> imagem anterior/próxima</div>
      </div>`;
    const $ = s => raiz.querySelector(s);
    const tela = $('canvas'), ctx = tela.getContext('2d');
    let dados = null, img = null, decisoes = {}, adicionadas = [], sel = 0, cego = false;
    let desenhando = false, traco = null, vista = {s: 1, x: 0, y: 0}, arrasto = null, autoAjuste = true;

    const enviar = (ev) => Shiny.setInputValue(id + '_evento', Object.assign({t: Date.now()}, ev), {priority: 'event'});
    const k = () => (img && dados) ? img.width / dados.largura : 1;

    function carregar(m) {
      dados = m; decisoes = Object.assign({}, m.decisoes || {}); adicionadas = (m.adicionadas || []).slice();
      sel = 0; cego = !!m.cego; desenhando = false; traco = null; modoDesenho(false); autoAjuste = true;
      $('.visor-titulo').textContent = m.titulo || m.id;
      img = new Image(); img.onload = () => { redimensionar(); ajustar(); desenhar(); }; img.src = m.url;
      painel(); atualizarCego(); tela.focus({preventScroll: true});
    }

    function painel() {
      const d = (dados && dados.deteccoes) || [];
      $('.v-semdet').style.display = d.length ? 'none' : 'block';
      $('.v-dets').innerHTML = d.map((x, i) => {
        const dec = decisoes[x.indice] || '';
        const conf = x.confianca == null ? '—' : Number(x.confianca).toFixed(2).replace('.', ',');
        return `<div class="v-det ${i === sel ? 'sel' : ''}" data-i="${i}">
          <div class="d-flex justify-content-between"><b>Lesão ${x.indice}</b><span class="text-muted small">confiança ${conf}</span></div>
          <div class="btn-group w-100 mt-1" role="group">
            <button type="button" class="btn btn-sm ${dec === 'confirmada' ? 'btn-success' : 'btn-outline-success'}" data-d="confirmada" aria-label="Confirmar lesão ${x.indice}">✓ Confirma</button>
            <button type="button" class="btn btn-sm ${dec === 'rejeitada' ? 'btn-danger' : 'btn-outline-danger'}" data-d="rejeitada" aria-label="Rejeitar lesão ${x.indice}">✗ Rejeita</button>
            <button type="button" class="btn btn-sm ${dec === 'duvida' ? 'btn-warning' : 'btn-outline-warning'}" data-d="duvida" aria-label="Marcar dúvida na lesão ${x.indice}">? Dúvida</button>
          </div></div>`;
      }).join('');
      $('.v-adds').innerHTML = adicionadas.map((a, i) =>
        `<div class="d-flex justify-content-between align-items-center small mb-1"><span style="color:${COR.add}">● Adicionada ${i + 1}</span>
         <button type="button" class="btn btn-sm btn-link text-danger p-0" data-del="${i}">remover</button></div>`).join('');
    }

    function decidir(d) {
      const x = dados && dados.deteccoes[sel]; if (!x) return;
      const novo = decisoes[x.indice] === d ? null : d;
      if (novo) decisoes[x.indice] = novo; else delete decisoes[x.indice];
      painel(); desenhar(); enviar({tipo: 'decisao', indice: x.indice, decisao: novo});
    }

    $('.v-dets').addEventListener('click', e => {
      const card = e.target.closest('.v-det'); if (!card) return;
      sel = +card.dataset.i; const b = e.target.closest('button');
      if (b) decidir(b.dataset.d); else { painel(); desenhar(); }
    });
    $('.v-adds').addEventListener('click', e => {
      const b = e.target.closest('[data-del]'); if (!b) return;
      const i = +b.dataset.del, a = adicionadas[i]; adicionadas.splice(i, 1); painel(); desenhar();
      enviar({tipo: 'remover', lesao_id: a.id || null});
    });

    // ---------- desenho ----------
    function redimensionar() {
      const r = tela.getBoundingClientRect(); if (!r.width) return;
      tela.width = r.width * devicePixelRatio; tela.height = r.height * devicePixelRatio;
      if (autoAjuste) ajustar();
      desenhar();
    }
    function ajustar() {
      if (!img || !img.width) return;
      const s = Math.min(tela.width / img.width, tela.height / img.height) * .97;
      vista = {s, x: (tela.width - img.width * s) / 2, y: (tela.height - img.height * s) / 2};
    }
    function caminho(pontos) {
      const p = new Path2D();
      pontos.forEach(([x, y], i) => i ? p.lineTo(x * k(), y * k()) : p.moveTo(x * k(), y * k()));
      p.closePath(); return p;
    }
    function desenhar() {
      ctx.setTransform(1, 0, 0, 1, 0, 0); ctx.fillStyle = '#05080c'; ctx.fillRect(0, 0, tela.width, tela.height);
      if (!img || !img.complete || !img.width) return;
      ctx.setTransform(vista.s, 0, 0, vista.s, vista.x, vista.y);
      ctx.filter = `contrast(${$('.v-contraste').value}%) brightness(${$('.v-brilho').value}%)`;
      ctx.drawImage(img, 0, 0); ctx.filter = 'none';
      if (cego) return;
      const lw = 2.2 / vista.s;
      dados.deteccoes.forEach((x, i) => {
        const cor = COR[decisoes[x.indice]] || COR.pend;
        ctx.lineWidth = i === sel ? lw * 2 : lw; ctx.strokeStyle = cor;
        ctx.setLineDash(decisoes[x.indice] === 'rejeitada' ? [6 / vista.s, 5 / vista.s] : []);
        ctx.stroke(caminho(x.pontos)); ctx.setLineDash([]);
        ctx.fillStyle = cor; ctx.font = `bold ${16 / vista.s}px system-ui, sans-serif`;
        ctx.fillText(String(x.indice), x.centro[0] * k() + 14 / vista.s, x.centro[1] * k() - 14 / vista.s);
      });
      ctx.lineWidth = lw; ctx.strokeStyle = COR.add; ctx.fillStyle = COR.add + '33';
      adicionadas.forEach(a => { const c = caminho(a.pontos); ctx.fill(c); ctx.stroke(c); });
      if (traco && traco.length > 1) {
        ctx.beginPath(); traco.forEach(([x, y], i) => i ? ctx.lineTo(x * k(), y * k()) : ctx.moveTo(x * k(), y * k())); ctx.stroke();
      }
    }
    function paraImagem(ev) {
      const r = tela.getBoundingClientRect(), px = (ev.clientX - r.left) * devicePixelRatio, py = (ev.clientY - r.top) * devicePixelRatio;
      return [((px - vista.x) / vista.s) / k(), ((py - vista.y) / vista.s) / k()];
    }
    tela.addEventListener('wheel', ev => {
      ev.preventDefault(); const r = tela.getBoundingClientRect();
      const px = (ev.clientX - r.left) * devicePixelRatio, py = (ev.clientY - r.top) * devicePixelRatio, f = Math.exp(-ev.deltaY * .0015);
      vista.x = px - (px - vista.x) * f; vista.y = py - (py - vista.y) * f; vista.s *= f; autoAjuste = false; desenhar();
    }, {passive: false});
    tela.addEventListener('mousedown', ev => {
      tela.focus({preventScroll: true});
      if (!dados) return;
      if (desenhando) { traco = [paraImagem(ev)]; return; }
      const [ox, oy] = paraImagem(ev);
      ctx.setTransform(1, 0, 0, 1, 0, 0);
      const i = cego ? -1 : dados.deteccoes.findIndex(x => ctx.isPointInPath(caminho(x.pontos), ox * k(), oy * k()));
      if (i >= 0) { sel = i; painel(); desenhar(); return; }
      arrasto = {x: ev.clientX, y: ev.clientY, vx: vista.x, vy: vista.y}; autoAjuste = false; tela.style.cursor = 'grabbing';
    });
    window.addEventListener('mousemove', ev => {
      if (traco) { traco.push(paraImagem(ev)); desenhar(); return; }
      if (!arrasto) return;
      vista.x = arrasto.vx + (ev.clientX - arrasto.x) * devicePixelRatio;
      vista.y = arrasto.vy + (ev.clientY - arrasto.y) * devicePixelRatio; desenhar();
    });
    window.addEventListener('mouseup', () => {
      if (traco) {
        if (traco.length >= 5) {
          const pontos = traco.map(([x, y]) => [Math.round(x), Math.round(y)]);
          adicionadas.push({id: null, pontos});
          enviar({tipo: 'adicionar', k: adicionadas.length - 1, pontos});
        }
        traco = null; modoDesenho(false); painel(); desenhar();
      }
      arrasto = null; tela.style.cursor = '';
    });
    function modoDesenho(on) {
      desenhando = on; tela.classList.toggle('desenho', on); $('.v-marcar').classList.toggle('active', on);
      $('.visor-dica').innerHTML = on ? 'Desenhe o contorno da lesão segurando o botão do mouse · <kbd>Esc</kbd> cancela'
        : 'Roda do mouse: zoom · arrastar: mover · <kbd>H</kbd> ocultar marcações · <kbd>0</kbd> ajustar';
    }
    function atualizarCego() { $('.v-cego').textContent = cego ? 'Mostrar marcações' : 'Ocultar marcações'; desenhar(); }
    $('.v-marcar').addEventListener('click', () => modoDesenho(!desenhando));
    $('.v-cego').addEventListener('click', () => { cego = !cego; atualizarCego(); });
    $('.v-ajustar').addEventListener('click', () => { autoAjuste = true; ajustar(); desenhar(); });
    $('.v-contraste').addEventListener('input', desenhar); $('.v-brilho').addEventListener('input', desenhar);

    raiz.addEventListener('keydown', ev => {
      if (!dados) return;
      const t = ev.target; if (t.tagName === 'TEXTAREA' || (t.tagName === 'INPUT' && t.type !== 'range')) return;
      const n = dados.deteccoes.length;
      if (ev.key === '1') decidir('confirmada'); else if (ev.key === '2') decidir('rejeitada'); else if (ev.key === '3') decidir('duvida');
      else if (ev.key === 'Tab' && n) { ev.preventDefault(); sel = (sel + (ev.shiftKey ? n - 1 : 1)) % n; painel(); desenhar(); }
      else if (ev.key === 'h' || ev.key === 'H') { cego = !cego; atualizarCego(); }
      else if (ev.key === '0') { autoAjuste = true; ajustar(); desenhar(); }
      else if (ev.key === 'm' || ev.key === 'M') modoDesenho(!desenhando);
      else if (ev.key === 'Escape') { traco = null; modoDesenho(false); desenhar(); }
      else if (ev.key === 'Enter') { ev.preventDefault(); enviar({tipo: 'concluir'}); }
      else if (ev.key === 'ArrowRight') { ev.preventDefault(); enviar({tipo: 'navegar', passo: 1}); }
      else if (ev.key === 'ArrowLeft') { ev.preventDefault(); enviar({tipo: 'navegar', passo: -1}); }
    });
    new ResizeObserver(() => { redimensionar(); }).observe($('.visor-palco'));

    return {
      carregar,
      adicionada: (m) => { if (adicionadas[m.k]) adicionadas[m.k].id = m.lesao_id; }
    };
  }

  function obter(id) {
    if (!visores[id]) { const el = document.getElementById(id); if (!el) return null; visores[id] = Visor(el); }
    return visores[id];
  }
  Shiny.addCustomMessageHandler('visor:carregar', m => { const v = obter(m.alvo); if (v) v.carregar(m); });
  Shiny.addCustomMessageHandler('visor:adicionada', m => { const v = obter(m.alvo); if (v) v.adicionada(m); });
})();
