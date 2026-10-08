# Wydajność (NFR-1)

> Aktualny pomiar M8 (wersja 1.0.0, GCC 13, Xeon @ 2,8 GHz) i ocena NFR-1: [measurements/cpu.md](measurements/cpu.md). Niżej historia pomiarów i optymalizacji z etapów M3–M7.

Pomiar: `ssc_benchmark 10` (narzędzie w `tools/`, buduje się z `-DSSC_BUILD_TOOLS=ON`). Silnik DSP bez GUI, różowy szum, bufor 128 próbek, pasma Bell rozłożone co 0,9 oktawy (±4 dB); dla Spectral próg Relative +6 dB. Wynik to czas przetwarzania w procentach czasu rzeczywistego jednego rdzenia.

Maszyna referencyjna tego pomiaru: kontener CI/rozwojowy, Intel Xeon @ 2,1 GHz (jeden rdzeń), GCC 13, `-O3` (Release). Pomiary na Apple M1 i współczesnym x86-64 (referencja NFR-1) — M8.

**Uwaga o zmienności**: wydajność tego hosta zmienia się między sesjami o ±40 % (obciążenie współdzielonej maszyny). Porównania wersji robimy więc naprzemiennie w jednej sesji: wersje M3 i M4 skompilowane tymi samymi flagami dały te same czasy (np. stereo 8 Spectral 3,84 / 4,06 % i 4,30 / 3,85 %; 7.1.4 20,3 / 20,2 % i 20,9 / 20,0 %) — integracja oversamplingu i Natural Phase nie spowolniła ścieżki bez nich.

Pomiar M4 (jedna sesja, minimum z 3 przebiegów po 5 s):

| Configuration (48 kHz unless noted, buffer 128) | CPU (% of one core) |
|---|---|
| stereo, 8 Static, Normal (NFR-1 ≤ 1 %) | 0.67 |
| stereo, 24 Static, Normal | 1.92 |
| stereo, 8 Static Warm, Normal | 0.87 |
| stereo, 8 Static Sharp, Normal | 0.98 |
| stereo, 8 Dynamic, Normal | 3.13 |
| stereo, 8 Spectral, Normal (NFR-1 ≤ 5 %) | 3.94 |
| stereo, 8 Spectral, Normal, Auto Gain | 4.61 |
| 7.1.4, 8 Spectral, Normal (NFR-1 ≤ 20 %) | 20.43 |
| 9.1.6, 192 kHz, 8 Spectral, Max (worst case) | 79.82 |
| stereo, 8 Static Warm, Natural Phase, 4x | 4.18 |
| 7.1.4, 8 Dynamic, Natural Phase Max, 2x | 38.72 |

Pomiar M3 (wcześniejsza, szybsza sesja tego samego hosta): stereo 8 Static 0,65 %, 8 Spectral 2,70 %, 7.1.4 8 Spectral 14,4 %, 9.1.6 192 kHz Max 60,3 %.

Tryby fazy × Quality i oversampling (PH-2): `ssc_benchmark 3 modes` — tabele w research/08 (pomiar M6, z wierszami Analog Feel) i research/10 („Realizacja w M4”).

## Ocena względem NFR-1

| Cel NFR-1 | Wynik | Status |
|---|---|---|
| stereo, 8 pasm Static, Normal ≤ 1 % | patrz tabela (≈ 0,6 %) | spełnione |
| stereo, 8 pasm Spectral, Normal ≤ 5 % | ≈ 2,6 % | spełnione |
| 7.1.4, 8 pasm Spectral, Normal ≤ 20 % | 14–20 % na tym hoście (zależnie od sesji) | spełnione na granicy na wolnym hoście CI; pomiar na maszynie referencyjnej — M8 |
| najgorszy przypadek (9.1.6, 192 kHz, Max) bez awarii | 60–80 % zależnie od sesji (oversampling przy 192 kHz ograniczony do 2×) | działa; ostrzeżenie w UI — M7 |

## Optymalizacje (M3)
- Pasmo wyłączone i wygaszone nie kosztuje nic; pasmo w stanie ustalonym nie liczy funkcji przestępnych (wcześniej `exp`/`log` wygładzania w każdym bloku kontrolnym każdego z 24 pasm).
- Linie (lanes) wyznaczane raz na wywołanie, bez kopiowania projektu filtra w każdym bloku kontrolnym.
- Stan ustalony: współczynniki SVF przygotowane raz na blok (bez dzielenia w każdej próbce); wynik identyczny co do bitu.
- Stan (procesory pasm, silnik spektralny) na stercie: obiekt silnika mieści się na stosie 1 MB (Windows).

## Koszt trybów M4
- Natural Phase: korektor FIR (2D + 1 współczynników na kanał) + aktualizacje co ≈ 21 ms tylko dla zmienionych pasm; stereo +0,1…0,8 % zależnie od Quality.
- Oversampling: koszt pasm rośnie proporcjonalnie do współczynnika; filtry półpasmowe IIR są tanie, FIR (Natural Phase) droższy przy 4× i 8×.
- Character: Warm +30…35 %, Sharp +45 % kosztu pasm Clear (Sharp: druga sekcja krzywej 4. rzędu); patrz odstępstwo CHR w research/09.
- Kandydaci do optymalizacji w M8: SIMD na liniach stereo/surround w pasmach, splot FFT dla długich korektorów (Max przy 96/192 kHz).

## Koszt trybów M6 (Analog Feel)
- Wspólna implementacja nieliniowości (9 rodzajów `ShaperKind`) ma koszt M4: porównanie naprzemienne M5/M6 w jednej sesji (minimum z 6 przebiegów) — 8 pasm Clear 0,68/0,68 %, Warm 0,88/0,90 %, Sharp 0,99/1,02 %. Współczynniki całki w ADAA są liczone raz przy przygotowaniu (bez dzielenia w próbce), a człon u⁵ tylko w modelu S.
- Analog Feel N, 8 pasm stereo: Eco/Normal 0,68 % (same krzywe), 1,0 % z nasyceniem Warm; High 2,1 / 2,8 % (korektor D = 64 i wewnętrzne 2× FIR, 125 próbek latencji); Max 4,4 / 6,2 % (D = 128 i 4×, 224 próbki). Tabela: research/08.

## Koszt mierników THD+N i aliasingu (M7, MTR-2)
`ssc_benchmark 8 meters`: dwa silniki (mierniki wyłączone i włączone) przetwarzają na przemian te same bloki, więc wynik jest odporny na obciążenie maszyny (rozrzut ±2 %); kolumna „wątek analizy” — praca wątku mierników przy analizie wszystkich pasm w każdej migawce (sam wątek robi najwyżej 8 analiz na migawkę).

| Konfiguracja (48 kHz, bufor 128) | Mierniki wył. | Mierniki wł. | Narzut w wątku audio | Wątek analizy |
|---|---|---|---|---|
| stereo, 8 Static Warm | 1,08 % | 1,12 % | +3,5 % DSP | 1,22 % rdzenia |
| stereo, 24 Static Warm | 2,64 % | 2,73 % | +3,6 % DSP | 3,35 % rdzenia |
| stereo, 8 Dynamic Sharp | 4,38 % | 4,59 % | +4,7 % DSP | 0,89 % rdzenia |
| stereo, 8 Static Warm, 4× | 4,42 % | 4,49 % | +1,5 % DSP | 1,21 % rdzenia |
| 7.1.4, 8 Dynamic Warm | 18,70 % | 19,43 % | +3,9 % DSP | 1,19 % rdzenia |

- Mierniki działają tylko przy otwartym edytorze; zamknięty edytor = zero narzutu.
- Narzut w wątku audio pochodzi z okien nagrywania (19 % czasu przy 1×: 1152 próbki na 125 ms) i z sumowania energii produktów w zmiennych lokalnych pętli. Pierwsza wersja (nagrywanie ciągłe, pola rejestratora aktualizowane w pamięci na każdą próbkę) kosztowała +15…38 %.
