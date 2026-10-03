# §8 Tryby fazy, jakość obliczeń i modele analogowe (PH-1…PH-3)

## a) Literatura i normy

**Filtry dopasowane do analogowych i struktury odporne na modulację**
- M. Vicanek, „Matched Second Order Digital Filters”, 2016 — https://www.vicanek.de/articles/BiquadFits.pdf
- S. J. Orfanidis, „Digital Parametric Equalizer Design with Prescribed Nyquist-Frequency Gain”, JAES 45(6), 1997.
- J. S. Abel, D. P. Berners, „Discrete-Time Shelf Filter Design for Analog Modeling”, AES 115th Conv., preprint 5939, 2003.
- V. Zavalishin, *The Art of VA Filter Design* (Native Instruments) — TPT, SVF, filtry bez opóźnienia w pętli.
- A. Simper, Cytomic technical papers — SVF z całkowaniem trapezowym; realizacja dowolnego biquada przez mieszanie wyjść SVF.
- J. O. Smith III, *Introduction to Digital Filters with Audio Applications*, W3K, 2007 — faza, opóźnienie grupowe, filtry minimalnofazowe.

**Natural / Linear Phase i splot**
- W. G. Gardner, „Efficient Convolution without Input-Output Delay”, JAES 43(3), 1995.
- F. Wefers, *Partitioned Convolution Algorithms for Real-Time Auralization*, rozprawa doktorska, RWTH Aachen (Logos), 2015.
- A. V. Oppenheim, R. W. Schafer, *Discrete-Time Signal Processing*, 3. wyd., 2010 — projekt FIR metodą próbkowania w częstotliwości, rozkład min-phase/all-pass.

**Modelowanie analogowe**
- A. Fettweis, „Wave Digital Filters: Theory and Practice”, Proc. IEEE 74(2), 1986.
- K. J. Werner, *Virtual Analog Modeling of Audio Circuitry Using Wave Digital Filters*, rozprawa doktorska, Stanford (CCRMA), 2016.
- M. Holters, U. Zölzer, „A Generalized Method for the Derivation of Non-Linear State-Space Models from Circuit Schematics”, EUSIPCO 2015.
- M. Holters, U. Zölzer, „Circuit Simulation with Inductors and Transformers Based on the Jiles-Atherton Model of Magnetization”, DAFx-16, 2016.
- D. T. Yeh, J. S. Abel, J. O. Smith III, „Simplified, Physically-Informed Models of Distortion and Overdrive Guitar Effects Pedals”, DAFx-07, 2007.
- J. Pakarinen, D. T. Yeh, „A Review of Digital Techniques for Modeling Vacuum-Tube Guitar Amplifiers”, Computer Music Journal 33(2), 2009.
- „Modeling the Pultec EQP-1A with Wave Digital Filters”, SMC 2024 (repozytorium UPF); kod: https://github.com/ABSounds/EQP1A-WDF — pasywna sieć EQP-1A jako WDF (pywdf → chowdsp_wdf), porównanie z LTspice.
- Universal Audio WebZine (2006) o modelu Neve 1073 — https://uaudio.com/webzine/2006/may/text/content2.html — charakter 1073 w dużej mierze wynika z elementów magnetycznych: ich rezonanse własne zmieniają krzywe względem schematu, a pojemności pasożytnicze cewek „ściągają” pasma środkowe ku wzmocnieniu jednostkowemu w wysokich częstotliwościach.

**Dane o urządzeniach (inspiracja, nie kopia)**
- **Pultec EQP-1A** (Pulse Techniques, 1961): pasywny EQ + lampowy wzmacniacz make-up; Low 20/30/60/100 Hz (boost 0…+13,5 dB, atten 0…−17,5 dB), High boost 3/4/5/8/10/12/16 kHz (0…+18 dB, regulowana szerokość), High atten 5/10/20 kHz (0…−16 dB). „Pultec trick”: jednoczesny boost i atten w basie daje podbicie z towarzyszącym wcięciem powyżej.
- **Neve 1073**: HPF 50/80/160/300 Hz, 18 dB/oct; półka HF 12 kHz ±16 dB; Class A, transformatory. Częstotliwości półki LF (35/60/110/220 Hz) i pasma środkowego (0,36/0,7/1,6/3,2/4,8/7,2 kHz) `[niezweryfikowane]` — do potwierdzenia w dokumentacji AMS Neve.
- **SSL serii E/G**: karta E „Black” (242) ma przełącznik bell/shelf dla HF i LF; seria G (1987) wprowadziła **Q proporcjonalne do gainu** i przełączniki ×3/÷3 dla HMF/LMF zamiast bell/shelf. Źródła wtórne są częściowo sprzeczne — weryfikacja w oryginalnej dokumentacji SSL przed kalibracją modelu.

## b) Wtyczki referencyjne

- **FabFilter Pro-Q 4** — Zero Latency (amplituda zgodna z analogowym EQ, bez latencji), **Natural Phase** (amplituda i faza zbliżone do analogowych, bez zauważalnego pre-ringu i długiej latencji), Linear Phase (wybierana rozdzielczość).
- **Kirchhoff-EQ** — krzywe dopasowane do analogowych; 30 modeli vintage EQ.
- **Universal Audio** — emulacje Pultec i Neve `[niezweryfikowane: szczegóły metod]`.

## c) Wybrane podejście

### Minimum Phase
- Projekt dopasowany (Vicanek dla bell/LP/HP/BP; Orfanidis lub Berners–Abel dla półek), realizowany jako SVF-TPT przez odwrotne przekształcenie biliniowe (ADR-0004). Zerowa latencja (poza lookahead i silnikiem STFT).

### Natural Phase (ADR-0005)
- **Idea**: H(z) = H_mp(z) · C(z), gdzie H_mp to dopasowany IIR (amplituda analogowa), a C(z) to krótki FIR korygujący fazę: |C| ≈ 1, ∠C ≈ φ_e(ω) − ω·D, gdzie φ_e = ∠H_a − ∠H_mp (błąd fazy względem prototypu analogowego), a D to opóźnienie (latencja).
- Wynik: faza analogowa + czyste opóźnienie D (kompensowane przez host). Pre-ring ograniczony do D.
- Uzasadnienie: dopasowany IIR ma już prawie analogową fazę w basie i średnicy; odchylenia pojawiają się głównie w wysokich częstotliwościach (odpowiedź cyfrowa „wraca” przy Nyquiście). Dlatego wystarcza krótki korektor (D = 32–256 próbek przy 48 kHz). Pełny FIR z H(jω) wymagałby bardzo długich filtrów dla dokładnego basu.
- Pasma Dynamic w Natural Phase: dynamika działa w IIR; korektor fazy jest przeliczany z częstotliwością sterowania (≤ 50 Hz) i przełączany crossfade’em.
- **Brickwall** (Low/High Cut) dostępny w Natural Phase jako FIR z tą samą latencją D (stromość ograniczona przez D; Max → najdłuższy).

### Analog Feel N / P / S (ADR-0009)
Każdy model składa się z trzech warstw:
1. **Krzywe** — modyfikacje funkcji przenoszenia pasm w formie parametrycznej:
   - N: łagodnie proporcjonalne Q w bell, półki z lekkim „bumpem” w przeciwną stronę przy częstotliwości granicznej, HPF domyślnie 18 dB/oct, „ściąganie” bell ku 0 dB w wysokich częstotliwościach (efekt pojemności pasożytniczych),
   - P: osobne krzywe dla boostu (rezonansowa półka LC) i tłumienia (szersza półka o innej częstotliwości granicznej) — złożenie daje „Pultec trick”; boost HF z regulowaną szerokością; P1: parametr „P-Atten” w półce Low,
   - S: Q proporcjonalne do |gain| (styl G), opcja bell/shelf dla skrajnych pasm (styl E).
2. **Faza** — jak w prototypie analogowym (dopasowany IIR; w High/Max dodatkowo korektor jak w Natural Phase).
3. **Nieliniowości** — przypisane do pasm (CH-6), zależne od materiału, z ADAA i oversamplingiem:
   - N: transformator (nasycenie rdzenia rosnące w basie; model Jiles–Atherton w Max) + asymetria wzmacniacza Class A (H2 + H3),
   - P: lampa (triody: dominująca H2, miękka kompresja przy wysokich poziomach) + transformator wyjściowy,
   - S: op-amp — bardzo czysty do wysokich poziomów, nieparzyste harmoniczne przy zbliżaniu się do przesterowania.
- **Kalibracja poziomu**: −18 dBFS (sinus) = +4 dBu (0 VU) — wspólny punkt odniesienia dla modeli nieliniowych.
- **Nazwy w UI**: „Analog N / P / S” z opisami bez znaków towarowych (np. „Class-A transformer console EQ”, „passive LC program EQ with tube make-up”, „1980s console EQ with proportional Q”). Nazwy Neve/Pultec/SSL — wyłącznie po konsultacji prawnej.

### Poziomy Quality (PH-2)
| Tryb | Eco | Normal | High | Max |
|---|---|---|---|---|
| Minimum Phase | parametry co 32 próbki, float | co 8 próbek, float | per próbka, stan w double | per próbka, double; pasma z fc > fs/4 w wewnętrznym 2× |
| Natural Phase | D ≈ 0,33 ms | D ≈ 0,67 ms | D ≈ 1,33 ms | D ≈ 2,67 ms (korekcja do 0,95·Nyquist) |
| Analog Feel | krzywe + saturacja bez pamięci, ADAA-1 | + interakcje zależne od poziomu, ADAA-1 | + modele z pamięcią (state-space/WDF), ADAA-2, wewnętrzne 2× | + pełne modele nieliniowe (Newton–Raphson), Jiles–Atherton, wewnętrzne 4× |
| Silnik spektralny | N = 2048, hop N/4 | 4096, N/4 | 8192, N/4 | 8192, N/8 + okienkowanie maski |

(Czasy D podane dla 48 kHz, skalowane z fs. Wartości są punktem startowym — finalne po benchmarku CPU/latencji i pomiarze błędu w M4).

### Linear Phase (PH-3, P2)
- Korektor FIR z Natural Phase i silnik splotu partycjonowanego są wspólne — Linear Phase to FIR z |H_a| i fazą liniową. Architektura tego nie blokuje.

### Odrzucone alternatywy
- **Natural Phase jako pełny FIR z odpowiedzi analogowej** — długie filtry i latencja dla dokładnego basu; odrzucone.
- **Natural Phase jako „mixed-phase” przez rozkład min-phase/all-pass całego EQ** — trudne do aktualizacji przy zmianach parametrów; odrzucone.
- **Pełna symulacja SPICE całych urządzeń** — za droga w czasie rzeczywistym i nie pasuje do w pełni parametrycznych pasm.

## d) Kryteria akceptacji

| ID | Kryterium | Metoda |
|---|---|---|
| PH-1 | Minimum Phase: obwiednia błędu amplitudy jak w research/05 §d (BAND-2), zmierzona w M2; ±0,1 dB do 20 kHz przy 44,1 kHz zapewnia Quality Max z wewnętrznym 2× dla pasm powyżej fs/8 (M4) | test jednostkowy |
| PH-1 | Natural Phase: amplituda ±0,1 dB; błąd fazy vs prototyp ≤ 5° (Normal) i ≤ 2° (Max) w 20 Hz–0,9·Nyquist; latencja = D; pre-ring (kryterium −60 dB) ≤ D | test jednostkowy |
| PH-1 | Analog Feel: odpowiedź dla ustawień referencyjnych ±0,5 dB względem krzywych referencyjnych (np. model WDF EQP-1A); THD w punktach kalibracji w oknie docelowym ±3 dB | test pomiarowy |
| PH-2 | Tabela CPU i latencji wszystkich kombinacji tryb × Quality wygenerowana przez benchmark; błąd odpowiedzi maleje monotonicznie z Quality | benchmark |
| PH-3 | Przegląd architektury: ścieżka FIR (korektor/splot) gotowa na Linear Phase | przegląd kodu |
