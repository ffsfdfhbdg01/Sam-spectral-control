# §7 Auto Gain — globalny i per pasmo (AG-1…AG-6)

## a) Literatura i normy

- H. Fletcher, W. A. Munson, „Loudness, Its Definition, Measurement and Calculation”, JASA 5:82–108, 1933 — pierwsze krzywe jednakowej głośności.
- D. W. Robinson, R. S. Dadson, „A Re-determination of the Equal-Loudness Relations for Pure Tones”, Br. J. Appl. Phys. 7:166–181, 1956.
- **ISO 226:2003** i **ISO 226:2023** — normalne krzywe jednakowej głośności (tony czyste, pole swobodne, słuchacze 18–25 lat). Wersja 2023 różni się od 2003 najwyżej o 0,6 dB (Y. Suzuki, H. Takeshima, K. Kurakata, „Revision of ISO 226 …”, Acoustical Science and Technology 45(1):1–8, 2024). Formuła 2003: A_f = 4,47·10⁻³·(10^(0,025·L_N) − 1,15) + (0,4·10^((T_f + L_U)/10 − 9))^α_f; L_p = (10/α_f)·log10(A_f) − L_U + 94 dB, z tabelami α_f, L_U, T_f dla 29 częstotliwości 20 Hz–12,5 kHz.
- S. S. Stevens, „On the Psychophysical Law”, Psychological Review 64(3), 1957 — głośność w sonach ~ intensywność^0,3 (podwojenie co 10 fonów).
- B. C. J. Moore, B. R. Glasberg, T. Baer, „A Model for the Prediction of Thresholds, Loudness, and Partial Loudness”, JAES 45(4), 1997; B. R. Glasberg, B. C. J. Moore, „A Model of Loudness Applicable to Time-Varying Sounds”, JAES 50(5):331–342, 2002.
- **ISO 532-1:2017** (Zwicker), **ISO 532-2:2017** (Moore–Glasberg), **ISO 532-3:2023** (Moore–Glasberg–Schlittenlacher, dźwięki zmienne w czasie).
- **ITU-R BS.1770-5**, **EBU R 128**, **EBU Tech 3341** — ważenie K, okna Momentary 400 ms i Short-term 3 s, bramkowanie bezwzględne −70 LUFS i względne −10 LU, wagi kanałów surround.

## b) Wtyczki referencyjne

- **FabFilter Pro-Q 4** — Auto Gain kompensuje zmianę głośności wynikającą z krzywej EQ; zachowanie statyczne, niezależne od chwilowego sygnału `[niezweryfikowane]`.
- **iZotope Ozone** — „Gain Match” do porównań z obejściem `[niezweryfikowane]`.
- Wniosek: żadna z popularnych wtyczek nie oferuje wyboru ważenia i szybkości śledzenia — tu jest przestrzeń na wyróżnik.

## c) Wybrane podejście

### Pomiar i korekcja (globalny Auto Gain)
- Schemat feed-forward: estymator głośności liczy L_in (wejście) i L_out (wyjście toru **przed** Auto Gain); korekcja c = L_in − L_out [dB], wygładzana zgodnie z wybraną szybkością i stosowana jako płynna rampa wzmocnienia. Pomiar nie widzi własnej korekcji, więc nie ma pętli sprzężenia.
- Mierzone są tylko kanały objęte co najmniej jednym pasmem (§4, CH-6); sumowanie kanałów z wagami BS.1770 (LFE pominięty).

### Tryb **Fletcher** (domyślny) — ważenie percepcyjne
1. Widmo mocy (FFT 4096 przy 48 kHz, hop 1024, okno Hann) wejścia i wyjścia.
2. Grupowanie w pasma 1 ERB (ok. 40 pasm od 20 Hz do 20 kHz).
3. Kalibracja poziomu: SPL_b = L_b[dBFS] + C. Domyślne C odpowiada konwencji K-20 (−20 dBFS RMS różowego szumu ≈ 83 dB SPL); P1: parametr „Listening level” (40–100 fonów, domyślnie 80).
4. Poziom głośności każdego pasma [fony] z odwrotnej formuły ISO 226 (interpolacja między 29 tabelowanymi częstotliwościami; powyżej 12,5 kHz wartości brzegowe).
5. Fony → sony (N = 2^((P − 40)/10) dla P ≥ 40; poniżej: N = (P/40)^2,642), suma sonów po pasmach.
6. Różnica głośności wejście/wyjście przeliczona z sonów na dB (10 fonów na podwojenie) → korekcja c.

Uzasadnienie: krzywe Fletchera–Munsona są historyczne; ISO 226 jest ich współczesnym następcą. Model sumy sonów w pasmach ERB to uproszczony model Moore’a–Glasberga (bez rozpraszania pobudzenia). Jako wariant High/Max (P1) — pełniejszy model wg ISO 532-2/532-3.

**Stałe ISO 226**: implementujemy formułę i tabele z wersji 2003 (szeroko publikowane), a tabela jest wymienialna na dane z 2023 po zakupie/licencji normy. Różnica ≤ 0,6 dB jest pomijalna dla kompensacji głośności (ADR-0007).

### Tryb **Digital**
- Energia (RMS) sygnału nieważonego, sumowana po kanałach; brak krzywej słuchu.

### Szybkość śledzenia (AG-4)
| Ustawienie | Realizacja |
|---|---|
| Very Fast | wykładnicza, τ = 50 ms; ogranicznik szybkości zmian 0,5 dB/ms przeciw zipper noise |
| Fast | okno przesuwne 400 ms (jak Momentary) |
| Medium | okno przesuwne 3 s (jak Short-term) |
| Long | okno przesuwne 12 s |
| **Stable** (domyślnie) | statyczna kompensacja: aktualna krzywa EQ (z długoterminowym średnim wzmocnieniem pasm dynamicznych) nałożona na **długoterminowe średnie widmo** wejścia (LTAS, bramkowane jak BS.1770) i przeliczona modelem głośności. Zmienia się tylko po zmianie ustawień albo bardzo powoli (τ ≈ 30 s) wraz z LTAS — bez pompowania. |

### Auto Gain pasma (AG-2)
- Tylko pasma Dynamic i Spectral: automatyczny make-up m_b = −ḡ_b, gdzie ḡ_b to **średnia ważona energią** dynamicznego wzmocnienia pasma (Σ E(t)·g(t) / Σ E(t)), uśredniana zgodnie z szybkością z AG-4. Ważenie energią sprawia, że cisza nie zaniża średniej.
- Fletcher vs Digital: w trybie Spectral średnia po binach jest ważona czułością słuchu (ISO 226) albo równomiernie.
- Ograniczenia: |m_b| ≤ Range pasma; zamrożenie w ciszy (poziom pasma < −70 dBFS).
- W paśmie Static opcja jest wyszarzona — kompensacja pasma statycznego zniwelowałaby jego działanie.

### Zabezpieczenia (AG-5)
- |c| ≤ 24 dB; zamrożenie korekcji przy bramce (−70 LUFS); płynna rampa; Auto Gain wyłączony w odsłuchu Solo i Delta.

### Odrzucone alternatywy
- **K-weighting (BS.1770) jako tryb Fletcher** — to ważenie nie zależy od poziomu i nie odpowiada krzywym jednakowej głośności; zostaje jako kontrola krzyżowa w testach.
- **Pełny ISO 532-3 w czasie rzeczywistym jako domyślny** — za drogi obliczeniowo przy wielu kanałach; wariant P1.
- **Kompensacja tylko z krzywej EQ (bez widma sygnału)** — ignoruje treść materiału (np. boost w paśmie, w którym nic nie gra); w trybie Stable łączymy krzywą z LTAS.

### Realizacja w M3 i wyniki pomiarów
Kod: `ssc/loudness/Loudness` (ISO 226:2003, pasma ERB, sony), `ssc/loudness/AutoGain` (globalny), make-up pasm w `BandProcessor` i `SpectralEngine`.

- **Model**: pasma 1 ERB z FFT (Hann, 4096 przy 48 kHz; 1024 dla Very Fast, żeby okno analizy nie dominowało nad τ = 50 ms), kalibracja K-20 (−20 dBFS RMS różowego szumu ≈ 83 dB SPL), fony z odwrotnego wzoru ISO 226:2003 (wartości konturu 40 fon zgodne z tabelą normy, np. 100 Hz: 64,37 dB SPL), sony, suma.
- **Prędkości Very Fast…Long**: różnica głośności wejście/wyjście każdej ramki jest wygładzana **w dB** stałą czasową prędkości — odpowiedź skokowa jest wykładnicza, a 63 % wartości końcowej przypada na τ (+ opóźnienie analizy: pół ramki i hop; ok. 17 ms dla Very Fast, 75 ms dla pozostałych). Uśrednianie mocy dawało 63 % już po ~0,76 τ.
- **Stable**: długoterminowe widmo wejścia (τ = 30 s, bramkowane) każdego kanału jest liczone stale, także zanim pasmo zacznie działać; korekcja = głośność tego widma minus głośność tego samego widma przeprowadzonego przez aktualną krzywą EQ kanału (projekty pasm w środkach pasm ERB, przeliczane tylko dla zmienionych pasm). Zmiana ustawień daje nową korekcję od razu; muzyka przy stałym EQ nie powoduje pompowania.
- **Kanały**: mierzone są wszystkie kanały poza LFE (wagi BS.1770: przestrzenne +1,5 dB), porównywane i korygowane tylko kanały objęte pasmami (AG-1, CH-6).
- **Zabezpieczenia**: limit ±24 dB, bramka −70 dBFS zamraża pomiar i korekcję, zmiana korekcji ≤ 0,5 dB/ms z krótkim wygładzaniem 5 ms; Auto Gain działa przed odsłuchem Solo, więc go nie zmienia.
- **Make-up pasm (AG-2)**: średnia zmiany wzmocnienia ważona energią detekcji (Dynamic: energia filtra obszaru; Spectral: energia punktów siatki w obszarze pasma), uśredniana stałą czasową globalnej prędkości Auto Gain, zamrażana w ciszy, ograniczona do Range; w trybie Spectral dodawana z wagą kształtu pasma.

**Zmierzone (testy Catch2)**
| Kryterium | Wynik |
|---|---|
| AG-6 Fletcher (Stable, Medium) | model −0,06 / −0,08 dB; BS.1770 −0,19 / −0,21 LU |
| AG-3 Digital | RMS −0,03 / −0,02 dB |
| AG-4 stałe czasowe (63 %) | Very Fast 0,067 s, Fast 0,45 s, Medium 3,04 s, Long 11,99 s |
| AG-4 Stable | stabilna ≤ 1 s po zmianie EQ (±0,1 dB) |
| AG-5 60 s ciszy | dryf ≤ 0,1 dB, brak skoków > 0,5 dB/ms |
| AG-1 | kanał bez pasm bit-exact |
| AG-2 make-up | ton w paśmie: −12 dB bez make-up → 0,001 dB (Dynamic) / −0,06 dB (Spectral) |

**Odstępstwo od kryterium AG-4**: tolerancja ±20 % τ obejmuje dodatkowo opóźnienie analizy (pół ramki + hop), nieuniknione przy pomiarze w paśmie ERB.

## d) Kryteria akceptacji

| ID | Kryterium | Metoda |
|---|---|---|
| AG-6 | Różowy szum + Bell +6 dB przy 3 kHz (Q 1): Fletcher/Stable — |ΔL| ≤ 0,5 dB wg modelu; ≤ 1 LU wg BS.1770 | test jednostkowy |
| AG-3 | Digital, ten sam test: różnica RMS wejście/wyjście ≤ 0,2 dB | test jednostkowy |
| AG-4 | Skokowa zmiana półki +6 dB: korekcja osiąga 63 % wartości po τ ±20 % (Very Fast/Fast/Medium/Long); Stable stabilizuje się ≤ 1 s po zmianie ustawień i nie dryfuje > 0,1 dB w 60 s muzyki przy stałym EQ | test jednostkowy |
| AG-5 | 60 s ciszy: dryf korekcji ≤ 0,1 dB; brak skoków > 0,5 dB/ms | test jednostkowy |
| AG-2 | Pasmo Dynamic (Range 12 dB) na materiale z rezonansem: po oknie Medium średnia ważona energią wzmocnienia pasma = 0 ±0,5 dB | test jednostkowy |
| AG-1 | Auto Gain nie zmienia kanałów nieobjętych pasmami (CH-6) | null test |
