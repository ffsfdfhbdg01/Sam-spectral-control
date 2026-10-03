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

## d) Kryteria akceptacji

| ID | Kryterium | Metoda |
|---|---|---|
| CHR-3 | Sinus 0 dBFS w fc, Bell +6 dB: Warm 0,1–0,5 % THD, H2 ≥ H3 + 6 dB; Sharp 0,1–0,5 % THD, H3 ≥ H2 + 6 dB; Clear < −120 dB | test pomiarowy (analiza harmonicznych) |
| CHR-2 | Pasmo Warm/Sharp z gainem 0 dB: residuum vs Clear ≤ −120 dB | null test |
| CHR-1 | Clear: odpowiedź identyczna z trybem Static Clear (null ≤ −140 dB) | null test |
| CHR-1 | Aliasing (Warm, +12 dB, ton 10 kHz przy 48 kHz): ≤ −80 dB przy 1× z ADAA; ≤ −100 dB przy 4× | test pomiarowy |
| CHR-4 | Analog Feel + Clear: THD < −120 dB | test pomiarowy |
| — | Narzut CPU Warm/Sharp ≤ 30 % kosztu pasma Clear (przy 1×) | benchmark |
