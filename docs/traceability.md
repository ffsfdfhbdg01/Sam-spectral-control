# Śledzenie wymagań (traceability)

Każde wymaganie z [PROMPT.md](../PROMPT.md) z priorytetem, etapem realizacji, źródłem kryteriów akceptacji i statusem. Tabela jest aktualizowana po każdym kamieniu milowym; kolumna „Dowód” zostanie uzupełniona nazwami testów i raportów pomiarowych.

Stan: **M3 zakończony** (M0 research, M1 szkielet, M2 pasma statyczne, M3 dynamika, Spectral i Auto Gain) — 88 wymagań, wszystkie z kryteriami akceptacji w notatkach researchu lub ADR. Kolumna „Dowód” wskazuje testy (tagi Catch2 w nawiasach) lub elementy kodu.

| ID | Wymaganie (skrót) | Priorytet | Etap | Kryteria / projekt | Dowód | Status |
|---|---|---|---|---|---|---|
| PLT-1 | Formaty: VST3 (P0) | P0 (AU P1; AAX, CLAP P2) | M1 (VST3), M8 (AU/AAX) | ADR-0001 | CI: build VST3 Linux/macOS/Windows + pluginval | Zrealizowane (VST3); AU/AAX — M8 |
| PLT-2 | Systemy: macOS 11+ (universal binary arm64 + x86_64), Windows 10/11 x64 | P0 (ARM64 P2) | M1 | ADR-0001 | CI: macOS universal (arm64 + x86_64, 11.0), Windows x64 | Zrealizowane (build CI) |
| PLT-3 | Stos: C++20, JUCE 8, Steinberg VST3 SDK (od wersji 3.8 na licencji MIT), CMake, GitHub Actions | P0 | M1 | ADR-0001, ADR-0002 (otwarta), ADR-0013 | CMakeLists.txt, .gitmodules (JUCE 9.0.3, VST3 SDK 3.8) | Zrealizowane (JUCE 9 zamiast 8 — nowsza wersja tej samej linii) |
| PLT-4 | Częstotliwości próbkowania 44,1–384 kHz | P0 | M1 | ADR-0001 | testy przy 44,1 / 48 / 96 kHz (`[BAND-2]`, `[NFR-2]`) | Częściowo: 44,1–96 kHz testowane; 192–384 kHz — TST-4 (M8) |
| PLT-5 | Bezpieczeństwo czasu rzeczywistego: w wątku audio żadnych alokacji, blokad, I/O ani logowania | P0 | M1 | ADR-0010 (RTSan w CI) | `[PLT-5]` Non-finite audio is muted…; prealokacja w prepare(); ScopedNoDenormals | Częściowo: brak alokacji/blokad w process, FTZ/DAZ, ochrona NaN/Inf; RTSan w CI — M3 |
| PLT-6 | Identyfikatory (VST3 Class ID, kody producenta i wtyczki) wygeneruj raz, zapisz w… | P0 | M1 | ADR-0016 (otwarta) | — | Otwarte (decyzja użytkownika) |
| ARC-1 | Zaproponuj i uzasadnij tor sygnału (diagram w docs/architecture.md) | P0 | M0 (projekt) → M1–M3 | architecture.md §2, ADR-0003 | architecture.md §2; `ssc::engine::Engine` | W realizacji: tor pasm statycznych (M2); STFT i korektor FIR — M3/M4 |
| ARC-2 | Oversampling obejmuje tylko etapy, które na nim zyskują: nieliniowości, szybką modulację… | P0 | M0 (projekt) → M1–M3 | ADR-0008 | — | Zaprojektowane (M0) |
| ARC-3 | Latencja jest raportowana hostowi dokładnie i nie zmienia się podczas odtwarzania | P0 | M0 (projekt) → M1–M3 | ADR-0011 | `[ARC-3]` Max Lookahead sets the reported latency; latencja Spectral (ADR-0011) | Zrealizowane dla M3 (lookahead, STFT); Natural Phase, OS — M4 |
| ARC-4 | Wątki: audio (DSP), analiza (analizator, wykrywanie rezonansów, odczyty fazy i ringingu… | P0 | M0 (projekt) → M1–M3 | ADR-0010 | `AnalyzerService` (wątek tła), triple buffer, SPSC FIFO | W realizacji: wątek analizatora (M1) |
| ARC-5 | Każdy blok DSP da się przetestować w izolacji, bez GUI | P0 | M0 (projekt) → M1–M3 | architecture.md | `ssc_core` bez JUCE; testy Catch2 rdzenia | Zrealizowane |
| ARC-6 | Każdy tryb pasma działa w każdym trybie fazy | P0 | M0 (projekt) → M1–M3 | architecture.md | — | Zaprojektowane (M0) |
| CH-1 | Układy kanałów: mono, stereo i każdy układ surround/immersive, który host przekazuje przez VST3 | P0 | M5 | research/04 §d | `[CH-1]` layouts; Host layouts map to speaker pairs… | Częściowo: układy 1–32 kanałów, role głośników i pary; panel głośników — M5 |
| CH-2 | Dolby Atmos: wtyczka działa na ścieżkach i szynach bed (do 7.1.2), na szynach monitorowych… | P0 | M5 | research/04 §d | — | Zaprojektowane, do realizacji |
| CH-3 | Cel pasma w stereo: All (domyślnie), Left, Right, Mid, Side | P0 | M2 | research/04 §d | `[CH-3]` Mid band leaves the side signal untouched…; Left target changes only…; Band parameters drive the engine… | Zrealizowane (M2) |
| CH-4 | Cel pasma w surround — panel wyboru głośników (nazewnictwo Dolby: L, R, C, LFE, Lss, Rss… | P0 | M5 | research/04 §d | — | Zaprojektowane, do realizacji |
| CH-5 | Linkowanie detekcji w pasmach Dynamic/Spectral: niezależnie / w obrębie pary / w obrębie… | P0 | M5 | research/04 §d | `[CH-5]` Linked lanes share one gain (Dynamic); linkowanie w Spectral | Zrealizowane (M3) |
| CH-6 | Kanały, których pasmo nie obejmuje, zostają nietknięte (weryfikacja null testem) | P0 | M5 | research/04 §d | `[CH-6]` Engine applies active bands and leaves inactive ones bit-exact; Band parameters drive the engine… | Zrealizowane dla pasm statycznych (M2); surround — M5 |
| BAND-1 | Do 24 pasm | P0 | M2 | research/05 §d | `[BAND-1]` Band slots…; Band solo auditions the band region…; Band parameters drive the engine… | Zrealizowane (M2); benchmark CPU — M3 |
| BAND-2 | Kształty: Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt Shelf | P0 (Flat Tilt, All Pass P1) | M2 | research/05 §d | `[BAND-2]` Matched bell / shelves / notch, band pass, all pass, tilt | Zrealizowane w Minimum Phase (obwiednia błędu: research/05 §c); ±0,1 dB — M4 |
| BAND-3 | Slope dla Low/High Cut: 6, 12, 18, 24, 30, 36, 48, 72 i 96 dB/oct (płynna regulacja — P1)… | P0 (płynny slope P1) | M2 | research/05 §d | `[BAND-3]` Cut filters: -3 dB at the corner and the nominal slope | Zrealizowane (slopy stałe); płynny slope P1; Brickwall w Natural Phase — M4 |
| BAND-4 | Tworzenie pasma kliknięciem — kształt zależy od miejsca kliknięcia (jak w Pro-Q 4) | P0 | M2 | research/05 §d | `[BAND-4]` Band shape follows the click zone; hover preview w EqDisplay | Zrealizowane (M2) |
| BAND-5 | Prawy przycisk myszy na paśmie otwiera menu kontekstowe: Shape, Slope, Mode, Target… | P0 | M2 | research/05 §d | `[BAND-5]` Slope menu lists only the slopes…; Creating, editing and deleting bands are single undo steps | Zrealizowane (M2) |
| BAND-6 | Interakcja: przeciąganie (frequency/gain), kółko myszy zmienia Q, Shift daje precyzyjną… | P0 | M2 | research/05 §d | `[BAND-6]` Frequency sweeps are zipper-free; Typed values are parsed…; undo/gesty | Zrealizowane (M2) |
| BAND-7 | Pływający panel pasma pod wyświetlaczem: wszystkie parametry pasma i odczyty z sekcji 12 | P0 | M2 | research/05 §d | `ui/BandPanel` (wszystkie parametry pasma, dynamika w trybach Dynamic/Spectral) | Częściowo: parametry (M2); odczyty §12 — M7 |
| DYN-1 | Static — klasyczny filtr | P0 | M2 | research/06 §d | `ssc::engine::BandProcessor` (tryb Static) | Zrealizowane (M2) |
| DYN-2 | Dynamic — wzmocnienie całego pasma zależy od poziomu wykrytego w paśmie (klasyczny dynamic EQ) | P0 | M3 | research/06 §d | `[DYN-4]` `[DYN-5]` testy pasma Dynamic; `BandProcessor` tryb Dynamic | Zrealizowane (M3) |
| DYN-3 | Spectral — w zakresie pasma (ważonym jego kształtem) przetwarzane są tylko częstotliwości… | P0 | M3 | research/06 §d | `[DYN-3]` Spectral band reduces only the tone over the threshold; `SpectralEngine` | Zrealizowane (M3) |
| DYN-4 | Parametry pasm Dynamic/Spectral: Threshold, Ratio, Direction, Range, Attack, Release, Lookahead, Knee, Q, Mix, Selectivity | P0 (Detector, Sidechain, program-dependent P1) | M3 | research/06 §d | `[DYN-4]` Gain computer…; Ratio 1:1…Range; Mode changes move default thresholds | Zrealizowane (M3); Detector Peak, Sidechain — P1 |
| DYN-5 | Znaczenie kombinacji Ratio × Direction (w obszarze aktywnym nachylenie charakterystyki… | P0 | M3 | research/06 §d | `[DYN-5]` Gain computer follows the static characteristic; Dynamic bell steady state | Zrealizowane (M3) |
| DYN-6 | Implementuj zgodnie z dobrą praktyką (Giannoulis, Massberg, Reiss 2012): układ… | P0 (program-dependent P1) | M3 | research/06 §d | `[DYN-6]` Attack and release are time constants | Zrealizowane (M3); program-dependent — P1 |
| DYN-7 | Mix pasma działa w domenie wzmocnienia (skaluje zmianę w dB), a nie jako równoległe… | P0 | M3 | research/06 §d | `[DYN-7]` Dynamic band mix scales the gain change | Zrealizowane (M3) |
| DYN-8 | Lookahead: tor główny jest opóźniony o globalny Max Lookahead (0–20 ms, domyślnie 0 ms)… | P0 | M3 | research/06 §d | `[DYN-8]` Lookahead delays the main path…; Max Lookahead sets the reported latency; band lookahead raises Max Lookahead | Zrealizowane w Dynamic (M3); Spectral — kwantowanie do hopu później |
| DYN-9 | Silnik spektralny: STFT z WOLA albo odwracalna transformata o stałym Q (oceń w researchu) | P0 (Low Latency P1) | M3 | research/06 §d, ADR-0006 | `[DYN-9]` unity mask −139 dB; research/06 §c | Zrealizowane (M3); okienkowanie IR maski (Max), Low Latency P1 — M4 |
| DYN-10 | Threshold Relative (inspiracja: soothe): próg jest liczony względem wygładzonej obwiedni widma | P0 | M3 | research/06 §d | `[DYN-10]` Relative threshold (Dynamic i Spectral) | Zrealizowane (M3) |
| DYN-11 | Bieżąca zmiana wzmocnienia każdego pasma jest rysowana na żywo (sekcja 11) | P0 | M3 | research/06 §d | EqDisplay: krzywa dynamiczna i maska spektralna na żywo | Zrealizowane (M3); poświata obszarów — M7 |
| AG-1 | Globalny Auto Gain dopasowuje głośność wyjścia do wejścia (uczciwe porównanie A/B)… | P0 | M3 | research/07 §d | `[AG-1]` Auto Gain leaves channels without bands untouched | Zrealizowane (M3) |
| AG-2 | Auto Gain pasma (pasma Dynamic i Spectral) to automatyczny make-up: przywraca średni poziom… | P0 | M3 | research/07 §d | `[AG-2]` Band Auto Gain gives back the average change | Zrealizowane (M3) |
| AG-3 | Dwa tryby ważenia: Fletcher (ISO 226, domyślny) i Digital (RMS bez krzywej słuchu) | P0 (regulacja fonów P1) | M3 | research/07 §d, ADR-0007 | `[AG-3]` ISO 226:2003 contours; Digital w `[AG-6]` | Zrealizowane (M3): ISO 226:2003 (tabela 2023 wymienna), 80 phon; regulacja fonów — P1 |
| AG-4 | Szybkość śledzenia: Very Fast / Fast / Medium / Long / Stable (domyślnie) | P0 | M3 | research/07 §d | `[AG-4]` time constant per speed; Stable settles | Zrealizowane (M3); tolerancja obejmuje opóźnienie analizy (research/07) |
| AG-5 | Zabezpieczenia: limit korekcji ±24 dB; zamrożenie korekcji w ciszy (bramka jak w BS.1770… | P0 | M3 | research/07 §d | `[AG-5]` silence holds, ≤ 0,5 dB/ms; przed Solo | Zrealizowane (M3); Delta — P1 |
| AG-6 | Test: różowy szum + Bell +6 dB przy 3 kHz → różnica głośności wejście/wyjście ≤ 0,5 dB… | P0 | M3 | research/07 §d | `[AG-6]` model −0,06 dB, BS.1770 −0,2 LU | Zrealizowane (M3) |
| PH-1 | Globalny Phase Mode: Minimum Phase, Natural Phase, Analog Feel N/P/S | P0 | M2 (Min Phase), M4 (Natural), M6 (Analog) | research/08 §d, ADR-0004, ADR-0005, ADR-0009 | `[BAND-2]` (Minimum Phase) | Minimum Phase zrealizowane (M2); Natural Phase — M4; Analog Feel — M6 |
| PH-2 | Quality: 4 poziomy w każdym trybie — Eco / Normal / High / Max | P0 | M4 | research/08 §d | — | Zaprojektowane, do realizacji |
| PH-3 | Architektura nie może blokować późniejszego dodania trybu Linear Phase (P2) | P2 | M4 (przegląd) | research/08 §d | — | Zaprojektowane, do realizacji |
| CHR-1 | Character per pasmo: Clear (domyślnie), Warm, Sharp | P0 | M4 | research/09 §d | — | Zaprojektowane, do realizacji |
| CHR-2 | Nieliniowość działa tylko na wkład pasma, więc pasmo nie barwi całego sygnału | P0 | M4 | research/09 §d | — | Zaprojektowane, do realizacji |
| CHR-3 | Cele pomiarowe THD dla Warm/Sharp/Clear (sinus 0 dBFS, Bell +6 dB) | P0 | M4 | research/09 §d | — | Zaprojektowane, do realizacji |
| CHR-4 | W trybie Analog Feel Character steruje nieliniowością modelu: Clear = sam model krzywych i… | P0 | M6 | research/09 §d | — | Zaprojektowane, do realizacji |
| CHR-5 | Kontekst: w Pro-Q 4 tryby Character (Clean/Subtle/Warm) są globalne — tutaj działają per pasmo | P0 | M4 | research/09 §d | — | Zaprojektowane, do realizacji |
| OS-1 | Ustawienie globalne: Off (1×, domyślnie) / 2× / 4× / 8× | P0 | M4 | research/10 §d | — | Zaprojektowane, do realizacji |
| OS-2 | Filtry: polifazowe IIR w Minimum Phase, liniowofazowe FIR w Natural Phase / Analog Feel | P0 | M4 | research/10 §d | — | Zaprojektowane, do realizacji |
| OS-3 | Zakres działania oversamplingu zgodnie z ARC-2 | P0 | M4 | research/10 §d | — | Zaprojektowane, do realizacji |
| OS-4 | Osobne ustawienie oversamplingu dla renderu offline (P1) | P1 | M4 | research/10 §d | — | Zaprojektowane, do realizacji |
| OS-5 | Gdy aktywne są nieliniowości, a miernik aliasingu (sekcja 13) przekroczy próg, UI subtelnie… | P0 | M4 | research/10 §d | — | Zaprojektowane, do realizacji |
| VIS-1 | Na środku okna analizator w czasie rzeczywistym (jak w Pro-Q 4): widmo pre/post (sidechain… | P0 (sidechain P1) | M1 (podstawy), M7 | research/11 §d | `SpectrumAnalyzer`, `[analyzer]` testy; EqDisplay | Częściowo: pre/post, oś log, tilt 4,5 dB/oct (M1); regulacje, freeze, peak hold — M7 |
| VIS-2 | Glow aktywności: najbardziej aktywne obszary widma świecą jaśniej | P0 (paleta dla daltonistów P1) | M7 | research/11 §d | — | Zaprojektowane, do realizacji |
| VIS-3 | Bieżąca redukcja lub podbicie pasm Dynamic i Spectral jest rysowana jako świecące… | P0 | M7 | research/11 §d | — | Zaprojektowane, do realizacji |
| VIS-4 | Podpowiedzi rezonansów wyłącznie graficzne i subtelne; nie zmieniają dźwięku | P0 | M7 | research/11 §d | — | Zaprojektowane, do realizacji |
| VIS-5 | Algorytm wykrywania rezonansów oparty na badaniach + ewaluacja precision/recall | P0 | M7 | research/11 §d | — | Zaprojektowane, do realizacji |
| VIS-6 | Wydajność: 60 fps, rendering akcelerowany przez GPU z programowym fallbackiem, analiza poza… | P0 | M1 (bramka), M7 | research/11 §d, ADR-0012 | VBlankAttachment, analiza w wątku tła, skalowalne okno | Częściowo (M1–M2); GPU/fallback — M7 |
| VIS-7 | Układ: ciemny motyw, duży wyświetlacz pośrodku, pływający panel pasma, górny pasek… | P0 | M1–M7 | research/11 §d | EqDisplay, BandPanel, ControlBar, nagłówek z zakresem EQ | Częściowo: wyświetlacz, pływający panel, dolny pasek, latencja/CPU; presety, A/B, mierniki — M7/M8 |
| VIS-8 | P2: widok spektrogramu i wizualizacja Delta (co zostaje usunięte) | P2 | M7 | research/11 §d | — | Zaprojektowane, do realizacji |
| PR-1 | Odczyty per pasmo: Δφ śr., opóźnienie grupowe, pre-ring / post-ring | P0 | M7 | research/12 §d | — | Zaprojektowane, do realizacji |
| PR-2 | Obliczenia w wątku tła, odświeżanie maksymalnie 10–20 Hz, nigdy w wątku audio | P0 | M7 | research/12 §d | — | Zaprojektowane, do realizacji |
| PR-3 | Kontrola poprawności: T60 ≈ 2,2·Q/f₀; odczyt vs pomiar ±5 % | P0 | M7 | research/12 §d | — | Zaprojektowane, do realizacji |
| PR-4 | Odczyty globalne: łączna latencja; nakładka krzywej fazy i opóźnienia grupowego całego EQ (P1) | P0 (nakładka fazy P1) | M7 | research/12 §d | — | Zaprojektowane, do realizacji |
| MTR-1 | Mierniki na żywo: THD+N [dB, %] i Aliasing [dB]; rozbicie na pasma | P0 | M7 | research/13 §d | — | Zaprojektowane, do realizacji |
| MTR-2 | Metoda: residuum nieliniowości, modulacja wzmocnienia, aliasing (OS i pomiar cieniowy) | P0 | M7 | research/13 §d | — | Zaprojektowane, do realizacji |
| MTR-3 | Walidacja: zgodność z pomiarem offline (sinus i sweep — Farina 2000; AES17) w granicach ±3 dB | P0 | M7 | research/13 §d | — | Zaprojektowane, do realizacji |
| MTR-4 | Prezentacja: mały, nienachalny miernik w dolnym pasku; kolor ostrzegawczy po przekroczeniu… | P0 | M7 | research/13 §d | — | Zaprojektowane, do realizacji |
| GLB-1 | On/Off (globalny bypass) powiązany z parametrem bypass hosta (VST3 kIsBypass), z płynnym… | P0 | M1 | research/14 §d | test bypass (procesor), pluginval | Zrealizowane (M1) |
| GLB-2 | Output gain, globalny Mix (dry/wet z wyrównaniem latencji), Max Lookahead (DYN-8]), Delta (P1) | P0 (Delta P1) | M1 (Delta: v1.x) | research/14 §d | Engine: output, mix z wyrównaniem latencji; Max Lookahead (`[DYN-8]`) | Częściowo: Output, Mix, Max Lookahead; Delta — P1 |
| GLB-3 | Presety fabryczne i użytkownika, A/B, Undo/Redo, kopiowanie ustawień pasm | P0 | M2 (Undo), M8 (presety) | research/14 §d | BandModel: Undo/Redo, kopiuj/wklej pasma; `[BAND-5]` undo test | Częściowo: Undo/Redo, kopiowanie pasm (M2); presety, A/B — M8 |
| GLB-4 | Wszystkie parametry można automatyzować i są wygładzane (bez zipper noise) | P0 | M1 | research/14 §d | `[GLB-4]` State round trip; `[BAND-6]` zipper-free | Zrealizowane dla parametrów M1/M2 |
| GLB-5 | Wskaźniki latencji i obciążenia CPU w UI | P0 | M1 | research/14 §d | Nagłówek edytora: latencja i CPU (4 Hz) | Zrealizowane (M1) |
| NFR-1 | Budżety CPU — cele startowe; zmierz je, raportuj i uzasadnij odstępstwa | P0 | M3, M8 | architecture.md §7, ADR-0010 | docs/benchmarks.md (`ssc_benchmark`) | Spełnione na Xeon 2,1 GHz; pomiar na M1/x86 referencyjnym — M8 |
| NFR-2 | To samo brzmienie przy 44,1 i 96 kHz (w tolerancji testów) | P0 | M2 | architecture.md §7, ADR-0010 | `[NFR-2]` Bands respond the same at 44.1 and 96 kHz | Zrealizowane (M2) — tolerancje w research/05 §c |
| NFR-3 | Render offline brzmi identycznie jak odtwarzanie w czasie rzeczywistym (chyba że włączono… | P0 | M3 | architecture.md §7, ADR-0010 | `[NFR-3]` Output does not depend on the host block size | Zrealizowane (M3): wynik bit-identyczny dla dowolnych bloków |
| NFR-4 | Jakość kodu: C++20, clang-format, ostrzeżenia traktowane jak błędy, ASan/UBSan w testach… | P0 | M1 | architecture.md §7, ADR-0010 | CI: clang-format, `SSC_WARNINGS_AS_ERRORS`, ASan/UBSan | Zrealizowane |
| TST-1 | Testy jednostkowe DSP (Catch2): filtry, null testy, M/S, dynamika, latencja, kryteria sekcji | P0 | M1–M8 | ADR-0015 | 41 testów rdzenia + 14 testów wtyczki (Catch2) | W realizacji (rośnie z każdym etapem) |
| TST-2 | CI: pluginval (strictness 10) na obu systemach, Steinberg VST3 validator, auval (dla AU) | P0 | M1 | ADR-0015 | CI: pluginval strictness 10 (Linux, macOS, Windows) | Częściowo: pluginval; VST3 validator, auval — M8 |
| TST-3 | Macierz hostów (ręczna checklista w docs/testing.md): Cubase/Nuendo (w tym Atmos 7.1.4 i… | P0 | M8 | docs/testing.md | — | Zaprojektowane, do realizacji |
| TST-4 | Testy obciążeniowe: wszystkie układy kanałów, fs 44,1–384 kHz, bufory 1–8192 próbek i… | P0 | M8 | ADR-0015 | — | Zaprojektowane, do realizacji |
| REL-1 | macOS: universal binary, podpis Developer ID, .pkg, notaryzacja, deinstalator | P0 | M8 | ADR-0014 | — | Zaprojektowane, do realizacji |
| REL-2 | Windows: bundle VST3, Inno Setup z deinstalatorem, podpis Authenticode | P0 | M8 | ADR-0014 | — | Zaprojektowane, do realizacji |
| REL-3 | GitHub Actions (macOS + Windows): build, testy, pluginval, pakowanie, podpis i notaryzacja… | P0 | M1 (CI), M8 (podpisy) | ADR-0014 | `.github/workflows/ci.yml` | Częściowo: build, testy, pluginval, format; pakowanie, podpisy, notaryzacja — M8 |
| REL-4 | Dokumentacja: README (instrukcja budowania), manual użytkownika (PL; EN — P1), licencje… | P0 (manual EN P1) | M1 (README), M8 | ADR-0014 | README.md (budowanie, struktura) | Częściowo: README; manual — M8 |
