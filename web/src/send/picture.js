import { decompressFrames, parseGIF } from "gifuct-js";

/// Must match src/os/picture.h (ADR-0018).
export const WIDTH = 64;
export const HEIGHT = 32;
export const MAX_FRAMES = 100;
const MIN_DELAY_MS = 20;

function panelCanvas() {
    const canvas = document.createElement("canvas");
    canvas.width = WIDTH;
    canvas.height = HEIGHT;
    return canvas;
}

/// Draws `source` onto a 64x32 frame: "fill" crops to cover the panel, "fit"
/// shows all of it with black bars. Returns the RGB bytes.
function toPanel(source, sourceWidth, sourceHeight, mode) {
    const canvas = panelCanvas();
    const context = canvas.getContext("2d", { willReadFrequently: true });
    context.fillStyle = "#000";
    context.fillRect(0, 0, WIDTH, HEIGHT);
    context.imageSmoothingEnabled = true;
    context.imageSmoothingQuality = "high";

    const scale =
        mode === "fill"
            ? Math.max(WIDTH / sourceWidth, HEIGHT / sourceHeight)
            : Math.min(WIDTH / sourceWidth, HEIGHT / sourceHeight);
    const width = sourceWidth * scale;
    const height = sourceHeight * scale;
    context.drawImage(source, (WIDTH - width) / 2, (HEIGHT - height) / 2, width, height);

    const rgba = context.getImageData(0, 0, WIDTH, HEIGHT).data;
    const rgb = new Uint8Array(WIDTH * HEIGHT * 3);
    for (let i = 0, j = 0; i < rgba.length; i += 4, j += 3) {
        rgb[j] = rgba[i];
        rgb[j + 1] = rgba[i + 1];
        rgb[j + 2] = rgba[i + 2];
    }
    return rgb;
}

/// Every GIF frame composed onto the full logical screen, honouring the
/// "restore to background" disposal; the others leave the canvas as it is.
function composeGif(buffer) {
    const gif = parseGIF(buffer);
    const frames = decompressFrames(gif, true);
    const width = gif.lsd.width;
    const height = gif.lsd.height;

    const screen = document.createElement("canvas");
    screen.width = width;
    screen.height = height;
    const context = screen.getContext("2d");
    const patch = document.createElement("canvas");
    const patchContext = patch.getContext("2d");

    const composed = [];
    let previous = null;
    for (const frame of frames) {
        if (previous && previous.disposalType === 2) {
            const { left, top, width: w, height: h } = previous.dims;
            context.clearRect(left, top, w, h);
        }

        const { left, top, width: w, height: h } = frame.dims;
        patch.width = w;
        patch.height = h;
        patchContext.putImageData(new ImageData(frame.patch, w, h), 0, 0);
        context.drawImage(patch, left, top);

        const copy = document.createElement("canvas");
        copy.width = width;
        copy.height = height;
        copy.getContext("2d").drawImage(screen, 0, 0);
        composed.push({ canvas: copy, delay: frame.delay });
        previous = frame;
    }
    return { frames: composed, width, height };
}

/// Too many frames are thinned out evenly, keeping the total running time.
function thin(frames) {
    if (frames.length <= MAX_FRAMES) return frames;
    const step = Math.ceil(frames.length / MAX_FRAMES);
    const kept = [];
    for (let i = 0; i < frames.length; i += step) {
        const group = frames.slice(i, i + step);
        kept.push({ ...group[0], delay: group.reduce((sum, f) => sum + f.delay, 0) });
    }
    return kept;
}

/// Returns `{ frames: [{ rgb, delay }] }`, ready for preview and for `encode`.
export async function convert(file, mode) {
    if (file.type === "image/gif") {
        const { frames, width, height } = composeGif(await file.arrayBuffer());
        if (frames.length > 1) {
            return {
                frames: thin(frames).map((frame) => ({
                    rgb: toPanel(frame.canvas, width, height, mode),
                    delay: Math.min(Math.max(frame.delay, MIN_DELAY_MS), 65535),
                })),
            };
        }
    }

    const bitmap = await createImageBitmap(file);
    const rgb = toPanel(bitmap, bitmap.width, bitmap.height, mode);
    bitmap.close();
    return { frames: [{ rgb, delay: 0 }] };
}

/// "MXP1", width, height, frame count (u16 LE), then per frame its delay
/// (u16 LE) and the RGB bytes.
export function encode(picture) {
    const frameBytes = WIDTH * HEIGHT * 3;
    const bytes = new Uint8Array(8 + picture.frames.length * (2 + frameBytes));
    bytes.set([0x4d, 0x58, 0x50, 0x31, WIDTH, HEIGHT], 0);
    const view = new DataView(bytes.buffer);
    view.setUint16(6, picture.frames.length, true);

    let at = 8;
    for (const frame of picture.frames) {
        view.setUint16(at, frame.delay, true);
        bytes.set(frame.rgb, at + 2);
        at += 2 + frameBytes;
    }
    return bytes;
}
