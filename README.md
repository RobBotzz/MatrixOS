# MatrixOS

Application for managing a matrix LED panel device and running apps on it.

## View

This module is creates and manages views to be displayed on the matrix

- Converting images/gifs (? to ?)
- Build views/layouts

## HAL

This module communicates with the hardware (rotary encoder and matrix display)

- React to button press/rotation/long press (Circuitpython rotaryio)
- Display pixels (? to datasignal) (Circuitpython RGB Matrix, Matrixportal library, hzeller library https://www.hackster.io/idreams/getting-started-with-rgb-matrix-panel-adaa49#toc-install-the-library-4)

## OS

This module manages all the apps and provides global functions (e.g. App selection menu)

- Switch Apps
- Load/Manage configs for apps
- Store data

## Apps

This module contains all apps to be running on the OS

### GIF- and Text-sending App

Senden eines Bilds/GIFs/Videos? mit kurzem Text über Webserver
Requirements:

- Imageconversion
- API Call
- Data storage

### Wetter

Wetteranzeige an einem Ort für verschiedene Zeitpunkte
Features:

- Rotieren um zwischen Zeiträumen zu wechseln
- Gedrückt halten um Ort oder Zeiträume (Tag, Woche) zu verstellen

Requirements:

- Wetter API

### Pomodoro

Einstellen eines Pomodoro Timers zum Lernen
Features:

- Rotieren um Zeit hochzudrehen
- Gedrückt halten um Timer einzustellen
- Einmal drücken zum pausieren/bestätigen
- Bildschirm blinkt hektisch wenn Zeit vorbei

Requirements:

- Timer

### GIF & Time

Zeige ein schönes GIF mit der richtigen Uhrzeit
Features:

- Drücken um GIF zu ändern

Themes:

- Colorized plasma animations
- Conway’s Game of Life
- Lissajous curves (mathematical patterns)
- Retro DVD screen saver
- Fire animation
- Rain animation (with controlled speed)

Requirements:

- None

### Strava

Greife auf Strava API zu und zeige Statistiken der Freunde an
Features:

- Rotieren um Ansichten / Freunde zu ändern

Requirements:

- Strava API

### Spotify

Zeige den aktuell gespielten Spotify-Song mit Cover an, pausiere/überspringe Songs und kontrolliere die Lautstärke
Features:

- Drücken um zu pausieren/resümieren
- Doppelt drücken um zu überspringen
- Rotieren um Lautstärke anzupassen

Requirements:

- Spotify API

### Chess puzzles

Zeige einfache Schachbretter bei welchen die nächsten gesuchten Züge herausgefunden werden müssen
Features:

- Rotieren um über alle Felder eines Brettes der Reihe nach zu iterieren
- Drücken um Figur auszuwählen
- Erneut über Feld rotieren und drücken, wenn Figur dort hinziehen soll

Requirements:

- Chess Engine / Interpreter
- Chess Database (https://database.lichess.org/#puzzles)

### Pong game

Run a simple pong game
Features:

- Rotieren um Pongzeige hoch und runter zu bewegen
- Drücken um Spiel zu starten

Requirements:

- Pong engine

### Breakout / Arkanoid game

Run a simple arkanoid game
Features:

- Rotieren um Fläche nach links und rechts zu bewegen
- Drücken um Spiel zu starten

Requirements:

- Arkanoid engine

### Snake game

Run a simple snake game
Features:

- Rotieren um mit Schlange Richtung zu wechseln
- Drücken um Spiel zu starten

Requirements:

- Snake engine

### Memory game

Run a simple memory game
Features:

- Rotieren um über verdeckte Kärtchen zu iterieren
- Drücken um Karte aufzudecken

Requirements:

- Memory engine

### Wordle game

Run a simple wordle game
Features:

- Rotieren um über Felder zu iterieren
- Drücken um Feld auszuwählen
- Rotieren um Buchstaben auszuwählen
- Drücken um Buchstaben zu bestätigen

Requirements:

- Wordle engine
- Wordle API or Database

### Creative additional apps

- Bit clock
- Morse trainer
- Stock ticker
- ASCII art
- 2048
- Infinite terrain generator
- Collatz sequence generator (Number to image)
