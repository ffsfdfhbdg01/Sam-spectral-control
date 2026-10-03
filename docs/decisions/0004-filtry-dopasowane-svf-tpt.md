# ADR-0004: Filtry dopasowane realizowane jako SVF-TPT

- **Status:** Proponowana (M0)
- **Wymagania:** BAND-2, BAND-3, BAND-6, DYN-2, PH-1, PH-2

## Kontekst
Potrzebujemy jednocześnie: (1) amplitudy zgodnej z prototypem analogowym aż do Nyquista (bez „cramping” przekształcenia biliniowego), (2) struktury odpornej na szybką modulację parametrów (automatyzacja, tryb Dynamic), (3) dobrej numeryki w basie.

## Decyzja
1. **Projekt**: biquady dopasowane — Vicanek 2016 (LP, HP, BP, peaking), półki: Berners & Abel 2003 (1. i 2. rząd), wyższe rzędy półek: Holters & Zölzer 2006, cut: kaskady Butterwortha z sekcjami dopasowanymi.
2. **Realizacja**: każdy stabilny biquad (b0, b1, b2, a1, a2; a0 = 1) mapujemy na SVF-TPT (Simper/Zavalishin) przez odwrotne przekształcenie biliniowe:
   - A0 = 1 + a1 + a2, A1 = 2·(1 − a2), A2 = 1 − a1 + a2 (stabilność ⇒ A0 > 0, A2 > 0)
   - g = √(A0 / A2), k = A1 / √(A0·A2)
   - B0 = b0 + b1 + b2, B1 = 2·(b0 − b2), B2 = b0 − b1 + b2
   - n0 = B0 / A0, n1 = B1·g / A0, n2 = B2 / A2
   - m0 = n2, m1 = n1 − n2·k, m2 = n0 − n2

   Krok SVF-TPT: c1 = 1/(1 + g·(g + k)), c2 = g·c1, c3 = g·c2; v3 = v0 − s2; v1 = c1·s1 + c2·v3; v2 = s2 + c2·s1 + c3·v3; s1 = 2·v1 − s1; s2 = 2·v2 − s2; y = m0·v0 + m1·v1 + m2·v2.

   Struktura realizuje dokładnie tę samą funkcję przenoszenia co biquad, ale jest odporna na modulację i numerycznie lepsza w basie. (Kontrola: dla bell Simpera m0 = 1, m1 = k·(A² − 1), m2 = 0, k = 1/(Q·A) daje prototyp RBJ peaking.)
3. **Tryb Dynamic**: tabela parametrów (g, k, m0, m1, m2) w funkcji gainu (krok 0,25 dB), przebudowywana przy zmianie fc/Q; per próbka interpolacja liniowa (g > 0, k > 0 ⇒ stabilność).
4. **Wygładzanie**: zmiany fc/Q/gain (Static) — przeliczenie projektu co pod-blok (Eco: 32, Normal: 8 próbek; High/Max: interpolacja per próbka); stan w double od High.
5. **Max**: pasma z fc > fs/4 w wewnętrznym 2× (dodatkowa dokładność przy Nyquiście).

## Konsekwencje
- Static i Dynamic mają identyczną odpowiedź statyczną (ta sama tabela/projekt).
- Koszt per próbka: ~10 mnożeń na sekcję + interpolacja 5 parametrów w Dynamic.
- Wymagane testy dokładności na siatce parametrów (BAND-2, PH-1).

## Rozważane alternatywy
- **Biquad RBJ + biliniowe** — cramping przy Nyquiście. Odrzucone.
- **TDF-II z interpolacją współczynników** — artefakty przy szybkiej modulacji. Odrzucone.
- **Przeliczanie projektu dopasowanego per próbka** — zbyt kosztowne przy wielu lane. Odrzucone.
