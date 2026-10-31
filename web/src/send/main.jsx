import { StrictMode } from "react";
import { createRoot } from "react-dom/client";

import Send from "./Send.jsx";
import "../styles.css";

createRoot(document.getElementById("root")).render(
    <StrictMode>
        <Send />
    </StrictMode>,
);
