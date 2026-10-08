# Dokładność filtrów

Wygenerowane przez `ssc_measure` (2026-10-08) z rdzenia DSP w wersji 1.0.0 — te same klasy co we wtyczce, sygnał przez cały silnik (`Engine`). Wymagania: BAND-2, BAND-3, PH-1, TST-1.

Metoda: odpowiedź impulsowa pasma przez silnik (po 0,25 s ciszy), widmo FFT z dopełnieniem zerami, porównanie amplitudy z prototypem analogowym pasma (`BandDesign::analogResponse`). W tabelach największy błąd |dB| w 20 Hz – min(20 kHz; 0,45·fs) tam, gdzie prototyp jest powyżej −30 dB (bez dna cięć i notcha). Character Clear, oversampling wyłączony.

Kryterium TST-1 / PH-1: ±0,1 dB do 20 kHz przy 44,1 kHz — w Natural Phase i w Minimum Phase z Quality Max (wewnętrzne 2× dla pasm powyżej fs/8). Minimum Phase Normal ma udokumentowaną obwiednię błędu filtrów dopasowanych (research/05 §c): przy pasmach blisko Nyquista błąd rośnie.

## Minimum Phase, Quality Normal

| Pasmo | 44,1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| Bell 100 Hz, +12 dB, Q 1 | 0,000 dB | 0,000 dB | 0,000 dB |
| Bell 1 kHz, −12 dB, Q 2 | 0,005 dB | 0,004 dB | 0,001 dB |
| Bell 5 kHz, +6 dB, Q 4 | 0,024 dB | 0,020 dB | 0,005 dB |
| Bell 12 kHz, +9 dB, Q 1.5 | 0,559 dB | 0,496 dB | 0,143 dB |
| Bell 16 kHz, −6 dB, Q 3 | 0,455 dB | 0,374 dB | 0,075 dB |
| Low Shelf 100 Hz, +6 dB | 0,000 dB | 0,000 dB | 0,000 dB |
| High Shelf 8 kHz, +6 dB | 0,055 dB | 0,042 dB | 0,003 dB |
| High Shelf 15 kHz, −9 dB | 0,322 dB | 0,299 dB | 0,022 dB |
| Low Cut 50 Hz, 24 dB/okt. | 0,001 dB | 0,001 dB | 0,001 dB |
| High Cut 15 kHz, 12 dB/okt. | 0,708 dB | 0,723 dB | 0,054 dB |
| Notch 1 kHz, Q 10 | 0,000 dB | 0,000 dB | 0,000 dB |
| Band Pass 2 kHz, Q 2 | 0,385 dB | 0,326 dB | 0,082 dB |
| Tilt Shelf 1 kHz, +6 dB | 0,000 dB | 0,000 dB | 0,000 dB |

Największy błąd przy 44,1 kHz: **0,708 dB** — kryterium ±0,1 dB nie dotyczy tego trybu (patrz wyżej).

## Minimum Phase, Quality High

| Pasmo | 44,1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| Bell 100 Hz, +12 dB, Q 1 | 0,000 dB | 0,000 dB | 0,000 dB |
| Bell 1 kHz, −12 dB, Q 2 | 0,005 dB | 0,004 dB | 0,001 dB |
| Bell 5 kHz, +6 dB, Q 4 | 0,002 dB | 0,002 dB | 0,005 dB |
| Bell 12 kHz, +9 dB, Q 1.5 | 0,001 dB | 0,001 dB | 0,001 dB |
| Bell 16 kHz, −6 dB, Q 3 | 0,003 dB | 0,002 dB | 0,001 dB |
| Low Shelf 100 Hz, +6 dB | 0,000 dB | 0,000 dB | 0,000 dB |
| High Shelf 8 kHz, +6 dB | 0,000 dB | 0,000 dB | 0,003 dB |
| High Shelf 15 kHz, −9 dB | 0,002 dB | 0,001 dB | 0,000 dB |
| Low Cut 50 Hz, 24 dB/okt. | 0,001 dB | 0,001 dB | 0,001 dB |
| High Cut 15 kHz, 12 dB/okt. | 0,005 dB | 0,002 dB | 0,000 dB |
| Notch 1 kHz, Q 10 | 0,000 dB | 0,000 dB | 0,000 dB |
| Band Pass 2 kHz, Q 2 | 0,001 dB | 0,001 dB | 0,001 dB |
| Tilt Shelf 1 kHz, +6 dB | 0,000 dB | 0,000 dB | 0,000 dB |

Największy błąd przy 44,1 kHz: **0,005 dB** — kryterium ±0,1 dB spełnione.

## Minimum Phase, Quality Max

| Pasmo | 44,1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| Bell 100 Hz, +12 dB, Q 1 | 0,000 dB | 0,000 dB | 0,000 dB |
| Bell 1 kHz, −12 dB, Q 2 | 0,005 dB | 0,004 dB | 0,001 dB |
| Bell 5 kHz, +6 dB, Q 4 | 0,001 dB | 0,000 dB | 0,005 dB |
| Bell 12 kHz, +9 dB, Q 1.5 | 0,000 dB | 0,000 dB | 0,000 dB |
| Bell 16 kHz, −6 dB, Q 3 | 0,001 dB | 0,001 dB | 0,000 dB |
| Low Shelf 100 Hz, +6 dB | 0,000 dB | 0,000 dB | 0,000 dB |
| High Shelf 8 kHz, +6 dB | 0,000 dB | 0,000 dB | 0,003 dB |
| High Shelf 15 kHz, −9 dB | 0,000 dB | 0,000 dB | 0,000 dB |
| Low Cut 50 Hz, 24 dB/okt. | 0,001 dB | 0,001 dB | 0,001 dB |
| High Cut 15 kHz, 12 dB/okt. | 0,001 dB | 0,000 dB | 0,000 dB |
| Notch 1 kHz, Q 10 | 0,000 dB | 0,000 dB | 0,000 dB |
| Band Pass 2 kHz, Q 2 | 0,000 dB | 0,000 dB | 0,000 dB |
| Tilt Shelf 1 kHz, +6 dB | 0,000 dB | 0,000 dB | 0,000 dB |

Największy błąd przy 44,1 kHz: **0,005 dB** — kryterium ±0,1 dB spełnione.

## Natural Phase, Quality Normal

| Pasmo | 44,1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| Bell 100 Hz, +12 dB, Q 1 | 0,000 dB | 0,000 dB | 0,000 dB |
| Bell 1 kHz, −12 dB, Q 2 | 0,002 dB | 0,002 dB | 0,000 dB |
| Bell 5 kHz, +6 dB, Q 4 | 0,006 dB | 0,005 dB | 0,001 dB |
| Bell 12 kHz, +9 dB, Q 1.5 | 0,008 dB | 0,004 dB | 0,001 dB |
| Bell 16 kHz, −6 dB, Q 3 | 0,012 dB | 0,009 dB | 0,001 dB |
| Low Shelf 100 Hz, +6 dB | 0,000 dB | 0,000 dB | 0,000 dB |
| High Shelf 8 kHz, +6 dB | 0,000 dB | 0,000 dB | 0,000 dB |
| High Shelf 15 kHz, −9 dB | 0,013 dB | 0,006 dB | 0,000 dB |
| Low Cut 50 Hz, 24 dB/okt. | 0,001 dB | 0,001 dB | 0,001 dB |
| High Cut 15 kHz, 12 dB/okt. | 0,057 dB | 0,025 dB | 0,005 dB |
| Notch 1 kHz, Q 10 | 0,000 dB | 0,000 dB | 0,000 dB |
| Band Pass 2 kHz, Q 2 | 0,015 dB | 0,010 dB | 0,007 dB |
| Tilt Shelf 1 kHz, +6 dB | 0,000 dB | 0,000 dB | 0,000 dB |

Największy błąd przy 44,1 kHz: **0,057 dB** — kryterium ±0,1 dB spełnione.

## Natural Phase, Quality Max

| Pasmo | 44,1 kHz | 48 kHz | 96 kHz |
|---|---|---|---|
| Bell 100 Hz, +12 dB, Q 1 | 0,000 dB | 0,000 dB | 0,000 dB |
| Bell 1 kHz, −12 dB, Q 2 | 0,000 dB | 0,000 dB | 0,000 dB |
| Bell 5 kHz, +6 dB, Q 4 | 0,001 dB | 0,000 dB | 0,000 dB |
| Bell 12 kHz, +9 dB, Q 1.5 | 0,000 dB | 0,000 dB | 0,000 dB |
| Bell 16 kHz, −6 dB, Q 3 | 0,001 dB | 0,001 dB | 0,000 dB |
| Low Shelf 100 Hz, +6 dB | 0,000 dB | 0,000 dB | 0,000 dB |
| High Shelf 8 kHz, +6 dB | 0,000 dB | 0,000 dB | 0,000 dB |
| High Shelf 15 kHz, −9 dB | 0,001 dB | 0,000 dB | 0,000 dB |
| Low Cut 50 Hz, 24 dB/okt. | 0,001 dB | 0,001 dB | 0,001 dB |
| High Cut 15 kHz, 12 dB/okt. | 0,003 dB | 0,002 dB | 0,000 dB |
| Notch 1 kHz, Q 10 | 0,000 dB | 0,000 dB | 0,000 dB |
| Band Pass 2 kHz, Q 2 | 0,001 dB | 0,002 dB | 0,001 dB |
| Tilt Shelf 1 kHz, +6 dB | 0,000 dB | 0,000 dB | 0,000 dB |

Największy błąd przy 44,1 kHz: **0,003 dB** — kryterium ±0,1 dB spełnione.

