# §13 Mierniki THD+N i aliasingu (MTR-1…MTR-4)

## a) Literatura i normy

- **AES17** — metody pomiaru cyfrowego sprzętu audio (definicje THD+N, filtr notch, pasmo pomiarowe).
- **IEC 60268-3** — pomiary wzmacniaczy, w tym zniekształceń.
- A. Farina, „Simultaneous Measurement of Impulse Response and Distortion with a Swept-Sine Technique”, AES 108th Conv., preprint 5093, 2000 — wykładniczy sweep separuje odpowiedzi harmoniczne.
- A. Novák, L. Simon, F. Kadlec, P. Lotton, „Nonlinear System Identification Using Exponential Swept-Sine Signal”, IEEE Trans. Instrum. Meas. 59(8), 2010 — synchronizowany sweep (dokładniejsza faza harmonicznych).
- F. Esqueda, *Aliasing Reduction in Nonlinear Audio Signal Processing*, Aalto, 2018; J. Kahles i in., JAES 2019 — pomiary aliasingu (składowe nieharmoniczne w odpowiedzi na sinus/sweep).
- C.-T. Tan, B. C. J. Moore, N. Zacharov, JAES 51(11), 2003; E. Geddes, L. Lee, AES 115th, 2003 — percepcyjna waga zniekształceń (P2: wskaźnik percepcyjny obok THD+N).
- Modulacja amplitudy: mnożenie sygnału przez zmienne wzmocnienie g(t) = splot widm → wstęgi boczne f ± f_mod (podstawy teorii sygnałów).

## b) Wtyczki i narzędzia referencyjne

- **Plugin Doctor (DDMF)** — offline: harmoniczne, aliasing (sweep), intermodulacja.
- Nie znaleźliśmy wtyczki EQ raportującej na żywo THD+N i aliasing własnego przetwarzania — funkcja nowa, więc kluczowa jest walidacja względem pomiaru offline.

## c) Wybrane podejście

### Składniki estymaty
1. **Nieliniowości** (Character, Analog Feel): residuum r = S(u) − S′(0)·u — różnica między wyjściem nieliniowym a jego linearyzacją małosygnałową. Dla nieliniowości bez pamięci liczone wprost; dla modeli z pamięcią — zlinearyzowany „bliźniak” modelu uruchamiany równolegle w cyklach pomiarowych (co N-ty blok). Residuum zawiera harmoniczne i IMD.
   - Podział residuum (w domenie OS) półpasmowym filtrem IIR na część w paśmie (≤ fs/2 → THD+N) i poza pasmem (> fs/2 → wejście do estymaty aliasingu).
2. **Aliasing**:
   - OS > 1×: energia residuum poza pasmem × tłumienie filtra decymującego w odpowiednich podpasmach (charakterystyka filtra znana z projektu),
   - OS = 1×: pomiar „cieniowy” — co ~100 ms jeden blok 256 próbek: kopia wejścia etapu nieliniowego (i kopia stanu modelu) przetwarzana z 8× i wysokiej jakości decymacją; różnica w paśmie użytecznym względem wyniku 1× = aliasing. Budżet: ≤ 5 % CPU DSP.
3. **Modulacja wzmocnienia** (Dynamic/Spectral): sygnał pasma b[n] i jego wzmocnienie g[n]; składnik „za szybkiej” modulacji m[n] = g[n] − LP(g)[n], gdzie LP ma częstotliwość odcięcia max(fc/8, 20 Hz). Moc zniekształceń ≈ E[b²]·E[m²]. Zamierzone (wolne) działanie dynamiki nie jest liczone.
4. **Szum numeryczny**: poniżej −120 dB wyświetlamy „< −120 dB”.

### Prezentacja i dane
- THD+N i Aliasing w dB względem mocy sygnału wyjściowego (oraz THD+N w %: 100·10^(dB/20)); EMA τ = 300 ms; publikacja do UI przez atomics, odświeżanie 5–10 Hz.
- Rozbicie per pasmo (tooltip): każdy etap jest oznaczony indeksem pasma.
- Próg ostrzegawczy (domyślnie: THD+N > −40 dB albo aliasing > −80 dB) — kolor ostrzegawczy; połączenie z podpowiedzią OS (§10).

### Narzędzie walidacyjne (tools/)
- CLI renderujące sygnały testowe (sinusy na siatce częstotliwości/poziomów, sweepy Farina/Novák, multiton, fragmenty muzyki CC0) przez rdzeń DSP; referencja: przetwarzanie z OS 32× i decymacja wysokiego rzędu; THD z analizy harmonicznych, aliasing = składowe nieharmoniczne.

### Odrzucone alternatywy
- **Analiza widma wyjścia w poszukiwaniu harmonicznych** — przy złożonym materiale nie da się odróżnić harmonicznych wytworzonych od obecnych w sygnale; odrzucone na rzecz residuum.
- **Stały pomiar cieniowy dla każdego bloku** — koszt CPU ×8; odrzucone na rzecz próbkowania co ~100 ms.

## d) Kryteria akceptacji

| ID | Kryterium | Metoda |
|---|---|---|
| MTR-3 | Warm, 1 kHz, 0 dBFS: odczyt THD+N ±3 dB względem analizy harmonicznych offline | test pomiarowy |
| MTR-3 | Ton 10 kHz, Warm +12 dB, 48 kHz, OS 1×/2×/4×/8×: odczyt aliasingu ±3 dB względem referencji (gdy > −110 dB) | test pomiarowy |
| MTR-1 | Konfiguracja liniowa (Clear, Static): oba mierniki „< −120 dB” | test jednostkowy |
| MTR-2 | Koszt mierników ≤ 5 % budżetu DSP | benchmark |
| MTR-4 | Brak alokacji w wątku audio; wartości publikowane przez atomics | przegląd kodu + RTSan |

## e) Realizacja w M7 i wyniki pomiarów

### Podział pracy (ADR-0010, MTR-4)
- **Wątek audio** tylko nagrywa, i to wyłącznie przy otwartym edytorze (`Engine::setDistortionMetering`). Okres pomiaru 125 ms (8 odczytów/s, MTR-1: 5–10 Hz). Rejestratory pasm (`BandRecorder`) są podłączane tylko na okno na końcu okresu: 1152 próbki przy 1× (1024 analizowane + marginesy 2 × 64), 2176 przy oversamplingu (2048 analizowanych — ten sam czas co przy 1×, ale rozdzielczość potrzebna w paśmie przejściowym filtru decymującego). Poza oknem pasma działają jak bez mierników.
- W oknie: wejście nieliniowości x[n] (po LF emphasis) jednego toru trafia do pierścienia; energia produktów każdego toru jest sumowana w zmiennych lokalnych pętli (bez zapisów do pamięci na próbkę); tory Mid/Side mają wagę 2 (trafiają do dwóch kanałów); moc wyjścia (suma po kanałach) liczona w tym samym oknie; mnożnik drogi produktów do wyjścia: (Auto Gain × Mix × Output × (1 − Bypass))².
- Nagrywany jest tor o największej energii produktów w poprzednim oknie; pozostałe skalują wynik (`lanesScale`).
- Migawki (`DistortionCapture`) przechodzą przez triple buffer: bez alokacji i blokad w wątku audio.
- **Wątek analizy** (`MeterService`, niski priorytet) analizuje migawki (`analysis::DistortionMeter`), uśrednia osobno moce produktów i wyjścia (EMA τ = 300 ms; liczba okresów od ostatniej przeanalizowanej migawki wchodzi do stałej, więc pominięte migawki nie spowalniają odczytu) i publikuje odczyty przez triple buffer. Najwyżej 8 analiz pasm na migawkę (round robin; pozostałe pasma zachowują ostatni wynik).

### Produkty (THD+N)
Dokładna replika toru audio: ADAA krzywej N między kolejnymi wejściami, minus część liniowa β·(x[n] + x[n−1])/2 dopasowana metodą najmniejszych kwadratów w paśmie (zmiana wzmocnienia krzywej bez pamięci — twierdzenie Bussganga; dla sinusa dokładnie składowa podstawowej w fazie). Moc w paśmie wyjścia (do połowy bazowej fs) z wagami filtrów produktów (DC blocker, LP). Przy oversamplingu część między fs/2 a fp/2 przechodzi przez filtr decymujący osłabiona `Oversampler::decimationGain` (analitycznie z filtrów kaskady; test: zgodność z odpowiedzią zmierzoną ±0,01 dB) i składa się w pasmo. THD+N zawiera aliasy — jak w AES17: wszystko poza sygnałem.

Plan z §c (residuum z „bliźniakiem” modelu z pamięcią) okazał się niepotrzebny: nieliniowości są bez pamięci (ADR-0009), filtry wokół nich są liniowe.

### Aliasing — zmiana względem planu
ADAA 1. rzędu jest wielomianem dwóch kolejnych wejść: y[n] = A(x[n], x[n−1]), A(u, w) = (∫N(u) − ∫N(w)) / (u − w). Cień liczy A(x(t), x(t − 1/fp)) na wejściu ograniczonym pasmowo (interpolacja FIR liniowo-fazowa, która zachowuje oryginalne próbki) przy 4× bazowej fs (2× fp przy oversamplingu): ŷ(n/fp) = y[n] dokładnie, a ŷ zawiera tylko prawdziwe częstotliwości kombinacyjne wejścia. To, co leży powyżej fp/2, składa się przy próbkowaniu — to jest aliasing, czyli nowe częstotliwości, które pomiar tonem znajduje między harmonicznymi.

Odrzucone w trakcie realizacji (pomiar tonem 10 kHz, Warm +12 dB):
- **różnica „wynik − cień bez próbkowania” w paśmie** (plan z §c): obrazy interpolacji liniowej w ADAA (k·fp ± f) tworzą produkty, które po próbkowaniu lądują dokładnie na częstotliwościach prawdziwych produktów (obraz działa jak ±f) — zmieniają harmoniczne (H2 o −43 dB względem niej przy 2×), ale nie dodają nowych częstotliwości. Różnica liczyła je jako aliasing: przy 2× miernik wskazywał −93 dB, a składowe nieharmoniczne miały −163 dB;
- **złożenie „idealnych” produktów N(x) z wagami sinc²**: przy 1× zawyżało o 3,4 dB — jądro ADAA dla kombinacji (f, f, f) to |Σₘ e^{−jmω}|/4 = 0,205 przy 10 kHz/48 kHz, a model sinc² dawał 0,305.

### Modulacja wzmocnienia (pasma Dynamic) — zmiana względem planu
Szybka część liniowego wzmocnienia pasma m = HP₂(G): górnoprzepustowy Butterworth 2. rzędu przy max(fc/8, 20 Hz) zamiast G − LP(G) z jednobiegunowym LP (ten przepuszczał zamierzone działanie dynamiki o częstotliwości 40 Hz tylko 10 dB niżej). Moc produktów ≈ E[b²·m²], b² — średnia kwadratowa detektora pasma (sygnał filtra detekcji), liczona co krok dynamiki. Modulacja pasm Spectral nie jest szacowana: maska STFT zmienia się co ramkę i jest wygładzana oknami syntezy, a jej skutek czasowy pokazuje odczyt pre-echo (§12).

### Prezentacja (MTR-1, MTR-4, OS-5)
`MeterView` w dolnym pasku: „THD+N −52,3 dB 0,24 %” i „Alias −66,5 dB”, odświeżanie 10 Hz; wartości poniżej −120 dB jako „< −120 dB”; bez sygnału „—”. Kolor ostrzegawczy powyżej progów (domyślnie THD+N −40 dB, aliasing −80 dB; menu Analyzer → Meters, zapisywane w stanie wtyczki). Tooltip: rozbicie na pasma („Band 2 (Bell 1.2 kHz, Warm): THD+N …, aliasing …”). Podpowiedź OS-5: gdy działa nieliniowość, aliasing > −80 dB, a współczynnik jest mniejszy od maksymalnego dla fs, etykieta „Oversampling ▴” w kolorze akcentu i tooltip z proponowanym współczynnikiem; ustawienie nigdy nie zmienia się samo.

### Walidacja (MTR-3)
Referencja: widmo wyjścia silnika (65 536 punktów, okno prostokątne, tony dokładnie na binach — każda składowa w swoim binie); THD+N = moc wszystkiego poza tonami wejścia; aliasing = moc wszystkiego poza tonami i ich prawdziwymi produktami (Σ aᵢ·kᵢ, Σ|aᵢ| ≤ 9, poniżej Nyquista). Miernik: średnia z migawek po 0,5 s, tak jak liczy wtyczka. Testy `[MTR-3]`; częstotliwości dobrane tak, by aliasy nie trafiały na harmoniczne (np. 3,1 kHz zamiast 3 kHz przy 96 kHz). Sweep (Farina, Novák) nie ma osobnego testu: siatka sinusów 100 Hz–15 kHz i para tonów pokrywają ten sam zakres, a referencja z binów jest dokładniejsza niż separacja harmonicznych z dekonwolucji.

| Przypadek (48 kHz) | THD+N miernik / referencja | Aliasing miernik / referencja |
|---|---|---|
| Warm, Bell +6 dB, 1 kHz, 0 dBFS | −52,2 / −52,2 dB | < −120 / brak |
| Warm, Bell +12 dB, 10 kHz, 0 dBFS, 1× | −48,3 / −48,3 dB | −66,5 / −66,5 dB |
| … 2× / 4× / 8× | −45,4 / −44,9 / −44,8 (= referencja) | −146 / −152 / −143 dB (referencja −163 / −164 / −162 dB; obie < −110 dB) |
| Warm, Bell +6 dB, 15 kHz, 1× (całość produktów to aliasy) | −76,9 / −76,9 dB | −76,9 / −76,9 dB |
| Sharp, Bell +9 dB, 7 kHz | −51,1 / −51,1 dB | < −120 / brak |
| Natural Phase, Sharp +9 dB, 9 kHz, 4× (H3 w paśmie przejściowym decymatora) | −106,8 / −108,0 dB | −106,8 / −108,0 dB |
| Analog Feel N soft, +6 dB, 100 Hz | −40,7 / −40,7 dB | < −120 / brak |
| Analog Feel P hard, +9 dB, 3,1 kHz, High (2× FIR, obcinanie) | −22,8 / −22,8 dB | −93,1 / −93,1 dB |
| Analog Feel S hard, +9 dB, 9 kHz | −30,2 / −30,2 dB | −30,2 / −30,2 dB |
| Dwa tony 9 + 11 kHz, Warm High Shelf +9 dB (IMD) | −49,8 / −49,8 dB | −80,3 / −80,3 dB |
| Sharp na Mid sygnału stereo | −51,0 / −51,0 dB | < −120 / brak |

- **MTR-1**: konfiguracja liniowa (Clear, Static, trzy pasma): oba mierniki −300 dB („< −120 dB”). Test wtyczki: Warm +12 dB, 10 kHz → −48,3 / −66,5 dB i podpowiedź 2×; po przełączeniu na 2× aliasing −124 dB, podpowiedź znika.
- **Modulacja** (`[MTR-2]`): ton 1 kHz bramkowany 50/50 ms, atak 0,1 ms: −12 dB; atak 50 ms: −60 dB; płynna obwiednia 8 Hz: −47 dB; pasmo statyczne: −300 dB.
- **MTR-4**: wyjście wtyczki z miernikami i bez nich identyczne co do bitu (test `[MTR-4]`).

### Koszt (MTR-2, `ssc_benchmark 8 meters`, Xeon 2,1 GHz w kontenerze)
Dwa silniki (mierniki wyłączone i włączone) przetwarzają na przemian te same bloki, więc porównanie jest odporne na obciążenie maszyny; rozrzut pomiaru ±2 %. Kolumna „wątek analizy” — analiza wszystkich pasm w każdej migawce (wątek mierników robi najwyżej 8 analiz na migawkę, więc dla 24 pasm ~1,2 % rdzenia).

| Konfiguracja (48 kHz, bufor 128) | Mierniki wył. | Mierniki wł. | Narzut w wątku audio | Wątek analizy |
|---|---|---|---|---|
| stereo, 8 Static Warm | 1,08 % | 1,12 % | +3,5 % DSP | 1,22 % rdzenia |
| stereo, 24 Static Warm | 2,64 % | 2,73 % | +3,6 % DSP | 3,35 % rdzenia |
| stereo, 8 Dynamic Sharp | 4,38 % | 4,59 % | +4,7 % DSP | 0,89 % rdzenia |
| stereo, 8 Static Warm, 4× | 4,42 % | 4,49 % | +1,5 % DSP | 1,21 % rdzenia |
| 7.1.4, 8 Dynamic Warm | 18,70 % | 19,43 % | +3,9 % DSP | 1,19 % rdzenia |

Wersja z ciągłym nagrywaniem i polami rejestratora aktualizowanymi w pamięci na każdą próbkę kosztowała +15…38 % — stąd okna i zmienne lokalne.

### Ograniczenia
- Produkty pasma nie są filtrowane przez pasma leżące za nim w łańcuchu (estymata zakłada, że trafiają na wyjście bez zmian; korektor Natural Phase nie zmienia mocy).
- Tory nienagrywane są skalowane energią swoich produktów (zakłada podobne widmo produktów w torach).
- W paśmie przejściowym filtru decymującego (≈ 0,5…0,57 bazowej fs) estymata wycieku jest zgrubna (rozdzielczość okna; przy oknie 1024 przy 4× zawyżała o 4 dB, stąd 2048 próbek przy oversamplingu).
- Interpolacja cienia w jakości Normal (−90 dB) daje podłogę aliasingu ok. 84 dB poniżej produktów (np. −125 dB dla Sharp 7 kHz +9 dB) — poniżej progu wyświetlania dla typowych ustawień.
