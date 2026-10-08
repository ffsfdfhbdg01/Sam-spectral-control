# Latencja

Wygenerowane przez `ssc_measure` (2026-10-08) z rdzenia DSP w wersji 1.0.0 — te same klasy co we wtyczce, sygnał przez cały silnik (`Engine`). Wymagania: PH-2, OS-3, OS-4, DYN-8, TST-1 (latencja raportowana = zmierzona).

Latencja raportowana hostowi i zmierzona: środek ciężkości odpowiedzi impulsowej przez pasmo 0 dB (dla filtrów o fazie liniowej dokładnie równy opóźnieniu; dla półpasmowych IIR oversamplingu w Minimum Phase — w granicach ±0,5/k próbki). 48 kHz, Max Lookahead 0 ms (każda milisekunda lookahead dodaje 48 próbek).

## Tryby fazy i Quality (bez oversamplingu, bez pasm Spectral)

| Tryb fazy | Eco | Normal | High | Max |
|---|---|---|---|---|
| Minimum Phase | 0 smp (0,00 ms), zmierzone 0,00 | 0 smp (0,00 ms), zmierzone 0,00 | 0 smp (0,00 ms), zmierzone 0,00 | 0 smp (0,00 ms), zmierzone 0,00 |
| Natural Phase | 16 smp (0,33 ms), zmierzone 16,00 | 32 smp (0,67 ms), zmierzone 32,00 | 64 smp (1,33 ms), zmierzone 64,00 | 128 smp (2,67 ms), zmierzone 128,00 |
| Analog Feel | 0 smp (0,00 ms), zmierzone 0,00 | 0 smp (0,00 ms), zmierzone 0,00 | 125 smp (2,60 ms), zmierzone 125,00 | 224 smp (4,67 ms), zmierzone 224,00 |

Analog Feel w High i Max działa z wewnętrznym oversamplingiem 2× / 4× i korektorem fazy (research/08), stąd latencja.

## Oversampling (Quality Normal)

| Tryb fazy (filtry OS) | 2× | 4× | 8× |
|---|---|---|---|
| Minimum Phase (IIR) | 4 smp, zmierzone 4,02 | 4 smp, zmierzone 4,10 | 5 smp, zmierzone 5,00 |
| Natural Phase (FIR + korektor) | 75 smp, zmierzone 75,00 | 81 smp, zmierzone 81,00 | 83 smp, zmierzone 83,00 |

## Pasma Spectral (silnik STFT, Minimum Phase)

| Częstotliwość próbkowania | Eco | Normal | High | Max |
|---|---|---|---|---|
| 44,1 kHz | 2048 smp (46,4 ms) | 4096 smp (92,9 ms) | 8192 smp (185,8 ms) | 8192 smp (185,8 ms) |
| 48,0 kHz | 2048 smp (42,7 ms) | 4096 smp (85,3 ms) | 8192 smp (170,7 ms) | 8192 smp (170,7 ms) |
| 96,0 kHz | 4096 smp (42,7 ms) | 8192 smp (85,3 ms) | 16384 smp (170,7 ms) | 16384 smp (170,7 ms) |
| 192,0 kHz | 8192 smp (42,7 ms) | 16384 smp (85,3 ms) | 32768 smp (170,7 ms) | 32768 smp (170,7 ms) |

Zmierzona latencja pasm Spectral jest równa raportowanej (różnica < 0,001 próbki), więc w tabeli podana jest tylko raz.

## Render offline z osobnym oversamplingiem (OS-4)

Czas rzeczywisty 1×, render 8×: obie konfiguracje raportują większą latencję (krótszy tor dopełniony opóźnieniem).

| Tryb fazy | Czas rzeczywisty | Render offline |
|---|---|---|
| Minimum Phase | 5 smp, zmierzone 5,00 | 5 smp, zmierzone 5,00 |
| Natural Phase | 83 smp, zmierzone 83,00 | 83 smp, zmierzone 83,00 |
