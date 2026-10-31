import { useCallback, useEffect, useRef, useState } from "react";

/// The page is served over plain HTTP, where the Clipboard API is not available;
/// selecting the field and the old copy command work everywhere.
function copyFrom(input) {
    input.select();
    input.setSelectionRange(0, input.value.length);
    return document.execCommand("copy");
}

export default function PicturesCard() {
    const [gallery, setGallery] = useState(null);
    const [copied, setCopied] = useState(false);
    const [confirming, setConfirming] = useState(false);
    const field = useRef(null);

    const load = useCallback(async () => {
        try {
            const response = await fetch("/api/gallery");
            setGallery(await response.json());
        } catch {
            // The status poll in App already tells the user the device is away.
        }
    }, []);

    useEffect(() => {
        load();
        const timer = setInterval(load, 10000);
        return () => clearInterval(timer);
    }, [load]);

    const copy = () => {
        setCopied(copyFrom(field.current));
        window.setTimeout(() => setCopied(false), 2000);
    };

    const renew = async () => {
        const response = await fetch("/api/gallery/address", { method: "POST" });
        if (response.ok) {
            setGallery(await response.json());
        }
        setConfirming(false);
    };

    if (!gallery) return null;

    return (
        <section className="card">
            <div className="card-head">
                <h2>Pictures</h2>
            </div>
            <p className="lead">
                Anyone with this link can send a picture or a GIF to this panel, from anywhere.{" "}
                {gallery.pictures} of {gallery.keep} stored.
            </p>

            <label className="field">
                <span>Your send link</span>
                <span className="field-input">
                    <input
                        ref={field}
                        readOnly
                        value={gallery.sendLink}
                        onFocus={(event) => event.target.select()}
                    />
                    <button type="button" className="ghost small" onClick={copy}>
                        {copied ? "Copied" : "Copy"}
                    </button>
                </span>
            </label>

            {confirming ? (
                <div className="row">
                    <button className="danger" onClick={renew}>
                        Yes, make a new link
                    </button>
                    <button className="ghost" onClick={() => setConfirming(false)}>
                        Keep this one
                    </button>
                </div>
            ) : (
                <button className="ghost" onClick={() => setConfirming(true)}>
                    Make a new link — the old one stops working
                </button>
            )}
        </section>
    );
}
