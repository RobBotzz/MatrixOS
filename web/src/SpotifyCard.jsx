import { useCallback, useEffect, useRef, useState } from "react";

import { copyFrom } from "./copy.js";

const NOTES = {
    denied: { tone: "bad", text: "Spotify was not allowed. Try again if that was a mistake." },
    expired: { tone: "bad", text: "That took too long. Start linking again." },
    linking: { tone: "busy", text: "Spotify said yes. Linking…" },
};

/// ADR-0019: every owner links Spotify through their own Spotify app, so this
/// card walks them through creating one.
export default function SpotifyCard() {
    const [spotify, setSpotify] = useState(null);
    const [clientId, setClientId] = useState("");
    const [code, setCode] = useState("");
    const [note, setNote] = useState(() => NOTES[new URLSearchParams(location.search).get("spotify")] ?? null);
    const [busy, setBusy] = useState(false);
    const [copied, setCopied] = useState(false);
    const redirect = useRef(null);

    const load = useCallback(async () => {
        try {
            const response = await fetch("/api/spotify");
            const data = await response.json();
            setSpotify(data);
            if (data.link === "connected") setNote(null);
        } catch {
            // The status poll in App already tells the user the device is away.
        }
    }, []);

    useEffect(() => {
        load();
        const timer = setInterval(load, 5000);
        return () => clearInterval(timer);
    }, [load]);

    useEffect(() => {
        if (spotify && !clientId) setClientId(spotify.clientId);
    }, [spotify]);

    const post = (path, fields) =>
        fetch(path, {
            method: "POST",
            headers: { "Content-Type": "application/x-www-form-urlencoded" },
            body: new URLSearchParams(fields).toString(),
        });

    const connect = async () => {
        setBusy(true);
        setNote(null);
        try {
            const response = await post("/api/spotify/link", {
                clientId: clientId.trim().toLowerCase(),
                origin: window.location.origin,
            });
            if (!response.ok) throw new Error();
            const { authorizeUrl } = await response.json();
            window.location.assign(authorizeUrl);
        } catch {
            setNote({ tone: "bad", text: "That does not look like a Client ID: 32 letters and digits." });
            setBusy(false);
        }
    };

    const paste = async () => {
        const response = await post("/api/spotify/code", { code: code.trim() });
        setNote(
            response.ok
                ? NOTES.linking
                : { tone: "bad", text: "That code has expired. Press Connect Spotify again." },
        );
        setCode("");
        load();
    };

    const unlink = async () => {
        await post("/api/spotify/unlink", {});
        load();
    };

    if (!spotify) return null;

    const copy = () => {
        setCopied(copyFrom(redirect.current));
        window.setTimeout(() => setCopied(false), 2000);
    };

    return (
        <section className="card">
            <div className="card-head">
                <h2>Spotify</h2>
            </div>

            {note && <p className={`strip strip-${note.tone}`}>{note.text}</p>}

            {spotify.link === "connected" ? (
                <>
                    <p className="lead">
                        Linked.{" "}
                        {spotify.track ? (
                            <>
                                Playing <strong>{spotify.track}</strong>
                            </>
                        ) : (
                            "Open the Spotify app on the panel to see what is playing."
                        )}
                    </p>
                    <button className="ghost" onClick={unlink}>
                        Unlink Spotify
                    </button>
                </>
            ) : spotify.link === "connecting" ? (
                <p className="lead">Linking…</p>
            ) : (
                <>
                    {spotify.link === "failed" && (
                        <p className="strip strip-bad">Spotify ended the link. Connect again below.</p>
                    )}
                    <p className="lead muted">
                        The panel uses a Spotify app of your own, so it needs Spotify Premium and five
                        minutes, once.
                    </p>
                    <ol className="steps">
                        <li>
                            Open{" "}
                            <a href="https://developer.spotify.com/dashboard" target="_blank" rel="noreferrer">
                                developer.spotify.com/dashboard
                            </a>
                            , sign in and choose <strong>Create app</strong>. Name and description can
                            be anything.
                        </li>
                        <li>
                            As <strong>Redirect URI</strong>, add exactly:
                            <span className="field-input">
                                <input ref={redirect} readOnly value={spotify.redirectUri} />
                                <button type="button" className="ghost small" onClick={copy}>
                                    {copied ? "Copied" : "Copy"}
                                </button>
                            </span>
                        </li>
                        <li>
                            Tick <strong>Web API</strong>, accept the terms and save.
                        </li>
                        <li>
                            Copy the app's <strong>Client ID</strong> into this field:
                            <input
                                value={clientId}
                                placeholder="32 letters and digits"
                                onChange={(event) => setClientId(event.target.value)}
                            />
                        </li>
                    </ol>
                    <button className="primary" onClick={connect} disabled={busy || !clientId.trim()}>
                        Connect Spotify
                    </button>
                    <p className="lead muted">
                        Linking another person's Spotify account? Add their email under{" "}
                        <strong>User Management</strong> in your Spotify app first — up to five people.
                    </p>

                    <details className="paste">
                        <summary>Spotify showed you a code instead?</summary>
                        <span className="field-input">
                            <input
                                value={code}
                                placeholder="Paste the code"
                                onChange={(event) => setCode(event.target.value)}
                            />
                            <button type="button" className="ghost small" onClick={paste} disabled={!code.trim()}>
                                Use it
                            </button>
                        </span>
                    </details>
                </>
            )}
        </section>
    );
}
