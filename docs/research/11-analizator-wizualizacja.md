# §11 Analizator, wizualizacja i podpowiedzi rezonansów (VIS-1…VIS-8)

## a) Literatura i normy

**Analiza widma**
- F. J. Harris, „On the Use of Windows for Harmonic Analysis with the Discrete Fourier Transform”, Proc. IEEE 66(1), 1978.
- P. Welch, „The Use of Fast Fourier Transform for the Estimation of Power Spectra”, IEEE Trans. Audio Electroacoust. 15(2), 1967 — uśrednianie periodogramów.
- P. D. Hatziantoniou, J. N. Mourjopoulos, JAES 48(4), 2000 — wygładzanie ułamkowo-oktawowe (wyświetlanie i obwiednia widma).

**Wykrywanie rezonansów**
- T. van Waterschoot, M. Moonen, „Fifty Years of Acoustic Feedback Control: State of the Art and Future Challenges”, Proc. IEEE 99(2):288–327, 2011; oraz „Comparative Evaluation of Howling Detection Criteria in Notch-Filter-Based Howling Suppression”, JAES 58(11), 2010 — cechy: PTPR, PAPR, **PNPR** (pik vs sąsiedztwo), **PHPR** (pik vs harmoniczne), **IPMP** (trwałość piku między ramkami), IMSD.
- B. R. Glasberg, B. C. J. Moore, „Derivation of Auditory Filter Shapes from Notched-Noise Data”, Hearing Research 47, 1990 — skala ERB.
- A. de Cheveigné, H. Kawahara, „YIN, a Fundamental Frequency Estimator for Speech and Music”, JASA 111(4), 2002 — estymacja f0 do odróżniania harmonicznych nut od rezonansów.
- M. R. Schroeder, B. S. Atal, J. L. Hall, „Optimizing Digital Speech Coders by Exploiting Masking Properties of the Human Ear”, JASA 66(6), 1979 — funkcja rozpraszania (próg maskowania).
- E. Zwicker, H. Fastl, *Psychoacoustics: Facts and Models*, 3. wyd., Springer, 2007 — ostrość (sharpness), szorstkość, maskowanie.
- ISO 226 — ważenie czułością słuchu.

**Wizualizacja**
- P. Kovesi, „Good Colour Maps: How to Design Them”, arXiv:1509.03700, 2015 — percepcyjnie równomierne palety, także dla daltonistów.

## b) Wtyczki referencyjne

- **FabFilter Pro-Q 4** — analizator pre/post/zewnętrzny; regulacja zakresu, szybkości, rozdzielczości i tiltu; freeze; collision detection; Spectrum Grab; EQ Match. Domyślny tilt 4,5 dB/oct `[niezweryfikowane]`.
- **oeksound soothe2** — Delta z odwróconym spektrogramem pokazującym tłumione rezonanse.
- **Waves Curves Equator** — „uczy się” materiału i proponuje krzywą tłumienia rezonansów.

## c) Wybrane podejście

### Analizator (VIS-1)
- Odczepy (taps) pre i post; dla wielu kanałów — suma mocy wybranej grupy (domyślnie wszystkie kanały przetwarzane).
- Audio → wątek analizy przez **SPSC ring buffer** (prealokowany, ≥ 2¹⁷ próbek na odczep, bez blokad; przy przepełnieniu odrzucamy najstarsze dane).
- FFT: Low 2048 / Medium 4096 / High 8192 / Max 16384 przy 48 kHz (skalowane z fs), okno Blackman-Harris, overlap 75 %.
- Wygładzanie w czasie (EMA, τ 50–2000 ms — parametr „Speed”), wygładzanie ułamkowo-oktawowe do wyświetlania (1/6–1/24 oct), tilt (domyślnie 4,5 dB/oct — do weryfikacji w testach odsłuchowo-wizualnych), zakres 30–120 dB, freeze, peak hold z opadaniem.

### Glow aktywności (VIS-2)
- Aktywność A[k] = EMA_τ(|X[k]|²) / średnia_k(EMA_τ), τ ≈ 300 ms (zakres 200–500 ms) — względna gęstość energii; dodatkowo składnik dodatniego spectral flux (świeże onsety).
- Mapowanie: jasność = f(log A) z obcięciem, paleta percepcyjnie równomierna (bursztyn → biel na ciemnym tle; wariant dla daltonistów — P1).
- Poświata (bloom): rendering do bufora `juce::Image` w połowie rozdzielczości, rozmycie separowalne (Gauss) w osi częstotliwości i pionowy zanik, nakładane na wypełnienie pod krzywą widma. Bufory i kernel są cache’owane.

### Wizualizacja pasm dynamicznych (VIS-3)
- Dla Dynamic: bieżący gain pasma (telemetria z wątku audio przez atomics) → krzywa pasma w czasie rzeczywistym.
- Dla Spectral: maska G[k] (dB) zdecymowana do rozdzielczości ekranu, wygładzona; świecące wypełnienie między 0 dB a krzywą maski, intensywność ∝ |G|.

### Podpowiedzi rezonansów (VIS-4, VIS-5)
1. Źródło: widmo **post** (pokazuje rezonanse, które zostały po obróbce); przełącznik pre/post (P1).
2. Widmo wysokiej rozdzielczości (8192 przy 48 kHz, ≈ 5,9 Hz/bin), moc uśredniana 50–100 ms.
3. Obwiednia E[k] w dB: mediana w oknie ⅓ oktawy (odporna na piki).
4. Wystawanie R[k] = P_dB[k] − E[k]; kandydaci: maksima lokalne z R ≥ R_min (6–12 dB zależnie od czułości) i szerokością −3 dB < ½ ERB.
5. Śledzenie pików między ramkami (dopasowanie w ±1/24 oktawy) → czas trwania, średnie wystawanie (IPMP), stabilność częstotliwości.
6. Odróżnianie harmonicznych nut: f0 z YIN na sygnale (wątek analizy, ramki 2048) + sito harmoniczne; piki zgodne z harmonicznymi bieżącego f0, które przesuwają się razem z f0, dostają karę (cecha typu PHPR). Rezonans stoi w miejscu przy zmianach nut.
7. Ważenie percepcyjne: czułość wg ISO 226 (np. 2–5 kHz ważniejsze) i słyszalność ponad progiem maskowania (funkcja rozpraszania Schroedera).
8. Wynik S = w₁·R + w₂·trwałość + w₃·stabilność − w₄·harmoniczność + w₅·czułość; **histereza**: pokaż, gdy S > θ_on przez ≥ 300 ms; ukryj, gdy S < θ_off przez ≥ 1 s.
9. Prezentacja: delikatna, półprzezroczysta (25–40 %) poświata nad pikiem w kolorze odróżnialnym od glow aktywności; tooltip: f, R [dB], czas [s], pewność. **Tylko grafika** — podpowiedzi nie wpływają na dźwięk ani nie tworzą pasm.
- **Ewaluacja (M7)**: zestaw syntetyczny — nagrania o otwartych licencjach (np. CC0) z dodanymi rezonansami (peaking +6…+15 dB, Q 5–30, losowe f) jako ground truth; negatywy: czysty materiał tonalny (np. fortepian solo). Metryki: precision/recall przy tolerancji ±1/12 oktawy, liczba fałszywych alarmów na minutę.

### Rendering i wydajność (VIS-6, ADR-0012)
- Natywne renderery JUCE 9 (Direct2D na Windows, CoreGraphics na macOS); synchronizacja z odświeżaniem przez `juce::VBlankAttachment`; ścieżka OpenGL tylko, jeśli profilowanie wykaże potrzebę.
- Analiza 30–60 Hz w wątku tła; UI tylko rysuje gotowe dane (double/triple buffering).

### Wygląd (VIS-7)
- Ciemny motyw, tokeny kolorów w jednym miejscu, 24 barwy pasm, czcionka z licencją OFL; duży wyświetlacz pośrodku, pływający panel pasma, górny i dolny pasek wg PROMPT.md.

### Odrzucone alternatywy
- **Wykrywanie rezonansów tylko progiem wystawania** — dużo fałszywych alarmów na harmonicznych instrumentów; dlatego trwałość, stabilność i kara za harmoniczność.
- **Automatyczne tworzenie pasm z podpowiedzi** — sprzeczne z wymaganiem „wyłącznie graficznie”; ewentualnie P2 jako akcja wywoływana przez użytkownika.

## d) Kryteria akceptacji

| ID | Kryterium | Metoda |
|---|---|---|
| VIS-1 | Sinus 1 kHz −20 dBFS wyświetlany na −20 dB ±0,5 dB (tilt 0); różowy szum płaski ±1 dB przy tilt 3 dB/oct | test jednostkowy analizy |
| VIS-5 | Rezonanse ≥ +9 dB, Q ≥ 8: recall ≥ 0,8, precision ≥ 0,8; materiał czysty: ≤ 1 fałszywa podpowiedź/min | ewaluacja na zestawie testowym |
| VIS-4 | Migotanie: ≤ 1 zmiana widoczności podpowiedzi na 2 s | test jednostkowy |
| VIS-4 | Podpowiedzi włączone/wyłączone → wyjście audio identyczne | null test |
| VIS-6 | 60 fps (p95 czasu klatki ≤ 16,7 ms) przy oknie 1920×1080 na maszynach referencyjnych; wątek UI ≤ 10 % rdzenia | profilowanie |
| VIS-6 | Brak blokad w wątku audio przy buforze 32 próbek i aktywnym analizatorze | test obciążeniowy |

## e) Realizacja w M7 i wyniki

### Analizator (VIS-1)
- Menu „Analyzer” w nagłówku: Pre + Post / Post / Pre / Off; rozdzielczość Low–Max (2048–16 384 punktów przy 48 kHz, skalowana z fs); szybkość 50–2000 ms; tilt 0–6 dB/okt.; zakres 60/90/120 dB; freeze; peak hold (2 s, potem −6 dB/s). Ustawienia w dziecku „Analyzer” stanu wtyczki — nie są parametrami hosta i nie trafiają do Undo. Analizatory, których nie widać, nie liczą.
- Wątek analizy przetwarza najwyżej jedną pojemność FIFO na przebieg i sprawdza żądanie zatrzymania: producent szybszy niż czas rzeczywisty (render offline, walidatory) nie blokuje zatrzymania wątku (regresja wykryta w CI: pluginval na Linuksie; test `[ARC-4]`).

### Glow aktywności (VIS-2) i pasma dynamiczne (VIS-3)
- `analysis::SpectrumActivity` na siatce wyświetlania: poziom uśredniony (300 ms) względem średniej słyszalnych punktów plus dodatni flux (onsety, 150 ms), wygaszanie poniżej −90 dB; liczone w wątku analizy z pojedynczych ramek.
- **Zmiana względem §c**: zamiast bitmapy z rozmyciem (26 ms na klatkę przy 1920×1080 w rendererze programowym) — pięć warstw wypełnionych gradientem pod linią widma (głębokość 70 px) i pasmo halo ±4 px; bursztyn → ciepła biel; ograniczone do zakresu analizatora. Koszt nie rośnie z polem okna (≈ 3 ms przy 1080p).
- Pasma Dynamic i Spectral: obszar zmiany wzmocnienia świeci tym jaśniej, im większa zmiana w danej częstotliwości; bieżąca krzywa ma halo rosnące z największą zmianą — tylko tam, gdzie pasmo zmienia wzmocnienie.

### Podpowiedzi rezonansów (VIS-4, VIS-5)
- Detektor (`analysis::ResonanceDetector`, `ResonanceHints`, wątek analizy, widmo post): 8192 punkty (skalowane z fs), uśrednianie 75 ms, co 1024 próbki; YIN (`PitchTracker`). Stabilne prążki (kształt okna: ≥ 12 dB spadku w ±4 binach) są pomijane, zostaje szerokopasmowa część programu. Podłoga: minimum na bin z 2,5 s (minimum statistics); obwiednia: mediana z ⅓ oktawy. Kandydaci: wystawanie ≥ czułość (12/9/6 dB), szerokość −3 dB od min(55 Hz; 0,3 ERB) do 0,8 ERB, najwyżej 18 dB poniżej średniego poziomu swojej ⅓ oktawy (inaczej zamaskowane). Śledzenie ±1/24 okt.; wynik = wystawanie + trwałość + stabilność − harmoniczność (tylko przy wyraźnym f0) + czułość ucha (ISO 226); histereza: pokaż po 300 ms powyżej 1,8, ukryj po 1 s poniżej 0,9.
- Prezentacja: fioletowa poświata 25–40 % (z pewnością) i znacznik nad pikiem; tooltip: częstotliwość, wystawanie, czas, pewność. Menu: włącz/wyłącz, czułość Low/Medium/High.
- **Ewaluacja — zmiana względem §c**: zamiast nagrań CC0 syntetyczny materiał „muzyczny” ze znanymi rezonansami (`tools/ssc_resonance_eval`): 2 × 48 klipów po 12 s, kryterium ≥ +9 dB i Q ≥ 8.

| Zakres | Recall | Precision | Fałszywe podpowiedzi (materiał czysty) |
|---|---|---|---|
| > 400 Hz (materiał tonalny / pełny miks) | 0,82 / 0,83 | 0,78 / 0,89 | 0,6 / min |
| < 400 Hz | < 0,1 | — | — |

**Odstępstwo VIS-5**: powyżej 400 Hz kryteria spełnione poza precision 0,78 na materiale tonalnym (cel 0,8); poniżej 400 Hz wąskie rezonanse wyglądają przy tej rozdzielczości jak prążki nut (próbowano 16 384/32 768 punktów i uśrednianych podłóg) — recall < 0,1. Do ewaluacji na nagraniach w M8.
- VIS-4: test migotania (rezonans oscylujący wokół progu: bez migotania), null test (podpowiedzi wł./wył. → wyjście identyczne co do bitu).

### Rendering i wydajność (VIS-6)
- Tło i siatka buforowane w rozdzielczości fizycznej (format docelowy ARGB, kopiowane bez skalowania); analiza wyłącznie w wątkach tła, UI rysuje gotowe dane z triple bufferów.
- Profil (`ssc_screenshot … profile=300`: czas rysowania całego edytora przy grającym audio, analizator Pre + Post, glow, peak hold, podpowiedzi, mierniki), renderer programowy JUCE w kontenerze CI (Xeon 2,1 GHz, współdzielony), mediana z trzech przebiegów:

| Okno | Mediana | p95 |
|---|---|---|
| 1100×660 (domyślne) | 6,2 ms | 9,2 ms |
| 1920×1080 | 12,2 ms | 17,1 ms |
| 1920×1080, bez glow | 9,3 ms | 14,3 ms |

- Przy 1100×660 60 fps ma zapas; przy 1920×1080 w rendererze programowym p95 przekracza 16,7 ms o ≈ 3 % (przed uproszczeniem geometrii poświaty w M7e: 18,6 ms). Kryterium „wątek UI ≤ 10 % rdzenia” nie jest spełnione w rendererze programowym (≈ 37 % rdzenia przy domyślnym oknie i 60 fps); renderery natywne JUCE z akceleracją GPU (Direct2D na Windows, CoreGraphics na macOS) trzeba zmierzyć na maszynach referencyjnych (M8). Rozważana optymalizacja: adaptacyjna liczba klatek (30 fps, gdy rysowanie przekracza budżet).

### Mierniki (§13)
- Dolny pasek: THD+N (dB i %) i aliasing (dB), kolor ostrzegawczy powyżej progów, rozbicie na pasma w tooltipie; podpowiedź oversamplingu (OS-5) na etykiecie „Oversampling”.

### Niezrealizowane w M7
- VIS-8 (P2): spektrogram i widok Delta.
- Paleta dla daltonistów (P1) — tokeny kolorów są w jednym miejscu (`Theme.h`), wariant w M8.
