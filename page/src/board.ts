import type { Position } from './types';

const red = ['', '帅', '仕', '相', '马', '车', '炮', '兵'];
const black = ['', '将', '士', '象', '马', '车', '炮', '卒'];

export function createBoard(svg: SVGSVGElement, pick: (square: number) => void): void {
  let lines = '';
  for (let row = 0; row < 10; ++row) lines += `<path d="M62 ${66 + row * 52}H478"/>`;
  for (let column = 0; column < 9; ++column) {
    const x = 62 + column * 52;
    lines += `<path d="M${x} 66V274 M${x} 326V534"/>`;
  }
  lines += '<path d="M62 274V326 M478 274V326 M218 66L322 170 M322 66L218 170 M218 430L322 534 M322 430L218 534"/>';
  let marks = '';
  for (const [column, row] of [[1, 2], [7, 2], [1, 7], [7, 7], ...[0, 2, 4, 6, 8].flatMap(column => [[column, 3], [column, 6]])]) {
    const x = 62 + column * 52, y = 66 + row * 52;
    for (const dx of [-1, 1]) {
      if (column === 0 && dx === -1 || column === 8 && dx === 1) continue;
      for (const dy of [-1, 1]) marks += `<path d="M${x + dx * 11} ${y + dy * 4}H${x + dx * 4}V${y + dy * 11}"/>`;
    }
  }
  svg.innerHTML = `<defs>
    <linearGradient id="board-paper" x2=".85" y2="1"><stop stop-color="#f3e8ce"/><stop offset="1" stop-color="#dfcda9"/></linearGradient>
    <radialGradient id="piece-paper" cx=".35" cy=".25" r=".85"><stop stop-color="#fff8e6"/><stop offset="1" stop-color="#ebddbc"/></radialGradient>
    <filter id="piece-shadow" x="-30%" y="-30%" width="160%" height="180%"><feDropShadow dx="0" dy="2" stdDeviation="1.6" flood-color="#695031" flood-opacity=".28"/></filter>
  </defs><rect x="5" y="5" width="530" height="590" rx="14" fill="url(#board-paper)"/>
  <rect x="20" y="20" width="500" height="560" rx="8" fill="none" stroke="#a88e66" stroke-opacity=".36"/>
  <g fill="none" stroke="#947c55" stroke-width="1.25">${lines}${marks}</g>
  <g class="river" fill="#927954" text-anchor="middle"><text x="170" y="309">楚 河</text><text x="370" y="309">汉 界</text></g>
  <g id="last-move"></g><g id="pieces"></g><g id="destinations"></g>`;
  svg.addEventListener('click', event => {
    const target = (event.target as Element).closest<SVGGElement>('[data-square]');
    if (target) pick(Number(target.dataset.square));
  });
  svg.addEventListener('keydown', event => {
    if (event.key !== 'Enter' && event.key !== ' ') return;
    event.preventDefault();
    const target = (event.target as Element).closest<SVGGElement>('[data-square]');
    if (target) pick(Number(target.dataset.square));
  });
}

export function renderBoard(svg: SVGSVGElement, position: Position, selected: number | null, interactive: boolean): void {
  let pieces = '';
  for (let square = 0; square < 90; ++square) {
    const piece = position.board[square], x = 62 + square % 9 * 52, y = 534 - Math.floor(square / 9) * 52;
    const character = piece > 0 ? red[piece] : black[-piece];
    const label = piece ? `${piece > 0 ? '红方' : '黑方'}${character}` : '空位';
    pieces += `<g data-square="${square}" transform="translate(${x} ${y})" ${interactive ? `role="button" tabindex="0" aria-label="${label} ${String.fromCharCode(97 + square % 9)}${Math.floor(square / 9)}"` : ''}>
      <circle r="25" fill="transparent"/>${piece ? `<circle r="21.5" fill="url(#piece-paper)" filter="url(#piece-shadow)"/>
      <circle r="18.4" fill="none" stroke="${piece > 0 ? '#a34235' : '#414b40'}" stroke-opacity=".6" stroke-width="1"/>
      <text class="piece-character" y="1" fill="${piece > 0 ? '#aa3e32' : '#354536'}">${character}</text>` : ''}
      ${selected === square ? '<circle r="24" fill="none" stroke="#5e8065" stroke-width="3"/>' : ''}</g>`;
  }
  svg.querySelector('#pieces')!.innerHTML = pieces;
  svg.querySelector('#last-move')!.innerHTML = (position.last ?? []).map(square => `<rect x="${62 + square % 9 * 52 - 24}" y="${534 - Math.floor(square / 9) * 52 - 24}" width="48" height="48" rx="10" fill="#8d7340" fill-opacity=".16"/>`).join('');
  svg.querySelector('#destinations')!.innerHTML = selected === null ? '' : position.moves.filter(move => move[0] === selected).map(([, square]) => `<circle cx="${62 + square % 9 * 52}" cy="${534 - Math.floor(square / 9) * 52}" r="${position.board[square] ? 23.5 : 5}" fill="${position.board[square] ? 'none' : '#58775c'}" stroke="#58775c" stroke-width="2" pointer-events="none"/>`).join('');
  svg.classList.toggle('interactive', interactive);
}
