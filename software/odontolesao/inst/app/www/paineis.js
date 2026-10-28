/* Divisórias arrastáveis entre painéis (.ml-row / .ml-col com .ml-gutter entre os filhos). */
(function () {
  document.addEventListener('mousedown', e => {
    const g = e.target.closest('.ml-gutter'); if (!g) return;
    const caixa = g.parentElement, horiz = caixa.classList.contains('ml-row');
    const a = g.previousElementSibling, b = g.nextElementSibling; if (!a || !b) return;
    const ra = a.getBoundingClientRect(), rb = b.getBoundingClientRect();
    const ini = horiz ? e.clientX : e.clientY, total = horiz ? ra.width + rb.width : ra.height + rb.height;
    const a0 = horiz ? ra.width : ra.height;
    g.classList.add('ativo'); document.body.style.cursor = horiz ? 'col-resize' : 'row-resize'; e.preventDefault();
    const mover = ev => {
      const na = Math.min(total - 90, Math.max(90, a0 + (horiz ? ev.clientX : ev.clientY) - ini));
      a.style.flex = `0 0 ${na}px`; b.style.flex = '1 1 0';
    };
    const soltar = () => {
      document.removeEventListener('mousemove', mover); document.removeEventListener('mouseup', soltar);
      g.classList.remove('ativo'); document.body.style.cursor = '';
      window.dispatchEvent(new Event('resize'));
    };
    document.addEventListener('mousemove', mover); document.addEventListener('mouseup', soltar);
  });
})();
