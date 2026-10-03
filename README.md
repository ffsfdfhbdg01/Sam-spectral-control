# Sam Spectral Control

Korektor parametryczny z pasmami statycznymi, dynamicznymi i spektralno-dynamicznymi, obsługą surround / Dolby Atmos, trybami fazy (Minimum Phase, Natural Phase, Analog Feel) oraz odczytami skutków ubocznych przetwarzania (faza, ringing, THD+N, aliasing). Formaty docelowe: VST3 (macOS, Windows), później AU i AAX.

## Status

| Etap | Zakres | Stan |
|---|---|---|
| M0 | Research, architektura, decyzje (ADR) | ✅ gotowe — czeka na akceptację |
| M1 | Szkielet: CMake + JUCE + VST3, CI, parametry, stan, bypass, analizator | ⏳ |
| M2–M8 | Pasma, dynamika, tryby fazy, surround, modele analogowe, wizualizacje, wydanie | ⏳ |

## Dokumentacja

- [PROMPT.md](PROMPT.md) — specyfikacja wymagań (z identyfikatorami wymagań).
- [docs/architecture.md](docs/architecture.md) — tor sygnału, wątki, moduły, budowanie, testy, ryzyka.
- [docs/research/](docs/research/) — research dla każdej funkcji: literatura, wtyczki referencyjne, wybrane podejście, kryteria akceptacji.
- [docs/decisions/](docs/decisions/) — decyzje architektoniczne (ADR), w tym otwarte pytania do właściciela projektu.
- [docs/traceability.md](docs/traceability.md) — śledzenie wszystkich wymagań.
- [docs/testing.md](docs/testing.md) — checklista testów w hostach.

## Budowanie

Instrukcja budowania pojawi się w M1 (CMake + JUCE 9, presety `dev` / `release`).
