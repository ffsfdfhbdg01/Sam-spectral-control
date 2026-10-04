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
- Render offline z osobnym współczynnikiem (P1) — z dopełnieniem do wspólnej latencji. Zrealizowane w M8: parametr „Offline” (Same / Off / 2× / 4× / 8×), silnik wybiera współczynnik według `isNonRealtime()` hosta w każdym bloku, a latencja obu ustawień jest równa większej z nich (tor krótszy dopełniony opóźnieniem na końcu toru przetwarzanego).

## Konsekwencje
- Kanały nieobjęte pasmami omijają oversampling (CH-6).
- Koszt CPU i latencja każdego wariantu mierzone i publikowane w `docs/measurements/`.

## Rozważane alternatywy
- **Oversampling całej wtyczki** (z STFT) — bez korzyści, duży koszt; odrzucone.
- **Automatyczny OS zależny od aktywnych nieliniowości** — zmiany latencji; odrzucone.

## Aktualizacja (M4, realizacja)
- Zamiast `juce::dsp::Oversampling` — własna implementacja w rdzeniu DSP (`ssc/oversampling/Oversampler`): rdzeń pozostaje niezależny od JUCE (ARC-5), specyfikacja bez zmian. Interfejs `IOversampler` okazał się zbędny — jedna klasa z dwoma typami filtrów (polifazowy IIR jak w HIIR, półpasmowy FIR z oknem Kaisera).
- Specyfikacja per Quality: Eco 0,420·fs / 80 dB, Normal 0,432·fs / 90 dB, High 0,444·fs / 105 dB, Max 0,454·fs / 120 dB; latencja zawsze całkowita (dopełnienie przy najwyższej częstotliwości).
- Częstotliwość wewnętrzna ≤ 400 kHz: 8× do 48 kHz, 4× przy 96 kHz, 2× przy 192 kHz.
- Wszystkie warianty przygotowane w `prepare()`; zmiana w czasie odtwarzania bez alokacji. Kanały bez pasm omijają OS bit-exact (crossfade 5 ms przy zmianie pokrycia).
- Wyniki: research/10 „Realizacja w M4”.

