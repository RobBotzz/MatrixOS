import { useEffect, useRef, useState } from "react";

import Logo from "../Logo.jsx";
import GifSearch, { GIPHY_KEY } from "./GifSearch.jsx";
import { HEIGHT, WIDTH, convert, encode } from "./picture.js";

const NTFY = "https://ntfy.sh";
const TOPIC = /^matrixos-[a-z0-9]{20}$/;

function Preview({ picture }) {
    const canvas = useRef(null);

    useEffect(() => {
        if (!picture) return undefined;
        const context = canvas.current.getContext("2d");
        const image = context.createImageData(WIDTH, HEIGHT);
        let index = 0;
        let timer = null;

        const draw = () => {
            const { rgb, delay } = picture.frames[index];
            for (let i = 0, j = 0; j < rgb.length; i += 4, j += 3) {
                image.data[i] = rgb[j];
                image.data[i + 1] = rgb[j + 1];
                image.data[i + 2] = rgb[j + 2];
                image.data[i + 3] = 255;
            }
            context.putImageData(image, 0, 0);
            if (picture.frames.length > 1) {
                index = (index + 1) % picture.frames.length;
                timer = window.setTimeout(draw, delay);
            }
        };
        draw();
        return () => window.clearTimeout(timer);
    }, [picture]);

    return <canvas ref={canvas} width={WIDTH} height={HEIGHT} className="preview" />;
}

export default function Send() {
    const topic = window.location.hash.slice(1);
    const [file, setFile] = useState(null);
    const [mode, setMode] = useState("fill");
    const [picture, setPicture] = useState(null);
    const [state, setState] = useState({ tone: null, text: null });
    const [busy, setBusy] = useState(false);

    useEffect(() => {
        if (!file) return;
        let cancelled = false;
        setState({ tone: null, text: null });
        convert(file, mode)
            .then((result) => !cancelled && setPicture(result))
            .catch(() => !cancelled && setState({ tone: "bad", text: "This file could not be read as a picture." }));
        return () => {
            cancelled = true;
        };
    }, [file, mode]);

    const send = async () => {
        setBusy(true);
        setState({ tone: "busy", text: "Sending…" });
        try {
            const response = await fetch(`${NTFY}/${topic}`, {
                method: "PUT",
                headers: { Filename: "picture.mxp" },
                body: new Blob([encode(picture)], { type: "application/octet-stream" }),
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

                {picture && (
                    <>
                        <Preview picture={picture} />
                        <div className="row">
                            <button
                                className={mode === "fill" ? "primary" : "ghost"}
                                onClick={() => setMode("fill")}
                                disabled={busy}
                            >
                                Fill the panel
                            </button>
                            <button
                                className={mode === "fit" ? "primary" : "ghost"}
                                onClick={() => setMode("fit")}
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
