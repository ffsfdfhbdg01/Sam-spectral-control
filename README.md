# Sam Spectral Control

Korektor parametryczny z pasmami statycznymi, dynamicznymi i spektralno-dynamicznymi, obsługą surround / Dolby Atmos, trybami fazy (Minimum Phase, Natural Phase, Analog Feel) oraz odczytami skutków ubocznych przetwarzania (faza, ringing, THD+N, aliasing). Formaty docelowe: VST3 (macOS, Windows), później AU i AAX.

## Status

| Etap | Zakres | Stan |
|---|---|---|
| M0 | Research, architektura, decyzje (ADR) | ✅ zaakceptowane |
| M1 | Szkielet: CMake + JUCE 9 + VST3, CI, parametry, stan, bypass, analizator | ✅ |
| M2–M8 | Pasma, dynamika, tryby fazy, surround, modele analogowe, wizualizacje, wydanie | ⏳ |

Szczegółowy stan każdego wymagania: [docs/traceability.md](docs/traceability.md).

## Budowanie

Wymagania: CMake ≥ 3.25, kompilator C++20 (Xcode 15+, Visual Studio 2022, GCC 13 / Clang 18+), Ninja (zalecany).

```bash
git clone --recursive https://github.com/ffsfdfhbdg01/Sam-spectral-control.git
cd Sam-spectral-control
cmake --preset release          # wtyczka VST3 + testy
cmake --build --preset release
ctest --preset release
```

Gotowa wtyczka: `build/release/src/SamSpectralControl_artefacts/Release/VST3/Sam Spectral Control.vst3`.

Inne presety: `dev` (Debug), `core-tests` (tylko rdzeń DSP, szybko), `sanitize` (rdzeń z ASan/UBSan).

- **macOS (universal):** dodaj `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0`.
- **Windows:** `cmake -B build -G "Visual Studio 17 2022" -A x64` i `cmake --build build --config Release`.
- **Linux:** pakiety `libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libxi-dev libglu1-mesa-dev`.
- **Narzędzia deweloperskie:** `-DSSC_BUILD_TOOLS=ON` buduje `ssc_screenshot` (zrzut edytora z sygnałem testowym, offscreen).

## Struktura

```
src/core/      rdzeń DSP (bez GUI): FFT, analizator, silnik, narzędzia lock-free
src/plugin/    procesor JUCE, parametry, stan, usługa analizatora
src/ui/        edytor: wyświetlacz widma, pasek sterowania, motyw
tests/         testy Catch2 (rdzeń i procesor)
tools/         narzędzia deweloperskie
docs/          research, architektura, ADR, śledzenie wymagań, testy w hostach
```

## Dokumentacja

- [PROMPT.md](PROMPT.md) — specyfikacja wymagań (z identyfikatorami wymagań).
- [docs/architecture.md](docs/architecture.md) — tor sygnału, wątki, moduły, budowanie, testy, ryzyka.
- [docs/research/](docs/research/) — research dla każdej funkcji: literatura, wtyczki referencyjne, wybrane podejście, kryteria akceptacji.
- [docs/decisions/](docs/decisions/) — decyzje architektoniczne (ADR), w tym otwarte pytania do właściciela projektu.
- [docs/testing.md](docs/testing.md) — checklista testów w hostach.

## Licencje komponentów

JUCE 9 (AGPLv3 albo licencja komercyjna — decyzja otwarta, ADR-0002), VST3 SDK 3.8 (MIT, dołączony w JUCE), pffft (licencja BSD-podobna, FFTPACK), Catch2 (BSL-1.0, tylko testy).
