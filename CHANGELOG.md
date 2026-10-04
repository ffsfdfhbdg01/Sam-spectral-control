# Changelog

Wszystkie istotne zmiany Sam Spectral Control. Format: [Keep a Changelog](https://keepachangelog.com/pl/1.1.0/), wersjonowanie: [Semantic Versioning](https://semver.org/lang/pl/) (REL-3). Wersja w `CMakeLists.txt` (`project(... VERSION ...)`) musi być równa tagowi wydania `vX.Y.Z`; workflow wydania to sprawdza ([docs/release.md](docs/release.md)).

## [Unreleased]

## [1.0.0] - niewydana

Pierwsze wydanie (etapy M0–M8). Data zostanie wpisana przy publikacji; publikacja czeka na decyzje ADR-0002 (licencja JUCE) i ADR-0016 (identyfikatory).

### Dodane
- **Pasma**: 24 pasma — Bell, Low/High Shelf, Low/High Cut (6–96 dB/okt. i Brickwall), Notch, Band Pass, Tilt Shelf, Flat Tilt, All Pass; filtry dopasowane do prototypu analogowego (SVF-TPT), cel L/R/Mid/Side; edycja na wykresie (tworzenie kliknięciem, menu kontekstowe, Undo/Redo).
- **Tryby pasma**: Static, Dynamic (próg bezwzględny lub względny, ratio 0,1–100, powyżej/poniżej progu, zakres, attack/release, lookahead, knee, mix, linkowanie detekcji) i Spectral (STFT/WOLA, selektywność); Auto Gain globalny i per pasmo.
- **Faza i jakość**: Minimum Phase, Natural Phase (korektor FIR do prototypu analogowego, Brickwall z fazą liniową) i Analog Feel z modelami N/P/S (krzywe i nieliniowości); Quality Eco/Normal/High/Max; oversampling 2×/4×/8× (IIR polifazowy lub FIR z fazą liniową) i osobny oversampling renderu offline przy tej samej latencji.
- **Character** pasma: Clear, Warm, Sharp (nasycenie z antyaliasingiem ADAA).
- **Surround i Atmos**: układy od mono do 9.1.6 i do 32 kanałów dyskretnych; wybór głośników pasma zapisywany semantycznie (przenośny między układami), LFE domyślnie pominięty; kanały poza pasmami przechodzą bit w bit.
- **Wizualizacja**: analizator pre/post z peak hold i nachyleniem, poświata aktywności, świecące pasma dynamiczne, podpowiedzi rezonansów, odczyty fazy, opóźnienia grupowego i pre/post ringingu pasma, mierniki THD+N i aliasingu z podpowiedzią oversamplingu.
- **Presety**: 16 presetów fabrycznych, presety użytkownika (pliki `.sscpreset`), porównanie A/B z kopiowaniem, każda zmiana jako krok Undo; migracja stanu między wersjami.
- **Globalne**: Mix, Output, Bypass hosta z crossfade i stałą latencją, odsłuch pasma (Solo), Max Lookahead.
- **Instalatory**: macOS `.pkg` (universal, wybór komponentów, deinstalator), Windows Inno Setup z deinstalatorem; podpisy i notaryzacja w workflow wydania.

### Jakość
- Testy rdzenia DSP i wtyczki (Catch2), testy obciążeniowe (20 układów kanałów, 44,1–384 kHz, bufory 1–8192, szybka automatyzacja), RealtimeSanitizer, ASan/UBSan, pluginval (strictness 10) i walidator VST3 Steinberga (tryb rozszerzony) w CI.

### Ograniczenia
- Format VST3; Audio Unit (P1) jest budowany tylko na życzenie (`SSC_FORMAT_AU`), AAX i CLAP — poza zakresem 1.0.
- Testy w hostach (docs/testing.md, TST-3) wykonuje się ręcznie przed publikacją.
