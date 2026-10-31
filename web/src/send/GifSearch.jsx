import { useEffect, useState } from "react";

/// Set at build time from the repository variable GIPHY_KEY. GIPHY keys for web
/// pages are public by design; keeping it out of git only keeps scrapers off it.
export const GIPHY_KEY = import.meta.env.VITE_GIPHY_KEY ?? "";

const API = "https://api.giphy.com/v1/gifs";
const LIMIT = 24;

/// What tends to survive being shrunk to 64x32.
const IDEAS = ["pixel art", "8 bit", "retro", "neon", "space", "cat"];

async function giphy(path, params) {
    const query = new URLSearchParams({ api_key: GIPHY_KEY, limit: String(LIMIT), rating: "pg", ...params });
    const response = await fetch(`${API}/${path}?${query}`);
    if (response.status === 429) throw new Error("busy");
    if (!response.ok) throw new Error("failed");
    const { data } = await response.json();
    return data.map((gif) => ({
        id: gif.id,
        title: gif.title,
        thumbnail: gif.images.fixed_width_small.url,
        source: gif.images.fixed_width.url,
    }));
}

export default function GifSearch({ onPick, disabled }) {
    const [query, setQuery] = useState("");
    const [gifs, setGifs] = useState([]);
    const [picked, setPicked] = useState(null);
    const [error, setError] = useState(null);
    const [loading, setLoading] = useState(false);

    const load = async (path, params) => {
        setLoading(true);
        setError(null);
        try {
            setGifs(await giphy(path, params));
        } catch (e) {
            setError(
                e.message === "busy"
                    ? "The GIF search is busy right now. Try again in a while, or upload your own."
                    : "The GIF search did not answer. Upload your own instead.",
            );
        } finally {
            setLoading(false);
        }
    };

    useEffect(() => {
        load("trending", {});
    }, []);

    const search = (text) => {
        setQuery(text);
        if (text.trim()) load("search", { q: text.trim() });
    };

    const pick = async (gif) => {
        setPicked(gif.id);
        try {
            const response = await fetch(gif.source);
            const blob = await response.blob();
            onPick(new File([blob], `${gif.id}.gif`, { type: "image/gif" }));
        } catch {
            setError("That GIF could not be loaded. Pick another one.");
        }
    };

    return (
        <section className="card">
            <div className="card-head">
                <h2>Find a GIF</h2>
                <span className="muted small-print">Powered by GIPHY</span>
            </div>

            <form
                className="join"
                onSubmit={(event) => {
                    event.preventDefault();
                    search(query);
                }}
            >
                <span className="field-input">
                    <input
                        type="search"
                        value={query}
                        placeholder="Search GIPHY"
                        onChange={(event) => setQuery(event.target.value)}
                    />
                    <button type="submit" className="ghost small" disabled={loading}>
                        Search
                    </button>
                </span>
            </form>

            <div className="chips">
                {IDEAS.map((idea) => (
                    <button key={idea} className="ghost small" onClick={() => search(idea)} disabled={loading}>
                        {idea}
                    </button>
                ))}
            </div>

            {error && <p className="strip strip-bad">{error}</p>}

            <div className="gif-grid">
                {gifs.map((gif) => (
                    <button
                        key={gif.id}
                        className={gif.id === picked ? "gif gif-picked" : "gif"}
                        onClick={() => pick(gif)}
                        disabled={disabled}
                        title={gif.title}
                    >
                        <img src={gif.thumbnail} alt={gif.title} loading="lazy" />
                    </button>
                ))}
            </div>
        </section>
    );
}
