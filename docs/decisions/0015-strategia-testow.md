# ADR-0015: Strategia testów

- **Status:** Zaakceptowana (M0, 2026-10-03)
- **Wymagania:** TST-1…TST-4, NFR-4, DoD

## Decyzja
- **Testy jednostkowe**: Catch2 v3 dla rdzenia DSP (`src/core`) i testy integracyjne `AudioProcessor` (render bloków bez hosta).
- **Testy pomiarowe**: narzędzie `ssc-measure` (CLI) — sygnały testowe, metryki (amplituda/faza vs prototyp, THD z analizy harmonicznych, aliasing jako składowe nieharmoniczne, głośność wg modelu i BS.1770, precision/recall detektora rezonansów). Raporty Markdown/CSV w `docs/measurements/`. Zrealizowane w M8 jako `ssc_measure` (tools/Measure.cpp): raporty Markdown filters / distortion / latency / autogain / resonance, plus `cpu.md` z `ssc_benchmark`.
- **Benchmarki**: `ssc-bench` — scenariusze NFR-1; wynik jako tabela.
- **Walidacja wtyczki**: pluginval (strictness 10, headless) na trzech systemach; Steinberg VST3 validator (z vst3sdk na licencji MIT) budowany w CI; `auval` dla AU.
- **Sanitizery**: ASan/UBSan (testy), TSan (komunikacja wątków), RealtimeSanitizer (ścieżka audio, Clang 20+; preset `rtsan`, zadanie CI „Real-time safety”).
- **Testy obciążeniowe** (TST-4, M8): `tests/plugin/TestStress.cpp` przez `AudioProcessor` jak host — macierz w `docs/testing.md`; każdy przebieg sprawdza skończone i ograniczone wyjście, brak wyciszeń NaN i zero alokacji w `processBlock`.
- **Śledzenie**: każde wymaganie z PROMPT.md ma wiersz w `docs/traceability.md` (test/dowód, kamień milowy, status).
- **Hosty**: ręczna checklista w `docs/testing.md` (Atmos i AAX poza CI).

## Rozważane alternatywy
- **GoogleTest** — równoważny; Catch2 wybrany za prostszą integrację z CMake i benchmarki mikro.
- **Testy tylko w hostach** — nie dają powtarzalnych kryteriów liczbowych; odrzucone.
