# ADR-0008: Oversampling

- **Status:** Zaakceptowana (M0, 2026-10-03)
- **Wymagania:** OS-1…OS-5, ARC-2

## Decyzja
- Globalnie Off (1×, domyślnie) / 2× / 4× / 8× — kaskada stopni 2×, obejmująca tylko łańcuch pasm czasowych.
- Na start **`juce::dsp::Oversampling`** za interfejsem `IOversampler`:
  - Minimum Phase → `filterHalfBandPolyphaseIIR` (niska latencja),
  - Natural Phase / Analog Feel → `filterHalfBandFIREquiripple` (liniowa faza).
- Specyfikacja: Normal — pasmo do 0,43·fs, zafalowania ≤ 0,05 dB, tłumienie ≥ 90 dB; Max — 0,45·fs, ≤ 0,01 dB, ≥ 120 dB (do potwierdzenia pomiarami wg Kahles i in. 2019).
- Przy OS > 1× oversampling działa zawsze (stała latencja), także bez aktywnych nieliniowości.
- HIIR (de Soras) jako kandydat do podmiany, jeśli benchmark M4 wykaże przewagę.
- Render offline z osobnym współczynnikiem (P1) — z dopełnieniem do wspólnej latencji.

## Konsekwencje
- Kanały nieobjęte pasmami omijają oversampling (CH-6).
- Koszt CPU i latencja każdego wariantu mierzone i publikowane w `docs/measurements/`.

## Rozważane alternatywy
- **Oversampling całej wtyczki** (z STFT) — bez korzyści, duży koszt; odrzucone.
- **Automatyczny OS zależny od aktywnych nieliniowości** — zmiany latencji; odrzucone.
