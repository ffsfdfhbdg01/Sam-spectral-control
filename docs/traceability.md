# Śledzenie wymagań (traceability)

Każde wymaganie z [PROMPT.md](../PROMPT.md) z priorytetem, etapem realizacji, źródłem kryteriów akceptacji i statusem. Tabela jest aktualizowana po każdym kamieniu milowym; kolumna „Dowód” zostanie uzupełniona nazwami testów i raportów pomiarowych.

Stan: **M0 zakończony** — 88 wymagań, wszystkie z kryteriami akceptacji w notatkach researchu lub ADR.

| ID | Wymaganie (skrót) | Priorytet | Etap | Kryteria / projekt | Dowód | Status |
|---|---|---|---|---|---|---|
| PLT-1 | Formaty: VST3 (P0) | P0 (AU P1; AAX, CLAP P2) | M1 (VST3), M8 (AU/AAX) | ADR-0001 | — | Zaprojektowane, do realizacji |
| PLT-2 | Systemy: macOS 11+ (universal binary arm64 + x86_64), Windows 10/11 x64 | P0 (ARM64 P2) | M1 | ADR-0001 | — | Zaprojektowane, do realizacji |
| PLT-3 | Stos: C++20, JUCE 8, Steinberg VST3 SDK (od wersji 3.8 na licencji MIT), CMake, GitHub Actions | P0 | M1 | ADR-0001, ADR-0002 (otwarta), ADR-0013 | — | Zaprojektowane, do realizacji |
| PLT-4 | Częstotliwości próbkowania 44,1–384 kHz | P0 | M1 | ADR-0001 | — | Zaprojektowane, do realizacji |
| PLT-5 | Bezpieczeństwo czasu rzeczywistego: w wątku audio żadnych alokacji, blokad, I/O ani logowania | P0 | M1 | ADR-0010 (RTSan w CI) | — | Zaprojektowane, do realizacji |
| PLT-6 | Identyfikatory (VST3 Class ID, kody producenta i wtyczki) wygeneruj raz, zapisz w… | P0 | M1 | ADR-0016 (otwarta) | — | Otwarte (decyzja użytkownika) |
| ARC-1 | Zaproponuj i uzasadnij tor sygnału (diagram w docs/architecture.md) | P0 | M0 (projekt) → M1–M3 | architecture.md §2, ADR-0003 | — | Zaprojektowane (M0) |
| ARC-2 | Oversampling obejmuje tylko etapy, które na nim zyskują: nieliniowości, szybką modulację… | P0 | M0 (projekt) → M1–M3 | ADR-0008 | — | Zaprojektowane (M0) |
| ARC-3 | Latencja jest raportowana hostowi dokładnie i nie zmienia się podczas odtwarzania | P0 | M0 (projekt) → M1–M3 | ADR-0011 | — | Zaprojektowane (M0) |
| ARC-4 | Wątki: audio (DSP), analiza (analizator, wykrywanie rezonansów, odczyty fazy i ringingu… | P0 | M0 (projekt) → M1–M3 | ADR-0010 | — | Zaprojektowane (M0) |
| ARC-5 | Każdy blok DSP da się przetestować w izolacji, bez GUI | P0 | M0 (projekt) → M1–M3 | architecture.md | — | Zaprojektowane (M0) |
| ARC-6 | Każdy tryb pasma działa w każdym trybie fazy | P0 | M0 (projekt) → M1–M3 | architecture.md | — | Zaprojektowane (M0) |
| CH-1 | Układy kanałów: mono, stereo i każdy układ surround/immersive, który host przekazuje przez VST3 | P0 | M5 | research/04 §d | — | Zaprojektowane, do realizacji |
| CH-2 | Dolby Atmos: wtyczka działa na ścieżkach i szynach bed (do 7.1.2), na szynach monitorowych… | P0 | M5 | research/04 §d | — | Zaprojektowane, do realizacji |
| CH-3 | Cel pasma w stereo: All (domyślnie), Left, Right, Mid, Side | P0 | M2 | research/04 §d | — | Zaprojektowane, do realizacji |
| CH-4 | Cel pasma w surround — panel wyboru głośników (nazewnictwo Dolby: L, R, C, LFE, Lss, Rss… | P0 | M5 | research/04 §d | — | Zaprojektowane, do realizacji |
| CH-5 | Linkowanie detekcji w pasmach Dynamic/Spectral: niezależnie / w obrębie pary / w obrębie… | P0 | M5 | research/04 §d | — | Zaprojektowane, do realizacji |
| CH-6 | Kanały, których pasmo nie obejmuje, zostają nietknięte (weryfikacja null testem) | P0 | M5 | research/04 §d | — | Zaprojektowane, do realizacji |
| BAND-1 | Do 24 pasm | P0 | M2 | research/05 §d | — | Zaprojektowane, do realizacji |
| BAND-2 | Kształty: Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt Shelf | P0 (Flat Tilt, All Pass P1) | M2 | research/05 §d | — | Zaprojektowane, do realizacji |
| BAND-3 | Slope dla Low/High Cut: 6, 12, 18, 24, 30, 36, 48, 72 i 96 dB/oct (płynna regulacja — P1)… | P0 (płynny slope P1) | M2 | research/05 §d | — | Zaprojektowane, do realizacji |
| BAND-4 | Tworzenie pasma kliknięciem — kształt zależy od miejsca kliknięcia (jak w Pro-Q 4) | P0 | M2 | research/05 §d | — | Zaprojektowane, do realizacji |
| BAND-5 | Prawy przycisk myszy na paśmie otwiera menu kontekstowe: Shape, Slope, Mode, Target… | P0 | M2 | research/05 §d | — | Zaprojektowane, do realizacji |
| BAND-6 | Interakcja: przeciąganie (frequency/gain), kółko myszy zmienia Q, Shift daje precyzyjną… | P0 | M2 | research/05 §d | — | Zaprojektowane, do realizacji |
| BAND-7 | Pływający panel pasma pod wyświetlaczem: wszystkie parametry pasma i odczyty z sekcji 12 | P0 | M2 | research/05 §d | — | Zaprojektowane, do realizacji |
| DYN-1 | Static — klasyczny filtr | P0 | M2 | research/06 §d | — | Zaprojektowane, do realizacji |
| DYN-2 | Dynamic — wzmocnienie całego pasma zależy od poziomu wykrytego w paśmie (klasyczny dynamic EQ) | P0 | M3 | research/06 §d | — | Zaprojektowane, do realizacji |
| DYN-3 | Spectral — w zakresie pasma (ważonym jego kształtem) przetwarzane są tylko częstotliwości… | P0 | M3 | research/06 §d | — | Zaprojektowane, do realizacji |
| DYN-4 | Parametry pasm Dynamic/Spectral: Threshold, Ratio, Direction, Range, Attack, Release, Lookahead, Knee, Q, Mix, Selectivity | P0 (Detector, Sidechain, program-dependent P1) | M3 | research/06 §d | — | Zaprojektowane, do realizacji |
| DYN-5 | Znaczenie kombinacji Ratio × Direction (w obszarze aktywnym nachylenie charakterystyki… | P0 | M3 | research/06 §d | — | Zaprojektowane, do realizacji |
| DYN-6 | Implementuj zgodnie z dobrą praktyką (Giannoulis, Massberg, Reiss 2012): układ… | P0 (program-dependent P1) | M3 | research/06 §d | — | Zaprojektowane, do realizacji |
| DYN-7 | Mix pasma działa w domenie wzmocnienia (skaluje zmianę w dB), a nie jako równoległe… | P0 | M3 | research/06 §d | — | Zaprojektowane, do realizacji |
| DYN-8 | Lookahead: tor główny jest opóźniony o globalny Max Lookahead (0–20 ms, domyślnie 0 ms)… | P0 | M3 | research/06 §d | — | Zaprojektowane, do realizacji |
| DYN-9 | Silnik spektralny: STFT z WOLA albo odwracalna transformata o stałym Q (oceń w researchu) | P0 (Low Latency P1) | M3 | research/06 §d, ADR-0006 | — | Zaprojektowane, do realizacji |
| DYN-10 | Threshold Relative (inspiracja: soothe): próg jest liczony względem wygładzonej obwiedni widma | P0 | M3 | research/06 §d | — | Zaprojektowane, do realizacji |
| DYN-11 | Bieżąca zmiana wzmocnienia każdego pasma jest rysowana na żywo (sekcja 11) | P0 | M3 | research/06 §d | — | Zaprojektowane, do realizacji |
| AG-1 | Globalny Auto Gain dopasowuje głośność wyjścia do wejścia (uczciwe porównanie A/B)… | P0 | M3 | research/07 §d | — | Zaprojektowane, do realizacji |
| AG-2 | Auto Gain pasma (pasma Dynamic i Spectral) to automatyczny make-up: przywraca średni poziom… | P0 | M3 | research/07 §d | — | Zaprojektowane, do realizacji |
| AG-3 | Dwa tryby ważenia: Fletcher (ISO 226, domyślny) i Digital (RMS bez krzywej słuchu) | P0 (regulacja fonów P1) | M3 | research/07 §d, ADR-0007 | — | Zaprojektowane, do realizacji |
| AG-4 | Szybkość śledzenia: Very Fast / Fast / Medium / Long / Stable (domyślnie) | P0 | M3 | research/07 §d | — | Zaprojektowane, do realizacji |
| AG-5 | Zabezpieczenia: limit korekcji ±24 dB; zamrożenie korekcji w ciszy (bramka jak w BS.1770… | P0 | M3 | research/07 §d | — | Zaprojektowane, do realizacji |
| AG-6 | Test: różowy szum + Bell +6 dB przy 3 kHz → różnica głośności wejście/wyjście ≤ 0,5 dB… | P0 | M3 | research/07 §d | — | Zaprojektowane, do realizacji |
| PH-1 | Globalny Phase Mode: Minimum Phase, Natural Phase, Analog Feel N/P/S | P0 | M2 (Min Phase), M4 (Natural), M6 (Analog) | research/08 §d, ADR-0004, ADR-0005, ADR-0009 | — | Zaprojektowane, do realizacji |
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
| VIS-1 | Na środku okna analizator w czasie rzeczywistym (jak w Pro-Q 4): widmo pre/post (sidechain… | P0 (sidechain P1) | M1 (podstawy), M7 | research/11 §d | — | Zaprojektowane, do realizacji |
| VIS-2 | Glow aktywności: najbardziej aktywne obszary widma świecą jaśniej | P0 (paleta dla daltonistów P1) | M7 | research/11 §d | — | Zaprojektowane, do realizacji |
| VIS-3 | Bieżąca redukcja lub podbicie pasm Dynamic i Spectral jest rysowana jako świecące… | P0 | M7 | research/11 §d | — | Zaprojektowane, do realizacji |
| VIS-4 | Podpowiedzi rezonansów wyłącznie graficzne i subtelne; nie zmieniają dźwięku | P0 | M7 | research/11 §d | — | Zaprojektowane, do realizacji |
| VIS-5 | Algorytm wykrywania rezonansów oparty na badaniach + ewaluacja precision/recall | P0 | M7 | research/11 §d | — | Zaprojektowane, do realizacji |
| VIS-6 | Wydajność: 60 fps, rendering akcelerowany przez GPU z programowym fallbackiem, analiza poza… | P0 | M1 (bramka), M7 | research/11 §d, ADR-0012 | — | Zaprojektowane, do realizacji |
| VIS-7 | Układ: ciemny motyw, duży wyświetlacz pośrodku, pływający panel pasma, górny pasek… | P0 | M1–M7 | research/11 §d | — | Zaprojektowane, do realizacji |
| VIS-8 | P2: widok spektrogramu i wizualizacja Delta (co zostaje usunięte) | P2 | M7 | research/11 §d | — | Zaprojektowane, do realizacji |
| PR-1 | Odczyty per pasmo: Δφ śr., opóźnienie grupowe, pre-ring / post-ring | P0 | M7 | research/12 §d | — | Zaprojektowane, do realizacji |
| PR-2 | Obliczenia w wątku tła, odświeżanie maksymalnie 10–20 Hz, nigdy w wątku audio | P0 | M7 | research/12 §d | — | Zaprojektowane, do realizacji |
| PR-3 | Kontrola poprawności: T60 ≈ 2,2·Q/f₀; odczyt vs pomiar ±5 % | P0 | M7 | research/12 §d | — | Zaprojektowane, do realizacji |
| PR-4 | Odczyty globalne: łączna latencja; nakładka krzywej fazy i opóźnienia grupowego całego EQ (P1) | P0 (nakładka fazy P1) | M7 | research/12 §d | — | Zaprojektowane, do realizacji |
| MTR-1 | Mierniki na żywo: THD+N [dB, %] i Aliasing [dB]; rozbicie na pasma | P0 | M7 | research/13 §d | — | Zaprojektowane, do realizacji |
| MTR-2 | Metoda: residuum nieliniowości, modulacja wzmocnienia, aliasing (OS i pomiar cieniowy) | P0 | M7 | research/13 §d | — | Zaprojektowane, do realizacji |
| MTR-3 | Walidacja: zgodność z pomiarem offline (sinus i sweep — Farina 2000; AES17) w granicach ±3 dB | P0 | M7 | research/13 §d | — | Zaprojektowane, do realizacji |
| MTR-4 | Prezentacja: mały, nienachalny miernik w dolnym pasku; kolor ostrzegawczy po przekroczeniu… | P0 | M7 | research/13 §d | — | Zaprojektowane, do realizacji |
| GLB-1 | On/Off (globalny bypass) powiązany z parametrem bypass hosta (VST3 kIsBypass), z płynnym… | P0 | M1 | research/14 §d | — | Zaprojektowane, do realizacji |
| GLB-2 | Output gain, globalny Mix (dry/wet z wyrównaniem latencji), Max Lookahead (DYN-8]), Delta (P1) | P0 (Delta P1) | M1 (Delta: v1.x) | research/14 §d | — | Zaprojektowane, do realizacji |
| GLB-3 | Presety fabryczne i użytkownika, A/B, Undo/Redo, kopiowanie ustawień pasm | P0 | M2 (Undo), M8 (presety) | research/14 §d | — | Zaprojektowane, do realizacji |
| GLB-4 | Wszystkie parametry można automatyzować i są wygładzane (bez zipper noise) | P0 | M1 | research/14 §d | — | Zaprojektowane, do realizacji |
| GLB-5 | Wskaźniki latencji i obciążenia CPU w UI | P0 | M1 | research/14 §d | — | Zaprojektowane, do realizacji |
| NFR-1 | Budżety CPU — cele startowe; zmierz je, raportuj i uzasadnij odstępstwa | P0 | M3, M8 | architecture.md §7, ADR-0010 | — | Zaprojektowane, do realizacji |
| NFR-2 | To samo brzmienie przy 44,1 i 96 kHz (w tolerancji testów) | P0 | M2 | architecture.md §7, ADR-0010 | — | Zaprojektowane, do realizacji |
| NFR-3 | Render offline brzmi identycznie jak odtwarzanie w czasie rzeczywistym (chyba że włączono… | P0 | M3 | architecture.md §7, ADR-0010 | — | Zaprojektowane, do realizacji |
| NFR-4 | Jakość kodu: C++20, clang-format, ostrzeżenia traktowane jak błędy, ASan/UBSan w testach… | P0 | M1 | architecture.md §7, ADR-0010 | — | Zaprojektowane, do realizacji |
| TST-1 | Testy jednostkowe DSP (Catch2): filtry, null testy, M/S, dynamika, latencja, kryteria sekcji | P0 | M1–M8 | ADR-0015 | — | Zaprojektowane, do realizacji |
| TST-2 | CI: pluginval (strictness 10) na obu systemach, Steinberg VST3 validator, auval (dla AU) | P0 | M1 | ADR-0015 | — | Zaprojektowane, do realizacji |
| TST-3 | Macierz hostów (ręczna checklista w docs/testing.md): Cubase/Nuendo (w tym Atmos 7.1.4 i… | P0 | M8 | docs/testing.md | — | Zaprojektowane, do realizacji |
| TST-4 | Testy obciążeniowe: wszystkie układy kanałów, fs 44,1–384 kHz, bufory 1–8192 próbek i… | P0 | M8 | ADR-0015 | — | Zaprojektowane, do realizacji |
| REL-1 | macOS: universal binary, podpis Developer ID, .pkg, notaryzacja, deinstalator | P0 | M8 | ADR-0014 | — | Zaprojektowane, do realizacji |
| REL-2 | Windows: bundle VST3, Inno Setup z deinstalatorem, podpis Authenticode | P0 | M8 | ADR-0014 | — | Zaprojektowane, do realizacji |
| REL-3 | GitHub Actions (macOS + Windows): build, testy, pluginval, pakowanie, podpis i notaryzacja… | P0 | M1 (CI), M8 (podpisy) | ADR-0014 | — | Zaprojektowane, do realizacji |
| REL-4 | Dokumentacja: README (instrukcja budowania), manual użytkownika (PL; EN — P1), licencje… | P0 (manual EN P1) | M1 (README), M8 | ADR-0014 | — | Zaprojektowane, do realizacji |
