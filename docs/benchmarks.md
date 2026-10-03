# Wydajność (NFR-1)

Pomiar: `ssc_benchmark 10` (narzędzie w `tools/`, buduje się z `-DSSC_BUILD_TOOLS=ON`). Silnik DSP bez GUI, różowy szum, bufor 128 próbek, pasma Bell rozłożone co 0,9 oktawy (±4 dB); dla Spectral próg Relative +6 dB. Wynik to czas przetwarzania w procentach czasu rzeczywistego jednego rdzenia.

Maszyna referencyjna tego pomiaru: kontener CI/rozwojowy, Intel Xeon @ 2,1 GHz (jeden rdzeń), GCC 13, `-O3` (Release). Pomiary na Apple M1 i współczesnym x86-64 (referencja NFR-1) — M8.

| Configuration (48 kHz unless noted, buffer 128) | CPU (% of one core) |
|---|---|
| stereo, 8 Static, Normal (NFR-1 ≤ 1 %) | 0.65 |
| stereo, 24 Static, Normal | 1.53 |
| stereo, 8 Dynamic, Normal | 2.38 |
| stereo, 8 Spectral, Normal (NFR-1 ≤ 5 %) | 2.70 |
| stereo, 8 Spectral, Normal, Auto Gain | 2.98 |
| 7.1.4, 8 Spectral, Normal (NFR-1 ≤ 20 %) | 14.43 |
| 9.1.6, 192 kHz, 8 Spectral, Max (worst case) | 60.26 |

## Ocena względem NFR-1

| Cel NFR-1 | Wynik | Status |
|---|---|---|
| stereo, 8 pasm Static, Normal ≤ 1 % | patrz tabela (≈ 0,6 %) | spełnione |
| stereo, 8 pasm Spectral, Normal ≤ 5 % | ≈ 2,6 % | spełnione |
| 7.1.4, 8 pasm Spectral, Normal ≤ 20 % | ≈ 15 % | spełnione |
| najgorszy przypadek (9.1.6, 192 kHz, Max) bez awarii | ≈ 57 % (bez oversamplingu — M4) | działa; ostrzeżenie w UI — M7 |

## Optymalizacje (M3)
- Pasmo wyłączone i wygaszone nie kosztuje nic; pasmo w stanie ustalonym nie liczy funkcji przestępnych (wcześniej `exp`/`log` wygładzania w każdym bloku kontrolnym każdego z 24 pasm).
- Linie (lanes) wyznaczane raz na wywołanie, bez kopiowania projektu filtra w każdym bloku kontrolnym.
- Stan ustalony: współczynniki SVF przygotowane raz na blok (bez dzielenia w każdej próbce); wynik identyczny co do bitu.
- Stan (procesory pasm, silnik spektralny) na stercie: obiekt silnika mieści się na stosie 1 MB (Windows).
