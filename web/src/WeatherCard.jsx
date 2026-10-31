import { useCallback, useEffect, useState } from "react";

/// The search runs here in the browser, which has the internet the device may
/// not be able to spare a request for; the device only stores the pick (ADR-0017).
const GEOCODER = "https://geocoding-api.open-meteo.com/v1/search";

export default function WeatherCard() {
    const [weather, setWeather] = useState(null);
    const [query, setQuery] = useState("");
    const [results, setResults] = useState(null);
    const [busy, setBusy] = useState(false);
    const [error, setError] = useState(null);

    const load = useCallback(async () => {
        try {
            const response = await fetch("/api/weather");
            setWeather(await response.json());
        } catch {
            // The status poll in App already tells the user the device is away.
        }
    }, []);

    useEffect(() => {
        load();
        const timer = setInterval(load, 10000);
        return () => clearInterval(timer);
    }, [load]);

    const search = async (event) => {
        event.preventDefault();
        if (!query.trim()) return;
        setBusy(true);
        setError(null);
        try {
            const params = new URLSearchParams({ name: query.trim(), count: "5", language: "en" });
            const response = await fetch(`${GEOCODER}?${params}`);
            const data = await response.json();
            setResults(data.results ?? []);
        } catch {
            setError("The city search needs this browser to be online.");
        } finally {
            setBusy(false);
        }
    };

    const choose = async (place) => {
        setBusy(true);
        setError(null);
        try {
            const body = new URLSearchParams({
                name: place.name,
                latitude: String(place.latitude),
                longitude: String(place.longitude),
            });
            const response = await fetch("/api/weather", {
                method: "POST",
                headers: { "Content-Type": "application/x-www-form-urlencoded" },
                body: body.toString(),
            });
            if (!response.ok) throw new Error(`/api/weather answered ${response.status}`);
            setResults(null);
            setQuery("");
            window.setTimeout(load, 3000);
            load();
        } catch (e) {
            setError(e.message);
        } finally {
            setBusy(false);
        }
    };

    return (
        <section className="card">
            <div className="card-head">
                <h2>Weather</h2>
            </div>

            {weather && (
                <p className="lead">
                    Showing <strong>{weather.name}</strong>
                    {weather.temperature !== null
                        ? ` · ${Math.round(weather.temperature)} °C${weather.stale ? " (not updated recently)" : ""}`
                        : " · no forecast yet"}
                </p>
            )}

            <form className="join" onSubmit={search}>
                <label className="field">
                    <span>Change the place</span>
                    <span className="field-input">
                        <input
                            type="search"
                            value={query}
                            placeholder="City name"
                            onChange={(event) => setQuery(event.target.value)}
                        />
                        <button type="submit" className="ghost small" disabled={busy}>
                            Search
                        </button>
                    </span>
                </label>
            </form>

            {error && <p className="strip strip-bad">{error}</p>}

            {results && results.length === 0 && <p className="lead muted">No place found.</p>}

            {results && results.length > 0 && (
                <ul className="networks">
                    {results.map((place) => (
                        <li key={place.id}>
                            <button
                                className="network"
                                onClick={() => choose(place)}
                                disabled={busy}
                            >
                                <span className="network-name">
                                    {place.name}
                                    <em className="tag">
                                        {[place.admin1, place.country].filter(Boolean).join(", ")}
                                    </em>
                                </span>
                            </button>
                        </li>
                    ))}
                </ul>
            )}
        </section>
    );
}
