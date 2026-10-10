# Sam Spectral Control

Korektor parametryczny z pasmami statycznymi, dynamicznymi i spektralno-dynamicznymi, obsługą surround / Dolby Atmos, trybami fazy (Minimum Phase, Natural Phase, Analog Feel) oraz odczytami skutków ubocznych przetwarzania (faza, ringing, THD+N, aliasing). Format: VST3 (macOS, Windows, Linux); Audio Unit — opcjonalnie na macOS.

![Sam Spectral Control](docs/images/editor.png)

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
| M8 | Presety fabryczne i użytkownika, A/B, Undo/Redo w nagłówku, testy obciążeniowe, oversampling renderu offline, Delta, nakładka fazy całego EQ, korektor Minimum Phase High/Max, raporty pomiarów, instalatory (pkg / Inno Setup) i workflow wydania z podpisami, instrukcja (PL), licencje | ✅ |
| UX | Przebudowa interfejsu ([research §15](docs/research/15-ux-ui.md)): trzy poziomy informacji, panel pasma pod węzłem z pokrętłami zależnymi od trybu, wysuwane panele Advanced (pasma i globalny), presety ‹ › ze znacznikiem zmian, animacje Full / Reduced / Off | ✅ |

Szczegółowy stan każdego wymagania: [docs/traceability.md](docs/traceability.md).

## Pobieranie

Instalatory wersji **1.0.0** dla macOS (`.pkg`, Apple silicon i Intel) i Windows (`.exe`, x64) są na stronie [Releases](https://github.com/ffsfdfhbdg01/Sam-spectral-control/releases), razem z archiwum pełnego kodu źródłowego i sumami SHA-256. Pierwsze wydanie nie jest podpisane cyfrowo (wydanie wstępne): macOS i Windows ostrzegają przed jego uruchomieniem, a notatki wydania opisują, jak przejść przez te ostrzeżenia. Na Linuksie wtyczkę buduje się ze źródeł (niżej). Jak powstaje wydanie: [docs/release.md](docs/release.md).

## Obsługa (skrót)

Pełna instrukcja obsługi: [docs/manual.md](docs/manual.md).

| Akcja | Gest |
|---|---|
| Nowe pasmo | podwójne kliknięcie tła (skraje: Low/High Cut, dolny pas: Notch, reszta: Bell z gain z pozycji) lub przeciągnięcie krzywej (przy końcach: półka) |
| Częstotliwość / gain | przeciąganie węzła (Shift — precyzyjnie); dla kształtów bez gain pion zmienia Q |
| Q | kółko myszy nad węzłem |
| Włącz / wyłącz pasmo | podwójne kliknięcie węzła |
| Solo (odsłuch obszaru pasma) | Alt/Option + przytrzymanie węzła albo 🎧 w panelu pasma (klik — zatrzaśnięcie, przytrzymanie — chwilowo) |
| Tryb pasma | przełącznik Static \| Dynamic \| Spectral w panelu pasma; pokrętła dynamiki pojawiają się w trybach Dynamic i Spectral |
| Ustawienia specjalistyczne | **Advanced ›** w panelu pasma (detektor, odczyty) i **Advanced ▴** na pasku dolnym (Quality, oversampling, Max Lookahead, Auto Gain, mierniki) |
| Zaznaczanie wielu pasm | prostokąt na tle, Cmd/Ctrl + klik; zaznaczone pasma przesuwają się razem |
| Menu pasma | prawy przycisk: kształt, slope / Q, tryb, cel (All/L/R/M/S), Character, Auto Gain, bypass, solo, kopiuj/wklej, reset, usuń |
| Usuń, Undo, Redo, zaznacz wszystko | Delete, Cmd/Ctrl+Z, Cmd/Ctrl+Shift+Z (lub Ctrl+Y), Cmd/Ctrl+A |
| Wpisanie wartości | kliknięcie pola wartości pod pokrętłem (np. „1.5k”, „-3 dB”, „1.2 s”); podwójne kliknięcie pokrętła — wartość domyślna |

## Budowanie

Wymagania: CMake ≥ 3.25, kompilator C++20 (Xcode 15+, Visual Studio 2022, GCC 13 / Clang 18+), Ninja (zalecany).

```bash
git clone --recursive https://github.com/ffsfdfhbdg01/Sam-spectral-control.git
cd Sam-spectral-control
cmake --preset release          # wtyczka VST3 + testy
cmake --build --preset release
ctest --preset release
```

Gotowa wtyczka: `build/release/src/SamSpectralControl_artefacts/Release/VST3/Sam Spectral Control.vst3` (na Linuksie skopiuj ją do `~/.vst3`). Instalatory macOS i Windows buduje workflow wydania ([docs/release.md](docs/release.md)).

Inne presety: `dev` (Debug), `core-tests` (tylko rdzeń DSP, szybko), `sanitize` (rdzeń z ASan/UBSan), `rtsan` (testy rdzenia i wtyczki z RealtimeSanitizerem — wymaga Clang 20: `CC=clang-20 CXX=clang++-20 cmake --preset rtsan`).

Styl kodu: `.clang-format` (clang-format 18, sprawdzany w CI). `-DSSC_WARNINGS_AS_ERRORS=ON` traktuje ostrzeżenia w kodzie projektu jak błędy (włączone w CI). Wszystkie kontrole CI (format, licencje, build z `-Werror`, testy, pluginval, walidator VST3, RTSan, ASan/UBSan) uruchamia lokalnie `tools/ci/local-check.sh` — bez minut GitHub Actions.

- **macOS (universal):** dodaj `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0`.
- **Windows:** `cmake -B build -G "Visual Studio 17 2022" -A x64` i `cmake --build build --config Release`.
- **Linux:** pakiety `libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev libxi-dev libglu1-mesa-dev`.
- **Narzędzia deweloperskie:** `-DSSC_BUILD_TOOLS=ON` buduje `ssc_screenshot` (zrzut edytora z sygnałem testowym, offscreen), `ssc_benchmark` (obciążenie CPU konfiguracji z NFR-1, wyniki w [docs/benchmarks.md](docs/benchmarks.md); `ssc_benchmark 3 modes` — tabela CPU i latencji trybów fazy × Quality i oversamplingu, PH-2), `ssc_measure` (raporty pomiarów w [docs/measurements/](docs/measurements/README.md)) i `ssc_resonance_eval` (ewaluacja podpowiedzi rezonansów).

## Struktura

```
src/core/      rdzeń DSP (bez GUI i JUCE): filtry dopasowane, pasma, dynamika, silnik spektralny, korektor fazy,
               oversampling, głośność (Auto Gain), analiza (analizator, rezonanse, odczyty, mierniki)
src/plugin/    procesor JUCE, parametry, stan i migracja, presety, usługi wątków tła
src/ui/        edytor: wyświetlacz (analizator, krzywe, węzły), panel pasma i jego Advanced, panel głośników,
               presety i A/B, pasek dolny i globalny Advanced z miernikami, kontrolki, ruch, dokowanie paneli
tests/         testy Catch2: rdzeń (tests/core), procesor, UI i testy obciążeniowe (tests/plugin)
tools/         narzędzia deweloperskie, generator THIRD-PARTY-NOTICES.txt, skrypty CI (tools/ci)
packaging/     instalatory macOS (pkg) i Windows (Inno Setup), licencja instalatora, notatki wydania
docs/          instrukcja, research, architektura, ADR, śledzenie wymagań, testy, pomiary, wydanie
```

## Dokumentacja

- [docs/manual.md](docs/manual.md) — instrukcja obsługi (PL).
- [PROMPT.md](PROMPT.md) — specyfikacja wymagań (z identyfikatorami wymagań).
- [docs/architecture.md](docs/architecture.md) — tor sygnału, wątki, moduły, budowanie, testy, ryzyka.
- [docs/research/](docs/research/) — research dla każdej funkcji: literatura, wtyczki referencyjne, wybrane podejście, kryteria akceptacji.
- [docs/decisions/](docs/decisions/) — decyzje architektoniczne (ADR).
- [docs/testing.md](docs/testing.md) — checklista testów w hostach i testy obciążeniowe.
- [docs/measurements/](docs/measurements/README.md) — raporty pomiarów: filtry, zniekształcenia, latencja, Auto Gain, rezonanse, CPU.
- [docs/release.md](docs/release.md) — instalatory, podpisy, notaryzacja, workflow wydania; [CHANGELOG.md](CHANGELOG.md).

## Licencja

Copyright © 2026 Sam Audio. Sam Spectral Control jest wolnym oprogramowaniem: możesz go rozpowszechniać i zmieniać na warunkach **GNU Affero General Public License, wersja 3** ([LICENSE](LICENSE); [ADR-0002](docs/decisions/0002-licencja-juce.md)). Program jest rozpowszechniany bez żadnej gwarancji. Kto rozpowszechnia zmienioną wersję, musi udostępnić jej kod źródłowy na tej samej licencji.

### Licencje komponentów

JUCE 9 (używany na AGPLv3), VST3 SDK 3.8 (MIT, dołączony w JUCE), pffft (licencja BSD-podobna, FFTPACK) oraz biblioteki wkompilowane przez moduły JUCE (zlib, HarfBuzz, SheenBidi, LunaSVG, PlutoVG, libpng, JPEG IJG). Pełne teksty: [THIRD-PARTY-NOTICES.txt](THIRD-PARTY-NOTICES.txt) (generowany przez `tools/third-party-notices.py`). Catch2 (BSL-1.0) — tylko testy, nie trafia do wtyczki.
