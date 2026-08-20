import { useCallback, useEffect, useRef, useState } from "react";

import Logo from "./Logo.jsx";
import NetworkPicker from "./NetworkPicker.jsx";

/// How the panel's own states read on a screen. The keys are the ones
/// os/provisioning.cpp emits, so a state added there shows up as "unknown"
/// rather than as a blank badge.
const STATES = {
    connected: { label: "Online", tone: "ok" },
    connecting: { label: "Connecting", tone: "busy" },
    setup: { label: "Setup mode", tone: "warn" },
    waiting: { label: "Looking for the network", tone: "busy" },
    failed: { label: "Not connected", tone: "bad" },
    unmanaged: { label: "WiFi not managed", tone: "idle" },
};

async function post(path, body) {
    const response = await fetch(path, {
        method: "POST",
        headers: body ? { "Content-Type": "application/x-www-form-urlencoded" } : {},
        body,
    });
    if (!response.ok) {
        throw new Error(`${path} answered ${response.status}`);
    }
    return response;
}

export default function App() {
    const [status, setStatus] = useState(null);
    const [unreachable, setUnreachable] = useState(false);
    const [busy, setBusy] = useState(false);
    const [scanning, setScanning] = useState(false);
    const [note, setNote] = useState(null);
    const [confirmingReset, setConfirmingReset] = useState(false);
    const scannedOnce = useRef(false);

    const refresh = useCallback(async () => {
        try {
            const response = await fetch("/api/status");
            setStatus(await response.json());
            setUnreachable(false);
        } catch {
            // Expected while the device switches radio modes: it drops off this
            // network for a few seconds. Saying so beats an error dialog.
            setUnreachable(true);
        }
    }, []);

    useEffect(() => {
        refresh();
        const timer = setInterval(refresh, 3000);
        return () => clearInterval(timer);
    }, [refresh]);

    /// A scan takes seconds on the device, and POST /api/scan returns before it
    /// finishes — the results arrive with a later status poll. So the flag is
    /// cleared on a timer rather than on the response.
    const scan = useCallback(async () => {
        setScanning(true);
        try {
            await post("/api/scan");
        } catch {
            // The next status poll shows whether anything came of it.
        }
        window.setTimeout(() => setScanning(false), 8000);
    }, []);

    // A device that is already online has never scanned — the scan happens when
    // the access point comes up, which on this device it did not. Without this,
    // opening the page shows an empty list and a button, which reads as "broken"
    // rather than as "ask me".
    useEffect(() => {
        if (!status || scannedOnce.current) return;
        if (status.state === "unmanaged") return;
        if ((status.networks ?? []).length > 0) return;

        scannedOnce.current = true;
        scan();
    }, [status, scan]);

    const act = async (work, message) => {
        setBusy(true);
        setNote(null);
        try {
            await work();
            if (message) {
                setNote({ tone: "ok", text: message });
            }
        } catch (error) {
            setNote({ tone: "bad", text: error.message });
        } finally {
            setBusy(false);
            refresh();
        }
    };

    const connect = (ssid, psk) =>
        act(
            () =>
                post(
                    "/connect",
                    new URLSearchParams({ ssid, psk, source: "config" }).toString(),
                ),
            `Joining ${ssid}…`,
        );

    const reset = () =>
        act(async () => {
            await post("/api/reset");
            setConfirmingReset(false);
        }, "Reset. The device is opening its setup network.");

    if (!status) {
        return (
            <main className="shell">
                <div className="loading">
                    <Logo animated />
                    <p>Reaching the device…</p>
                </div>
            </main>
        );
    }

    const state = STATES[status.state] ?? { label: status.state, tone: "idle" };

    return (
        <main className="shell">
            <header className="masthead">
                <Logo />
                <div className="masthead-text">
                    <h1>{status.hostname || "MatrixOS"}</h1>
                    <p className="subtitle">LED matrix appliance</p>
                </div>
                <span className={`badge badge-${state.tone}`}>{state.label}</span>
            </header>

            {unreachable && (
                <p className="strip strip-busy">
                    The device is not answering right now. If it just changed network, this
                    page will come back on its own.
                </p>
            )}

            {status.message && status.state === "failed" && (
                <p className="strip strip-bad">{status.message}</p>
            )}

            {note && <p className={`strip strip-${note.tone}`}>{note.text}</p>}

            <section className="card">
                <div className="card-head">
                    <h2>Network</h2>
                    <button className="ghost" onClick={scan} disabled={busy || scanning}>
                        {scanning ? "Scanning…" : "Rescan"}
                    </button>
                </div>

                {status.state === "connected" ? (
                    <p className="lead">
                        Connected to <strong>{status.ssid}</strong>
                    </p>
                ) : status.state === "unmanaged" ? (
                    <p className="lead muted">
                        This build does not manage WiFi. Nothing to configure here.
                    </p>
                ) : (
                    <p className="lead">
                        The device is offering the network{" "}
                        <strong>{status.accessPoint}</strong>. Pick a network below to put it
                        online.
                    </p>
                )}

                {status.state !== "unmanaged" && (
                    <NetworkPicker
                        networks={status.networks ?? []}
                        current={status.ssid}
                        busy={busy || status.state === "connecting"}
                        scanning={scanning}
                        onConnect={connect}
                    />
                )}
            </section>

            <section className="card">
                <div className="card-head">
                    <h2>Device</h2>
                </div>
                <dl className="facts">
                    <div>
                        <dt>Version</dt>
                        <dd>{status.version}</dd>
                    </div>
                    <div>
                        <dt>Build</dt>
                        <dd className="mono">{status.commit}</dd>
                    </div>
                    <div>
                        <dt>Address</dt>
                        <dd className="mono">{status.hostname}.local</dd>
                    </div>
                </dl>
            </section>

            <section className="card card-danger">
                <div className="card-head">
                    <h2>Factory reset</h2>
                </div>
                <p className="lead muted">
                    Forgets the WiFi network and every linked account. The device reopens its
                    setup network, and someone has to set it up again from a phone.
                </p>
                {confirmingReset ? (
                    <div className="row">
                        <button className="danger" onClick={reset} disabled={busy}>
                            Yes, reset the device
                        </button>
                        <button className="ghost" onClick={() => setConfirmingReset(false)}>
                            Keep everything
                        </button>
                    </div>
                ) : (
                    <button
                        className="outline-danger"
                        onClick={() => setConfirmingReset(true)}
                        disabled={busy || status.state === "unmanaged"}
                    >
                        Reset this device
                    </button>
                )}
            </section>

            <footer className="footer">MatrixOS {status.version} · GPL-2.0</footer>
        </main>
    );
}
