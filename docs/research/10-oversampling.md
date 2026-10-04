# §10 Oversampling (OS-1…OS-5)

## a) Literatura i normy

- J. Kahles, F. Esqueda, V. Välimäki, „Oversampling for Nonlinear Waveshaping: Choosing the Right Filters”, JAES, 2019 — porównanie filtrów FIR/IIR do oversamplingu nieliniowości: tłumienie zaporowe, pasmo przejściowe, koszt, latencja.
- F. Esqueda, *Aliasing Reduction in Nonlinear Audio Signal Processing*, rozprawa doktorska, Aalto University, 2018.
- J. Parker, V. Zavalishin, E. Le Bivic, DAFx-16, 2016; S. Bilbao i in., IEEE SPL 24(7), 2017 — ADAA pozwala zmniejszyć wymagany współczynnik oversamplingu.
- R. A. Valenzuela, A. G. Constantinides, „Digital Signal Processing Schemes for Efficient Interpolation and Decimation”, IEE Proc. G 130(6), 1983 — półpasmowe filtry polifazowe z sekcjami all-pass (podstawa HIIR).
- L. de Soras, HIIR — biblioteka polifazowych półpasmowych filtrów IIR.

## b) Wtyczki i biblioteki referencyjne

- **juce::dsp::Oversampling** — kaskady 2× z filtrami `filterHalfBandFIREquiripple` (liniowa faza) albo `filterHalfBandPolyphaseIIR` (niska latencja); raportuje latencję.
- **soothe2** — uwaga terminologiczna: tam „oversampling” oznacza rozdzielczość częstotliwościową analizy spektralnej, a nie oversampling czasowy. W UI Sam Spectral Control „Oversampling” zawsze oznacza oversampling czasowy, a rozdzielczość STFT należy do Quality.

## c) Wybrane podejście

- **Ustawienie globalne**: Off (1×, domyślnie) / 2× / 4× / 8× — kaskada stopni 2×.
- **Zakres** (ARC-2): łańcuch pasm czasowych (filtry, dynamika, nieliniowości Character/Analog). Silnik STFT pracuje na bazowym fs.
- **Typ filtrów** zależny od trybu fazy:
  - Minimum Phase → polifazowy półpasmowy IIR (niska latencja; niewielkie zniekształcenia fazy blisko Nyquista),
  - Natural Phase / Analog Feel → półpasmowy FIR equiripple (liniowa faza, stała latencja),
  - P1: ręczny wybór typu filtra w ustawieniach zaawansowanych.
- **Specyfikacja filtrów** (cel startowy, weryfikacja wg Kahles i in. 2019):
  - Normal: pasmo przepustowe do 0,43·fs (≈ 19 kHz przy 44,1 kHz), zafalowania ≤ 0,05 dB, tłumienie ≥ 90 dB,
  - Max: pasmo do 0,45·fs (≈ 20 kHz), zafalowania ≤ 0,01 dB, tłumienie ≥ 120 dB.
- **Stała latencja**: gdy OS > 1×, oversampling działa zawsze (także gdy chwilowo nie ma aktywnej nieliniowości), żeby włączanie Character nie zmieniało latencji.
- **Implementacja**: na start `juce::dsp::Oversampling`; jeśli benchmark M4 pokaże przewagę HIIR (CPU) przy tej samej specyfikacji — podmiana za wspólnym interfejsem `IOversampler`.
- **Render offline (OS-4, P1)**: osobny współczynnik dla `isNonRealtime()`. Żeby nie zmieniać latencji, wszystkie warianty są dopełniane opóźnieniem do wspólnej, maksymalnej latencji (wymaga włączenia opcji przed startem renderu).
- **Podpowiedź UI (OS-5)**: gdy aktywne są nieliniowości, a miernik aliasingu (§13) przekracza −80 dB, w dolnym pasku pojawia się subtelna sugestia zwiększenia OS.

### Odrzucone alternatywy
- **Oversampling całej wtyczki (z STFT)** — mnoży koszt FFT bez korzyści; odrzucone.
- **Automatyczne włączanie OS tylko przy aktywnych nieliniowościach** — zmienia latencję w trakcie pracy; odrzucone.

### Realizacja w M4 i wyniki pomiarów

- **Zmiana względem ADR-0008**: zamiast `juce::dsp::Oversampling` własna implementacja w rdzeniu DSP (`ssc/oversampling/Oversampler`), bo rdzeń jest niezależny od JUCE (ARC-5), a specyfikacja jest ta sama. Kaskada stopni 2×:
  - **PolyphaseIir** (Minimum Phase): eliptyczny półpasmowy filtr z dwóch gałęzi sekcji all-pass 1. rzędu (projekt Valenzueli–Constantinidesa w postaci z HIIR); 6–10 współczynników w pierwszym stopniu, latencja 3–6 próbek.
  - **LinearPhaseFir** (Natural Phase, Analog Feel): półpasmowy FIR z oknem Kaisera w postaci polifazowej (długość 4M − 1, środek ½); docelowe tłumienie jest podnoszone, aż zmierzone pasmo zaporowe spełni specyfikację (wzory Kaisera są dla krótkich filtrów półpasmowych o ~1–8 dB za optymistyczne).
  - Każdy następny stopień chroni tylko pasmo przepustowe częstotliwości bazowej, więc ma szersze pasmo przejściowe i jest krótki.
- **Specyfikacja per Quality** (pasmo przepustowe / tłumienie): Eco 0,420·fs / 80 dB, Normal 0,432·fs / 90 dB (19 kHz przy 44,1 kHz), High 0,444·fs / 105 dB, Max 0,454·fs / 120 dB (20 kHz przy 44,1 kHz).
- **Latencja zawsze całkowita**: ułamkowe opóźnienie filtrów jest dopełniane opóźnieniem przy najwyższej częstotliwości. FIR (48 kHz, Normal): 43 / 49 / 51 próbek dla 2× / 4× / 8×; IIR: 4 / 4 / 5.
- **Limit częstotliwości wewnętrznej 400 kHz**: 8× do 48 kHz, 4× przy 88,2/96 kHz, 2× przy 176,4/192 kHz.
- **Zakres** (ARC-2): wejście jest nadpróbkowywane przed opóźnieniem lookahead — historia do detekcji pasm Dynamic i ścieżka główna pracują w częstotliwości OS, pasma (filtry, dynamika, Character) też; korektor Natural Phase i silnik STFT pracują w fs.
- **Kanały nieobjęte pasmami** omijają oversampling dokładnie (opóźnienie lookahead + OS), z crossfadem 5 ms przy zmianie pokrycia; filtry OS działają na wszystkich kanałach, więc przejście nie ma stanu przejściowego.
- **Czasu rzeczywistego**: wszystkie kombinacje typ × współczynnik × Quality są przygotowane w `prepare()` (projekty filtrów liczone raz na proces), zmiana trybu w `process()` nie alokuje; wewnętrzny blok ≤ 512 próbek ogranicza pamięć buforów OS.

**Zmierzone (testy `[oversampling]`, `[OS-*]`)**

| Kryterium | Wynik |
|---|---|
| OS-2 zafalowania i tłumienie | wszystkie typy × Quality × 2/4/8×: interpolator, decymator i przejście w obie strony w specyfikacji (np. Normal ≤ 0,05 dB do 0,432·fs, obrazy/aliasy ≤ −90 dB) |
| OS-3 latencja | FIR: przejście symetryczne wokół zgłaszanej latencji (różnica ≤ 1e-12); IIR: opóźnienie grupowe przy DC = latencja ±0,5/F próbki; przez silnik i wtyczkę (zgłaszana hostowi) tak samo |
| OS-3 / CH-6 | kanał bez pasm bit-exact przy 4×, Minimum i Natural Phase |
| OS-1 aliasing (Warm, +12 dB, 10 kHz) | 1× −66,5 dB, 2× −163 dB, 4× −164 dB, 8× −163 dB (cele 2×/4×/8× spełnione; 1× — patrz §9) |
| NFR-3 | wynik niezależny od rozmiaru bloku hosta (OS + Natural Phase + Dynamic z lookahead + brickwall) |

**CPU** (`ssc_benchmark 3 modes`; stereo, 8 pasm Static, Normal):

| Oversampling | Minimum Phase (IIR) | Natural Phase (FIR) |
|---|---|---|
| 1× | 0,66 % · 0 smp | 0,94 % · 32 smp |
| 2× | 1,60 % · 4 smp | 1,80 % · 75 smp |
| 4× | 2,99 % · 4 smp | 4,53 % · 81 smp |
| 8× | 6,22 % · 5 smp | 7,47 % · 83 smp |

**Nie zrealizowane w M4**: OS-4 (osobny współczynnik dla renderu offline, P1) — M8; OS-5 (podpowiedź w UI na podstawie miernika aliasingu) — razem z miernikami w M7.

**Realizacja OS-5 w M7**: gdy działa nieliniowość pasma, uśredniony aliasing (§13e) przekracza −80 dB, a współczynnik jest mniejszy od maksymalnego dla częstotliwości próbkowania, etykieta „Oversampling ▴” w dolnym pasku przybiera kolor akcentu, a tooltip podaje aliasing i proponowany współczynnik (dwukrotnie większy od bieżącego). Ustawienie nigdy nie zmienia się samo. Logika `plugin::suggestedOversampling` jest testowana osobno (`[OS-5]`), a test wtyczki sprawdza, że podpowiedź pojawia się przy Warm +12 dB / 10 kHz / 1× (aliasing −66,5 dB) i znika po przełączeniu na 2× (−124 dB). `Oversampler::decimationGain` (wzmocnienie decymacji dla dowolnej częstotliwości, z kaskady filtrów) służy miernikom do oceny wycieku przez filtr decymujący.

## d) Kryteria akceptacji

| ID | Kryterium | Metoda |
|---|---|---|
| OS-1 | Aliasing (Warm, +12 dB, ton 10 kHz, 0 dBFS, 48 kHz): 1× ≤ −80 dB (ADAA), 2× ≤ −95 dB, 4× ≤ −110 dB, 8× ≤ −120 dB (cele do potwierdzenia w M4) | test pomiarowy |
| OS-2 | Zafalowania pasma przepustowego ≤ 0,05 dB do 19 kHz przy 44,1 kHz (Normal); ≤ 0,01 dB do 20 kHz (Max) | test jednostkowy |
| OS-3 | Latencja zgłaszana = zmierzona dla każdego współczynnika i typu filtra | test jednostkowy |
| OS-3 | Kanały nieobjęte pasmami omijają OS (CH-6) | null test |
| OS-5 | Sugestia pojawia się tylko przy aktywnej nieliniowości i aliasingu > −80 dB | test UI |
| — | Tabela CPU dla 1×/2×/4×/8× w `docs/measurements/` | benchmark |
