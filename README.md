# Sam Spectral Control

Korektor parametryczny z pasmami statycznymi, dynamicznymi i spektralno-dynamicznymi, obsługą surround / Dolby Atmos, trybami fazy (Minimum Phase, Natural Phase, Analog Feel) oraz odczytami skutków ubocznych przetwarzania (faza, ringing, THD+N, aliasing). Formaty docelowe: VST3 (macOS, Windows), później AU i AAX.

## Status

| Etap | Zakres | Stan |
|---|---|---|
| M0 | Research, architektura, decyzje (ADR) | ✅ zaakceptowane |
| M1 | Szkielet: CMake + JUCE 9 + VST3, CI, parametry, stan, bypass, analizator | ✅ |
| M2 | 24 pasma statyczne (filtry dopasowane, SVF-TPT), cele L/R/M/S, edycja na wyświetlaczu, panel pasma, Undo/Redo | ✅ |
| M3 | Pasma Dynamic (lookahead, linkowanie) i Spectral (STFT, siatka ERB, próg względny), Auto Gain globalny i pasm (ISO 226) | ✅ |
| M4 | Natural Phase (korektor FIR amplitudy i fazy), Quality, oversampling 2×/4×/8× (IIR/FIR), Character Warm/Sharp z ADAA, brickwall liniowofazowy | ✅ |
| M5 | Surround/Atmos: wybór głośników per pasmo (zapisywany po rolach Dolby, przenośny między układami), panel głośników, null testy 7.1.4 | ✅ |
| M6 | Analog Feel N/P/S: krzywe modeli, nieliniowości per pasmo (Character wybiera wariant), kalibracja −18 dBFS = +4 dBu, wewnętrzny oversampling w High/Max | ✅ |
| M7 | Analizator (ustawienia, peak hold), poświata aktywności, podpowiedzi rezonansów, odczyty fazy i ringingu pasm, mierniki THD+N i aliasingu z podpowiedzią oversamplingu | ✅ |
| M8 | Wydanie: pomiary referencyjne, instalatory (pkg / Inno Setup), podpisy, manual, testy hostów | ⏳ |

Szczegółowy stan każdego wymagania: [docs/traceability.md](docs/traceability.md).

## Obsługa pasm (M2)

| Akcja | Gest |
|---|---|
| Nowe pasmo | podwójne kliknięcie tła (skraje: Low/High Cut, dolny pas: Notch, reszta: Bell z gain z pozycji) lub przeciągnięcie krzywej (przy końcach: półka) |
| Częstotliwość / gain | przeciąganie węzła (Shift — precyzyjnie); dla kształtów bez gain pion zmienia Q |
| Q | kółko myszy nad węzłem |
| Włącz / wyłącz pasmo | podwójne kliknięcie węzła |
| Solo (odsłuch obszaru pasma) | Alt/Option + przytrzymanie węzła albo przycisk Solo w panelu |
| Zaznaczanie wielu pasm | prostokąt na tle, Cmd/Ctrl + klik; zaznaczone pasma przesuwają się razem |
| Menu pasma | prawy przycisk: kształt, slope / Q, tryb, cel (All/L/R/M/S), Character, Auto Gain, bypass, solo, kopiuj/wklej, reset, usuń |
| Usuń, Undo, Redo, zaznacz wszystko | Delete, Cmd/Ctrl+Z, Cmd/Ctrl+Shift+Z (lub Ctrl+Y), Cmd/Ctrl+A |
| Wpisanie wartości | podwójne kliknięcie pola wartości w panelu (np. „1.5k”, „-3 dB”, „1.2 s”) |

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

Inne presety: `dev` (Debug), `core-tests` (tylko rdzeń DSP, szybko), `sanitize` (rdzeń z ASan/UBSan), `rtsan` (testy rdzenia i wtyczki z RealtimeSanitizerem — wymaga Clang 20: `CC=clang-20 CXX=clang++-20 cmake --preset rtsan`).

Styl kodu: `.clang-format` (clang-format 18, sprawdzany w CI). `-DSSC_WARNINGS_AS_ERRORS=ON` traktuje ostrzeżenia w kodzie projektu jak błędy (włączone w CI).

- **macOS (universal):** dodaj `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0`.
- **Windows:** `cmake -B build -G "Visual Studio 17 2022" -A x64` i `cmake --build build --config Release`.
- **Linux:** pakiety `libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libxi-dev libglu1-mesa-dev`.
- **Narzędzia deweloperskie:** `-DSSC_BUILD_TOOLS=ON` buduje `ssc_screenshot` (zrzut edytora z sygnałem testowym, offscreen) i `ssc_benchmark` (obciążenie CPU konfiguracji z NFR-1, wyniki w [docs/benchmarks.md](docs/benchmarks.md); `ssc_benchmark 3 modes` — tabela CPU i latencji trybów fazy × Quality i oversamplingu, PH-2).

## Struktura

```
src/core/      rdzeń DSP (bez GUI): filtry dopasowane, pasma, dynamika, silnik spektralny, głośność (Auto Gain), analizator
src/plugin/    procesor JUCE, parametry, stan, usługa analizatora
src/ui/        edytor: wyświetlacz (analizator, krzywe, węzły), panel pasma, model edycji z Undo, pasek sterowania
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
