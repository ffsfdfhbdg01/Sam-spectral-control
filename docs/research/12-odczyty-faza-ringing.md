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

## e) Realizacja w M7 i wyniki

- **Pomiar** (`analysis::BandReadouts`): pasmo samo przechodzi przez `Engine` z trybem fazy, Quality, oversamplingiem i modelem Analog Feel wtyczki (ten sam kod DSP co audio): cicha rozbiegówka (wygładzanie i korektor się ustalają), potem mały impuls (nieliniowe Character pozostają liniowe), śledzony do zaniku −90 dB (≤ 5 s).
- **Opóźnienie grupowe** w fc: τ = Re{DFT(n·h) / DFT(h)} minus latencja silnika. **Faza nadmiarowa**: faza z usuniętą latencją, rozwijana od częstotliwości, gdzie pasmo działa najmniej; średnia ważona |gain| tam, gdzie |gain| ≥ 0,5 dB.
- **Ringing** przy −60 dB piku z obwiedni (maksima |h| interpolowane w dB). **Zmiana względem §c**: pre-ring liczony do czasu zerowego układu (latencji silnika — tam pojawiłby się nieprzetworzony impuls), nie do piku: przyczynowe odpowiedzi narastające do późniejszego piku (pasmo przepustowe 4. rzędu, stroma Low Pass) dawały wcześniej niezerowy pre-ring, choć przed impulsem nic nie ma. Post-ring od piku.
- Dynamic: przy bieżącym wzmocnieniu i przy pełnym Range; Spectral: maska STFT zerofazowa, pre-echo = połowa ramki.
- **Wątek** (`ReadoutService`, tylko przy otwartym edytorze, priorytet normalny): odczytuje ustawienia od razu po zmianie parametru z wątku komunikatów (edytor; słuchacz `AudioProcessorValueTreeState`, wołany po zapisie nowej wartości), a poza tym co 50 ms (automatyka hosta z wątku audio — tam budzenie wątku nie jest bezpieczne w czasie rzeczywistym — i ruch wzmocnienia pasm Dynamic ≥ 0,5 dB); przelicza zmienione pasma, publikuje przez triple buffer. Odczyt po zmianie w edytorze: ok. 1–2 ms (PR-2). Pomiar sprawdza żądanie zatrzymania między blokami (`setAbortCheck`): odpowiedź do 5 s przez pełny silnik (Natural Phase Max, 8×) nie opóźnia zatrzymania wątku; przerwany pomiar nie jest publikowany.
- **UI**: linia odczytów w panelu pasma (Δφ śr., opóźnienie grupowe, pre/post-ring, wartości dla pełnego Range), wyróżniona przy post-ring > 50 ms lub pre-ring > 5 ms; łączna latencja w nagłówku w ms i próbkach (PR-4).

| Kryterium | Wynik |
|---|---|
| PR-3: rezonator, post-ring = 2,2·Q/f₀ ±5 % | 22,06 / 55,00 / 14,68 ms wobec 22,00 / 55,00 / 14,67 ms (≤ 0,3 %) |
| PR-1: Minimum Phase bez pre-ringu | Bell, cięcia, pasmo przepustowe 4. rzędu, stroma High Cut: 0 |
| PR-1: Natural Phase pre-ring ≤ D | spełnione dla Normal i Max, latencja = D |
| PR-1: opóźnienie grupowe w fc vs projekt ±2 % | ≤ 0,6 % |
| PR-2: odczyt ≤ 100 ms po zmianie | mediana 52 ms |

**Niezrealizowane w M7**: ostrzeżenia na podstawie progów słyszalności opóźnienia grupowego (Blauert–Laws) — wyróżniane są tylko długie pre/post-ringi; nakładka fazy i opóźnienia grupowego całego EQ (PR-4, P1).
