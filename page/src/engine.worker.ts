import * as ort from 'onnxruntime-web/wasm';
import type { Command, Engine, Event, Model, Module } from './types';

let module: Module;
let engine: Engine;
let session: ort.InferenceSession;
let model: Model;
let generation = 0;
let computation: Promise<void> = Promise.resolve();

function send(event: Event): void {
  self.postMessage(event);
}

function state(thinking: boolean, seconds = 0): void {
  send({ type: 'state', position: JSON.parse(engine.state()), thinking, seconds });
}

async function load(base: string): Promise<void> {
  send({ type: 'loading', text: '正在加载对弈引擎', fraction: 0 });
  const factory = await import(/* @vite-ignore */ `${base}engine.mjs`);
  module = await factory.default({ locateFile: (name: string) => `${base}${name}` });
  engine = new module.Engine();
  state(false);
  const response = await fetch(`${base}model.json`);
  if (!response.ok) throw new Error(`模型信息加载失败：HTTP ${response.status}`);
  model = await response.json();
  if (model.encoding !== 'history8-rules10-actions4500-v1' || model.rules !== 'WXF-2018' || model.simulations !== 32) throw new Error('模型与当前对弈引擎不匹配');
  const cache = await caches.open('xiangqi-models');
  const url = `${base}${model.file}`;
  const cached = await cache.match(url);
  let data: ArrayBuffer;
  if (cached) {
    send({ type: 'loading', text: '正在读取缓存模型', fraction: 1 });
    data = await cached.arrayBuffer();
  } else {
    const download = await fetch(url);
    if (!download.ok) throw new Error(`模型下载失败：HTTP ${download.status}`);
    const reader = download.body!.getReader();
    const bytes = new Uint8Array(model.bytes);
    let offset = 0;
    while (true) {
      const chunk = await reader.read();
      if (chunk.done) break;
      bytes.set(chunk.value, offset);
      offset += chunk.value.length;
      send({ type: 'loading', text: `正在下载模型 · ${(offset / 1048576).toFixed(1)} / ${(model.bytes / 1048576).toFixed(1)} MB`, fraction: offset / model.bytes });
    }
    data = bytes.buffer;
    await cache.put(url, new Response(data, { headers: { 'Content-Type': 'application/octet-stream' } }));
  }
  send({ type: 'loading', text: '正在初始化 CPU 推理', fraction: 1 });
  ort.env.wasm.numThreads = 1;
  ort.env.wasm.wasmPaths = `${base}runtime/`;
  session = await ort.InferenceSession.create(data, { executionProviders: ['wasm'], graphOptimizationLevel: 'all' });
  send({ type: 'ready', model, cached: Boolean(cached) });
  state(false);
}

async function search(id: number): Promise<void> {
  if (id !== generation) return;
  const started = performance.now();
  engine.begin();
  while (engine.advance()) {
    const pointer = engine.observation();
    if (pointer) {
      const observation = module.HEAPF32.slice(pointer / 4, pointer / 4 + 128 * 90);
      const output = await session.run({ observation: new ort.Tensor('float32', observation, [1, 128, 10, 9]) });
      if (id !== generation) return;
      const address = engine.predictionAddress / 4;
      module.HEAPF32.set(output.policy.data as Float32Array, address);
      module.HEAPF32.set(output.wdl.data as Float32Array, address + 4500);
      engine.accept();
    }
    send({ type: 'progress', completed: engine.completed, total: model.simulations, seconds: (performance.now() - started) / 1000 });
    await new Promise(resolve => setTimeout(resolve, 0));
    if (id !== generation) return;
  }
  engine.commit();
  state(false, (performance.now() - started) / 1000);
}

self.onmessage = (message: MessageEvent<Command>): void => {
  const command = message.data;
  try {
    if (command.type === 'load') {
      void load(command.base).catch(error => send({ type: 'error', text: String(error) }));
    } else if (command.type === 'restart') {
      ++generation;
      engine.restart();
      state(false);
    } else {
      const id = ++generation;
      engine.play(command.from, command.to);
      const ended = JSON.parse(engine.state()).outcome !== 0;
      state(!ended);
      if (!ended) computation = computation.then(() => search(id)).catch(error => send({ type: 'error', text: String(error) }));
    }
  } catch (error) {
    send({ type: 'error', text: String(error) });
  }
};
