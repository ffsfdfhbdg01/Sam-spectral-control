# Auto Gain

Wygenerowane przez `ssc_measure` (2026-10-08) z rdzenia DSP w wersji 1.0.0 — te same klasy co we wtyczce, sygnał przez cały silnik (`Engine`). Wymagania: AG-3, AG-6.

Metoda: 20 s różowego szumu (−20 dBFS RMS) przez EQ z globalnym Auto Gain (Speed Stable), ostatnie 4 s porównane z wejściem: zmiana głośności według modelu wtyczki (ISO 226 i sony dla Fletcher; energia RMS dla Digital) i według ITU-R BS.1770-4 (K-weighting, bez bramkowania) jako niezależny punkt odniesienia. Bez Auto Gain zmiana byłaby równa „korekcji” z przeciwnym znakiem.

| EQ | Ważenie | Korekcja | Zmiana wg modelu | Zmiana wg BS.1770 |
|---|---|---|---|---|
| Bell 3 kHz +6 dB, Q 1 | Fletcher | −2,10 dB | −0,07 dB | −0,05 LU |
| Bell 3 kHz +6 dB, Q 1 | Digital | −1,41 dB | −0,02 dB | 0,63 LU |
| Low Shelf 100 Hz +6 dB | Fletcher | −0,18 dB | 0,01 dB | 0,47 LU |
| Low Shelf 100 Hz +6 dB | Digital | −2,26 dB | 0,12 dB | −1,59 LU |
| High Shelf 8 kHz −6 dB | Fletcher | 0,74 dB | 0,03 dB | −0,44 LU |
| High Shelf 8 kHz −6 dB | Digital | 0,49 dB | 0,01 dB | −0,69 LU |
| Low Cut 200 Hz 24 dB/okt. | Fletcher | 0,53 dB | 0,01 dB | −0,05 LU |
| Low Cut 200 Hz 24 dB/okt. | Digital | 1,92 dB | −0,08 dB | 1,33 LU |
| Bell 250 Hz −4 dB + Bell 4 kHz +5 dB | Fletcher | −0,70 dB | −0,02 dB | 0,32 LU |
| Bell 250 Hz −4 dB + Bell 4 kHz +5 dB | Digital | −0,33 dB | 0,01 dB | 0,69 LU |

Kryterium AG-6: zmiana wg modelu ≤ ±0,5 dB (Fletcher) i ≤ ±0,2 dB (Digital). BS.1770 mierzy głośność inaczej niż model (K-weighting zamiast konturów ISO 226), więc różnice rzędu 1 LU przy mocnych zmianach basu lub góry są oczekiwane.
