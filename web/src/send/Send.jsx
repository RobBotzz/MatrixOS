import { useEffect, useRef, useState } from "react";

import Logo from "../Logo.jsx";
import GifSearch, { GIPHY_KEY } from "./GifSearch.jsx";
import {
    HEIGHT,
    WIDTH,
    clampCrop,
    decode,
    encode,
    fillZoom,
    maxZoom,
    panCrop,
    presetCrop,
    renderFrame,
} from "./picture.js";

const NTFY = "https://ntfy.sh";
const TOPIC = /^matrixos-[a-z0-9]{20}$/;

/// The panel as it will look. Drag to move the crop, pinch or scroll to zoom.
function Preview({ decoded, crop, onCrop }) {
    const canvas = useRef(null);
    const cropRef = useRef(crop);
    const frameRef = useRef(0);
    const pointers = useRef(new Map());
    const pinch = useRef(null);

    const draw = () => {
        const context = canvas.current.getContext("2d");
        const rgb = renderFrame(decoded, decoded.frames[frameRef.current], cropRef.current);
        const image = context.createImageData(WIDTH, HEIGHT);
        for (let i = 0, j = 0; j < rgb.length; i += 4, j += 3) {
            image.data[i] = rgb[j];
            image.data[i + 1] = rgb[j + 1];
            image.data[i + 2] = rgb[j + 2];
            image.data[i + 3] = 255;
        }
        context.putImageData(image, 0, 0);
    };

    useEffect(() => {
        cropRef.current = crop;
        draw();
    }, [crop]);

    useEffect(() => {
        frameRef.current = 0;
        if (decoded.frames.length < 2) return undefined;
        let timer = null;
        const tick = () => {
            frameRef.current = (frameRef.current + 1) % decoded.frames.length;
            draw();
            timer = window.setTimeout(tick, decoded.frames[frameRef.current].delay);
        };
        timer = window.setTimeout(tick, decoded.frames[0].delay);
        return () => window.clearTimeout(timer);
    }, [decoded]);

    useEffect(() => {
        const element = canvas.current;
        const wheel = (event) => {
            event.preventDefault();
            const zoom = cropRef.current.zoom * Math.exp(-event.deltaY * 0.002);
            onCrop(clampCrop(decoded, { ...cropRef.current, zoom }));
        };
        element.addEventListener("wheel", wheel, { passive: false });
        return () => element.removeEventListener("wheel", wheel);
    }, [decoded, onCrop]);

    const distance = () => {
        const [a, b] = [...pointers.current.values()];
        return Math.hypot(a.x - b.x, a.y - b.y);
    };

    const down = (event) => {
        event.currentTarget.setPointerCapture(event.pointerId);
        pointers.current.set(event.pointerId, { x: event.clientX, y: event.clientY });
        pinch.current = pointers.current.size === 2 ? distance() : null;
    };

    const move = (event) => {
        const last = pointers.current.get(event.pointerId);
        if (!last) return;
        const next = { x: event.clientX, y: event.clientY };
        pointers.current.set(event.pointerId, next);

        if (pointers.current.size === 2 && pinch.current) {
            const now = distance();
            const zoom = cropRef.current.zoom * (now / pinch.current);
            pinch.current = now;
            onCrop(clampCrop(decoded, { ...cropRef.current, zoom }));
        } else if (pointers.current.size === 1) {
            const pixels = WIDTH / canvas.current.clientWidth;
            onCrop(panCrop(decoded, cropRef.current, (next.x - last.x) * pixels, (next.y - last.y) * pixels));
        }
    };

    const up = (event) => {
        pointers.current.delete(event.pointerId);
        pinch.current = null;
    };

    return (
        <canvas
            ref={canvas}
            width={WIDTH}
            height={HEIGHT}
            className="preview preview-interactive"
            onPointerDown={down}
            onPointerMove={move}
            onPointerUp={up}
            onPointerCancel={up}
        />
    );
}

export default function Send() {
    const topic = window.location.hash.slice(1);
    const [file, setFile] = useState(null);
    const [decoded, setDecoded] = useState(null);
    const [crop, setCrop] = useState(null);
    const [state, setState] = useState({ tone: null, text: null });
    const [busy, setBusy] = useState(false);

    useEffect(() => {
        if (!file) return;
        let cancelled = false;
        setState({ tone: null, text: null });
        decode(file)
            .then((result) => {
                if (cancelled) return;
                setDecoded(result);
                setCrop(presetCrop(result, "fill"));
            })
            .catch(() => !cancelled && setState({ tone: "bad", text: "This file could not be read as a picture." }));
        return () => {
            cancelled = true;
        };
    }, [file]);

    const send = async () => {
        setBusy(true);
        setState({ tone: "busy", text: "Sending…" });
        try {
            const response = await fetch(`${NTFY}/${topic}`, {
                method: "PUT",
                headers: { Filename: "picture.mxp" },
                body: new Blob([encode(decoded, crop)], { type: "application/octet-stream" }),
            });
            if (!response.ok) throw new Error();
            setState({ tone: "ok", text: "Sent. It shows up on the panel within a minute or two." });
        } catch {
            setState({ tone: "bad", text: "Sending failed. Check the connection and try again." });
        } finally {
            setBusy(false);
        }
    };

    if (!TOPIC.test(topic)) {
        return (
            <main className="shell">
                <header className="masthead">
                    <Logo />
                    <div className="masthead-text">
                        <h1>MatrixOS</h1>
                        <p className="subtitle">Send a picture</p>
                    </div>
                </header>
                <p className="strip strip-bad">
                    This link is incomplete. Ask the owner of the MatrixOS for their full send link.
                </p>
            </main>
        );
    }

    return (
        <main className="shell">
            <header className="masthead">
                <Logo />
                <div className="masthead-text">
                    <h1>MatrixOS</h1>
                    <p className="subtitle">Send a picture to this panel</p>
                </div>
            </header>

            {GIPHY_KEY && <GifSearch onPick={setFile} disabled={busy} />}

            <section className="card">
                <label className="field">
                    <span>{GIPHY_KEY ? "Or upload your own picture or GIF" : "Picture or GIF"}</span>
                    <input
                        type="file"
                        accept="image/*"
                        onChange={(event) => setFile(event.target.files?.[0] ?? null)}
                    />
                </label>

                {decoded && crop && (
                    <>
                        <Preview decoded={decoded} crop={crop} onCrop={setCrop} />
                        <p className="hint-line">Drag to move · pinch, scroll or slide to zoom</p>
                        <input
                            type="range"
                            className="zoom"
                            min={1}
                            max={maxZoom(decoded)}
                            step="any"
                            value={crop.zoom}
                            onChange={(event) =>
                                setCrop(clampCrop(decoded, { ...crop, zoom: Number(event.target.value) }))
                            }
                            disabled={busy}
                        />
                        <div className="row">
                            <button
                                className={Math.abs(crop.zoom - fillZoom(decoded)) < 0.001 ? "primary" : "ghost"}
                                onClick={() => setCrop(presetCrop(decoded, "fill"))}
                                disabled={busy}
                            >
                                Fill the panel
                            </button>
                            <button
                                className={crop.zoom === 1 ? "primary" : "ghost"}
                                onClick={() => setCrop(presetCrop(decoded, "fit"))}
                                disabled={busy}
                            >
                                Show all of it
                            </button>
                        </div>
                        <button className="primary" onClick={send} disabled={busy}>
                            Send
                        </button>
                    </>
                )}

                {state.text && <p className={`strip strip-${state.tone}`}>{state.text}</p>}
            </section>

            <p className="footer">
                Pictures travel through ntfy.sh and wait there for up to three hours, until the panel
                collects them. Anyone with this link can send to this panel.
            </p>
        </main>
    );
}
