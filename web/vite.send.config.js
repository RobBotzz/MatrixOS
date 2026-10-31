import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { viteSingleFile } from "vite-plugin-singlefile";

// The public sending page (ADR-0018), published to GitHub Pages rather than
// compiled into the binary. Same single-file build as the configuration page.
export default defineConfig({
    plugins: [react(), viteSingleFile()],
    base: "./",
    build: {
        outDir: "dist-send",
        emptyOutDir: true,
        rollupOptions: { input: "send/index.html" },
        assetsInlineLimit: 100_000_000,
        cssCodeSplit: false,
    },
});
