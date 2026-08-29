import { defineConfig } from 'vite';
import preact from '@preact/preset-vite';
import tailwindcss from '@tailwindcss/vite';

// https://vitejs.dev/config/
export default defineConfig({
  plugins: [preact(), tailwindcss()],

  build: {
    // SPIFFS on the firmware caps path length at 32 chars and silently drops
    // files whose path exceeds it. Default Vite chunk names like
    // `assets/chartjs-plugin-annotation.esm-_AL1m4_O.js.gz` blow past that and
    // never make it to the device, leaving the served index.html referencing
    // nonexistent JS files. Emit hash-only filenames so every asset path stays
    // well under the limit.
    rollupOptions: {
      output: {
        entryFileNames: 'assets/[hash].js',
        chunkFileNames: 'assets/[hash].js',
        assetFileNames: 'assets/[hash][extname]',
        // The firmware's HTTP server (ESPAsyncWebServer over AsyncTCP) sends
        // Connection: close on every response and listens with a backlog of 5
        // on a 16-pcb lwIP pool, so each asset costs a fresh TCP connection
        // and a burst of parallel fetches drops SYNs. Route-level code
        // splitting made every navigation such a burst (55 lazy chunks), and
        // a dropped SYN on a dynamic import is fatal: the import rejects,
        // preact-iso has no retry, and the page body stays blank until a full
        // reload. Inlining dynamic imports ships one JS bundle fetched once
        // over one connection; navigation then costs zero network requests.
        inlineDynamicImports: true,
      },
    },
  },

  server: {
    proxy: {
      '/api': {
        target: 'http://localhost:8080',
        changeOrigin: true,
      },
      '/ws': {
        target: 'ws://localhost:8080',
        ws: true,
      },
    },
    watch: {
      usePolling: true,
    },
  },
});
