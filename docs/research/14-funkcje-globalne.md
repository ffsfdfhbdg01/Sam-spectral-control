# §14 Funkcje globalne (GLB-1…GLB-5)

## a) Literatura i normy

- **VST3 SDK (Steinberg, od 3.8 na licencji MIT)**: flaga parametru `ParameterInfo::kIsBypass` (bypass sterowany przez host), raportowanie latencji (`getLatencySamples`, `restartComponent(kLatencyChanged)`), format presetów `.vstpreset`.
- **JUCE**: `AudioProcessorValueTreeState` (parametry i stan), `AudioProcessor::getBypassParameter`, `UndoManager`, `SmoothedValue`, `AudioProcessLoadMeasurer`.
- Crossfade sygnałów skorelowanych (suchy/mokry z wyrównaną latencją) — przejście liniowe (amplitudowo komplementarne) zachowuje poziom; equal-power jest właściwe dla sygnałów nieskorelowanych.

## b) Wtyczki referencyjne

- **FabFilter Pro-Q 4** — Undo/Redo, A/B, presety, regulacja wyjścia `[niezweryfikowane: szczegóły]`.
- **oeksound soothe2** — Delta (odsłuch różnicy wet/dry).
- Pomysł na P2: globalna skala gainów wszystkich pasm (ułatwia „ile EQ” jednym pokrętłem).

## c) Wybrane podejście

- **On/Off (GLB-1)**: parametr `bypass` oznaczony jako bypass hosta (`kIsBypass`; w JUCE przez `getBypassParameter()`). UI: przycisk zasilania (świeci = On = bypass wyłączony). Przełączenie: liniowy crossfade 20 ms między torem przetworzonym a suchym, opóźnionym o latencję; latencja pozostaje stała w obu stanach.
- **Output, Mix, Max Lookahead, Delta (GLB-2)**:
  - Output: ±24 dB, wygładzany,
  - Mix: dry/wet z linią opóźniającą wyrównującą latencję. **Uwaga w manualu**: w Minimum Phase równoległe mieszanie z suchym sygnałem daje interferencje fazowe — do łagodzenia działania lepiej używać Mix pasma (DYN-7),
  - Max Lookahead: 0–20 ms (DYN-8),
  - Delta (P1): wyjście = suchy (wyrównany) − mokry.
- **Stan i presety (GLB-3, GLB-4)**:
  - stabilne ID parametrów (np. `b07_freq`), nigdy nie zmieniane po wydaniu,
  - stan jako `ValueTree` z atrybutem `version` i funkcjami migracji między wersjami,
  - presety fabryczne w zasobach binarnych; presety użytkownika w katalogu dokumentów użytkownika (XML),
  - A/B: dwa migawkowe stany; kopiowanie ustawień pasm (schowek wewnętrzny),
  - Undo/Redo: `UndoManager`; gest myszy lub edycja z panelu = jedna transakcja.
- **Wygładzanie automatyzacji**: parametry ciągłe przez `SmoothedValue` (≈ 20 ms) lub interpolację w ramach bloku; parametry dyskretne (kształt, tryb) przełączane crossfade’em 10–20 ms.
- **CPU i latencja (GLB-5)**: `AudioProcessLoadMeasurer` → procent budżetu bloku; latencja z `getLatencySamples()` → ms.

### Odrzucone alternatywy
- **Natychmiastowy bypass (bez crossfade’u)** — trzaski; odrzucone.
- **Equal-power crossfade dla bypassu** — podbicie poziomu o ok. 3 dB w połowie przejścia dla sygnałów skorelowanych; odrzucone.

## d) Kryteria akceptacji

| ID | Kryterium | Metoda |
|---|---|---|
| GLB-1 | Przełączanie On/Off na sinusie: brak nieciągłości > −60 dBFS; latencja identyczna w obu stanach; host widzi parametr jako bypass | test jednostkowy + pluginval |
| GLB-4 | Zapis/odczyt stanu: wartości parametrów identyczne bitowo; migracja stanu z wersji N−1 | test jednostkowy |
| GLB-3 | 100 operacji Undo/Redo przywraca dokładnie stan wyjściowy | test jednostkowy |
| GLB-4 | Automatyzacja wszystkich parametrów (rampy, skoki) bez trzasków: residuum względem referencji z interpolacją per próbka ≤ −60 dB | test jednostkowy |
| GLB-5 | Odczyt CPU i latencji zgodny z pomiarem (±1 punkt procentowy; ±0 próbek) | test |
