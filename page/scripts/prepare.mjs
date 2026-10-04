import { cp, mkdir, readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';

const root = fileURLToPath(new URL('../..', import.meta.url));
const engine = resolve(process.argv[2] ?? `${root}/cmake-build-release/page-engine`);
const models = resolve(process.argv[3] ?? `${root}/cmake-build-release/page-model`);
const destination = resolve(root, 'cmake-build-release/page-site');
const metadata = JSON.parse(await readFile(resolve(models, 'model.json'), 'utf8'));
const model = await readFile(resolve(models, metadata.file));
if (createHash('sha256').update(model).digest('hex') !== metadata.sha256) throw new Error('Model digest does not match model.json');
await mkdir(resolve(destination, 'runtime'), { recursive: true });
await cp(resolve(engine, 'engine.mjs'), resolve(destination, 'engine.mjs'));
await cp(resolve(engine, 'engine.wasm'), resolve(destination, 'engine.wasm'));
await cp(resolve(models, metadata.file), resolve(destination, metadata.file));
await writeFile(resolve(destination, 'model.json'), JSON.stringify(metadata, null, 2) + '\n');
await cp(resolve(root, 'page/licenses'), resolve(destination, 'licenses'), { recursive: true });
// Keep the ONNX JavaScript and WASM runtime from the same locked package.
const runtime = resolve(root, 'page/node_modules/onnxruntime-web/dist');
for (const name of ['ort-wasm-simd-threaded.mjs', 'ort-wasm-simd-threaded.wasm']) await cp(resolve(runtime, name), resolve(destination, 'runtime', name));
console.log(`Prepared page assets: model v${metadata.version}`);
