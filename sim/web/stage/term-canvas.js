// Draws an @xterm/headless buffer onto a canvas (the laptop screen texture):
// one fillText per cell so wide/fallback glyphs never shift the grid, box
// drawing as vector strokes, 16/256/truecolor, bold/dim/italic/inverse/underline.
const THEME = {
  bg: '#14161c', fg: '#d6d9e0', bar: '#1d2028', barText: '#9aa1b2',
  ansi: ['#1b1d23', '#ef6b73', '#7fd39a', '#e5c07b', '#6fa8ff', '#c792ea', '#5fd7d7', '#cfd3dc',
    '#5c6370', '#ff7f86', '#98e0ad', '#f0d08e', '#8dbcff', '#d7a8f2', '#7fe7e7', '#ffffff'],
};

function palette256() {
  const p = THEME.ansi.slice();
  const lv = [0, 95, 135, 175, 215, 255];
  for (let r = 0; r < 6; r++) for (let g = 0; g < 6; g++) for (let b = 0; b < 6; b++) p.push(`rgb(${lv[r]},${lv[g]},${lv[b]})`);
  for (let i = 0; i < 24; i++) { const v = 8 + i * 10; p.push(`rgb(${v},${v},${v})`); }
  return p;
}
const PAL = palette256();
const rgbHex = (n) => '#' + n.toString(16).padStart(6, '0');

export const TITLE_H = 46;

export class TermCanvas {
  constructor(term, {width = 1600, height = 1000, title = 'claude — demo', font = '"DM Mono", ui-monospace, Menlo, monospace'} = {}) {
    this.term = term;
    this.canvas = document.createElement('canvas');
    this.canvas.width = width;
    this.canvas.height = height;
    this.ctx = this.canvas.getContext('2d');
    this.title = title;
    this.fontFamily = font;
    this.dirty = true;
    this.status = '';
    this.layout();
  }

  layout() {
    const {cols, rows} = this.term;
    const padX = 22, padY = 14;
    this.cw = (this.canvas.width - padX * 2) / cols;
    this.ch = (this.canvas.height - TITLE_H - padY * 2) / rows;
    this.ox = padX;
    this.oy = TITLE_H + padY;
    // Largest font whose advance fits the cell width and whose height fits the row.
    const ctx = this.ctx;
    let size = Math.floor(this.ch * 0.8);
    for (; size > 6; size--) {
      ctx.font = `${size}px ${this.fontFamily}`;
      if (ctx.measureText('M').width <= this.cw * 0.98) break;
    }
    this.fontSize = size;
    this.dirty = true;
  }

  setTitle(t) { if (t !== this.title) { this.title = t; this.dirty = true; } }
  setStatus(t) { if (t !== this.status) { this.status = t; this.dirty = true; } }

  color(cell, fg) {
    if (fg) {
      if (cell.isFgRGB()) return rgbHex(cell.getFgColor());
      if (cell.isFgPalette()) return PAL[cell.getFgColor()] || THEME.fg;
      return THEME.fg;
    }
    if (cell.isBgRGB()) return rgbHex(cell.getBgColor());
    if (cell.isBgPalette()) return PAL[cell.getBgColor()] || THEME.bg;
    return null;
  }

  draw() {
    if (!this.dirty) return false;
    this.dirty = false;
    const {ctx, cw, ch, ox, oy} = this;
    const W = this.canvas.width, H = this.canvas.height;
    ctx.fillStyle = THEME.bg;
    ctx.fillRect(0, 0, W, H);
    this.drawTitleBar(W);

    const buf = this.term.buffer.active;
    const cell = buf.getNullCell();
    const base = `500 ${this.fontSize}px ${this.fontFamily}`;
    ctx.textBaseline = 'middle';
    for (let y = 0; y < this.term.rows; y++) {
      const line = buf.getLine(buf.viewportY + y);
      if (!line) continue;
      const cy = oy + y * ch;
      for (let x = 0; x < this.term.cols; x++) {
        line.getCell(x, cell);
        const width = cell.getWidth();
        if (width === 0) continue;
        let fg = this.color(cell, true);
        let bg = this.color(cell, false);
        if (cell.isInverse()) { const f = fg; fg = bg || THEME.bg; bg = f; }
        const cx = ox + x * cw;
        if (bg) { ctx.fillStyle = bg; ctx.fillRect(Math.floor(cx), Math.floor(cy), Math.ceil(cw * width) + 1, Math.ceil(ch) + 1); }
        const chars = cell.getChars();
        if (!chars || chars === ' ' || cell.isInvisible()) continue;
        ctx.globalAlpha = cell.isDim() ? 0.55 : 1;
        ctx.fillStyle = fg;
        ctx.strokeStyle = fg;
        if (!this.boxGlyph(chars, cx, cy, cw, ch, cell.isBold())) {
          ctx.font = (cell.isItalic() ? 'italic ' : '') + (cell.isBold() ? base.replace('500', '700') : base);
          ctx.fillText(chars, cx, cy + ch * 0.54, cw * width * 1.05);
        }
        if (cell.isUnderline()) ctx.fillRect(cx, cy + ch - 3, cw * width, 2);
        ctx.globalAlpha = 1;
      }
    }
    return true;
  }

  drawTitleBar(W) {
    const ctx = this.ctx;
    ctx.fillStyle = THEME.bar;
    ctx.fillRect(0, 0, W, TITLE_H);
    ctx.fillStyle = '#0b0c10';
    ctx.fillRect(0, TITLE_H - 1, W, 1);
    ['#ff5f57', '#febc2e', '#28c840'].forEach((c, i) => {
      ctx.fillStyle = c;
      ctx.beginPath();
      ctx.arc(28 + i * 26, TITLE_H / 2, 8, 0, Math.PI * 2);
      ctx.fill();
    });
    ctx.fillStyle = THEME.barText;
    ctx.font = `500 21px ${this.fontFamily}`;
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(this.title, W / 2, TITLE_H / 2 + 1);
    if (this.status) {
      ctx.textAlign = 'right';
      ctx.font = `500 18px ${this.fontFamily}`;
      ctx.fillText(this.status, W - 22, TITLE_H / 2 + 1);
    }
    ctx.textAlign = 'left';
  }

  // Box-drawing glyphs as strokes so rules and frames join seamlessly.
  boxGlyph(c, x, y, w, h, bold) {
    const ctx = this.ctx;
    const mx = x + w / 2, my = y + h / 2, lw = bold || c === '━' ? 2.6 : 1.6;
    const H = (a, b) => ctx.fillRect(a, my - lw / 2, b - a, lw);
    const V = (a, b) => ctx.fillRect(mx - lw / 2, a, lw, b - a);
    const arc = (sx, sy, ex, ey) => {
      ctx.lineWidth = lw;
      ctx.beginPath();
      ctx.moveTo(sx, sy);
      ctx.quadraticCurveTo(mx, my, ex, ey);
      ctx.stroke();
    };
    switch (c) {
      case '─': case '━': H(x, x + w + 0.5); return true;
      case '│': case '┃': V(y, y + h + 0.5); return true;
      case '╌': case '┄': case '┈': H(x + w * 0.1, x + w * 0.6); return true;
      case '╭': arc(mx, y + h, x + w, my); return true;
      case '╮': arc(x, my, mx, y + h); return true;
      case '╰': arc(mx, y, x + w, my); return true;
      case '╯': arc(x, my, mx, y); return true;
      case '┌': H(mx, x + w); V(my, y + h); return true;
      case '┐': H(x, mx); V(my, y + h); return true;
      case '└': H(mx, x + w); V(y, my); return true;
      case '┘': H(x, mx); V(y, my); return true;
      case '├': V(y, y + h); H(mx, x + w); return true;
      case '┤': V(y, y + h); H(x, mx); return true;
      default: return this.blockGlyph(c, x, y, w, h);
    }
  }

  // Block elements (Claude's logo, progress bars) as filled quadrants.
  blockGlyph(c, x, y, w, h) {
    const Q = {'█': 15, '▀': 3, '▄': 12, '▌': 5, '▐': 10, '▘': 1, '▝': 2, '▖': 4, '▗': 8, '▚': 9, '▞': 6,
      '▛': 7, '▜': 11, '▙': 13, '▟': 14};
    const q = Q[c];
    if (q === undefined) return false;
    const hw = w / 2, hh = h / 2, ctx = this.ctx;
    if (q & 1) ctx.fillRect(x, y, hw + 0.5, hh + 0.5);
    if (q & 2) ctx.fillRect(x + hw, y, hw + 0.5, hh + 0.5);
    if (q & 4) ctx.fillRect(x, y + hh, hw + 0.5, hh + 0.5);
    if (q & 8) ctx.fillRect(x + hw, y + hh, hw + 0.5, hh + 0.5);
    return true;
  }
}
