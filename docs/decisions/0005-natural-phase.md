# ADR-0005: Natural Phase — dopasowany IIR + korektor fazy FIR

- **Status:** Proponowana (M0)
- **Wymagania:** PH-1, PH-2, PH-3, BAND-3 (Brickwall)

## Kontekst
Natural Phase ma odtwarzać amplitudę **i fazę** prototypu analogowego przy małej latencji i minimalnym pre-ringu. Dopasowany IIR (ADR-0004) ma analogową amplitudę i niemal analogową fazę w basie i średnicy; odchylenia fazy koncentrują się w wysokich częstotliwościach.

## Decyzja
1. Łańcuch pasm pozostaje minimalnofazowy (dopasowany IIR). Na wyjściu działa **jeden korektor FIR** C na kanał (albo na składową M/S pary), z opóźnieniem D:
   - φ_e(ω) = ∠T(ω) − ∠A(ω), gdzie T = iloczyn prototypów analogowych pasm, A = odpowiedź cyfrowego łańcucha,
   - C(ω) = exp(j·(φ_e(ω) − ω·D)) dla ω ≤ ω_max (0,9–0,95·Nyquist), powyżej zbieganie do exp(−j·ω·D),
   - FIR o długości 2D + 1: IFFT z gęstej siatki (4096 punktów) + okno Kaisera, wyśrodkowane w D.
2. **D wg Quality** (48 kHz, skalowane z fs): Eco 16, Normal 32, High 64, Max 128 próbek.
3. **Baza korekcji**: per kanał, gdy pasma na parze należą do {All, Left, Right}; w M/S, gdy należą do {All, Mid, Side}. Przy mieszaniu L/R i M/S na tej samej parze — korekcja przybliżona (bez składników krzyżowych) i komunikat w UI.
4. **Pasma Dynamic**: korektor przeliczany w wątku audio z częstotliwością sterowania (≤ 50 Hz, na granicach pod-bloków, deterministycznie) i przełączany crossfade’em.
5. **Brickwall** (Low/High Cut) w Natural Phase: składnik FIR projektowany razem z korektorem (ta sama latencja D; stromość rośnie z Quality).

## Konsekwencje
- Latencja Natural Phase = D (+ inne składniki z ADR-0011); pre-ring ≤ D.
- Pojedynczy korektor nie mnoży latencji przez liczbę pasm.
- Ograniczona dokładność przy mieszaniu L/R i M/S na jednej parze — udokumentowana.

## Rozważane alternatywy
- **Pełny FIR z H(jω) całego EQ** — długie filtry dla dokładnego basu; odrzucone.
- **Korektor per pasmo** — latencje sumowałyby się (N·D); odrzucone.
- **Korekcja MIMO 2×2 dla par** — dokładna przy mieszaniu L/R i M/S, ale złożona; P2, jeśli testy odsłuchowe pokażą potrzebę.
