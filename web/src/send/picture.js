import { decompressFrames, parseGIF } from "gifuct-js";

/// Must match src/os/picture.h (ADR-0018).
export const WIDTH = 64;
export const HEIGHT = 32;
export const MAX_FRAMES = 100;
const MIN_DELAY_MS = 20;

/// How far past "fill" the zoom goes.
const MAX_ZOOM_PAST_FILL = 4;

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
        composed.push({ source: copy, delay: frame.delay });
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

/// Decodes once: `{ frames: [{ source, delay }], width, height }` at full size.
/// Cropping happens later, as often as the user drags.
export async function decode(file) {
    if (file.type === "image/gif") {
        const gif = composeGif(await file.arrayBuffer());
        if (gif.frames.length > 1) {
            const frames = thin(gif.frames).map((frame) => ({
                source: frame.source,
                delay: Math.min(Math.max(frame.delay, MIN_DELAY_MS), 65535),
            }));
            return { frames, width: gif.width, height: gif.height };
        }
    }

    const bitmap = await createImageBitmap(file);
    return { frames: [{ source: bitmap, delay: 0 }], width: bitmap.width, height: bitmap.height };
}

/// The crop is a zoom relative to "show all of it" and the point of the
/// picture, in 0..1, that sits in the middle of the panel.
function fitScale(decoded) {
    return Math.min(WIDTH / decoded.width, HEIGHT / decoded.height);
}

export function fillZoom(decoded) {
    return Math.max(WIDTH / decoded.width, HEIGHT / decoded.height) / fitScale(decoded);
}

export function maxZoom(decoded) {
    return fillZoom(decoded) * MAX_ZOOM_PAST_FILL;
}

/// Keeps the panel inside the picture wherever the picture is larger than the
/// panel, and centred where it is smaller.
export function clampCrop(decoded, crop) {
    const zoom = Math.min(Math.max(crop.zoom, 1), maxZoom(decoded));
    const scale = fitScale(decoded) * zoom;
    const axis = (centre, size, panel) => {
        const half = panel / scale / 2 / size;
        return half >= 0.5 ? 0.5 : Math.min(Math.max(centre, half), 1 - half);
    };
    return {
        zoom,
        x: axis(crop.x, decoded.width, WIDTH),
        y: axis(crop.y, decoded.height, HEIGHT),
    };
}

export function presetCrop(decoded, preset) {
    return { zoom: preset === "fill" ? fillZoom(decoded) : 1, x: 0.5, y: 0.5 };
}

/// Moves the crop by a distance measured in panel pixels.
export function panCrop(decoded, crop, dx, dy) {
    const scale = fitScale(decoded) * crop.zoom;
    return clampCrop(decoded, {
        ...crop,
        x: crop.x - dx / scale / decoded.width,
        y: crop.y - dy / scale / decoded.height,
    });
}

/// One frame, cropped, as 64x32 RGB bytes.
export function renderFrame(decoded, frame, crop) {
    const canvas = document.createElement("canvas");
    canvas.width = WIDTH;
    canvas.height = HEIGHT;
    const context = canvas.getContext("2d", { willReadFrequently: true });
    context.fillStyle = "#000";
    context.fillRect(0, 0, WIDTH, HEIGHT);
    context.imageSmoothingEnabled = true;
    context.imageSmoothingQuality = "high";

    const scale = fitScale(decoded) * crop.zoom;
    const width = decoded.width * scale;
    const height = decoded.height * scale;
    context.drawImage(
        frame.source,
        WIDTH / 2 - crop.x * width,
        HEIGHT / 2 - crop.y * height,
        width,
        height,
    );

    const rgba = context.getImageData(0, 0, WIDTH, HEIGHT).data;
    const rgb = new Uint8Array(WIDTH * HEIGHT * 3);
    for (let i = 0, j = 0; i < rgba.length; i += 4, j += 3) {
        rgb[j] = rgba[i];
        rgb[j + 1] = rgba[i + 1];
        rgb[j + 2] = rgba[i + 2];
    }
    return rgb;
}

/// "MXP1", width, height, frame count (u16 LE), then per frame its delay
/// (u16 LE) and the RGB bytes — every frame cropped the same way.
export function encode(decoded, crop) {
    const frameBytes = WIDTH * HEIGHT * 3;
    const bytes = new Uint8Array(8 + decoded.frames.length * (2 + frameBytes));
    bytes.set([0x4d, 0x58, 0x50, 0x31, WIDTH, HEIGHT], 0);
    const view = new DataView(bytes.buffer);
    view.setUint16(6, decoded.frames.length, true);

    let at = 8;
    for (const frame of decoded.frames) {
        view.setUint16(at, frame.delay, true);
        bytes.set(renderFrame(decoded, frame, crop), at + 2);
        at += 2 + frameBytes;
    }
    return bytes;
}
