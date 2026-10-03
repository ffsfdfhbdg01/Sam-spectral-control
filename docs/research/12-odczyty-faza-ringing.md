# §12 Odczyty pasma: faza i ringing (PR-1…PR-4)

## a) Literatura i normy

- J. O. Smith III, *Introduction to Digital Filters with Audio Applications*, W3K, 2007 — opóźnienie fazowe i grupowe (τ_g = −dφ/dω), filtry minimalnofazowe.
- A. V. Oppenheim, R. W. Schafer, *Discrete-Time Signal Processing*, 3. wyd., 2010.
- S. L. Marple Jr., „Computing the Discrete-Time Analytic Signal via FFT”, IEEE Trans. Signal Process. 47(9), 1999 — obwiednia przez sygnał analityczny (Hilbert).
- J. Blauert, P. Laws, „Group Delay Distortions in Electroacoustical Systems”, JASA 63(5), 1978 — progi słyszalności zniekształceń opóźnienia grupowego.
- S. P. Lipshitz, M. Pocock, J. Vanderkooy, „On the Audibility of Midrange Phase Distortion in Audio Systems”, JAES 30(9), 1982.
- E. Zwicker, H. Fastl, *Psychoacoustics: Facts and Models*, 2007 — maskowanie wsteczne (krótkie) i w przód (dłuższe): pre-echo jest słyszalne łatwiej niż post-ring tej samej długości.
- Rezonator 2. rzędu: obwiednia ~ e^(−t/τ), τ = Q/(π·f₀); spadek o 60 dB: T60 = τ·ln(1000) ≈ 2,2·Q/f₀ (Q bieguna). Dla peaking EQ (RBJ) przy wzmocnieniu A: Q bieguna = A·Q dla boostu, więc ringing rośnie z boostem.

## b) Wtyczki i narzędzia referencyjne

- **Plugin Doctor (DDMF)** — offline pomiar amplitudy, fazy, opóźnienia grupowego i odpowiedzi impulsowej wtyczek.
- Nie znaleźliśmy EQ, który na bieżąco podaje liczbowo pre/post-ring per pasmo — to funkcja wyróżniająca; tym ważniejsza jest jej poprawność.

## c) Wybrane podejście

- **Wątek analizy pasm** (`BandAnalysisWorker`): przeliczenie po zmianie parametrów pasma albo po zmianie dynamicznego gainu o > 0,5 dB; odświeżanie UI ≤ 20 Hz; zero pracy w wątku audio.
- **Odpowiedź częstotliwościowa** H_b(f) na siatce logarytmicznej (512 punktów, 10 Hz–Nyquist) dla bieżącego trybu fazy (z korektorem Natural Phase).
- **Δφ śr.**: faza nadmiarowa φ_ex(f) = unwrap(∠H_b(f)) + 2πf·D/fs (usunięcie czystego opóźnienia D); obszar aktywny: |20·log10|H_b|| ≥ 0,5 dB; wagi w(f) = |20·log10|H_b(f)|| na siatce log-f; Δφ = Σ w·|φ_ex| / Σ w [°].
- **Opóźnienie grupowe w fc**: analitycznie dla sekcji SVF/biquad (suma po sekcjach) albo numerycznie z rozwiniętej fazy.
- **Odpowiedź impulsowa**: liczona tym samym kodem DSP co ścieżka audio (deterministyczna instancja pasma w wątku tła), długość do momentu spadku poniżej −80 dB (limit 5 s — np. 20 Hz przy Q 40 daje T60 ≈ 4,4 s).
- **Pre/Post-ring**: obwiednia |h_a[n]| (Marple 1999); pik główny; pre-ring = czas od pierwszego przekroczenia −60 dB względem piku do piku; post-ring = czas od piku do ostatniego przekroczenia −60 dB. W Minimum Phase pre-ring = 0 z definicji (układ przyczynowy, pik w n = 0).
- **Dynamic/Spectral**: dwie wartości — dla bieżącego wzmocnienia (telemetria) i dla pełnego Range (najgorszy przypadek).
- **Spectral**: efektywna odpowiedź impulsowa z maski pasma w ramie STFT (IFFT maski × okna analizy/syntezy) → pre-echo rzędu ½ długości okna; raportowane osobno („STFT pre-echo”).
- **Ostrzeżenia** (kolor żółty): post-ring > 50 ms albo odchylenie opóźnienia grupowego powyżej progów Blauerta–Lawsa w danym zakresie częstotliwości; pre-ring > 5 ms (pre-echo jest łatwiej słyszalne).
- **Globalnie (PR-4)**: łączna latencja (próbki i ms) w dolnym pasku; P1: nakładka krzywej fazy/opóźnienia grupowego całego EQ.

### Odrzucone alternatywy
- **Odpowiedź impulsowa z wzorów zamiast z kodu DSP** — ryzyko rozbieżności z tym, co faktycznie słychać; odrzucone.
- **Kryterium −40 dB** — zaniża długość ringingu przy głośnym odsłuchu; −60 dB jest zgodne z definicją T60.

## d) Kryteria akceptacji

| ID | Kryterium | Metoda |
|---|---|---|
| PR-3 | Rezonator 2. rzędu o znanym Q: post-ring (−60 dB) = 2,2·Q/f₀ ±5 % | test jednostkowy |
| PR-1 | Minimum Phase: brak energii powyżej −60 dB przed pikiem (pre-ring = 0) | test jednostkowy |
| PR-1 | Natural Phase: pre-ring ≤ D | test jednostkowy |
| PR-1 | Opóźnienie grupowe w fc vs wzór analityczny ±2 % | test jednostkowy |
| PR-2 | Odczyt aktualny ≤ 100 ms po zmianie parametru; zero alokacji i obliczeń odczytów w wątku audio | test + przegląd kodu |
