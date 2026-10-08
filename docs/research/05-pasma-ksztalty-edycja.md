# §5 Pasma: kształty, tworzenie i edycja (BAND-1…BAND-7)

## a) Literatura i normy

- R. Bristow-Johnson, *Audio EQ Cookbook* (W3C Note) — prototypy analogowe i biquady: LPF/HPF, BPF, notch, all-pass, peaking, low/high shelf; definicje Q, BW i S.
- S. J. Orfanidis, „Digital Parametric Equalizer Design with Prescribed Nyquist-Frequency Gain”, JAES 45(6), 1997 — peaking EQ bez „ściskania” krzywej przy Nyquiście.
- M. Vicanek, „Matched Second Order Digital Filters”, 2016 — zamknięte wzory na biquady (LP, BP, HP, peaking) dopasowane do odpowiedzi analogowej; współczynniki można modulować prawie z częstotliwością próbkowania.
- J. S. Abel, D. P. Berners, „Discrete-Time Shelf Filter Design for Analog Modeling”, AES 115th Conv., preprint 5939, 2003 — półki zbliżone do analogowych przy Nyquiście.
- M. Holters, U. Zölzer, „Parametric Higher-Order Shelving Filters”, EUSIPCO 2006 — półki wyższych rzędów (strome nachylenia) z parametrami gain/fc.
- Kaskady Butterwortha dla filtrów odcinających 6N dB/oct: Q sekcji k-tej Q_k = 1 / (2·cos((2k−1)·π / (2N))), k = 1…⌊N/2⌋, plus sekcja 1. rzędu dla N nieparzystego.
- V. Välimäki, J. D. Reiss, „All About Audio Equalization: Solutions and Frontiers”, Applied Sciences 6(5):129, 2016 — przegląd kształtów, struktur i interfejsów EQ.
- P. A. Regalia, S. K. Mitra, „Tunable Digital Frequency Response Equalization Filters”, IEEE Trans. ASSP 35(1), 1987 — struktura z filtrem all-pass, w której gain jest parametrem mieszania (przydatna przy modulacji).

## b) Wtyczki referencyjne

- **FabFilter Pro-Q 4**: do 24 pasm; kształty Bell, Notch, Low/High Shelf, Low/High Cut, Band Pass, Tilt Shelf, Flat Tilt, All Pass; płynny slope 0–96 dB/oct oraz Brickwall dla Low/High Cut.
  - Kształt nowego pasma zależy od miejsca kliknięcia: skrajnie lewa/prawa strefa tworzy Low/High Cut, podwójne kliknięcie w dolnej części wyświetlacza tworzy Notch, przeciągnięcie wypadkowej krzywej przy końcach tworzy półkę.
  - Po najechaniu kursorem wyświetlany jest subtelny podgląd kształtu, który powstanie.
- **Three-Body Technology Kirchhoff-EQ**: 32 pasma, 11 typów filtrów o płynnie zmiennym kształcie, krzywe dopasowane do analogowych, modele vintage, dynamika.

## c) Wybrane podejście

### Model danych pasma
```
struct BandParams {
    bool       enabled, solo;
    Shape      shape;          // Bell, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass, TiltShelf, (FlatTilt, AllPass — P1)
    double     freqHz;         // 10 Hz … 30 kHz (ograniczane do 0,49·fs)
    double     gainDb;         // −30 … +30 dB
    double     q;              // 0,025 … 40
    Slope      slope;          // zależny od kształtu
    BandMode   mode;           // Static | Dynamic | Spectral (§6)
    BandTarget target;         // §4
    Character  character;      // Clear | Warm | Sharp (§9)
    bool       autoGain;       // §7 (tylko Dynamic/Spectral)
    DynamicParams dyn;         // §6
};
```
Zakresy (10 Hz–30 kHz, ±30 dB, Q 0,025–40) są typowe dla profesjonalnych EQ; 24 pasma × ok. 25 parametrów ≈ 600 parametrów hosta — akceptowalne dla VST3.

### Realizacja kształtów (Minimum Phase; szczegóły w ADR-0004)
| Kształt | Realizacja | Slope / parametr kształtu |
|---|---|---|
| Bell | biquad dopasowany (Vicanek / Orfanidis) zrealizowany jako SVF-TPT | Q (definicja RBJ, symetryczna dla boost/cut) |
| Low/High Shelf | 6 dB/oct: 1. rząd; 12 dB/oct: 2. rząd z Q (rezonans/„bump”); 18–48 dB/oct: Holters–Zölzer | 6 / 12 / 18 / 24 (P0); 36 / 48 (P1) |
| Low/High Cut | kaskady Butterwortha; Q zmienia rezonans ostatniej sekcji (0,707 = płasko) | 6, 12, 18, 24, 30, 36, 48, 72, 96 dB/oct; płynnie (P1); Brickwall tylko w Natural Phase (ADR-0005) |
| Notch | biquad notch dopasowany | Q |
| Band Pass | kaskada HP + LP (Butterworth) | 12 / 24 / 48 dB/oct |
| Tilt Shelf | para półek 1. rzędu (−g/2 low, +g/2 high) wokół fc | 6 / 12 dB/oct |
| Flat Tilt (P1) | kaskada półek 1. rzędu rozmieszczonych co oktawę 20 Hz–20 kHz (jak filtry „pinking”) | nachylenie w dB/oct |
| All Pass (P1) | all-pass 1. lub 2. rzędu | Q |

Dynamic i Spectral są dostępne dla kształtów z parametrem gain (Bell, Low/High Shelf, Tilt Shelf, Flat Tilt). Cut, Band Pass, Notch i All Pass są tylko statyczne — UI wyszarza wtedy opcje dynamiczne z tooltipem.

### Realizacja w M2 i zmierzona dokładność
Implementacja: `ssc/filters/MatchedDesign` i `ssc/filters/BandDesign` (rdzeń DSP), `ssc/engine/BandProcessor` (wygładzanie, przełączanie topologii, linie M/S).

- **Projekt dopasowany sekcji 2. rzędu.** Bieguny: odwzorowanie impulsowo-niezmiennicze (z = e^{sT}), więc rezonanse zachowują częstotliwość i tłumienie aż do Nyquista. Zera: |H|² sekcji jest liniowe względem trzech współczynników w bazie φ (φ1 = sin²(ω/2), φ0 = 1 − φ1, φ2 = 4φ0φ1), więc dopasowanie |H|² prototypu przy DC, przy Nyquiście i w fc to układ 3×3; licznik jest potem faktoryzowany do postaci minimalnofazowej. Sekcje 1. rzędu dopasowujemy przy DC i w fc.
- **Cięcia jako odwrotność wzmocnień.** Bell i półki z ujemnym gain to dokładna odwrotność projektu z dodatnim gain (H(−G) = 1/H(+G)), więc boost i cut są symetryczne co do bitu. Wysokie półki dopasowujemy w orientacji „cut” (bieguny w paśmie), a następnie odwracamy — to usuwa błędy rzędu dziesiątek dB, które daje dopasowanie bezpośrednie.
- **Realizacja SVF-TPT.** Każdy biquad jest odwzorowany na parametry SVF (g, k, m0, m1, m2); impuls SVF zgadza się z transmitancją biquada (test „SVF realisation…”). Parametry są przeliczane co blok kontrolny (Quality: Eco 32, Normal 16, High 8, Max 4 próbki) i interpolowane liniowo per próbka; zmiany kształtu, slope, celu i włączenia pasma przechodzą crossfadem 10 ms między dwiema instancjami filtra (tylko na kanałach, których dotyczą).
- **Zmierzona obwiednia błędu amplitudy** (cyfrowy vs prototyp analogowy, fs 44,1/48/96 kHz, Q 0,5–10, gain ±18 dB, poziomy powyżej −30 dB): gain w fc, przy DC i przy Nyquiście dokładny; w oktawie wokół fc ≤ 0,12 dB dla fc ≤ fs/16 i ≤ 0,45 dB dla fc ≤ fs/8; w całym zakresie 10 Hz–0,45·fs ≤ 0,5 dB i ≤ 0,8 dB. Półki (Q 0,707, 6–24 dB/oct): plateau dokładne, połowa gain w fc (±0,02 dB), ≤ 0,2 dB lokalnie i ≤ 0,6 dB globalnie. Cięcia: −3,01 dB w fc i nominalne nachylenie ±0,5 dB/oct dla 6–96 dB/oct.
- **Odstępstwo od pierwotnego kryterium (±0,1 dB do 20 kHz przy 44,1 kHz).** Filtr IIR minimalnofazowy rzędu 2 na sekcję nie może jednocześnie dokładnie odtworzyć amplitudy prototypu analogowego w całym paśmie dla fc blisko Nyquista (wynik znany z literatury o filtrach dopasowanych). Kryterium ±0,1 dB realizują: Natural Phase (korektor FIR amplitudy i fazy, M4) oraz Minimum Phase w Quality High i Max (minimalnofazowy korektor amplitudy bez latencji, M8 — research/08 „Realizacja w M8”; planowane w M4 wewnętrzne 2× dla pasm powyżej fs/8 nie powstało, a korektor jest dokładniejszy i nie wnosi latencji). Kryterium BAND-2 w tabeli niżej opisuje to, co sam dopasowany IIR (Minimum Phase Eco/Normal) gwarantuje i co jest testowane.
- **Ta sama barwa przy 44,1 i 96 kHz (NFR-2):** różnica odpowiedzi ≤ 0,25 dB w paśmie przepustowym dla fc ≤ 2,5 kHz i ≤ 0,6 dB dla fc ≤ 5 kHz (≤ 0,5 / 1,0 dB na zboczach −30…−12 dB).

### Edycja w UI (M2)
- Węzeł pasma: dla kształtów z gain na wysokości gain, dla Low/High Cut na poziomie odpowiedzi w fc (rezonans widać na wysokości węzła), dla Notch, Band Pass i All Pass na linii 0 dB; pionowe przeciąganie zmienia wtedy Q.
- Każda edycja to transakcja (`ui/BandModel`): pierwszy ruch parametru otwiera gest hosta, koniec gestu myszy lub akcji menu zamyka wszystkie gesty i zapisuje jeden krok Undo (migawka znormalizowanych wartości 24 × 23 parametrów). Historia Undo należy do procesora, więc przetrwa zamknięcie edytora.
- Solo (odsłuch) nie jest parametrem: to stan monitoringu edytora, kasowany przy jego zamknięciu. Filtr odsłuchu: Band Pass o fc i Q pasma (Q ograniczone do 0,5–20), dla półek i cięć Low/High Cut po stronie, na którą działa pasmo; przejście crossfadem 10 ms.
- Panel pasma jest zadokowany pod wyświetlaczem i przenosi się na górę, gdy zasłaniałby węzeł edytowanego pasma.

### Tworzenie pasm kliknięciem (BAND-4)
Funkcja czysta `ShapeForGesture(x, y, gesture)` w znormalizowanych współrzędnych wyświetlacza (x: 0…1 na logarytmicznej osi częstotliwości, y: 0…1 od góry do dołu):

| Gest | Warunek | Kształt |
|---|---|---|
| podwójne kliknięcie / hover | x < 0,04 | Low Cut |
| podwójne kliknięcie / hover | x > 0,96 | High Cut |
| podwójne kliknięcie / hover | y > 0,88 (dolny pas) | Notch |
| przeciągnięcie wypadkowej krzywej | start przy x < 0,15 | Low Shelf |
| przeciągnięcie wypadkowej krzywej | start przy x > 0,85 | High Shelf |
| pozostałe | — | Bell (gain z pozycji y względem linii 0 dB) |

- Progi są stałymi w jednym nagłówku i zostaną dostrojone w testach użyteczności (M2); opis trafi do manualu.
- **Ghost preview**: przy hoverze rysujemy półprzezroczystą krzywą kształtu, który powstanie (domyślny gain ±6 dB zależnie od y).

### Menu kontekstowe i interakcja (BAND-5, BAND-6)
- Prawy przycisk na węźle: Shape, **Slope** (lista zależna od kształtu), Mode, Target, Character, Auto Gain, Solo, Bypass, Copy/Paste, Reset, Delete. Dla Bell/Notch zamiast Slope pokazujemy Q.
- Przeciąganie: freq/gain; kółko myszy: Q (krok logarytmiczny); Shift: precyzja ×0,1; Alt/Option + klik: Solo.
- **Solo** = odsłuch obszaru pasma: dla Bell/Notch filtr pasmowy o fc i Q pasma; dla półek i cut — obszar, na który pasmo działa.
- Zaznaczanie prostokątem, przeciąganie grupy (zmiany względne), Delete, Undo/Redo przez `juce::UndoManager` (gest myszy = jedna transakcja), podwójne kliknięcie na wartość = pole tekstowe.
- Kolor pasma: 24 stałe barwy rozmieszczone równomiernie w przestrzeni percepcyjnej (spójne w węźle, panelu i menu).

### Odrzucone alternatywy
- **Biquady RBJ z prostym przekształceniem biliniowym** — „ściskanie” krzywych przy Nyquiście (szczególnie półki i bell powyżej ~10 kHz przy 44,1 kHz); odrzucone na rzecz projektów dopasowanych.
- **Bezpośrednia struktura DF-II/TDF-II przy szybkiej modulacji** — artefakty przy szybkich zmianach współczynników; SVF-TPT jest odporniejszy (ADR-0004).
- **Brickwall w Minimum Phase** (np. eliptyczny IIR wysokiego rzędu) — silny ringing i duże przesunięcia fazy przy cięciu; w v1.0 tylko w trybie Natural Phase.

## d) Kryteria akceptacji

| ID | Kryterium | Metoda |
|---|---|---|
| BAND-1 | 24 pasma działają jednocześnie (stereo, Minimum Phase, Normal) w budżecie CPU z NFR-1 | benchmark |
| BAND-2 | Minimum Phase (zmierzone w M2, fs 44,1/48/96 kHz, Q 0,5–10, gain ±18 dB): gain dokładny w fc, przy DC i przy Nyquiście; błąd amplitudy vs prototyp w oktawie wokół fc ≤ 0,12 dB (fc ≤ fs/16) i ≤ 0,45 dB (fc ≤ fs/8), w 10 Hz–0,45·fs ≤ 0,5 / 0,8 dB; półki ≤ 0,2 dB lokalnie, ≤ 0,6 dB globalnie. Pierwotne ±0,1 dB do 20 kHz przy 44,1 kHz — w Natural Phase (M4) i w Minimum Phase High/Max (M8: ≤ 0,005 dB, `[PH-1]`, docs/measurements/filters.md) | test jednostkowy na siatce parametrów (`[BAND-2]`) |
| BAND-3 | Nachylenie Cut mierzone 2–3 oktawy od fc: wartość nominalna ±0,5 dB/oct dla 6–96 dB/oct | test jednostkowy |
| BAND-4 | `ShapeForGesture` zwraca kształty zgodnie z tabelą stref (pokrycie wszystkich stref i granic) | test jednostkowy |
| BAND-5 | Menu pokazuje Slope tylko dla kształtów, które go mają; zmiana slope z menu jest jedną transakcją Undo | test UI / checklista |
| BAND-6 | Przemiatanie fc 20 Hz → 20 kHz w 1 s (sinus −6 dBFS) nie daje kliknięć: różnica względem referencji z interpolacją per próbka ≤ −60 dB | test jednostkowy |
| BAND-7 | Panel pokazuje wszystkie parametry i odczyty §12 aktualnego pasma | checklista UI |
