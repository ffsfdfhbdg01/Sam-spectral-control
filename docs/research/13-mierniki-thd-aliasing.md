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
