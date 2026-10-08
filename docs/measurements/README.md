# Raporty pomiarowe

Raporty wymagane przez Definition of Done (PROMPT §20) i ADR-0015. Wszystkie poza `cpu.md` generuje narzędzie `ssc_measure` z tego samego kodu DSP, którego używa wtyczka (sygnał przez cały silnik `Engine`), więc można je odtworzyć po każdej zmianie:

```bash
cmake --preset release -DSSC_BUILD_TOOLS=ON
cmake --build --preset release --target ssc_measure ssc_benchmark
build/release/tools/ssc_measure docs/measurements            # wszystkie raporty (ok. 1–2 min)
build/release/tools/ssc_measure docs/measurements filters    # albo jeden: filters | distortion | latency | autogain | resonance
build/release/tools/ssc_benchmark 10                          # CPU (cpu.md)
```

| Raport | Co mierzy | Wymagania | Wynik w skrócie |
|---|---|---|---|
| [filters.md](filters.md) | błąd amplitudy względem prototypu analogowego, 44,1 / 48 / 96 kHz, tryby fazy × Quality | BAND-2, BAND-3, PH-1, TST-1 | ±0,1 dB do 20 kHz przy 44,1 kHz: Natural Phase (≤ 0,057 dB w Normal) i Minimum Phase High/Max (≤ 0,005 dB, bez latencji); Minimum Phase Eco/Normal — obwiednia dopasowanego IIR (do 0,7 dB przy samym Nyquiście) |
| [distortion.md](distortion.md) | THD+N, THD i aliasing: Character, oversampling, modele Analog Feel, tor liniowy | CHR-3, CHR-4, OS-1, OS-4, MTR-3 | tor liniowy −250 dB (precyzja double); Warm/Sharp przy 0 dBFS −52 dB; aliasing przypadku OS-1: 1× −66,5 dB, 2× −163 dB |
| [latency.md](latency.md) | latencja raportowana i zmierzona dla trybów fazy, Quality, oversamplingu, pasm Spectral, renderu offline | PH-2, OS-3, OS-4, TST-1 | zmierzona = raportowana (faza liniowa dokładnie, IIR ±0,5/k próbki) |
| [autogain.md](autogain.md) | zmiana głośności po Auto Gain wg modelu i BS.1770 | AG-3, AG-6 | ≤ 0,12 dB wg modelu (kryterium 0,5 / 0,2 dB) |
| [resonance.md](resonance.md) | precision / recall podpowiedzi rezonansów i fałszywe alarmy | VIS-5 | precision 0,87, recall powyżej 400 Hz 0,85, 0,4 fałszywych/min |
| [cpu.md](cpu.md) | obciążenie CPU konfiguracji NFR-1 oraz trybów fazy × Quality | NFR-1, PH-2 | stereo 8 × Static 0,7 %, 8 × Spectral 4,1 %; 7.1.4 Spectral 21,7 % na tym hoście |

Pozostałe pomiary są częścią testów (wartości drukowane przez `ssc_core_tests` i `ssc_plugin_tests` z tagami wymagań, np. `[MTR-3]`, `[PR-1]`, `[TST-4]`) i są cytowane w notatkach researchu (§e) oraz w docs/traceability.md. Mierniki THD+N/aliasingu w czasie rzeczywistym porównano z analizą offline w `[MTR-3]` (research/13 §e), ringing i opóźnienie grupowe pasm w `[PR-1]`/`[PR-3]` (research/12 §e).

## Czego te raporty nie obejmują

- Pomiarów CPU na referencyjnych Apple M1 i współczesnym x86-64 (NFR-1) — wymagają tych maszyn.
- Wydajności renderowania GUI z akceleracją GPU (VIS-6) — zmierzono tylko renderer programowy (research/11 §e).
- Ewaluacji podpowiedzi rezonansów na nagraniach rzeczywistych (VIS-5) — brak materiału z prawami do publikacji.
- Porównania krzywych Analog Feel z pomiarami sprzętu (ADR-0009).
