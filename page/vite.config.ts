import { defineConfig } from 'vite';

export default defineConfig({
  base: './',
  publicDir: false,
  resolve: { conditions: ['onnxruntime-web-use-extern-wasm', 'module', 'browser', 'development|production'] },
  worker: { format: 'es' },
  build: { outDir: '../cmake-build-release/page-site', emptyOutDir: true }
});
