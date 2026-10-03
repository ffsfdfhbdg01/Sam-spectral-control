# Sam Spectral Control — prompt dla agenta (specyfikacja wtyczki)

> **Jak używać:** przekaż ten plik agentowi kodującemu w repozytorium projektu, np.: „Zrealizuj PROMPT.md, zaczynając od kamienia milowego M0”.
> Wymagania mają identyfikatory (np. `[DYN-5]`), żeby można było się do nich odwoływać w testach i raportach.

---

## 0. Rola i cel

Działasz jako zespół: **inżynier DSP (C++)**, **badacz audio** (psychoakustyka, przetwarzanie sygnałów), **projektant UI/UX wtyczek audio** i **release engineer**.

Zaprojektuj, zaimplementuj, przetestuj i wydaj (z instalatorami na macOS i Windows) wtyczkę audio **Sam Spectral Control**. To korektor parametryczny, w którym każde pasmo może być statyczne, dynamiczne lub spektralno-dynamiczne. Każde pasmo może działać tylko na wybranych kanałach lub składowych sygnału (L/R/M/S, grupy głośników surround). Wtyczka obsługuje surround i Dolby Atmos oraz ma tryby fazy, w tym modele analogowe. Na bieżąco pokazuje skutki uboczne przetwarzania: przesunięcie fazy, pre-/post-ring, THD+N i aliasing.

Wzorce UX: **FabFilter Pro-Q 4** (praca z pasmami, analizator) i **oeksound soothe2/soothe3** (estetyka, „świecąca” wizualizacja redukcji). Inspiruj się ich zachowaniem i ergonomią. Nie kopiuj grafiki, układu 1:1, nazw ani znaków towarowych.

## 1. Zasady pracy

1. **Najpierw research, potem kod.** Dla każdej funkcji z sekcji 4–14 utwórz `docs/research/<funkcja>.md` zawierający:
   1. przegląd badań naukowych i norm,
   2. opis, jak rozwiązują to najlepsze wtyczki (na podstawie manuali, recenzji i własnych pomiarów — bez dekompilacji),
   3. wybrane podejście z uzasadnieniem i odrzuconymi alternatywami,
   4. mierzalne kryteria akceptacji.

   Sekcja 16 zawiera zweryfikowane punkty startowe — to minimum, nie limit.
2. **Nie zmyślaj źródeł.** Przy każdym źródle podaj autorów, rok, tytuł, miejsce publikacji i DOI/URL. Źródło, którego nie da się zweryfikować, oznacz jako `[niezweryfikowane]`.
3. **Zapisuj decyzje** jako krótkie ADR w `docs/decisions/` (kontekst → decyzja → konsekwencje). Pytaj użytkownika tylko o decyzje blokujące. W pozostałych przypadkach wybierz rozsądną wartość domyślną i ją udokumentuj.
4. **Pracuj iteracyjnie** według kamieni milowych (sekcja 19). Po każdym etapie projekt ma się budować na obu systemach, testy mają przechodzić, a Ty raportujesz wynik (sekcja 21). Commituj małymi, opisanymi krokami. Nigdy nie commituj sekretów.
5. **Priorytety:** **P0** — wymagane w v1.0; **P1** — ważne (v1.x); **P2** — opcjonalne: architektura ma ich nie blokować, ale nie implementuj ich bez zgody użytkownika. Wymaganie bez oznaczenia ma priorytet P0.

## 2. Platformy, formaty i technologia

- **[PLT-1]** Formaty: **VST3** (P0). **AU** (P1) — potrzebny w Logic Pro, także przy miksie Dolby Atmos. **AAX** (P2) — potrzebny w Pro Tools; wymaga konta deweloperskiego Avid i podpisu PACE. CLAP (P2).
- **[PLT-2]** Systemy: macOS 11+ (universal binary arm64 + x86_64), Windows 10/11 x64. Windows ARM64 (P2).
- **[PLT-3]** Stos: C++20, JUCE 8, Steinberg VST3 SDK (od wersji 3.8 na licencji MIT), CMake, GitHub Actions. JUCE 8 jest dostępny na AGPLv3 albo na licencji komercyjnej — opisz konsekwencje i zapytaj użytkownika o wybór przed publikacją binarek. Bibliotekę FFT (JUCE dsp, pffft, vDSP/Accelerate, IPP…) wybierz na podstawie benchmarku.
- **[PLT-4]** Częstotliwości próbkowania 44,1–384 kHz. Dowolny rozmiar bufora, także zmienny i bardzo mały (1–32 próbki). Przetwarzanie w float i double.
- **[PLT-5]** Bezpieczeństwo czasu rzeczywistego: w wątku audio żadnych alokacji, blokad, I/O ani logowania. FTZ/DAZ, ochrona przed NaN/Inf, wszystkie zasoby przygotowane w `prepareToPlay`.
- **[PLT-6]** Identyfikatory (VST3 Class ID, kody producenta i wtyczki) wygeneruj raz, zapisz w repozytorium i nie zmieniaj po pierwszym wydaniu.

## 3. Architektura toru sygnału

- **[ARC-1]** Zaproponuj i uzasadnij tor sygnału (diagram w `docs/architecture.md`). Punkt wyjścia do weryfikacji:

  ```
  wejście
   → macierz kanałów (L/R/M/S, grupy surround)
   → [↑ oversampling]
   → łańcuch pasm czasowych Static/Dynamic
       (IIR lub FIR zależnie od trybu fazy; nieliniowości Character/Analog wewnątrz pasma)
   → [↓ oversampling]
   → silnik spektralny (STFT; wszystkie pasma Spectral jako jedna wspólna maska wzmocnień)
   → Auto Gain → Output / Mix / Bypass
   → wyjście
  ```
- **[ARC-2]** Oversampling obejmuje tylko etapy, które na nim zyskują: nieliniowości, szybką modulację wzmocnienia i filtry blisko częstotliwości Nyquista. Silnik spektralny pracuje na bazowej częstotliwości próbkowania.
- **[ARC-3]** Latencja jest raportowana hostowi dokładnie i nie zmienia się podczas odtwarzania. Zmienia się tylko po zmianie trybu fazy, Quality, oversamplingu lub Max Lookahead.
- **[ARC-4]** Wątki: audio (DSP), analiza (analizator, wykrywanie rezonansów, odczyty fazy i ringingu, mierniki THD+N/aliasingu) oraz UI. Komunikacja między nimi wyłącznie lock-free (FIFO, atomics, double-buffering).
- **[ARC-5]** Każdy blok DSP da się przetestować w izolacji, bez GUI.
- **[ARC-6]** Każdy tryb pasma działa w każdym trybie fazy. Jeśli jakaś kombinacja ma ograniczenia (np. większą latencję), UI wyraźnie to komunikuje.

## 4. Kanały, surround i Dolby Atmos

- **[CH-1]** Układy kanałów: mono, stereo i każdy układ surround/immersive, który host przekazuje przez VST3. Między innymi LCR, LCRS, quad, 5.0/5.1, 6.0/6.1, 7.0/7.1, 5.1.2, 5.1.4, 7.1.2 (bed Atmos), 7.1.4, 7.1.6, 9.1.4, 9.1.6 oraz układy z dwoma LFE (np. 10.2, 22.2). „7.2.4” nie jest standardowym układem Dolby Atmos (najpewniej chodzi o 7.1.4). Dlatego obsłuż dowolny `SpeakerArrangement`, także niestandardowy z dwoma LFE, zamiast zamkniętej listy układów.
- **[CH-2]** Dolby Atmos: wtyczka działa na ścieżkach i szynach bed (do 7.1.2), na szynach monitorowych i re-renderowanych (7.1.4, 9.1.6) oraz na obiektach (mono/stereo). Przetestuj ją w co najmniej jednym DAW z natywnym Atmos dla VST3 (Nuendo/Cubase Pro, Studio One, DaVinci Resolve Fairlight).
- **[CH-3]** Cel pasma w stereo: **All** (domyślnie), **Left**, **Right**, **Mid**, **Side**. Macierz M/S z idealną rekonstrukcją.
- **[CH-4]** Cel pasma w surround — panel wyboru głośników (nazewnictwo Dolby: L, R, C, LFE, Lss, Rss, Lrs, Rrs, Lw, Rw, Ltf, Rtf, Ltm, Rtm, Ltr, Rtr…):
  - **All** = wszystkie głośniki oprócz LFE (domyślnie); osobny przełącznik dołącza LFE,
  - pojedyncze głośniki i całe grupy jednym kliknięciem: Front L/R, Center, LFE, Side surround, Rear surround, Wides, Top front, Top middle, Top rear, wszystkie Height,
  - dowolne kombinacje (np. L/C/R),
  - **L/R/M/S działa wewnątrz każdej wybranej symetrycznej pary** — np. tylko Side par surround (Lss/Rss, Lrs/Rrs) albo tylko Mid par wysokościowych.
- **[CH-5]** Linkowanie detekcji w pasmach Dynamic/Spectral: niezależnie / w obrębie pary / w obrębie grupy / wszystkie kanały razem (domyślnie: w obrębie grupy).
- **[CH-6]** Kanały, których pasmo nie obejmuje, zostają nietknięte (weryfikacja null testem).

## 5. Pasma: kształty, tworzenie i edycja

- **[BAND-1]** Do 24 pasm. Każde pasmo ma: On/Off, Solo, Shape, Frequency, Gain, Q, Slope, Mode (sekcja 6), Target (sekcja 4), Character (sekcja 9) i Auto Gain (sekcja 7).
- **[BAND-2]** Kształty: Bell, Low Shelf, High Shelf, Low Cut, High Cut, Notch, Band Pass, Tilt Shelf. Flat Tilt i All Pass (P1).
- **[BAND-3]** Slope dla Low/High Cut: 6, 12, 18, 24, 30, 36, 48, 72 i 96 dB/oct (płynna regulacja — P1) oraz Brickwall tam, gdzie pozwala na to tryb fazy. Zakres dla pozostałych kształtów ustal w researchu (Pro-Q 4 pozwala na płynny slope 0–96 dB/oct).
- **[BAND-4]** Tworzenie pasma kliknięciem — kształt zależy od miejsca kliknięcia (jak w Pro-Q 4):
  - skrajnie lewa krawędź → Low Cut; skrajnie prawa → High Cut,
  - lewa część wyświetlacza lub przeciągnięcie wypadkowej krzywej przy lewym końcu → Low Shelf; analogicznie po prawej → High Shelf,
  - środek → Bell; dolny pas wyświetlacza (głębokie cięcie) → Notch,
  - po najechaniu kursorem pojawia się subtelny podgląd („ghost”) kształtu, który powstanie. Podwójne kliknięcie tworzy pasmo, a przeciągnięcie krzywej tworzy pasmo i od razu ustawia jego gain.

  Granice stref dobierz w researchu i opisz w manualu.
- **[BAND-5]** Prawy przycisk myszy na paśmie otwiera menu kontekstowe: Shape, **Slope**, Mode, Target, Character, Auto Gain, Solo, Bypass, kopiuj/wklej ustawienia, Reset, Delete. Slope pokazuj dla kształtów, które go mają; dla Bell i Notch pokazuj Q.
- **[BAND-6]** Interakcja: przeciąganie (frequency/gain), kółko myszy zmienia Q, Shift daje precyzyjną regulację, Alt/Option + klik włącza solo pasma. Zaznaczanie prostokątem i edycja wielu pasm naraz, Delete usuwa pasmo, Undo/Redo, podwójne kliknięcie na wartości pozwala wpisać liczbę.
- **[BAND-7]** Pływający panel pasma pod wyświetlaczem: wszystkie parametry pasma i odczyty z sekcji 12.

## 6. Tryby pasma: Static / Dynamic / Spectral

- **[DYN-1]** **Static** — klasyczny filtr.
- **[DYN-2]** **Dynamic** — wzmocnienie całego pasma zależy od poziomu wykrytego w paśmie (klasyczny dynamic EQ).
- **[DYN-3]** **Spectral** — w zakresie pasma (ważonym jego kształtem) przetwarzane są tylko częstotliwości, które przekraczają próg. Reszta pasma zostaje nietknięta (jak Spectral Dynamics w Pro-Q 4 i jak w soothe).
- **[DYN-4]** Parametry pasm Dynamic i Spectral:

  | Parametr | Zakres | Domyślnie |
  |---|---|---|
  | Threshold | Absolute: −80…0 dBFS; Relative: −30…+30 dB względem lokalnej obwiedni widma ([DYN-10]) | Dynamic: Absolute −20 dBFS; Spectral: Relative +6 dB |
  | Ratio | płynnie 0.1:1…100:1 oraz ∞:1; szybkie presety 0.5 / 0.8 / 0.9 / 1.5 / 2 / 3 / 4 / 8 / ∞ | 2:1 |
  | Direction | Above (działa na najgłośniejsze dźwięki) / Below (działa na najcichsze) | Above |
  | Range (maks. zmiana) | 0…30 dB | 12 dB |
  | Attack | 0,05…500 ms | 5 ms |
  | Release | 1…5000 ms (+ tryb program-dependent — P1) | 100 ms |
  | Lookahead | 0…20 ms | 0 ms |
  | Knee | 0…24 dB (soft knee) | 6 dB |
  | Q | jak w paśmie (szerokość przetwarzania i detekcji) | — |
  | Mix | 0…100 % | 100 % |
  | Selectivity (tylko Spectral) | 0…100 % (jak wąsko, „chirurgicznie” działa maska) | 50 % |
  | Detector (P1) | Peak / RMS | RMS |
  | Sidechain (P1) | wewnętrzny / zewnętrzny bus VST3 | wewnętrzny |

- **[DYN-5]** Znaczenie kombinacji Ratio × Direction (w obszarze aktywnym nachylenie charakterystyki statycznej = 1/Ratio):

  | | Ratio > 1 | Ratio < 1 (np. 0.5:1, 0.9:1) |
  |---|---|---|
  | **Above** (powyżej progu) | kompresja w dół — tłumi szczyty i rezonanse | ekspansja w górę — podbija szczyty |
  | **Below** (poniżej progu) | kompresja w górę — podnosi ciche fragmenty | ekspansja w dół — jeszcze bardziej ścisza ciche fragmenty |

  Range ogranicza zmianę w każdym wariancie. Ratio 1:1 oznacza brak działania dynamicznego.
- **[DYN-6]** Implementuj zgodnie z dobrą praktyką (Giannoulis, Massberg, Reiss 2012): układ feed-forward, detekcja i wygładzanie w dB, kwadratowy soft knee, osobne wygładzanie dla attack i release. Zachowanie program-dependent (attack, release i knee zależne od materiału i zakresu częstotliwości, jak w Pro-Q 4) jako opcja P1.
- **[DYN-7]** Mix pasma działa w domenie wzmocnienia (skaluje zmianę w dB), a nie jako równoległe miksowanie sygnałów. W trybie Minimum Phase równoległe miksowanie dawałoby efekt filtra grzebieniowego.
- **[DYN-8]** Lookahead: tor główny jest opóźniony o globalny **Max Lookahead** (0–20 ms, domyślnie 0 ms), więc latencja jest stała. Pasmo z mniejszym lookahead opóźnia własną detekcję o różnicę. Ustawienie w paśmie lookahead większego niż Max Lookahead podnosi Max Lookahead, a UI informuje o zmianie latencji.
- **[DYN-9]** Silnik spektralny: STFT z WOLA albo odwracalna transformata o stałym Q (oceń w researchu). Maska wzmocnień jest wygładzana w czasie i w częstotliwości (skala ERB), żeby uniknąć artefaktów typu „musical noise”. Długość odpowiedzi impulsowej maski jest ograniczona, żeby nie dochodziło do zawijania splotu kołowego. Rozmiar FFT i hop zależą od Quality. Oceń też tryb **Low Latency** (P1): filtr minimalnofazowy wyznaczany z maski i stosowany w dziedzinie czasu (idea „filter-bank equalizer”, Löllmann & Vary; soothe3 ma tryb niskiej latencji).
- **[DYN-10]** Threshold **Relative** (inspiracja: soothe): próg jest liczony względem wygładzonej obwiedni widma. Dzięki temu pasmo reaguje na rezonanse wystające ponad otoczenie, niezależnie od głośności materiału. Threshold **Absolute** to klasyczny próg w dBFS.
- **[DYN-11]** Bieżąca zmiana wzmocnienia każdego pasma jest rysowana na żywo (sekcja 11).

## 7. Auto Gain — globalny i per pasmo

- **[AG-1]** **Globalny Auto Gain** dopasowuje głośność wyjścia do wejścia (uczciwe porównanie A/B) szerokopasmowym wzmocnieniem wyjściowym.
- **[AG-2]** **Auto Gain pasma** (pasma Dynamic i Spectral) to automatyczny make-up: przywraca średni poziom pasma, uśredniany zgodnie z szybkością z [AG-4]. Pasmo kontroluje wtedy dynamikę bez zmiany średniej barwy. W paśmie Static opcja jest nieaktywna, bo zniwelowałaby działanie pasma — tam kompensację zapewnia globalny Auto Gain.
- **[AG-3]** Dwa tryby ważenia:
  - **Fletcher** (domyślny) — ważenie percepcyjne. Krzywe Fletchera–Munsona (1933) są historyczne, więc zaimplementuj ten tryb na współczesnych krzywych jednakowej głośności **ISO 226:2023** przy referencyjnym poziomie odsłuchu (domyślnie 80 phon; regulacja 40–100 phon — P1). Preferowany model: pobudzenie w pasmach ERB → głośność właściwa → suma (Moore–Glasberg; ISO 532-2 / ISO 532-3). Minimum: ważenie widma konturem ISO 226 i sumowanie energii.
  - **Digital** — bez krzywej słuchu: dopasowanie energii (RMS) nieważonego sygnału.
- **[AG-4]** Szybkość śledzenia (okno pomiaru i stała wygładzania korekcji):

  | Ustawienie | Okno / stała czasowa | Zachowanie |
  |---|---|---|
  | Very Fast | ~50 ms | śledzi prawie każdą zmianę, może „pompować” |
  | Fast | ~400 ms (jak Momentary w BS.1770 / EBU R 128) | szybka reakcja |
  | Medium | ~3 s (jak Short-term) | kompromis |
  | Long | ~10–15 s | powolne podążanie |
  | **Stable** (domyślnie) | integracja długoterminowa / statyczna kompensacja z krzywej EQ i średniego widma; zmienia się tylko po zmianie ustawień | bez pompowania |

- **[AG-5]** Zabezpieczenia: limit korekcji ±24 dB; zamrożenie korekcji w ciszy (bramka jak w BS.1770, −70 LUFS); płynne zmiany bez trzasków. Auto Gain nie wpływa na odsłuch Solo ani Delta.
- **[AG-6]** Test: różowy szum + Bell +6 dB przy 3 kHz → różnica głośności wejście/wyjście ≤ 0,5 dB według modelu trybu Fletcher (Stable) oraz ≤ 1 LU według BS.1770 jako kontrola krzyżowa.

## 8. Tryby fazy i jakość obliczeń

- **[PH-1]** Globalny Phase Mode:
  - **Minimum Phase** — bez latencji (poza lookahead i silnikiem spektralnym). Filtry IIR dopasowane do prototypu analogowego, bez „cramping” w pobliżu Nyquista (Vicanek 2016; Orfanidis 1997; Berners & Abel 2003). Struktury stabilne przy modulacji parametrów (SVF/TPT — Zavalishin, Simper).
  - **Natural Phase** — zgodność amplitudy *i* fazy z prototypem analogowym w całym paśmie, przy małej latencji i minimalnym pre-ringu (wzorzec funkcjonalny: Natural Phase w Pro-Q). Metodę wybierz w researchu — np. FIR wyznaczony z odpowiedzi H(jω) prototypu + splot partycjonowany albo IIR z korekcją fazy.
  - **Analog Feel** z modelami **N** (inspiracja: Neve 1073), **P** (Pultec EQP-1A) i **S** (konsole SSL serii E/G). Model obejmuje:
    - kształty krzywych i ich interakcje (np. „Pultec trick” — jednoczesne podbicie i tłumienie w basie),
    - proportional / constant Q,
    - rezonanse elementów magnetycznych,
    - charakterystykę fazową,
    - nieliniowości zależne od materiału (transformator, lampa, op-amp) z antyaliasingiem (ADAA + oversampling).

    Pasma pozostają w pełni parametryczne: model nadaje charakter, ale nie ogranicza pasm do przełączanych częstotliwości oryginału.
  - Neve, Pultec i SSL to znaki towarowe. W UI użyj neutralnych nazw (np. „Analog N / P / S” z opisem „inspired by…”), a ostateczne nazewnictwo skonsultuj z prawnikiem.
- **[PH-2]** **Quality: 4 poziomy w każdym trybie — Eco / Normal / High / Max.** Wyższy poziom daje dokładniejszy wynik kosztem CPU i/lub latencji. W researchu zdefiniuj, co dokładnie zmienia każdy poziom, np.:
  - Minimum Phase: interpolacja współczynników (per blok → per próbka), precyzja (float → double), wewnętrzny oversampling filtrów blisko Nyquista,
  - Natural Phase: długość FIR / rozmiar FFT (dokładność w basie) kosztem latencji,
  - Analog Feel: złożoność modelu (krzywa statyczna + prosta saturacja → model WDF/state-space z solverem Newtona–Raphsona), rząd ADAA, wewnętrzny oversampling,
  - silnik spektralny: rozmiar FFT i overlap.

  Zmierz CPU i latencję każdej kombinacji; wyniki umieść w manualu i w tooltipach.
- **[PH-3]** Architektura nie może blokować późniejszego dodania trybu **Linear Phase** (P2).

## 9. Character pasma: Clear / Warm / Sharp

- **[CHR-1]** Każde pasmo ma ustawienie Character:
  - **Clear** (domyślne) — liniowo, symetryczna krzywa, bez dodanych harmonicznych (tylko szum numeryczny),
  - **Warm** — łagodna, zależna od materiału saturacja z przewagą parzystych harmonicznych (H2), łagodniejsze zbocza (proportional Q), lekko zaokrąglona góra,
  - **Sharp** — saturacja z przewagą nieparzystych harmonicznych (H3), węższe, „twardsze” zbocza (constant Q), szybsza reakcja na transjenty, więcej „presence”.
- **[CHR-2]** Nieliniowość działa tylko na wkład pasma, więc pasmo nie barwi całego sygnału. Przykład dla Bell: y = x + (G−1)·BP(x) — saturowany jest tylko człon (G−1)·BP(x).
- **[CHR-3]** Wstępne cele pomiarowe (do kalibracji odsłuchowej) — sinus 0 dBFS w środku pasma, Bell +6 dB:
  - Warm ≈ 0,1–0,5 % THD z dominującą H2,
  - Sharp ≈ 0,1–0,5 % THD z dominującą H3,
  - Clear < −120 dB.
- **[CHR-4]** W trybie Analog Feel Character steruje nieliniowością modelu: Clear = sam model krzywych i fazy, Warm/Sharp = dwa warianty nasycenia.
- **[CHR-5]** Kontekst: w Pro-Q 4 tryby Character (Clean/Subtle/Warm) są globalne — tutaj działają per pasmo.

## 10. Oversampling

- **[OS-1]** Ustawienie globalne: **Off (1×, domyślnie) / 2× / 4× / 8×**.
- **[OS-2]** Filtry: w Minimum Phase polifazowe półpasmowe IIR (niska latencja, np. HIIR). W Natural Phase i Analog Feel opcjonalnie liniowofazowe półpasmowe FIR (stałe opóźnienie). Tłumienie w paśmie zaporowym, pasmo przejściowe i latencję dobierz na podstawie Kahles, Esqueda, Välimäki (2019).
- **[OS-3]** Zakres działania oversamplingu zgodnie z [ARC-2].
- **[OS-4]** Osobne ustawienie oversamplingu dla renderu offline (P1).
- **[OS-5]** Gdy aktywne są nieliniowości, a miernik aliasingu (sekcja 13) przekroczy próg, UI subtelnie sugeruje włączenie oversamplingu.

## 11. Analizator i wizualizacja

- **[VIS-1]** Na środku okna analizator w czasie rzeczywistym (jak w Pro-Q 4): widmo pre/post (sidechain — P1), logarytmiczna oś częstotliwości, regulowany zakres dB, szybkość, rozdzielczość, tilt (np. 4,5 dB/oct), freeze i peak hold.
- **[VIS-2]** **Glow aktywności:** najbardziej aktywne obszary widma świecą jaśniej. Jasność i poświata (bloom) są proporcjonalne do energii uśrednionej w czasie (np. EMA, τ ≈ 200–500 ms) względem średniego poziomu widma. Paleta czytelna także dla daltonistów (P1).
- **[VIS-3]** Bieżąca redukcja lub podbicie pasm Dynamic i Spectral jest rysowana jako świecące wypełnienie pod lub nad krzywą (estetyka zbliżona do soothe). Intensywność jest proporcjonalna do chwilowej zmiany w dB.
- **[VIS-4]** **Podpowiedzi rezonansów** — wyłącznie graficzne i subtelne (np. delikatna poświata lub znacznik nad pikiem). Nigdy nie zmieniają dźwięku i nie tworzą pasm samodzielnie. Użytkownik może je wyłączyć i ustawić ich czułość. Tooltip pokazuje częstotliwość, wystawanie ponad otoczenie [dB], czas trwania [s] i pewność wykrycia.
- **[VIS-5]** Algorytm wykrywania rezonansów (oparty na badaniach):
  - wystawanie piku ponad wygładzoną obwiednię widma (wygładzanie ułamkowo-oktawowe/ERB — Hatziantoniou & Mourjopoulos 2000; Glasberg & Moore 1990),
  - trwałość piku w kolejnych ramkach (IPMP) i jego stosunek do sąsiednich prążków (PNPR) — cechy z literatury o wykrywaniu sprzężeń akustycznych (van Waterschoot & Moonen 2011),
  - odróżnianie rezonansów od harmonicznych granej nuty: rezonans stoi w miejscu, gdy nuty się zmieniają (cecha typu PHPR),
  - ważenie czułością słuchu (ISO 226) i maskowaniem,
  - histereza, żeby znaczniki nie migotały.

  Zweryfikuj algorytm na zestawie testowym ze sztucznie dodanymi rezonansami o znanych parametrach; precision/recall umieść w raporcie.
- **[VIS-6]** Wydajność: 60 fps, rendering akcelerowany przez GPU z programowym fallbackiem, analiza poza wątkiem audio, HiDPI, skalowalne okno.
- **[VIS-7]** Układ: ciemny motyw, duży wyświetlacz pośrodku, pływający panel pasma, górny pasek (presety, A/B, Undo/Redo), dolny pasek (Phase Mode, Quality, Oversampling, Auto Gain, Output, On/Off, latencja, mierniki). Projekt graficzny oryginalny.
- **[VIS-8]** P2: widok spektrogramu i wizualizacja Delta (co zostaje usunięte).

## 12. Odczyty pasma: faza i ringing

- **[PR-1]** Dla każdego pasma (w panelu pasma i w tooltipie węzła):
  - **Δφ śr. [°]** — średnie |odchylenie fazy| w aktywnym zakresie pasma (tam, gdzie |gain| ≥ 0,5 dB), ważone odchyleniem amplitudy; dodatkowo opóźnienie grupowe w częstotliwości środkowej [ms],
  - **Pre-ring / Post-ring [ms]** — wyznaczone z odpowiedzi impulsowej pasma przy bieżących ustawieniach i trybie fazy: czas przed i po głównym piku, w którym obwiednia (transformata Hilberta) przekracza −60 dB względem piku. Post-ring pokazuje, jak bardzo pasmo „przedłuża” dźwięk; w Minimum Phase pre-ring = 0,
  - dla Dynamic/Spectral: wartości dla bieżącego wzmocnienia i dla pełnego Range (najgorszy przypadek). W Spectral uwzględnij rozmycie czasowe STFT (pre-echo rzędu długości okna).
- **[PR-2]** Obliczenia w wątku tła, odświeżanie maksymalnie 10–20 Hz, nigdy w wątku audio.
- **[PR-3]** Kontrola poprawności: dla rezonansu drugiego rzędu T60 ≈ 2,2·Q/f₀ (Q bieguna; np. Q = 10 przy 1 kHz → ≈ 22 ms). Odczyt może się różnić od pomiaru numerycznego najwyżej o ±5 %.
- **[PR-4]** Odczyty globalne: łączna latencja; nakładka krzywej fazy i opóźnienia grupowego całego EQ (P1).

## 13. Mierniki THD+N i aliasingu

- **[MTR-1]** Na żywo (odświeżanie 5–10 Hz, wygładzone): **THD+N** [dB i %] oraz **Aliasing** [dB względem sygnału]. Mierniki pokazują, ile zniekształceń i aliasingu wtyczka wytwarza *teraz*, dla bieżącego materiału i ustawień. Rozbicie na pasma w tooltipie.
- **[MTR-2]** Metoda (do potwierdzenia w researchu):
  - nieliniowości: residuum = wyjście etapu nieliniowego − jego zlinearyzowany odpowiednik liczony równolegle → moc harmonicznych i IMD,
  - modulacja wzmocnienia: zniekształcenie wynikające z szybkich zmian gain (attack/release krótsze niż kilka okresów częstotliwości pasma), estymowane z widma sygnału sterującego,
  - aliasing: energia powyżej bazowego Nyquista przed decymacją × tłumienie filtra decymującego. Przy 1× — okresowy pomiar „cieniowy”: co N-ty blok kopia sygnału jest przetwarzana z wyższym oversamplingiem, a różnica w paśmie użytecznym daje estymatę. Budżet: ≤ 5 % CPU,
  - liniowa korekcja i zamierzone działanie dynamiki nie są liczone jako zniekształcenia.
- **[MTR-3]** Walidacja: zgodność z pomiarem offline (sinus i sweep — Farina 2000; AES17) w granicach ±3 dB. Wartości poniżej −120 dB wyświetlaj jako „< −120 dB”.
- **[MTR-4]** Prezentacja: mały, nienachalny miernik w dolnym pasku; kolor ostrzegawczy po przekroczeniu konfigurowalnego progu.

## 14. Funkcje globalne

- **[GLB-1]** **On/Off** (globalny bypass) powiązany z parametrem bypass hosta (VST3 `kIsBypass`), z płynnym przejściem (crossfade) i stałą latencją.
- **[GLB-2]** Output gain, globalny Mix (dry/wet z wyrównaniem latencji), Max Lookahead ([DYN-8]), Delta (P1).
- **[GLB-3]** Presety fabryczne i użytkownika, A/B, Undo/Redo, kopiowanie ustawień pasm.
- **[GLB-4]** Wszystkie parametry można automatyzować i są wygładzane (bez zipper noise). Stan wtyczki jest wersjonowany (zgodność wstecz presetów i projektów).
- **[GLB-5]** Wskaźniki latencji i obciążenia CPU w UI.

## 15. Wymagania niefunkcjonalne

- **[NFR-1]** Budżety CPU — cele startowe; zmierz je, raportuj i uzasadnij odstępstwa. Referencja: Apple M1 i współczesny x86-64, 48 kHz, bufor 128 próbek:
  - stereo, 8 pasm Static, Minimum Phase, Normal: ≤ 1 % rdzenia,
  - stereo, 8 pasm Spectral, Normal: ≤ 5 %,
  - 7.1.4, 8 pasm Spectral, Normal: ≤ 20 %,
  - najgorszy przypadek (9.1.6, 192 kHz, 8×, Max) nie może powodować awarii; UI ostrzega o obciążeniu.
- **[NFR-2]** To samo brzmienie przy 44,1 i 96 kHz (w tolerancji testów).
- **[NFR-3]** Render offline brzmi identycznie jak odtwarzanie w czasie rzeczywistym (chyba że włączono osobny oversampling offline).
- **[NFR-4]** Jakość kodu: C++20, clang-format, ostrzeżenia traktowane jak błędy, ASan/UBSan w testach CI, brak undefined behavior.

## 16. Research — punkty startowe (zweryfikowane; rozszerz je)

### 16.1 Filtry EQ i dopasowanie do analogowych krzywych
- R. Bristow-Johnson, *Audio EQ Cookbook* (W3C Note).
- S. J. Orfanidis, „Digital Parametric Equalizer Design with Prescribed Nyquist-Frequency Gain”, JAES 45(6), 1997.
- M. Vicanek, „Matched Second Order Digital Filters”, 2016 — https://www.vicanek.de/articles/BiquadFits.pdf
- J. S. Abel, D. P. Berners, „Discrete-Time Shelf Filter Design for Analog Modeling”, AES 115th Convention, preprint 5939, 2003.
- V. Zavalishin, *The Art of VA Filter Design* (Native Instruments); A. Simper, Cytomic technical papers (SVF z całkowaniem trapezowym).
- V. Välimäki, J. D. Reiss, „All About Audio Equalization: Solutions and Frontiers”, Applied Sciences 6(5):129, 2016 (kod: https://code.soundsoftware.ac.uk/projects/allaboutaudioeq).
- Wtyczki: FabFilter Pro-Q 4 (Zero Latency / Natural Phase / Linear Phase; kształty; slope 0–96 dB/oct + Brickwall; tworzenie pasm zależne od miejsca kliknięcia z podglądem kształtu; pokrewne: Spectrum Grab, collision detection, EQ Match, EQ Sketch — do rozważenia jako P2), Three-Body Technology Kirchhoff-EQ (krzywe dopasowane do analogowych, modele vintage, dynamika).

### 16.2 Natural/Linear Phase, splot, STFT
- W. G. Gardner, „Efficient Convolution without Input-Output Delay”, JAES 43(3), 1995.
- F. Wefers, *Partitioned Convolution Algorithms for Real-Time Auralization*, rozprawa doktorska, RWTH Aachen (Logos Verlag), 2015.
- J. B. Allen, L. R. Rabiner, „A Unified Approach to Short-Time Fourier Analysis and Synthesis”, Proc. IEEE 65(11), 1977.
- R. E. Crochiere, „A Weighted Overlap-Add Method of Short-Time Fourier Analysis/Synthesis”, IEEE Trans. ASSP 28(1), 1980.
- J. O. Smith III, *Spectral Audio Signal Processing* (CCRMA, online) — STFT, WOLA, filtry minimalnofazowe wyznaczane z cepstrum.

### 16.3 Dynamika, dynamic EQ i spectral dynamics
- D. Giannoulis, M. Massberg, J. D. Reiss, „Digital Dynamic Range Compressor Design — A Tutorial and Analysis”, JAES 60(6):399–408, 2012.
- D. K. Wise, „Concept, Design, and Implementation of a General Dynamic Parametric Equalizer”, AES E-Library.
- N. Holighaus, M. Dörfler, G. A. Velasco, T. Grill, „A Framework for Invertible, Real-Time Constant-Q Transforms”, IEEE TASLP 21(4):775–785, 2013 (arXiv:1210.0084).
- H. W. Löllmann, P. Vary, „Generalized Filter-Bank Equalizer for Noise Reduction with Reduced Signal Delay”, Interspeech 2005; oraz „Uniform and Warped Low Delay Filter-Banks for Speech Enhancement”, 2007.
- Wtyczki:
  - Pro-Q 4 — tryby Dynamic i Spectral Dynamics; attack, release i knee zależne od materiału,
  - oeksound soothe2 — Depth, Sharpness, Selectivity, Soft/Hard, Attack/Release, Delta, Stereo Link/Balance, sidechain; „oversampling” = rozdzielczość częstotliwościowa, „resolution” = rozdzielczość czasowa,
  - oeksound soothe3 — tryb niskiej latencji, multichannel do 9.1.6, parametry Detail, Tilt i Max Cut; Soft prawie niezależny od poziomu, Hard bliższy kompresorowi,
  - TBProAudio DSEQ3 — przetwarzanie w dziedzinie FFT, ponad 1000 filtrów,
  - Waves Curves Equator — tłumienie rezonansów i „unmasking”,
  - Soundtheory Gullfoss — korekcja oparta na modelu percepcji słuchowej,
  - TDR Nova — kompresja i ekspansja w górę i w dół per pasmo.

### 16.4 Wykrywanie rezonansów
- T. van Waterschoot, M. Moonen, „Fifty Years of Acoustic Feedback Control: State of the Art and Future Challenges”, Proc. IEEE 99(2):288–327, 2011; oraz „Comparative Evaluation of Howling Detection Criteria in Notch-Filter-Based Howling Suppression”, JAES 58(11), 2010 — cechy detekcji: PTPR, PAPR, PNPR, PHPR, IPMP, IMSD.
- P. D. Hatziantoniou, J. N. Mourjopoulos, „Generalized Fractional-Octave Smoothing of Audio and Acoustic Responses”, JAES 48(4):259–280, 2000.
- B. R. Glasberg, B. C. J. Moore, „Derivation of Auditory Filter Shapes from Notched-Noise Data”, Hearing Research 47, 1990 (skala ERB).

### 16.5 Głośność i Auto Gain
- H. Fletcher, W. A. Munson, „Loudness, Its Definition, Measurement and Calculation”, JASA 5:82–108, 1933.
- ISO 226:2023 — Normal equal-loudness-level contours.
- ITU-R BS.1770-5 oraz EBU R 128 / EBU Tech 3341 (okna 400 ms i 3 s, bramkowanie).
- B. C. J. Moore, B. R. Glasberg, T. Baer, „A Model for the Prediction of Thresholds, Loudness, and Partial Loudness”, JAES 45(4), 1997.
- B. R. Glasberg, B. C. J. Moore, „A Model of Loudness Applicable to Time-Varying Sounds”, JAES 50(5):331–342, 2002.
- ISO 532-1:2017 (Zwicker), ISO 532-2:2017 (Moore–Glasberg), ISO 532-3:2023 (Moore–Glasberg–Schlittenlacher, dźwięki zmienne w czasie).

### 16.6 Nieliniowości, oversampling, aliasing
- J. Parker, V. Zavalishin, E. Le Bivic, „Reducing the Aliasing of Nonlinear Waveshaping Using Continuous-Time Convolution”, DAFx-16, 2016.
- S. Bilbao, F. Esqueda, J. Parker, V. Välimäki, „Antiderivative Antialiasing for Memoryless Nonlinearities”, IEEE Signal Processing Letters 24(7):1049–1053, 2017.
- F. Esqueda, *Aliasing Reduction in Nonlinear Audio Signal Processing*, rozprawa doktorska, Aalto University, 2018.
- J. Kahles, F. Esqueda, V. Välimäki, „Oversampling for Nonlinear Waveshaping: Choosing the Right Filters”, JAES, 2019.
- L. de Soras, HIIR — biblioteka polifazowych półpasmowych filtrów IIR.

### 16.7 Modele analogowe (Analog Feel)
- A. Fettweis, „Wave Digital Filters: Theory and Practice”, Proc. IEEE 74(2), 1986.
- „Modeling the Pultec EQP-1A with Wave Digital Filters”, SMC 2024 (repozytorium UPF); kod: https://github.com/ABSounds/EQP1A-WDF (JUCE, chowdsp_wdf).
- M. Holters, U. Zölzer, „Circuit Simulation with Inductors and Transformers Based on the Jiles-Atherton Model of Magnetization”, DAFx-16, 2016.
- Universal Audio WebZine (2006) o modelowaniu Neve 1073 — wpływ rezonansów własnych elementów magnetycznych na krzywe: https://uaudio.com/webzine/2006/may/text/content2.html
- SSL serii E/G: źródła wtórne podają sprzeczne informacje o proportional vs constant Q i nachyleniu półek — zweryfikuj w oryginalnej dokumentacji SSL.

### 16.8 Pomiary
- A. Farina, „Simultaneous Measurement of Impulse Response and Distortion with a Swept-Sine Technique”, AES 108th Convention, preprint 5093, 2000.
- AES17 (pomiary cyfrowego sprzętu audio); do porównań z konkurencją: Plugin Doctor (DDMF).

### 16.9 Narzędzia i dystrybucja
- JUCE 8 (AGPLv3 albo licencja komercyjna), VST3 SDK 3.8 (MIT).
- pluginval (Tracktion): poziomy strictness 1–10, minimum 5 dla kompatybilności z hostami, kod wyjścia do CI.
- Steinberg VST3 validator, Apple `notarytool`, Azure Artifact Signing (dawniej Trusted Signing).

## 17. Testy i walidacja

- **[TST-1]** Testy jednostkowe DSP (Catch2 lub GoogleTest):
  - amplituda filtrów vs prototyp analogowy: ±0,1 dB do 20 kHz przy 44,1 kHz (Minimum Phase, Quality Normal i wyżej),
  - null test: wszystkie pasma płaskie lub wyłączone, Character Clear → residuum ≤ −140 dBFS,
  - M/S i macierze surround: idealna rekonstrukcja,
  - dynamika: charakterystyka statyczna zgodna ze wzorem (±0,1 dB), poprawne stałe czasowe attack/release,
  - latencja raportowana = latencja zmierzona,
  - kryteria z [AG-6], [CHR-3], [PR-3], [MTR-3] i [VIS-5].
- **[TST-2]** CI: pluginval (strictness 10) na obu systemach, Steinberg VST3 validator, auval (dla AU).
- **[TST-3]** Macierz hostów (ręczna checklista w `docs/testing.md`): Cubase/Nuendo (w tym Atmos 7.1.4 i 9.1.6), Reaper, Ableton Live, Studio One, Bitwig, FL Studio, DaVinci Resolve; Logic Pro (AU) i Pro Tools (AAX), jeśli te formaty są włączone.
- **[TST-4]** Testy obciążeniowe: wszystkie układy kanałów, fs 44,1–384 kHz, bufory 1–8192 próbek i zmienne, szybka automatyzacja wszystkich parametrów, zmiana układu kanałów w trakcie sesji.

## 18. Instalatory, podpisy i CI

- **[REL-1]** macOS:
  - universal binary,
  - podpis Developer ID Application (hardened runtime),
  - instalator `.pkg` (pkgbuild/productbuild) podpisany Developer ID Installer,
  - notaryzacja przez `notarytool` + `stapler`,
  - instalacja do `/Library/Audio/Plug-Ins/VST3/` (AU: `/Library/Audio/Plug-Ins/Components/`),
  - wybór komponentów i deinstalator.
- **[REL-2]** Windows:
  - bundle VST3 w `C:\Program Files\Common Files\VST3\`,
  - instalator Inno Setup (lub WiX) z deinstalatorem,
  - podpis Authenticode (np. Azure Artifact Signing — dawniej Trusted Signing — albo certyfikat OV). Reputacja SmartScreen buduje się z czasem — opisz to w dokumentacji wydania.
- **[REL-3]** GitHub Actions (macOS + Windows): build, testy, pluginval, pakowanie, podpis i notaryzacja oraz upload artefaktów. Sekrety trzymaj w GitHub Secrets; bez sekretów pipeline buduje niepodpisane artefakty. Wersjonowanie SemVer + CHANGELOG.
- **[REL-4]** Dokumentacja: README (instrukcja budowania), manual użytkownika (PL; EN — P1), licencje komponentów zewnętrznych (JUCE, VST3 SDK, biblioteka FFT itd.).

## 19. Kamienie milowe

| Etap | Zakres |
|---|---|
| M0 | Research (sekcja 16) + `docs/architecture.md` (tor sygnału, wątki) + ADR. Krótkie podsumowanie; w trybie interaktywnym poczekaj na akceptację |
| M1 | Szkielet: CMake + JUCE + VST3, CI na obu systemach, parametry, stan, bypass, okno z analizatorem |
| M2 | Pasma Static: kształty, slope, Minimum Phase, tworzenie pasm kliknięciem, menu pod prawym przyciskiem myszy, cele L/R/M/S |
| M3 | Pasma Dynamic i Spectral, Auto Gain globalny i per pasmo |
| M4 | Natural Phase, poziomy Quality, oversampling, Character |
| M5 | Surround/Atmos: układy kanałów, panel głośników, linkowanie detekcji |
| M6 | Analog Feel N/P/S |
| M7 | Wizualizacje: glow, podpowiedzi rezonansów, odczyty fazy i ringingu, mierniki THD+N/aliasingu |
| M8 | Utwardzanie: wydajność, pluginval, macierz hostów, instalatory, podpisy, dokumentacja, wydanie 1.0 |

## 20. Definition of Done (v1.0)

- Wszystkie wymagania P0 są zrealizowane i powiązane z testami lub dowodami (`docs/traceability.md`: ID wymagania → test/pomiar).
- CI jest zielone na macOS i Windows; pluginval (strictness 10) przechodzi bez błędów.
- Podpisane instalatory (na macOS także notaryzowane) są dostępne jako artefakty CI.
- Raporty pomiarowe w `docs/measurements/`: CPU, latencja, THD+N/aliasing, dokładność filtrów, skuteczność wykrywania rezonansów.
- Gotowy manual użytkownika i lista znanych ograniczeń.

## 21. Format raportowania

- Na starcie: lista założeń i pytań blokujących (jeśli są).
- Po każdym kamieniu milowym: co zrobiono → wyniki testów i pomiarów → ryzyka i znane problemy → następny krok.
- Nie deklaruj ukończenia bez dowodu (wynik testu, pomiar, log CI).
