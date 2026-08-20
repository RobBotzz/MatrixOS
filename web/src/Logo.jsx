/// The device drawn as what it is: a small grid of lit pixels. Inline SVG, so it
/// costs nothing to embed and scales with the text around it.
const LIT = [
    [1, 0], [2, 0], [3, 0], [4, 0],
    [0, 1], [5, 1],
    [0, 2], [2, 2], [3, 2], [5, 2],
    [0, 3], [5, 3],
    [1, 4], [4, 4],
    [2, 5], [3, 5],
];

export default function Logo({ animated = false }) {
    return (
        <svg
            className={`logo${animated ? " logo-animated" : ""}`}
            viewBox="0 0 6 6"
            role="img"
            aria-label="MatrixOS"
        >
            {Array.from({ length: 36 }, (_, index) => {
                const x = index % 6;
                const y = Math.floor(index / 6);
                const on = LIT.some(([lx, ly]) => lx === x && ly === y);
                return (
                    <rect
                        key={index}
                        x={x + 0.15}
                        y={y + 0.15}
                        width="0.7"
                        height="0.7"
                        rx="0.15"
                        className={on ? "pixel pixel-on" : "pixel"}
                        style={{ animationDelay: `${(x + y) * 90}ms` }}
                    />
                );
            })}
        </svg>
    );
}
