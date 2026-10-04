# §9 Character pasma: Clear / Warm / Sharp (CHR-1…CHR-5)

## a) Literatura i normy

- R. O. Hamm, „Tubes Versus Transistors — Is There an Audible Difference?”, JAES 21(4):267–273, 1973 — przesterowanie lampowe: dominują niskie, parzyste harmoniczne (H2); tranzystorowe: więcej nieparzystych i wyższych rzędów.
- C.-T. Tan, B. C. J. Moore, N. Zacharov, „The Effect of Nonlinear Distortion on the Perceived Quality of Music and Speech Signals”, JAES 51(11):1012–1031, 2003 — percepcyjna miara zniekształceń nieliniowych.
- E. Geddes, L. Lee, „Auditory Perception of Nonlinear Distortion — Theory”, AES 115th Conv., 2003 — wyższe rzędy harmonicznych są bardziej uciążliwe niż niskie (metryka GedLee).
- P. A. Regalia, S. K. Mitra, IEEE Trans. ASSP 35(1), 1987 oraz Zölzer, *DAFX* (2011) — rozkład pasma peaking na sygnał bezpośredni + człon pasmowy, który pozwala obrabiać tylko „wkład” pasma.
- J. Parker, V. Zavalishin, E. Le Bivic, DAFx-16, 2016; S. Bilbao, F. Esqueda, J. Parker, V. Välimäki, IEEE SPL 24(7), 2017 — ADAA dla nieliniowości bez pamięci.
- Asymetryczna charakterystyka nieliniowa generuje harmoniczne parzyste; symetryczna — nieparzyste (podstawy szeregów potęgowych).

## b) Wtyczki referencyjne

- **FabFilter Pro-Q 4** — tryby Character: **Clean** (przezroczysty), **Subtle** (transformatorowe, zależne od materiału i częstotliwości nasycenie), **Warm** (wyraźniejsze, lampowe). Działają **globalnie** na instancję; na charakter nasycenia wpływają pasma EQ.
- **Kirchhoff-EQ** — modele vintage z własnym charakterem.
- Wyróżnik Sam Spectral Control: Character **per pasmo** i trzecia, „jasna” barwa (Sharp).

## c) Wybrane podejście

### Gdzie działa nieliniowość (CHR-2)
- Pasmo SVF-TPT w postaci y = m0·v0 + m1·v1 + m2·v2 (v0 = wejście, v1 = wyjście pasmowe, v2 = dolnoprzepustowe). Dla Bell (m0 = 1, m2 = 0): y = x + m1·v1 → **wkład pasma** u = m1·v1. Dla półek wkład u = m1·v1 + m2·v2.
- Saturujemy tylko u: y = x + S(u). Pasmo o gainie 0 dB (u = 0) nie wnosi żadnych zniekształceń, a zniekształcenia rosną z aktywnością pasma — zachowanie „program-dependent”.
- Kształty bez gainu (Cut, Band Pass, Notch, All Pass): Warm/Sharp działają tylko na rezonans (Q > 0,707) — na człon nadwyżki rezonansowej; przy Q ≤ 0,707 brak efektu (UI to sygnalizuje).

### Definicje barw
| Character | Krzywa | Nieliniowość S(u) | Dodatki |
|---|---|---|---|
| **Clear** (domyślnie) | symetryczna, Q wg RBJ | brak (S(u) = u) | — |
| **Warm** | Q proporcjonalne: przy małym |gain| szerzej (Q_eff ≈ 0,6·Q przy 0 dB → Q przy ≥ 12 dB) | asymetryczna miękka: tanh(a·u + b) − tanh(b), znormalizowana do pochodnej 1 w zerze → dominuje H2 | łagodny dolnoprzepustowy 1. rzędu (≈ 12 kHz) na produktach nieliniowych, nie na całym sygnale |
| **Sharp** | strome zbocza: bell 4. rzędu (dwie sekcje po ½ gainu, Q dobrane tak, by zachować szerokość −3 dB) | symetryczna: tanh(a·u)/a → dominuje H3 | brak filtracji produktów — więcej „presence” |

- Parametry a i b dobieramy tak, żeby spełnić cele CHR-3 (kalibracja odsłuchowa w M4).
- **ADAA** pierwszego rzędu dla tanh (antypochodna: log(cosh)) w Eco/Normal; drugiego rzędu w High/Max; plus globalny oversampling (§10).

### Analog Feel (CHR-4)
- W trybie Analog Feel Character przełącza wariant nieliniowości modelu: Clear = sam model krzywych i fazy (bez nieliniowości), Warm/Sharp = dwa warianty nasycenia modelu (np. dla N: transformator „miękki” vs „twardy”).

### Odrzucone alternatywy
- **Saturacja całego sygnału wyjściowego pasma** — barwiłaby cały sygnał nawet przy gainie 0 dB; odrzucone.
- **Globalny Character jak w Pro-Q 4** — wymaganie mówi o per pasmo.
- **Sharp = tylko inna krzywa, bez nieliniowości** — barwa nie różniłaby się wtedy od Clear w odsłuchu przy łagodnych ustawieniach; odrzucone.

### Realizacja w M4 i wyniki pomiarów

- **Character jest częścią kształtu pasma** (`BandShape::character`): ten sam projekt krzywej trafia do audio, tablicy wzmocnień pasm Dynamic, krzywych UI i krzywych Auto Gain.
- **Warm**: bell z Q proporcjonalnym, Q_eff = Q·(0,6 + 0,4·min(1, |G|/12 dB)).
- **Sharp**: bell 4. rzędu jako transformacja dolnoprzepustowo-pasmowa p = (s² + ω₀²)/(s·B), B = ω₀/Q, półki 2. rzędu Holtersa–Zölzera — to samo wzmocnienie w fc i ta sama szerokość w połowie wzmocnienia (dB) co bell 2. rzędu przy tym Q, płaski szczyt i zbocza opadające dwa razy szybciej; rozkłada się na dwie sekcje strojone poniżej i powyżej fc. (Kaskada dwóch identycznych belli po ½ gainu nie byłaby bardziej stroma — w dB daje praktycznie ten sam kształt.)
- **Cięcia z rezonansem** (Q > 0,707, Warm/Sharp): kaskada Butterwortha + osobna sekcja rezonansu (s² + d·ω·s + ω²)/(s² + (d/r)·ω·s + ω²) na końcu (ta sama odpowiedź analogowa) — kształtowany jest tylko wkład rezonansu. Notch, Band Pass, All Pass i cięcia bez rezonansu: Character nie zmienia nic.
- **Zmiana względem planu — nieliniowość**: zamiast tanh zastosowano gładki wielomianowy soft clipper S(u) = P(a·u)/a, P(v) = v + p₂v² + p₃v³ między punktami P′ = 0 i stały poza nimi. Do wyjścia liniowego dodawane są wyłącznie produkty N(u) = S(u) − u, więc odpowiedź liniowa jest dokładnie odpowiedzią projektu, a pasmo 0 dB nic nie dodaje (CHR-2). N i jego całka to wielomiany niskiego stopnia: ADAA 1. rzędu ma postać dokładnego ilorazu bez dzielenia (wewnątrz obszaru) i nie traci precyzji przy małych sygnałach — dla tanh (log cosh) i dla S(u) − u z sigmoidy różnica antypochodnych traci cyfry znaczące i daje szum na poziomie −50 dB względem cichych sygnałów (sprawdzone analitycznie przy projektowaniu). Półpróbkowe opóźnienie ADAA dotyczy tylko produktów.
- **Warm**: p₂ = −0,129, p₃ = −1/3, a = 0,0775 (asymetria: obcinanie +0,55/a i −0,81/a), produkty przez filtr DC 5 Hz i dolnoprzepustowy 12 kHz. **Sharp**: p₂ = 0, p₃ = −1/3, a = 0,245 (symetrycznie, same nieparzyste harmoniczne), bez filtracji.
- **ADAA 1. rzędu we wszystkich Quality** (zamiast 2. rzędu w High/Max): pomiar pokazał, że ADAA-2 dałoby tylko kilka dB dla harmonicznych tuż nad Nyquistem (tłumienie sinc² zamiast sinc), a oversampling ≥ 2× usuwa aliasing produktów stopnia ≤ 3 praktycznie całkowicie.

**Zmierzone (testy `[character]`, 48 kHz)**

| Kryterium | Wynik |
|---|---|
| CHR-3 Warm (sinus 0 dBFS w fc, Bell +6 dB) | THD 0,245 %, H2 −52,2 dB, H3 −72,4 dB (H2 − H3 = 20 dB) |
| CHR-3 Sharp | THD 0,246 %, H3 −52,2 dB, H2 −308 dB |
| CHR-3 Clear | THD −276 dB |
| CHR-2 pasmo 0 dB | bit-exact względem wejścia |
| CHR-1 aliasing (Warm, +12 dB, 10 kHz, 0 dBFS) | 1×: −66,5 dB; 2×/4×/8×: ≈ −163 dB (OS IIR Normal); przez silnik z 2×: < −95 dB |
| ADAA, sinus 5 kHz mocno obcięty | aliasing −10,1 dB względem −4,6 dB bez ADAA |
| Krzywa Sharp | szczyt = G, punkty połowy wzmocnienia identyczne z bellem 2. rzędu, 2 oktawy od fc ≤ 0,6 wartości (dB) bella 2. rzędu |

**Odstępstwa od kryteriów**
- Aliasing przy 1× (−66,5 dB) nie osiąga celu −80 dB: H3 tonu 10 kHz (30 kHz) odbija się na 18 kHz, a ADAA 1. rzędu tłumi go tylko o ~6 dB. Przy 2× i więcej cel jest spełniony z dużym zapasem (≈ −160 dB); UI ma podpowiadać oversampling (OS-5, M7).
- Narzut CPU Warm/Sharp (cel ≤ 30 % kosztu pasma Clear): zmierzone 8 pasm Warm 0,69 % vs Clear 0,51 % (+35 %), Sharp 0,74 % (+45 %, w tym druga sekcja krzywej 4. rzędu). Możliwa dalsza optymalizacja (SIMD na liniach stereo) — M8.

### Realizacja w M6 (CHR-4)

- W Analog Feel Character wybiera nieliniowość modelu (`filters::shaperKindFor`): **Clear** — tylko krzywe i faza modelu, bez nieliniowości; **Warm** — wariant miękki modelu (N/P/S Soft); **Sharp** — wariant twardy (6–10 dB więcej zniekształceń). Krzywe Warm (Q proporcjonalne) i Sharp (bell 4. rzędu) z trybów Minimum/Natural Phase nie są wtedy używane — krzywą wyznacza model.
- Wszystkie warianty mają tę samą strukturę co Warm/Sharp (produkty N(u) = S(u) − u, ADAA 1. rzędu, opcjonalnie filtr DC i dolnoprzepustowy na produktach); doszły człon u⁵ (model S) i podbicie basu przed nieliniowością (model N). Struktura `Shaper` jest wspólna — 9 rodzajów (`ShaperKind`): None, Warm, Sharp, N/P/S Soft i Hard.
- Pomiary (testy `[CHR-4]`): Analog Feel + Clear < −120 dB THD (N i P, sinus 0 dBFS); kalibracja wariantów i rozkład harmonicznych — tabela w research/08 („Realizacja w M6”).
- Koszt: wspólna implementacja ma koszt M4 (porównanie naprzemienne M5/M6: 8 pasm Warm 0,88/0,90 %, Sharp 0,99/1,02 %, Clear 0,68/0,68 %); 8 pasm Analog Feel N Warm w Eco/Normal ≈ 1,0 % wobec 0,68 % Clear (dodatkowy filtr podbicia basu).

## d) Kryteria akceptacji

| ID | Kryterium | Metoda |
|---|---|---|
| CHR-3 | Sinus 0 dBFS w fc, Bell +6 dB: Warm 0,1–0,5 % THD, H2 ≥ H3 + 6 dB; Sharp 0,1–0,5 % THD, H3 ≥ H2 + 6 dB; Clear < −120 dB | test pomiarowy (analiza harmonicznych) |
| CHR-2 | Pasmo Warm/Sharp z gainem 0 dB: residuum vs Clear ≤ −120 dB | null test |
| CHR-1 | Clear: odpowiedź identyczna z trybem Static Clear (null ≤ −140 dB) | null test |
| CHR-1 | Aliasing (Warm, +12 dB, ton 10 kHz przy 48 kHz): ≤ −80 dB przy 1× z ADAA; ≤ −100 dB przy 4× | test pomiarowy |
| CHR-4 | Analog Feel + Clear: THD < −120 dB | test pomiarowy |
| — | Narzut CPU Warm/Sharp ≤ 30 % kosztu pasma Clear (przy 1×) | benchmark |
