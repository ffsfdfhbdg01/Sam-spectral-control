# Changelog

Wszystkie istotne zmiany Sam Spectral Control. Format: [Keep a Changelog](https://keepachangelog.com/pl/1.1.0/), wersjonowanie: [Semantic Versioning](https://semver.org/lang/pl/) (REL-3). Wersja w `CMakeLists.txt` (`project(... VERSION ...)`) musi być równa tagowi wydania `vX.Y.Z`; workflow wydania to sprawdza ([docs/release.md](docs/release.md)).

## [Unreleased]

### Dodane
- **Tryb pasma Gate** — czwarta zakładka obok Static, Dynamic i Spectral: bramka działająca tylko w obszarze pasma (z jego kształtem, Q i Slope). Gdy poziom w obszarze pasma spada pod **Threshold**, obszar jest ściszany o **Range**; bramka otwiera się z czasem **Attack**, a po spadku pod próg − **Hysteresis** zostaje otwarta przez **Hold** i zamyka się z czasem **Release** (także w nutach, ♪). Działa z Sidechain External (bramka kluczowana innym śladem), Lookahead, Link i Mix pasma; bez latencji. Panel pasma: Threshold, Range, Attack, Hold, Release i nagłówek **GATE**; panel Advanced: Threshold Mode, Hysteresis, Lookahead, Mix, Link, Sidechain. Po przełączeniu w Gate nieruszone ustawienia stają się Threshold −40 dB i Range 24 dB. Nowe parametry pasma **Hold** (0–500 ms) i **Hysteresis** (0–24 dB).

### Zmienione
- Schowek pasma ma format w wersji 4; teksty z 1.0–1.2 wklejają się z tym samym trybem pasma. Projekty i presety z 1.0–1.2 wczytują się bez zmian (Hold i Hysteresis z wartościami domyślnymi).

## [1.2.0] - 2026-11-05

Delta pasma obok Solo, ikony kształtów pasm, wycinanie rezonansu (gwizdu) kliknięciem w podpowiedź, okres fali i Attack w okresach w panelu pasma oraz Release w wartościach nut w tempie projektu. Pasmo Spectral działa teraz tylko w obszarze swojego Q i Slope, jak pasmo Dynamic — projekty z pasmami Spectral mogą przez to brzmieć nieco inaczej (słabsze działanie na zboczach pasm). Projekty i presety z 1.0.0 i 1.1.0 wczytują się bez zmian ustawień. Instalatory nadal nie są podpisane cyfrowo — to wydanie wstępne.

### Dodane
- **Delta pasma**: przycisk △ obok słuchawek (Solo) w panelu pasma i pozycja *Delta* w menu węzła — odsłuch tylko tego, co wtyczka zmienia w obszarze pasma (to, co tnie lub dodaje), na celu pasma; kliknięcie zostawia włączone, przytrzymanie działa tylko na czas trzymania. Solo i Delta pasma wykluczają się; etykieta na wykresie: *Delta · Band …*.
- **Ikony kształtów** pasm (Bell, półki, cięcia, Notch, Band Pass, Tilt Shelf, Flat Tilt, All Pass) w wyborze **Shape** panelu, w jego menu i w podmenu *Shape* menu węzła.
- **Kliknięcie w podpowiedź rezonansu** dodaje pasmo, które go wycina: Bell Q 8 na częstotliwości rezonansu, obniżony o tyle, o ile rezonans wystaje (3–18 dB), od razu zaznaczony; jeden krok Undo.
- **Okres fali** w panelu pasma: po prawej stronie rzędu EQ **Period** (1000 / f ms), a w pasmach Dynamic i Spectral także **Attack =** — Attack w okresach fali częstotliwości pasma, jako pomoc przy ustawianiu Attack.
- **Release w nutach (tryb rytmiczny)**: przełącznik ♪ przy Release pasm Dynamic i Spectral; pokrętło wybiera wartość rytmiczną 1/64T … 1/1D (z ikoną nuty, z triolami i kropkami), a czas wynika z tempa projektu odczytywanego z hosta (bez tempa — ostatnie znane, na początku 120 BPM). Nowe parametry pasma **Release Sync** i **Release Note** (automatyzowalne).

### Zmienione
- **Pasmo Spectral działa w obszarze swojego Q i Slope jak pasmo Dynamic**: poziom każdej częstotliwości jest mierzony przez ten sam filtr obszaru pasma, którego używa detekcja Dynamic. Dotąd próg porównywany był z pełnym poziomem częstotliwości, więc głośne składowe na zboczach wąskiego pasma były tłumione mocniej niż w paśmie Dynamic (np. Bell 2 kHz Q 2 o oktawę od środka: −2,0 dB zamiast −0,8 dB) i pasmo działało szerzej, niż wskazywały Q i Slope. Przy pełnym działaniu kształt tłumienia był i jest taki jak kształt pasma Static.
- Panel pasma jest szerszy o kolumnę okresu fali (526 px w mono i stereo).
- Schowek pasma (*Copy* / *Paste*) ma format w wersji 3 (z nowymi parametrami); teksty z 1.0 i 1.1 nadal da się wkleić (Release Sync wyłączony).

## [1.1.0] - 2026-10-27

Odsłuch Solo taki, jak pasmo brzmi w miksie, wejście sidechain dla pasm Dynamic i Spectral oraz kompresja w górę. Projekty i presety z 1.0.0 wczytują się i brzmią tak samo. Instalatory nadal nie są podpisane cyfrowo — to wydanie wstępne.

### Dodane
- **Kompresja w górę** (upward compression) w pasmach Dynamic i Spectral: przełącznik **Down | Up** w nagłówku rzędu dynamiki panelu pasma (obok Auto Gain) i podmenu *Direction* w menu węzła. Up z Ratio powyżej 1:1 podnosi obszar pasma, gdy jest cichszy niż próg (najwyżej o Range), a głośne fragmenty zostawia; Up z Ratio poniżej 1:1 to ekspansja w górę, Down — kompresja albo ekspansja w dół (jak bramka). Nagłówek rzędu dynamiki nazywa działanie (**UPWARD COMPRESSION**, **EXPANSION**, **UPWARD EXPANSION**).
- **Sidechain** pasm Dynamic i Spectral (DYN-4): opcjonalne wejście **Sidechain** wtyczki (mono lub stereo, domyślnie wyłączone) i wybór **Sidechain: Internal | External** w panelu Advanced pasma oraz *External Sidechain* w menu węzła. Pasmo z External mierzy poziom w sidechainie (na swoim celu: Left, Right, Mid, Side, All; sidechain mono steruje wszystkimi kanałami), a przetwarza własny sygnał; pasma Spectral biorą poziomy z widma sidechaina. Sidechain jest wyrównany w czasie z torem głównym (Lookahead, oversampling, Natural Phase) i nie dodaje latencji. Panel pasma pokazuje **· SIDECHAIN**, a bez sygnału na wejściu — bursztynowe **SIDECHAIN: NO INPUT**.
- Solo pokazuje na wykresie słyszalny zakres: przyciemnia to, czego nie słychać, zaznacza granice (−3 dB) i podaje pasmo, cel i zakres (jak w Pro-Q 4).

### Zmienione
- **Direction** pasma to teraz **Down** (pasmo tnie) / **Up** (podbija) zamiast Above / Below; po której stronie progu pasmo działa, wynika z Ratio. Przełącznik przeszedł z panelu Advanced do panelu pasma.
- **Attack i Release** odpowiadają teraz zawsze na rosnący i malejący poziom (DYN-6). W kompresji w górę podbicie znika przy głośnym wejściu z czasem Attack — wcześniej z czasem Release, przez co początek każdego głośnego dźwięku po cichym fragmencie był podbijany o pełny Range; ekspansja w dół (bramka) otwiera się z czasem Attack. Kompresja i ekspansja powyżej progu działają jak dotąd.
- Stan zapisu ma wersję 2: projekty, presety, porównanie A/B i skopiowane pasma z 1.0 są przy wczytaniu przeliczane i brzmią tak samo (Direction: Above / Below → Down / Up według Ratio; w pasmach działających poniżej progu Attack i Release zamieniają się miejscami, w granicach swoich zakresów). Wersja 1.0 nie wczyta stanu zapisanego przez nowszą wersję.
- **Solo** pasma brzmi tak głośno, jak pasmo naprawdę gra w miksie: z jego wzmocnieniem i działaniem Dynamic lub Spectral (podbicie głośniej, cięcie ciszej) zamiast stałego poziomu filtra pasmowego, i tylko na celu pasma — Mid daje samą składową środkową, Side samą boczną, Left / Right tylko ten kanał, w surround tylko głośniki pasma. Auto Gain nie zmienia poziomu Solo.
- Stan zapisany przez 1.0 (bez parametru Sidechain) wczytuje się z Sidechain Internal; schowek pasma (*Copy* / *Paste*) ma nowy format, starszy nadal da się wkleić.

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
