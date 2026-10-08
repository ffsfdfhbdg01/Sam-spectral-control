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
| Minimum Phase | parametry co 32 próbki, float | co 8 próbek, float | per próbka, stan w double | per próbka, double; pasma z fc > fs/4 w wewnętrznym 2× (zrealizowane inaczej w M8: minimalnofazowy korektor amplitudy w High i Max — niżej) |
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

### Realizacja w M4 i wyniki pomiarów

**Natural Phase** (`ssc/phase/PhaseCorrector`, ADR-0005):
- Korektor jest pojedynczym FIR na kanał za łańcuchem pasm: C(ω) = B(ω) · T(ω)/A(ω) · e^{−jωD}. T — iloczyn prototypów analogowych pasm działających na sygnał, A — ich odpowiedzi cyfrowe przy częstotliwości przetwarzania (fs × oversampling), B — amplituda liniowofazowych cięć Brickwall.
- **Zmiana względem ADR-0005**: korektor poprawia także resztkowy błąd amplitudy dopasowanego IIR (obiecane w odstępstwie BAND-2), nie tylko fazę. Poniżej −80 dB odpowiedzi pasma korygowana jest tylko faza; korekcja amplitudy kanału ograniczona do ±12 dB.
- Siatka ≥ 8·(2D + 1) punktów (512…8192), odwrotna FFT, okno Kaisera β = 4 (β = 8, gdy w filtrze jest brickwall — ok. 80 dB tłumienia), 2D + 1 współczynników wyśrodkowanych w D, więc pre-ring ≤ D. Korekcja wygasa między 0,95·Nyquist a Nyquistem (β i granicę dobrano na zestawie referencyjnym; próbowane β 4…8 i granice 0,9…0,95).
- D = 16 / 32 / 64 / 128 próbek przy 48 kHz (Eco/Normal/High/Max), skalowane z fs.
- Baza korekcji per kanał, a M/S, gdy na parze jest pasmo Mid lub Side; obie realizowane jako FIR 2×2 na historiach kanałów (y_L = P∗x_L + Q∗x_R, P = (C_M + C_S)/2, Q = (C_M − C_S)/2), więc zmiana bazy to zwykły crossfade. Pasmo działające tylko na jednym kanale pary w bazie M/S nie jest korygowane (udokumentowane przybliżenie).
- Odpowiedzi pasm są buforowane i przeliczane tylko po zmianie (pasma Dynamic: przy zmianie wzmocnienia ≥ 0,05 dB); korektor aktualizuje się na stałej siatce próbek (≈ 21 ms, 1024 próbki przy 48 kHz) i przechodzi na nowy filtr crossfadem 5 ms — wynik nie zależy od rozmiaru bloku hosta (NFR-3). Kanały bez pasm są dokładnym opóźnieniem D.
- **Brickwall w Natural Phase**: liniowofazowy FIR tylko tam, gdzie jest bardziej stromy niż IIR 96 dB/oct — od fc ≥ 7·fs/(2D + 1) (≈ 1,3 kHz w Max, 5,2 kHz w Normal przy 48 kHz). Niżej pasmo pozostaje minimalnofazowym IIR 96 dB/oct (z korekcją jak inne pasma): FIR o długości 2D + 1 nie da stromego cięcia w basie bez dużo większej latencji.
- **Analog Feel** używa korektora w High i Max (tabela wyżej); krzywe i nieliniowości modeli — M6 (niżej).

**Zmierzone (PH-1, testy `[PH-1]`; 20 Hz – 0,9·Nyquist, 48 kHz)** — największy błąd względem prototypu analogowego na zestawie: bell 1 kHz +12 dB, bell 10 kHz −9 dB Q 2, bell 16 kHz +6 dB Q 3, high shelf 8 kHz +12 dB, low shelf 100 Hz, high cut 15 kHz, low cut 30 Hz 24 dB/oct, miks 5 pasm:

| Quality (D) | Amplituda | Faza | Kryterium |
|---|---|---|---|
| Eco (16) | 0,21 dB | 1,7° | — |
| Normal (32) | 0,058 dB | 0,17° | ±0,1 dB, ≤ 5° — spełnione |
| High (64) | 0,014 dB | 0,06° | — |
| Max (128) | 0,004 dB | 0,01° | ±0,1 dB, ≤ 2° — spełnione |

Dodatkowo: przez cały silnik (bell 12 kHz + półka, Normal, 1× i 2×) ≤ 0,1 dB i ≤ 5° (z oversamplingiem do krawędzi pasma przepustowego filtrów OS, 0,432·fs); baza M/S (półka na Mid) — błąd ≤ −40 dB względem odpowiedzi jednostkowej; brickwall 3 kHz (Max) symetryczny co do 1e-6, ≤ −60 dB oktawę niżej, płaski ±0,01 dB w paśmie; latencja = D; bez pasm — dokładne opóźnienie.

**PH-2 — CPU i latencja** (`ssc_benchmark 3 modes`; stereo, 8 pasm Bell Static, 48 kHz, bufor 128; pomiar powtórzony w M6 z modelami Analog Feel, jedna sesja, minimum z 3 przebiegów):

| Tryb fazy | Eco | Normal | High | Max |
|---|---|---|---|---|
| Minimum Phase | 0,68 % · 0 smp | 0,68 % · 0 smp | 0,67 % · 0 smp | 0,75 % · 0 smp |
| Natural Phase | 0,80 % · 16 smp | 0,91 % · 32 smp | 1,06 % · 64 smp | 1,52 % · 128 smp |
| Analog Feel N, Clear (krzywe modelu) | 0,69 % · 0 smp | 0,68 % · 0 smp | 2,12 % · 125 smp | 4,44 % · 224 smp |
| Analog Feel N, Warm (krzywe + nasycenie) | 0,99 % · 0 smp | 1,04 % · 0 smp | 2,82 % · 125 smp | 6,15 % · 224 smp |

Błąd odpowiedzi maleje monotonicznie z Quality (tabela PH-1). W Minimum Phase Quality zmieniała w M4 tylko okres aktualizacji współczynników (32/16/8/4 próbki) — koszt prawie stały; od M8 High i Max dodają minimalnofazowy korektor amplitudy (niżej). Analog Feel w High/Max: korektor fazy (D = 64/128) plus wewnętrzny oversampling 2×/4× z filtrami FIR (M6) — stąd latencja 125 i 224 próbek i koszt pasm × 2/× 4.

### Realizacja w M6 i wyniki pomiarów (Analog Feel N / P / S)

**Gdzie działa model** (`filters::analogCurve`, `filters::ShaperKind`, `engine::Engine`; ADR-0009 „Aktualizacja (M6)”):
- Analog Feel to globalny tryb fazy z globalnym wyborem modelu (parametr *Analog Model*; w UI „Analog N / P / S” z opisami bez znaków towarowych w podpowiedzi). Silnik tworzy kopię ustawień pasm „tak jak słychać” (`BandShape::analog` = model). Ta sama kopia trafia do łańcucha IIR, korektora fazy, krzywych Auto Gain i krzywych w UI — rysunek, kompensacja głośności i dźwięk są zgodne.
- **Warstwa 1 — krzywe**: reguły parametryczne nakładane na kształt pasma przed projektem dopasowanym; pasma pozostają w pełni parametryczne.
  - **N**: Q bell łagodnie proporcjonalne (0,75·Q przy 0 dB → Q przy |G| ≥ 15 dB); bell powyżej 5 kHz „ściągany” ku 0 dB (do −25 % wzmocnienia przy 20 kHz — pojemności własne cewek); półki z Q ≥ 1, czyli mały „bump” w przeciwną stronę przy fc (półka +6 dB przy 100 Hz: szczyt +6,4 dB, −0,4 dB przy 200 Hz).
  - **P**: boost półek jako rezonansowa półka LC (Q ≥ 1,4 w Low, ≥ 1,2 w High; półka +6 dB przy 100 Hz: +7,4 dB przy 64 Hz i −1,4 dB przy 156 Hz); tłumienie jako szersza półka bez rezonansu (Q 0,5 — krytycznie tłumiona, praktycznie jak kaskada półek RC 1. rzędu), w basie przy 0,7·fc (bez przerzutu; −3 dB w 0,7·fc przy −6 dB); bell z Q·0,8 (szerokie szczyty pasywne). Tłumienie ma ten sam rząd co boost, więc pasmo Dynamic zachowuje topologię przy przejściu przez 0 dB. Boost i cut w tej samej częstotliwości (dwa pasma) dają znany efekt EQP-1A: +6/−6 dB przy 100 Hz → +4,3 dB przy 71 Hz i dołek −2,5 dB przy 145 Hz.
  - **S**: Q bell proporcjonalne do |gain| (styl serii G): 0,4·Q przy 0 dB → Q przy |G| ≥ 15 dB. Przełącznik bell/shelf „stylu E” to po prostu wybór kształtu pasma.
- **Warstwa 2 — faza**: Eco/Normal — dopasowany IIR (faza minimalna, 0 latencji); High/Max — korektor z Natural Phase (D = 64/128 przy 48 kHz) do fazy prototypu analogowego krzywej modelu.
- **Warstwa 3 — nieliniowości**: ta sama struktura co Character (research/09): do wyjścia dodawane są tylko produkty N(u) wkładu pasma, więc odpowiedź liniowa to dokładnie krzywa modelu, pasmo 0 dB nic nie dodaje, a kanały poza pasmami pozostają nietknięte (CH-2, CH-6). W Analog Feel Character wybiera wariant (CHR-4): Clear — tylko krzywe, Warm — wariant miękki, Sharp — twardy.
  - **N**: wielomian z H2 (asymetria Class A) i H3; przed nieliniowością podbicie basu x = u + k·LP₁₂₀ Hz(u) (k = 1 / 1,5), więc nasycenie rośnie w basie jak w rdzeniu transformatora; produkty przez filtr DC i dolnoprzepustowy 18 kHz.
  - **P**: dominująca H2 (trioda) i miękka kompresja (ujemny człon u³) przy wysokich poziomach; produkty przez filtr DC i dolnoprzepustowy 15 kHz (transformator wyjściowy).
  - **S**: symetryczny wielomian z członem u⁵ — czysto przy poziomie nominalnym, nieparzyste harmoniczne (H3, H5) rosnące stromo w pobliżu „szyn” op-ampa.
  - **Wewnętrzny oversampling**: w High co najmniej 2×, w Max co najmniej 4× (filtry FIR liniowofazowe, łącznie najwyżej 400 kHz; wyższe ustawienie globalne wygrywa). Pasma i kanały bez pasm omijają go bit-exact jak w M4.

**Kalibracja (CHR-4, testy `[CHR-4]`)** — −18 dBFS = +4 dBu, 0 dBFS = +22 dBu; sinus 1 kHz w środku bella +6 dB, Q 1, 48 kHz, bez oversamplingu:

| Model, Character | THD przy −18 dBFS | THD przy 0 dBFS | Harmoniczne przy −18 / 0 dBFS |
|---|---|---|---|
| N Warm | −65,4 dB | −42,0 dB | H2 −65,6 / −47,4 dB, H3 −79,6 / −43,4 dB |
| N Sharp | −59,5 dB | −34,2 dB | H2 −59,8 / −41,4 dB, H3 −71,5 / −35,1 dB |
| P Warm | −56,2 dB | −37,8 dB | H2 −56,2 / −38,1 dB, H3 −84,8 / −48,7 dB |
| P Sharp | −50,2 dB | −31,3 dB | H2 −50,2 / −31,9 dB, H3 −76,3 / −40,1 dB |
| S Warm | −106,7 dB | −49,5 dB | H3 −106,7 / −49,7 dB, H5 −136,5 / −64,4 dB, bez parzystych |
| S Sharp | −97,1 dB | −39,9 dB | H3 −97,1 / −40,0 dB, H5 −126,9 / −54,8 dB |
| N, P, S Clear | < −120 dB | < −120 dB | — |

- N Warm przy −6 dBFS: THD −40,7 dB przy 100 Hz wobec −51,4 dB przy 1 kHz — nasycenie rośnie w basie.
- Charakter jakościowo zgodny z §c: N — H2 przy poziomie nominalnym, H3 przy wysokim; P — H2 dominuje przy każdym poziomie; S — tylko nieparzyste, bardzo czysto przy +4 dBu. Wartości bezwzględne są punktem startowym `[niezweryfikowane]` — porównanie z pomiarami urządzeń i odsłuch — M8. Test pilnuje okna ±3 dB wokół tych wartości.

**Odstępstwa od planu (§c i tabela Quality)** — szczegóły i uzasadnienie w ADR-0009 „Aktualizacja (M6)”:
- Zamiast modeli z pamięcią (state-space/WDF), Newtona–Raphsona i Jiles–Atherton w High/Max: wielomianowe nieliniowości bez pamięci z ADAA 1. rzędu we wszystkich Quality; „pamięć” transformatora N przybliża podbicie basu przed nieliniowością. Quality w Analog Feel zmienia fazę (korektor w High/Max), wewnętrzny oversampling (2×/4×) i okres aktualizacji współczynników.
- Walidacja krzywych względem modeli referencyjnych (np. WDF EQP-1A) z tolerancją ±0,5 dB — nie wykonana; testy `[PH-1]` sprawdzają reguły modeli (bump, Q, wzmocnienie, efekt boost + cut) oraz to, że każdy model zachowuje topologię pasma przy zmianie znaku wzmocnienia (pasmo Dynamic P ściśnięte poniżej 0 dB brzmi jak statyczny projekt przy tym samym wzmocnieniu, ±0,1 dB). Do M8.
- N: HPF 18 dB/oct nie jest wymuszany — nachylenie cięć pozostaje wyborem użytkownika.
- Parametr „P-Atten” (P1) — nie zrealizowany; efekt boost + cut uzyskuje się dwoma pasmami P.

### Realizacja w M8: Minimum Phase High/Max — minimalnofazowy korektor amplitudy

Raport pomiarowy M8 (`ssc_measure`, docs/measurements/filters.md) pokazał, że obiecane w M4 „wewnętrzne 2× dla pasm powyżej fs/8” w Minimum Phase Max nie powstało: Max miał ten sam błąd co Normal (0,71 dB przy 44,1 kHz dla cięcia 15 kHz). Zamiast wewnętrznego oversamplingu pasm (latencja filtrów półpasmowych, koszt) korektor z Natural Phase dostał tryb minimalnofazowy:
- korygowana jest tylko amplituda |T/A| (ta sama siatka, ograniczenie ±12 dB, wygaszanie powyżej 0,95·Nyquista, bez korekcji tam, gdzie pasmo jest poniżej −80 dB);
- filtr FIR o fazie minimalnej budowany jest z niej metodą cepstrum rzeczywistego (log |C| → cepstrum → złożenie na czasy dodatnie → widmo → exp → odpowiedź impulsowa), D + 1 współczynników (65 w High, 129 w Max przy 48 kHz) z prawą połową okna Kaisera β = 4;
- latencja 0; kanały bez pasm przechodzą bit w bit; aktualizacje i crossfade jak w Natural Phase (wynik nie zależy od rozmiaru bloku);
- liczba współczynników nie rośnie z fs (korekcja leży przy Nyquiście bazowym i ma w próbkach podobną długość przy każdej częstotliwości), a kanał, którego korekcja do 20 kHz jest mniejsza niż 0,02 dB (wysokie fs, pasma daleko od Nyquista), nie jest filtrowany — pierwsza wersja ze współczynnikami skalowanymi z fs podniosła najgorszy przypadek (9.1.6, 192 kHz, Max) z ok. 80 % do 132 % rdzenia, po zmianie znów ok. 80–90 %.

Wyniki (`[PH-1]`, docs/measurements/filters.md; 20 Hz – 20 kHz, poziomy powyżej −30 dB): największy błąd amplitudy względem prototypu przy 44,1 kHz — Normal 0,84 dB (sam dopasowany IIR), **High 0,004 dB, Max 0,001 dB**. Faza jest minimalna dla analogowej amplitudy: poniżej ok. fs/4 bliska fazie prototypu, bliżej Nyquista inna (do ~43° przy 19,7 kHz i 44,1 kHz dla półki −9 dB przy 15 kHz) — tego przy zerowej latencji nie da się uniknąć; fazę prototypu w całym paśmie daje Natural Phase (latencja D). Względem samego IIR faza zmienia się najwyżej o ~6° przy samym Nyquiście. CPU (`ssc_benchmark 3 modes`, stereo, 8 pasm): Minimum Phase Normal 0,69 %, High 1,00 %, Max 1,24 % (wcześniej ok. 0,7 %). Normal pozostaje samym IIR, żeby domyślne ustawienie spełniało budżet NFR-1 z zapasem.

## d) Kryteria akceptacji

| ID | Kryterium | Metoda |
|---|---|---|
| PH-1 | Minimum Phase: Eco/Normal — obwiednia błędu amplitudy dopasowanego IIR jak w research/05 §d (BAND-2); High/Max — ±0,1 dB do 20 kHz przy 44,1 kHz bez latencji (minimalnofazowy korektor amplitudy, M8) | test jednostkowy, docs/measurements/filters.md |
| PH-1 | Natural Phase: amplituda ±0,1 dB; błąd fazy vs prototyp ≤ 5° (Normal) i ≤ 2° (Max) w 20 Hz–0,9·Nyquist; latencja = D; pre-ring (kryterium −60 dB) ≤ D | test jednostkowy |
| PH-1 | Analog Feel: odpowiedź dla ustawień referencyjnych ±0,5 dB względem krzywych referencyjnych (np. model WDF EQP-1A); THD w punktach kalibracji w oknie docelowym ±3 dB | test pomiarowy |
| PH-2 | Tabela CPU i latencji wszystkich kombinacji tryb × Quality wygenerowana przez benchmark; błąd odpowiedzi maleje monotonicznie z Quality | benchmark |
| PH-3 | Przegląd architektury: ścieżka FIR (korektor/splot) gotowa na Linear Phase | przegląd kodu |
