# THD+N i aliasing

Wygenerowane przez `ssc_measure` (2026-10-08) z rdzenia DSP w wersji 1.0.0 — te same klasy co we wtyczce, sygnał przez cały silnik (`Engine`). Wymagania: CHR-3, CHR-4, OS-1, OS-4, MTR-3.

Metoda: sinus przez silnik (po 0,5 s ustalania), okno 65 536 próbek z całkowitą liczbą okresów tonu i wszystkich produktów, DFT w podwójnej precyzji. THD — harmoniczne poniżej Nyquista, aliasing — harmoniczne odbite od Nyquista (do 64. harmonicznej), THD+N — wszystko poza składową stałą i podstawową. Wartości względem tonu podstawowego, 48 kHz.

## Character pasma (Bell 1 kHz, +6 dB, Q 1; ton 1 kHz; Minimum Phase, bez oversamplingu)

| Character | Poziom | THD+N | THD | Aliasing |
|---|---|---|---|---|
| Clear | −18 dBFS | −250,4 dB | −300,0 dB | −280,3 dB |
| Clear | −6 dBFS | −250,4 dB | −300,0 dB | −280,2 dB |
| Clear | 0 dBFS | −250,4 dB | −300,0 dB | −280,3 dB |
| Warm | −18 dBFS | −70,2 dB | −70,2 dB | −241,2 dB |
| Warm | −6 dBFS | −58,2 dB | −58,2 dB | −229,2 dB |
| Warm | 0 dBFS | −52,2 dB | −52,2 dB | −223,1 dB |
| Sharp | −18 dBFS | −88,2 dB | −88,2 dB | −280,2 dB |
| Sharp | −6 dBFS | −64,2 dB | −64,2 dB | −280,0 dB |
| Sharp | 0 dBFS | −52,2 dB | −52,2 dB | −280,2 dB |

## Oversampling (Warm Bell 10 kHz, +12 dB, Q 1; ton 10 kHz, 0 dBFS — przypadek OS-1)

| Tryb fazy (filtry OS) | 1× | 2× | 4× | 8× |
|---|---|---|---|---|
| Minimum Phase (IIR) | aliasing −66,5 dB, THD −48,4 dB | aliasing −163,3 dB, THD −45,4 dB | aliasing −163,9 dB, THD −44,9 dB | aliasing −162,3 dB, THD −44,8 dB |
| Natural Phase (FIR) | aliasing −65,9 dB, THD −47,8 dB | aliasing −172,3 dB, THD −45,2 dB | aliasing −173,0 dB, THD −44,9 dB | aliasing −173,4 dB, THD −44,8 dB |

Kryterium OS-1 (aliasing): 1× ≤ −80 dB nie jest osiągalne przy +12 dB i 0 dBFS w samym ADAA (odstępstwo opisane w research/10); 2× ≤ −95 dB, 4× ≤ −110 dB, 8× ≤ −120 dB — spełnione. Podpowiedź OS-5 i oversampling renderu offline (OS-4) pozwalają z tego korzystać.

## Analog Feel (Bell 1 kHz, +6 dB, Q 1; ton 1 kHz; Quality Normal)

| Model | Character | THD+N przy −18 dBFS | THD+N przy 0 dBFS |
|---|---|---|---|
| N | Warm | −65,4 dB | −42,0 dB |
| N | Sharp | −59,5 dB | −34,2 dB |
| P | Warm | −56,2 dB | −37,8 dB |
| P | Sharp | −50,2 dB | −31,3 dB |
| S | Warm | −106,7 dB | −49,5 dB |
| S | Sharp | −97,1 dB | −39,9 dB |

## Tor liniowy (Clear)

- Minimum Phase, 2×, trzy pasma Clear, ton 997 Hz −6 dBFS: THD+N −246,4 dB.
- Natural Phase, 2×, trzy pasma Clear, ton 997 Hz −6 dBFS: THD+N −246,6 dB.
