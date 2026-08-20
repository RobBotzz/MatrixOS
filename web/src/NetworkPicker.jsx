import { useState } from "react";

function Signal({ strength }) {
    // Four bars, because that is what every phone shows and nobody has to learn
    // it. The thresholds are NetworkManager's 0-100 scale.
    const bars = strength >= 75 ? 4 : strength >= 50 ? 3 : strength >= 25 ? 2 : 1;
    return (
        <span className="signal" title={`${strength}%`}>
            {[1, 2, 3, 4].map((bar) => (
                <i key={bar} className={bar <= bars ? "bar bar-on" : "bar"} />
            ))}
        </span>
    );
}

export default function NetworkPicker({ networks, current, busy, scanning, onConnect }) {
    const [selected, setSelected] = useState(null);
    const [password, setPassword] = useState("");
    const [reveal, setReveal] = useState(false);

    const chosen = networks.find((network) => network.ssid === selected);

    const submit = (event) => {
        event.preventDefault();
        onConnect(selected, password);
        setPassword("");
        setSelected(null);
    };

    if (networks.length === 0) {
        return (
            <p className="lead muted">
                {scanning ? "Looking for networks…" : "No networks in range. Try a rescan."}
            </p>
        );
    }

    return (
        <>
            <ul className="networks">
                {networks.map((network) => (
                    <li key={network.ssid}>
                        <button
                            className={`network${network.ssid === selected ? " network-selected" : ""}`}
                            onClick={() => {
                                setSelected(network.ssid === selected ? null : network.ssid);
                                setPassword("");
                            }}
                            disabled={busy}
                        >
                            <span className="network-name">
                                {network.ssid}
                                {network.ssid === current && <em className="tag">current</em>}
                                {!network.secured && <em className="tag tag-open">open</em>}
                            </span>
                            <Signal strength={network.signal} />
                        </button>
                    </li>
                ))}
            </ul>

            {chosen && (
                <form className="join" onSubmit={submit}>
                    {chosen.secured && (
                        <label className="field">
                            <span>Password for “{chosen.ssid}”</span>
                            <span className="field-input">
                                <input
                                    type={reveal ? "text" : "password"}
                                    value={password}
                                    onChange={(event) => setPassword(event.target.value)}
                                    autoComplete="off"
                                    autoFocus
                                />
                                <button
                                    type="button"
                                    className="ghost small"
                                    onClick={() => setReveal(!reveal)}
                                >
                                    {reveal ? "Hide" : "Show"}
                                </button>
                            </span>
                        </label>
                    )}
                    <button
                        type="submit"
                        className="primary"
                        disabled={busy || (chosen.secured && password.length === 0)}
                    >
                        Connect to {chosen.ssid}
                    </button>
                </form>
            )}
        </>
    );
}
