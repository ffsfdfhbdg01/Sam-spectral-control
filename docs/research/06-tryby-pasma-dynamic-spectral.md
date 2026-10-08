# §6 Tryby pasma: Static / Dynamic / Spectral (DYN-1…DYN-11)

## a) Literatura i normy

**Dynamika i dynamic EQ**
- D. Giannoulis, M. Massberg, J. D. Reiss, „Digital Dynamic Range Compressor Design — A Tutorial and Analysis”, JAES 60(6):399–408, 2012. Rekomendacje: układ feed-forward, detekcja w domenie logarytmicznej, soft knee o zmiennej szerokości, wygładzanie „smooth branching” (osobne stałe attack/release), linkowanie kanałów.
- D. K. Wise, „Concept, Design, and Implementation of a General Dynamic Parametric Equalizer”, AES E-Library — dynamika wbudowana w strukturę EQ parametrycznego.
- G. W. McNally, „Dynamic Range Control of Digital Audio Signals”, JAES 32(5), 1984 — klasyczne podstawy cyfrowej kontroli dynamiki.
- J. D. Reiss, A. McPherson, *Audio Effects: Theory, Implementation and Application*, CRC Press, 2014 — kompresja i ekspansja w górę i w dół.
- U. Zölzer (red.), *DAFX: Digital Audio Effects*, 2. wyd., Wiley, 2011 — dynamika, filtry, przetwarzanie spektralne.

**Przetwarzanie spektralne**
- J. B. Allen, L. R. Rabiner, „A Unified Approach to Short-Time Fourier Analysis and Synthesis”, Proc. IEEE 65(11), 1977.
- R. E. Crochiere, „A Weighted Overlap-Add Method of Short-Time Fourier Analysis/Synthesis”, IEEE Trans. ASSP 28(1), 1980.
- D. Griffin, J. Lim, „Signal Estimation from Modified Short-Time Fourier Transform”, IEEE Trans. ASSP 32(2), 1984 — modyfikacja STFT daje transformatę „niespójną”; WOLA z oknem syntezy jest estymatą najmniejszych kwadratów.
- J. O. Smith III, *Spectral Audio Signal Processing* (CCRMA, online) — mnożenie widma przez maskę = splot kołowy z jej odpowiedzią impulsową; zero-padding i ograniczenie długości odpowiedzi zapobiegają zawijaniu w czasie.
- O. Cappé, „Elimination of the Musical Noise Phenomenon with the Ephraim and Malah Noise Suppressor”, IEEE Trans. Speech Audio Process. 2(2):345–349, 1994 — czasowe wygładzanie wzmocnień eliminuje „musical noise”.
- N. Holighaus, M. Dörfler, G. A. Velasco, T. Grill, „A Framework for Invertible, Real-Time Constant-Q Transforms”, IEEE TASLP 21(4):775–785, 2013 — odwracalna transformata o stałym Q (sliCQ).
- H. W. Löllmann, P. Vary, „Generalized Filter-Bank Equalizer for Noise Reduction with Reduced Signal Delay”, Interspeech 2005 — wzmocnienia wyznaczane w dziedzinie częstotliwości, filtracja w dziedzinie czasu; mniejsze opóźnienie niż bank analiza-synteza.
- A. V. Oppenheim, R. W. Schafer, *Discrete-Time Signal Processing*, 3. wyd., Pearson, 2010 — filtr minimalnofazowy z amplitudy (cepstrum).
- P. D. Hatziantoniou, J. N. Mourjopoulos, „Generalized Fractional-Octave Smoothing of Audio and Acoustic Responses”, JAES 48(4):259–280, 2000 — wygładzanie obwiedni widma.

**Własność intelektualna** — w wynikach wyszukiwania pojawiają się patenty na kompresję wielopasmową w dziedzinie częstotliwości (m.in. US 8,903,109 B2, US 9,673,770 B2). Przed wydaniem komercyjnym potrzebna jest analiza freedom-to-operate (zadanie poza zakresem inżynierskim).

## b) Wtyczki referencyjne

- **FabFilter Pro-Q 4** — tryby Static, Dynamic i **Spectral Dynamics**: w trybie Spectral nie zmienia się wzmocnienie całego pasma; przetwarzane są tylko częstotliwości w paśmie, które przekraczają próg. Attack, release i knee są zależne od materiału i zakresu częstotliwości pasma.
- **oeksound soothe2** — Depth, Sharpness, Selectivity, Soft/Hard, Attack/Release, Delta, Stereo Link/Balance, sidechain. „Oversampling” = rozdzielczość częstotliwościowa (ważna dla basu), „Resolution” = rozdzielczość czasowa (częstsze odświeżanie filtra redukcji).
- **oeksound soothe3** — Detail, Tilt, Max Cut; tryb Soft prawie niezależny od poziomu (dobry na rezonanse), Hard bliższy kompresorowi; tryb niskiej latencji; multichannel do 9.1.6.
- **TDR Nova** — kompresja i ekspansja w górę i w dół per pasmo.
- **TBProAudio DSEQ3** — przetwarzanie w dziedzinie FFT, ponad 1000 filtrów.
- **Kirchhoff-EQ** — rozbudowana dynamika per pasmo.

## c) Wybrane podejście

### Dostępność trybów
Dynamic i Spectral są dostępne dla kształtów z parametrem gain (Bell, Low/High Shelf, Tilt Shelf, Flat Tilt) — zob. §5.

### Komputer wzmocnienia (wspólny dla Dynamic i Spectral)
Wejście: poziom detekcji x [dB]; parametry: próg T, ratio R, szerokość kolana W, kierunek, Range.
Niech c = 1/R − 1 (dla R = ∞: c = −1).

**Above** (działa powyżej progu):
- 2(x − T) < −W: g = 0
- |2(x − T)| ≤ W: g = c · (x − T + W/2)² / (2W)
- 2(x − T) > W: g = c · (x − T)

**Below** (działa poniżej progu; lustrzane odbicie z ciągłą pochodną):
- 2(x − T) > W: g = 0
- |2(x − T)| ≤ W: g = −c · (T − x + W/2)² / (2W)
- 2(x − T) < −W: g = c · (x − T)

Wynik: g ograniczone do [−Range, +Range]. Znak g wynika automatycznie z tabeli w DYN-5 (np. Above + R > 1 → g ≤ 0; Below + R > 1 → g ≥ 0). R = 1 → c = 0 → brak działania.

### Wygładzanie, Mix, lookahead, linkowanie
- **Wygładzanie** w dB po komputerze wzmocnienia (decoupled smooth branching): jeśli |g[n]| > |y[n−1]| → stała attack, w przeciwnym razie release; α = exp(−1 / (τ · fs)). Attack/Release to stałe czasowe τ (63 %), nie czasy 10–90 %.
- **Detektor**: RMS (wygładzanie średniokwadratowe o stałej ≈ 2 ms) albo Peak (P1).
- **Mix pasma**: g_eff = Mix · g — w domenie wzmocnienia, więc bez interferencji fazowej z równoległym sygnałem suchym (DYN-7).
- **Lookahead**: tor główny opóźniony o globalne `Max Lookahead` D; detekcja pasma z lookahead L jest opóźniona o D − L. Ustawienie L > D podnosi D (zmiana latencji zgłaszana hostowi, z informacją w UI).
- **Linkowanie**: zob. §4 (maksimum poziomów lane w zbiorze).
- **Bezpieczne stałe czasowe** (P1, domyślnie włączone): minimalny attack ≥ 1 okres fc pasma, żeby szybka modulacja nie generowała zniekształceń (zgodne z kierunkiem „program-dependent” z Pro-Q 4). Użytkownik może to wyłączyć.

### Tryb Dynamic (dziedzina czasu)
- **Detekcja**: sygnał lane filtrowany filtrem detekcji odpowiadającym obszarowi pasma (Bell → filtr pasmowy o fc i Q pasma; Shelf → obszar półki).
- **Aplikacja**: pasmo realizowane jako SVF-TPT. Częstotliwość i Q są w trybie Dynamic stałe, więc dla każdej lane zmienia się tylko gain. Dla każdego pasma budujemy (poza ścieżką próbkową, przy zmianie fc/Q) **tabelę parametrów SVF w funkcji gainu** (krok 0,25 dB, zakres ±(|Gain| + Range)), wyznaczoną z projektu dopasowanego (ADR-0004). Per próbka: interpolacja liniowa parametrów (g, k, m0, m1, m2) z tabeli — stabilna (SVF-TPT jest stabilny dla g > 0, k > 0) i tania, a odpowiedź statyczna jest identyczna jak w trybie Static.

### Tryb Spectral (silnik STFT; ADR-0006)
- **Ramka**: N_fft = 2048 / 4096 / 8192 / 8192 przy 48 kHz dla Eco / Normal / High / Max (skalowane proporcjonalnie do fs); hop = N/4, a dla Max N/8. Okna: √Hann analizy i syntezy (WOLA), z normalizacją do idealnej rekonstrukcji.
- **Wspólna maska**: wszystkie pasma Spectral jednej lane tworzą jedną maskę G[k] (suma w dB), więc koszt FFT nie rośnie z liczbą pasm.
- **Poziom detekcji per bin** = energia w otoczeniu ±½ ERB wokół binu, znormalizowana energią okna — interpretowana jako poziom pasma o szerokości 1 ERB w dBFS. Dzięki temu próg Absolute ma sens fizyczny niezależnie od N_fft.
- **Próg Relative** (DYN-10): obwiednia E[k] = wygładzone widmo z wyłączeniem piku (mediana albo średnia w oknie ⅓–1 oktawy); x_rel = L[k] − E[k]. Działanie prawie niezależne od poziomu sygnału (jak Soft w soothe3).
- **Ważenie kształtem pasma**: w_b[k] ∈ [0, 1] = znormalizowany kształt pasma (|H_b(f)| w dB podzielone przez gain pasma); g_b[k] = w_b[k] · g(x[k]). Przetwarzanie ogranicza się do obszaru pasma (DYN-3).
- **Selectivity**: szerokość wygładzania w częstotliwości (skala ERB) poziomu detekcji i maski — wysoka = wąskie, „chirurgiczne” działanie.
- **Wygładzanie czasowe**: per bin attack/release w dB z α liczonym od czasu hopu (Cappé 1994 — przeciw „musical noise”).
- **Ograniczenie zawijania**: maska jest wygładzana w częstotliwości (krótsza odpowiedź impulsowa); w Quality Max dodatkowo jawne okienkowanie odpowiedzi impulsowej maski (IFFT → okno → FFT).
- **Latencja**: ≈ N_fft próbek (dokładna wartość z implementacji, zgłaszana hostowi). **Pre-echo** do ~N/2 przed transjentem — raportowane w odczytach §12.
- **Lookahead** w Spectral jest kwantowany do wielokrotności hopu.
- **Low Latency (P1)**: z maski każdej ramki wyznaczamy filtr minimalnofazowy (cepstrum, długość 256–1024) i stosujemy go w dziedzinie czasu (splot partycjonowany z zerowym opóźnieniem, Gardner 1995), z crossfade’em między ramkami — idea „filter-bank equalizer” (Löllmann & Vary).

### Odrzucone alternatywy
- **Dynamic przez przeliczanie projektu dopasowanego per próbka** — za drogie (exp/cos per lane per próbka); tabela gainów daje ten sam wynik taniej.
- **Równoległa struktura z modulowanym BP (y = x + (G−1)·BP(x))** — asymetria boost/cut (inna szerokość cięcia); pozostaje jako opcja dla Character (§9), nie dla trybu Clear.
- **Bank filtrów IIR (np. 1/6 oktawy) zamiast STFT** — gorsza selektywność, trudna idealna rekonstrukcja; odrzucone.
- **sliCQ (stały Q) jako domyślny silnik** — lepsza rozdzielczość w basie, ale większa złożoność i latencja; do oceny w benchmarku M3 jako wariant High/Max.

### Realizacja w M3 i wyniki pomiarów
Kod: `ssc/dynamics/Dynamics.h` (komputer wzmocnienia, wygładzanie, detektor), `ssc/engine/GainTable`, `ssc/engine/BandProcessor` (tryb Dynamic), `ssc/spectral/SpectralEngine` (tryb Spectral).

**Dynamic**
- **Sygnał detekcji = wejście wtyczki** (nieopóźnione, z bufora historii) przepuszczone przez filtr obszaru pasma (ten sam co odsłuch Solo). Dzięki temu lookahead jest możliwy, a zachowanie pasma nie zależy od kolejności pasm. Tor główny jest opóźniony o Max Lookahead; pasmo z lookahead L czyta detekcję z opóźnieniem Max − L.
- **Detektor**: średni kwadrat z dwoma kaskadowymi biegunami (każdy o połowie stałej czasowej, łącznie max (2 ms, 2 okresy fc)). Pojedynczy biegun dawał tętnienie ±0,17 dB na 2·f, które przez asymetrię attack/release przesuwało charakterystykę statyczną o ~0,11 dB; kaskada zmniejsza tętnienie ok. 13×.
- **Komputer wzmocnienia, attack/release i Mix** dokładnie wg wzorów z §c; liczone co ~1/12000 s (4 próbki przy 48 kHz) z liniową rampą per próbka.
- **Modulacja filtra**: tablica parametrów SVF w funkcji wzmocnienia (krok 0,25 dB, ±60 dB) projektowana leniwie projektem dopasowanym i interpolowana per próbka — odpowiedź przy każdym wzmocnieniu równa projektowi pasma Static (różnica w fc ≤ 0,01 dB, test „Static gain of a Dynamic band…”). Zmiana fc/Q/kształtu unieważnia tablicę w O(1).
- **Próg Relative w trybie Dynamic**: poziom pasma minus poziom kontekstu. Dla kształtów dzwonowych kontekst to filtr pasmowy 4× szerszy (z normalizacją +6 dB: płaskie widmo → 0 dB, wystający rezonans → wartości dodatnie); dla półek i tiltu kontekstem jest cały sygnał lane. Wynik nie zależy od poziomu materiału (DYN-10).
- **Linkowanie**: poziom zbioru = maksimum poziomów lane (Independent / Pair / Group / All; grupy głośników jak w §4), osobne wygładzanie per lane z tym samym wejściem — identyczne wzmocnienie w zbiorze.

**Spectral**
- STFT √Hann/WOLA, N = 2048/4096/8192/8192 przy 48 kHz (skalowane z fs), hop N/4 (Max N/8); latencja = N próbek i tylko wtedy, gdy istnieje pasmo w trybie Spectral (ADR-0011). Kanały bez lane spektralnych omijają STFT dokładną linią opóźniającą o tej samej długości (bit-exact, CH-6).
- Detekcja i maska na **siatce ERB (8 punktów/ERB)** zamiast na binach: poziom w paśmie o szerokości zależnej od Selectivity (2 ERB … ⅛ ERB, min. listek główny okna), kalibrowany do dBFS sinusa; próg Relative względem średniej z ±2 ERB. Siatka daje naturalne wygładzenie maski w częstotliwości i stały, niewielki stan per pasmo (≈ 330 punktów), niezależny od N i fs.
- Ważenie kształtem pasma (1 w środku/plateau), wygładzanie attack/release per punkt w tempie hopu, suma pasm w dB w jedną maskę na kanał i na parę (Mid/Side), interpolacja do binów.

**Zmierzone (testy Catch2)**
| Kryterium | Wynik |
|---|---|
| DYN-4/5 charakterystyka statyczna (R ∈ {0,5; 2; 4; ∞} × Above/Below × knee {0, 6}) | ±0,1 dB (sinus w fc, −50…−10 dBFS) |
| DYN-4 |g| ≤ Range | 20 000 losowych przypadków |
| DYN-6 stałe czasowe | 63 % po τ ±2 % (attack i release) |
| DYN-7 Mix 50 % | połowa zmiany ±0,05 dB |
| DYN-8 lookahead 5 ms | redukcja przed transjentem; latencja = Max Lookahead |
| CH-5 linkowanie | różnica wzmocnień w zbiorze ≤ 1e-6 dB |
| DYN-9 maska = 1 | residuum −139 dB |
| DYN-3 multiton | ton 1 kHz −7,5 dB, tony 1,1 kHz i 3 kHz < 0,001 dB |
| DYN-10 Relative | identyczne wzmocnienie przy −30 i −10 dBFS (Dynamic i Spectral) |

**Odstępstwa i dalsze kroki**: lookahead w trybie Spectral nie jest jeszcze kwantowany do hopu (ramka STFT i tak „widzi” transjent ~N/2 wcześniej); okienkowanie odpowiedzi impulsowej maski w Quality Max i tryb Low Latency (P1) — M4; zachowanie program-dependent (P1) — później.

**Stan w wersji 1.0 (M8)**: Lookahead pasma Spectral nie jest używany — pokrętło jest w trybie Spectral wyłączone, a jego wartość nie podnosi Max Lookahead (dodałaby latencję bez efektu); ramka STFT wyprzedza transjent o ~N/2. Jawnego okienkowania odpowiedzi impulsowej maski nie dodano: Quality Max różni się od High gęstszym hopem (N/8), a wygładzenie maski na siatce ERB skraca jej odpowiedź impulsową. Low Latency i program-dependent (P1) — poza 1.0. Selectivity działa tylko w trybie Spectral (w Dynamic pokrętło jest wyłączone).

## d) Kryteria akceptacji

| ID | Kryterium | Metoda |
|---|---|---|
| DYN-4/5 | Charakterystyka statyczna (sinus w fc, poziomy −80…0 dBFS) zgodna ze wzorami ±0,1 dB dla Above/Below × R ∈ {0,5; 0,9; 2; 4; ∞} × knee ∈ {0, 6, 24} dB | test jednostkowy |
| DYN-4 | |g| ≤ Range + 0,01 dB w każdych warunkach | test jednostkowy (fuzz) |
| DYN-6 | Skok poziomu o 20 dB: zmiana gainu osiąga 63 % wartości końcowej po τ_attack ±10 % (analogicznie release) | test jednostkowy |
| DYN-7 | Mix 50 % → zmiana gainu w stanie ustalonym = 50 % zmiany przy 100 % (±0,05 dB) | test jednostkowy |
| DYN-8 | Lookahead 5 ms: redukcja zaczyna się ≥ 5 ms przed transjentem w sygnale wyjściowym; latencja zgłoszona = Max Lookahead | test jednostkowy |
| DYN-3 | Multiton (1 kHz, 1,1 kHz, 3 kHz) z progiem pomiędzy poziomami tonów: tłumiony jest tylko najgłośniejszy ton, pozostałe ±0,2 dB | test jednostkowy |
| DYN-9 | Ścieżka STFT z maską = 1: residuum vs opóźnione wejście ≤ −120 dB | null test |
| DYN-9 | Wąskie cięcie −24 dB na sweepie: energia pre-echo dalej niż N/2 przed transjentem ≤ −60 dB | test jednostkowy |
| DYN-10 | Ten sam materiał na −30 i −10 dBFS (Relative) → wzór wzmocnień identyczny ±0,5 dB | test jednostkowy |
| DYN-2/3 | CPU w budżecie NFR-1 (8 pasm Spectral stereo ≤ 5 %, 7.1.4 ≤ 20 %) | benchmark |
