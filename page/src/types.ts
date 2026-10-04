export interface Position {
  turn: number;
  outcome: number;
  reason: number;
  check: boolean;
  plies: number;
  board: number[];
  moves: [number, number][];
  last: [number, number] | null;
}

export interface Model {
  version: number;
  file: string;
  sha256: string;
  bytes: number;
  encoding: string;
  rules: string;
  simulations: number;
}

export interface Engine {
  predictionAddress: number;
  completed: number;
  state(): string;
  restart(): void;
  play(from: number, to: number): void;
  begin(): void;
  advance(): boolean;
  observation(): number;
  accept(): void;
  commit(): void;
}

export interface Module {
  HEAPF32: Float32Array;
  Engine: new () => Engine;
}

export type Command = { type: 'load'; base: string } | { type: 'restart' } | { type: 'play'; from: number; to: number };
export type Event =
  | { type: 'loading'; text: string; fraction: number }
  | { type: 'ready'; model: Model; cached: boolean }
  | { type: 'state'; position: Position; thinking: boolean; seconds: number }
  | { type: 'progress'; completed: number; total: number; seconds: number }
  | { type: 'error'; text: string };
