# Changelog

Wszystkie istotne zmiany Sam Spectral Control. Format: [Keep a Changelog](https://keepachangelog.com/pl/1.1.0/), wersjonowanie: [Semantic Versioning](https://semver.org/lang/pl/) (REL-3). Wersja w `CMakeLists.txt` (`project(... VERSION ...)`) musi być równa tagowi wydania `vX.Y.Z`; workflow wydania to sprawdza ([docs/release.md](docs/release.md)).

## [Unreleased]

## [1.0.0] - 2026-10-10

Pierwsze wydanie (etapy M0–M8 i przebudowa interfejsu według [research §15](docs/research/15-ux-ui.md)). Licencja: GNU AGPLv3 ([ADR-0002](docs/decisions/0002-licencja-juce.md)); identyfikatory wtyczki zamrożone ([ADR-0016](docs/decisions/0016-identyfikatory-nazwy.md)). Instalatory nie są jeszcze podpisane cyfrowo — to wydanie wstępne.

### Dodane
- **Pasma**: 24 pasma — Bell, Low/High Shelf, Low/High Cut (6–96 dB/okt. i Brickwall), Notch, Band Pass, Tilt Shelf, Flat Tilt, All Pass; filtry dopasowane do prototypu analogowego (SVF-TPT), cel L/R/Mid/Side; edycja na wykresie (tworzenie kliknięciem, menu kontekstowe, Undo/Redo).
- **Tryby pasma**: Static, Dynamic (próg bezwzględny lub względny, ratio 0,1–100, powyżej/poniżej progu, zakres, attack/release, lookahead, knee, mix, linkowanie detekcji) i Spectral (STFT/WOLA, selektywność); Auto Gain globalny i per pasmo.
- **Faza i jakość**: Minimum Phase (w High i Max z minimalnofazowym korektorem amplitudy do prototypu analogowego, bez latencji), Natural Phase (korektor FIR do prototypu analogowego, Brickwall z fazą liniową) i Analog Feel z modelami N/P/S (krzywe i nieliniowości); Quality Eco/Normal/High/Max; oversampling 2×/4×/8× (IIR polifazowy lub FIR z fazą liniową) i osobny oversampling renderu offline przy tej samej latencji.
- **Character** pasma: Clear, Warm, Sharp (nasycenie z antyaliasingiem ADAA).
- **Surround i Atmos**: układy od mono do 9.1.6 i do 32 kanałów dyskretnych; wybór głośników pasma zapisywany semantycznie (przenośny między układami), LFE domyślnie pominięty; kanały poza pasmami przechodzą bit w bit.
- **Wizualizacja**: analizator pre/post z peak hold i nachyleniem, poświata aktywności, świecące pasma dynamiczne, podpowiedzi rezonansów, nakładka fazy lub opóźnienia grupowego całego EQ, odczyty fazy, opóźnienia grupowego i pre/post ringingu pasma, mierniki THD+N i aliasingu z podpowiedzią oversamplingu.
- **Presety**: 15 presetów fabrycznych, presety użytkownika (pliki `.sscpreset`), porównanie A/B z kopiowaniem, Undo/Redo w nagłówku i ze skrótów klawiszowych, każda zmiana jako krok Undo; migracja stanu między wersjami.
- **Globalne**: Mix, Output, Bypass hosta z crossfade i stałą latencją, odsłuch pasma (Solo), odsłuch różnicy (Delta, bez korekcji Auto Gain), Max Lookahead.
- **Interfejs** ([research §15](docs/research/15-ux-ui.md)): trzy poziomy informacji — zawsze widoczne (nagłówek, wykres, pasek dolny), na żądanie (panel wybranego pasma) i zaawansowane (za przyciskami **Advanced**).
  - Panel pasma pod węzłem (nie zasłania go); tryb jako przełącznik Static \| Dynamic \| Spectral; pokrętła zależne od trybu: Static — Freq, Gain, Q; Dynamic — także Threshold, Ratio, Range, Attack, Release; Spectral — Threshold, Selectivity, Range, Attack, Release; bypass, Solo, menu ⋯ (Copy, Paste, Reset, Delete).
  - **Advanced** pasma wysuwany z boku panelu: Threshold Mode, Direction, Knee, Lookahead (Dynamic) lub Ratio (Spectral), Mix, Link i odczyty pasma.
  - Globalny **Advanced** wysuwany z paska dolnego: Quality, Oversampling, Offline, Max Lookahead, ważenie i szybkość Auto Gain, mierniki THD+N i aliasingu (progi ostrzeżeń po kliknięciu w mierniki).
  - Nagłówek: presety ‹ › z kropką „zmieniony”, A \| B, Undo/Redo, status latencji i CPU, Analyzer, ⚙ (animacje Full / Reduced / Off, rozmiary okna), ? (instrukcja, mysz i klawiatura, informacje); pasek dolny: Phase, Advanced, Delta, Auto Gain, Mix, Output.
  - Kolory z kontrastem WCAG (tekst ≥ 4,5:1), gałki w kolorze pasma, przerywalne animacje paneli 110–200 ms.
- **Instalatory**: macOS `.pkg` (universal, wybór komponentów, deinstalator), Windows Inno Setup z deinstalatorem; podpisy i notaryzacja w workflow wydania; zawsze instalowane licencje (`THIRD-PARTY-NOTICES.txt` generowany z plików licencji bibliotek) i instrukcja obsługi po polsku (`docs/manual.md`).

### Jakość
- Testy rdzenia DSP i wtyczki (Catch2), testy obciążeniowe (20 układów kanałów, 44,1–384 kHz, bufory 1–8192, szybka automatyzacja), RealtimeSanitizer, ASan/UBSan, pluginval (strictness 10) i walidator VST3 Steinberga (tryb rozszerzony) w CI i lokalnie (`tools/ci/local-check.sh`).
- Raporty pomiarów w `docs/measurements` (`ssc_measure`): filtry względem prototypów analogowych, zniekształcenia, latencja, Auto Gain, podpowiedzi rezonansów, CPU.

### Ograniczenia
- Format VST3; Audio Unit (P1) jest budowany tylko na życzenie (`SSC_FORMAT_AU`), AAX i CLAP — poza zakresem 1.0.
- Brak wejścia sidechain i trybu niskiej latencji dla pasm Spectral (latencja ramki 43–171 ms); ambisonia nie jest obsługiwana.
- Instalatory bez podpisu cyfrowego: macOS (Gatekeeper) i Windows (SmartScreen) ostrzegają przy pierwszym uruchomieniu; notatki wydania opisują, jak je otworzyć.
- Wydanie przeszło testy automatyczne, pluginval (strictness 10) i walidator VST3 Steinberga na macOS i Windows; ręczne testy w hostach (docs/testing.md, TST-3) nie zostały jeszcze wykonane — zgłoszenia problemów z konkretnymi DAW są mile widziane.
