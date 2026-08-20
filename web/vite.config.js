import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { viteSingleFile } from "vite-plugin-singlefile";

// One self-contained index.html: CSS and JS inlined, no code splitting, no
// separate assets. The device compiles the result into its binary (ADR-0014),
// where a request waterfall would be the one thing our own server is weakest at.
export default defineConfig({
    plugins: [react(), viteSingleFile()],
    build: {
        outDir: "dist",
        emptyOutDir: true,
        assetsInlineLimit: 100_000_000,
        cssCodeSplit: false,
        reportCompressedSize: true,
    },
});
