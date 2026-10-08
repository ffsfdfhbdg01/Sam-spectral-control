# ADR-0005: Natural Phase — dopasowany IIR + korektor fazy FIR

- **Status:** Zaakceptowana (M0, 2026-10-03)
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

## Aktualizacja (M4, realizacja)
- Korektor poprawia także resztkowy błąd **amplitudy** dopasowanego IIR: C = B · T/A · e^{−jωD} (poniżej −80 dB odpowiedzi pasma tylko faza; korekcja amplitudy ograniczona do ±12 dB). To realizuje kryterium ±0,1 dB z odstępstwa BAND-2.
- Okno Kaisera β = 4 (β = 8 z brickwallem), korekcja do 0,95·Nyquist, siatka ≥ 8·(2D + 1) punktów; aktualizacja na stałej siatce próbek (≈ 21 ms) z crossfadem 5 ms — deterministycznie (NFR-3).
- Baza M/S realizowana jako FIR 2×2 na historiach kanałów, więc zmiana bazy to crossfade.
- Brickwall jako FIR tylko od fc ≥ 7·fs/(2D + 1), gdzie jest bardziej stromy niż IIR 96 dB/oct; niżej pozostaje IIR (z korekcją). Pełny liniowofazowy brickwall w basie wymagałby latencji rzędu setek ms — należy do trybu Linear Phase (P2).
- Wyniki: research/08 „Realizacja w M4” (PH-1 Normal ≤ 0,058 dB / 0,17°, Max ≤ 0,004 dB / 0,01°).

## Aktualizacja (M8)
- Korektor ma drugi tryb, **minimalnofazowy**, używany w Minimum Phase przy Quality High i Max: z tej samej korekcji amplitudy |T/A| (bez fazy) powstaje metodą cepstrum filtr FIR o fazie minimalnej (D + 1 współczynników) bez latencji. Pasma mają wtedy amplitudę prototypów analogowych (≤ 0,005 dB do 20 kHz przy 44,1 kHz) i pozostają minimalnofazowe. Szczegóły i pomiary: research/08 „Realizacja w M8”.

