import './style.css';
import { createBoard, renderBoard } from './board';
import type { Command, Event, Position } from './types';

const board = document.querySelector<SVGSVGElement>('#board')!;
const restart = document.querySelector<HTMLButtonElement>('#restart')!;
const progress = document.querySelector<HTMLProgressElement>('#progress')!;
const worker = new Worker(new URL('./engine.worker.ts', import.meta.url), { type: 'module' });
let position: Position;
let selected: number | null = null;
let ready = false;
let thinking = false;
const reasons = ['', '将死', '困毙', '长将', '长捉', '重复局面', '自然限着', '双方均无法获胜'];

function text(id: string, value: string): void {
  document.getElementById(id)!.textContent = value;
}

function send(command: Command): void {
  worker.postMessage(command);
}

function paint(): void {
  const interactive = ready && !thinking && position.turn === 0 && position.outcome === 0;
  renderBoard(board, position, selected, interactive);
}

function fail(message: string): void {
  ready = false;
  thinking = false;
  restart.disabled = true;
  text('phase', '运行错误');
  text('message', '对弈暂时无法继续');
  text('detail', message);
  text('opponent-status', '运行错误');
  document.querySelector('.status-card')!.classList.add('error');
  if (position) paint();
}

createBoard(board, square => {
  if (!ready || thinking || position.outcome !== 0 || position.turn !== 0) return;
  if (selected !== null && position.moves.some(move => move[0] === selected && move[1] === square)) {
    send({ type: 'play', from: selected, to: square });
    selected = null;
    thinking = true;
  } else {
    selected = position.board[square] > 0 && selected !== square ? square : null;
    text('detail', selected === null ? '选中红方棋子，再点击落点。' : '点击标记的落点。');
  }
  paint();
});

restart.addEventListener('click', () => {
  selected = null;
  thinking = false;
  text('thinking-time', '');
  send({ type: 'restart' });
});

worker.onmessage = (message: MessageEvent<Event>): void => {
  const event = message.data;
  if (event.type === 'loading') {
    progress.value = event.fraction;
    text('detail', event.text);
    text('progress-label', event.fraction < 1 ? `加载 ${Math.round(event.fraction * 100)}%` : '模型初始化中');
  } else if (event.type === 'ready') {
    ready = true;
    restart.disabled = false;
    text('model-version', `v${event.model.version}`);
    text('cache-note', event.cached ? '模型已从本地缓存加载。' : '模型已下载并缓存，下次无需重复下载。');
  } else if (event.type === 'state') {
    position = event.position;
    thinking = event.thinking;
    selected = null;
    text('move-number', `第 ${Math.floor(position.plies / 2) + 1} 回合`);
    if (ready) {
      const ended = position.outcome !== 0;
      text('phase', ended ? '对局结束' : thinking ? 'AI 思考中' : '轮到你');
      text('message', ended ? ['', '你赢了', 'AI 获胜', '本局和棋'][position.outcome] : thinking ? '正在寻找下一步' : position.check ? '将军，请应将' : '请走下一步');
      text('detail', ended ? reasons[position.reason] : thinking ? '你可以随时重新开局，取消本次思考。' : '选中红方棋子，再点击落点。');
      text('opponent-status', ended ? '对局结束' : thinking ? '思考中' : '等待你落子');
      text('progress-label', thinking ? '搜索 0 / 32' : ended ? '重新开局，再来一盘' : '你执红 · 落子无悔');
      progress.value = thinking ? 0 : 1;
      if (event.seconds) text('thinking-time', `上一步 ${event.seconds.toFixed(2)} 秒`);
      document.getElementById('phase-dot')!.classList.toggle('thinking', thinking);
    }
    paint();
  } else if (event.type === 'progress') {
    progress.value = event.completed / event.total;
    text('progress-label', `搜索 ${event.completed} / ${event.total}`);
    text('thinking-time', `${event.seconds.toFixed(1)} 秒`);
  } else fail(event.text);
};
worker.onerror = event => fail(event.message);
send({ type: 'load', base: new URL(import.meta.env.BASE_URL, window.location.href).href });
