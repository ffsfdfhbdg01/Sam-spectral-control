# CPU (NFR-1, PH-2)

Pomiar: `ssc_benchmark` (tools/Benchmark.cpp) — silnik DSP bez GUI, różowy szum, bufor 128 próbek, pasma Bell rozłożone co 0,9 oktawy (±4 dB); w trybie Spectral próg Relative +6 dB. Wynik to czas przetwarzania w procentach czasu rzeczywistego jednego rdzenia. Kompilacja: GCC 13.3, `-O3`, M8 (wersja 1.0.0). Host: kontener rozwojowy, Intel Xeon @ 2,8 GHz (współdzielony; wyniki zmieniają się między sesjami o ±20–40 %, porównania wersji robimy naprzemiennie w jednej sesji — docs/benchmarks.md). Pomiary na referencyjnym Apple M1 i współczesnym x86-64 (NFR-1) wymagają tych maszyn i nie zostały wykonane w tym środowisku.

## Konfiguracje NFR-1 (`ssc_benchmark 10`)

| Configuration (48 kHz unless noted, buffer 128) | CPU (% of one core) |
|---|---|
| stereo, 8 Static, Normal (NFR-1 ≤ 1 %) | 0,69 |
| stereo, 24 Static, Normal | 2,02 |
| stereo, 8 Static Warm, Normal | 0,91 |
| stereo, 8 Static Sharp, Normal | 1,05 |
| stereo, 8 Dynamic, Normal | 3,11 |
| stereo, 8 Spectral, Normal (NFR-1 ≤ 5 %) | 4,11 |
| stereo, 8 Spectral, Normal, Auto Gain | 4,75 |
| 7.1.4, 8 Spectral, Normal (NFR-1 ≤ 20 %) | 21,70 |
| 9.1.6, 192 kHz, 8 Spectral, Max (worst case) | 88,72 |
| stereo, 8 Static Warm, Natural Phase, 4x | 4,95 |
| 7.1.4, 8 Dynamic, Natural Phase Max, 2x | 40,50 |

Ocena: stereo 8 × Static Normal (≤ 1 %) i stereo 8 × Spectral Normal (≤ 5 %) — spełnione; 7.1.4 8 × Spectral Normal (≤ 20 %) — na granicy na tym wolnym, współdzielonym hoście (14–23 % zależnie od sesji); najgorszy przypadek działa bez awarii (oversampling przy 192 kHz ograniczony do 2×).

## Tryby fazy × Quality i oversampling (`ssc_benchmark 3 modes`, stereo, 8 pasm Static)

| Phase mode | Eco | Normal | High | Max |
|---|---|---|---|---|
| Minimum Phase | 0,71 % · 0 smp | 0,77 % · 0 smp | 1,00 % · 0 smp | 1,17 % · 0 smp |
| Natural Phase | 0,80 % · 16 smp | 0,94 % · 32 smp | 1,06 % · 64 smp | 1,55 % · 128 smp |
| Analog Feel N, Clear (curves) | 0,70 % · 0 smp | 0,69 % · 0 smp | 2,10 % · 125 smp | 4,74 % · 224 smp |
| Analog Feel N, Warm (curves + saturation) | 1,32 % · 0 smp | 1,04 % · 0 smp | 3,07 % · 125 smp | 6,89 % · 224 smp |

| Oversampling (Normal) | Minimum Phase (IIR) | Natural Phase (FIR) |
|---|---|---|
| 1x | 0,68 % · 0 smp | 0,87 % · 32 smp |
| 2x | 1,55 % · 4 smp | 1,88 % · 75 smp |
| 4x | 3,05 % · 4 smp | 3,68 % · 81 smp |
| 8x | 6,20 % · 5 smp | 8,30 % · 83 smp |

W Minimum Phase High i Max działa od M8 minimalnofazowy korektor amplitudy (research/08 „Realizacja w M8”): koszt ok. +0,3–0,5 % przy zerowej latencji. Kolejność kosztów jest stała między sesjami; wartości bezwzględne zależą od hosta.
