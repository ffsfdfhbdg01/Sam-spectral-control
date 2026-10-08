# Podpowiedzi rezonansów

Wygenerowane przez `ssc_measure` (2026-10-08) z rdzenia DSP w wersji 1.0.0 — te same klasy co we wtyczce, sygnał przez cały silnik (`Engine`). Wymaganie: VIS-5.

Metoda: 24 klipy po 12 s syntetycznego materiału muzycznego (podkład z różowego szumu, perkusja, melodia z vibrato, akordy) z dwoma rezonansami o znanych parametrach (filtry peaking: 100 Hz – 8 kHz, +6…+15 dB, Q 5…30) oraz klipy czystego materiału tonalnego bez rezonansów (fałszywe alarmy). Analiza jak we wtyczce (`ResonanceHints`, czułość Medium), po 2 s rozgrzewania. Trafienie: podpowiedź w ±1/12 oktawy od rezonansu; recall liczony dla rezonansów ≥ +9 dB i Q ≥ 8. Narzędzie interaktywne: `ssc_resonance_eval`.

| Miara | Wynik |
|---|---|
| Precision (trafne / wszystkie podpowiedzi) | 0,87 (20 / 23) |
| Recall, wszystkie rezonanse | 0,55 (12 / 22) |
| Recall powyżej 400 Hz | 0,85 (11 / 13) |
| Recall poniżej 400 Hz | 1 / 9 |
| Fałszywe podpowiedzi na czystym materiale | 0,38 / min (1 w 2,6 min) |

Odstępstwo (research/11 §e): poniżej 400 Hz rezonanse giną w gęstym widmie basu i harmonicznych — podpowiedzi są tam rzadkie; kryterium VIS-5 jest spełnione powyżej 400 Hz. Ewaluacja na nagraniach rzeczywistych nie była możliwa w tym środowisku (brak materiału z prawami do publikacji).
