# Research — indeks (M0)

Notatki researchu dla funkcji z sekcji 4–14 [PROMPT.md](../../PROMPT.md) i propozycja przebudowy interfejsu po M8 (§15). Każda notatka ma ten sam układ:

1. **a) Literatura i normy** — publikacje i standardy, na których opieramy rozwiązanie.
2. **b) Wtyczki referencyjne** — jak problem rozwiązują najlepsze produkty (na podstawie manuali, recenzji i pomiarów; bez dekompilacji).
3. **c) Wybrane podejście** — decyzja techniczna, uzasadnienie i odrzucone alternatywy.
4. **d) Kryteria akceptacji** — mierzalne warunki, które trafiają do testów (`docs/traceability.md`).

Twierdzenia o produktach komercyjnych, których nie dało się potwierdzić w źródłach pierwotnych, oznaczono `[niezweryfikowane]`.
Decyzje o dużym wpływie na architekturę opisano dodatkowo jako ADR w [`docs/decisions/`](../decisions/).

| § | Notatka | Wymagania |
|---|---|---|
| 4 | [Kanały, surround i Dolby Atmos](04-kanaly-surround-atmos.md) | CH-1…CH-6 |
| 5 | [Pasma: kształty, tworzenie i edycja](05-pasma-ksztalty-edycja.md) | BAND-1…BAND-7 |
| 6 | [Tryby pasma: Static / Dynamic / Spectral](06-tryby-pasma-dynamic-spectral.md) | DYN-1…DYN-11 |
| 7 | [Auto Gain](07-auto-gain.md) | AG-1…AG-6 |
| 8 | [Tryby fazy, jakość obliczeń i modele analogowe](08-tryby-fazy-jakosc.md) | PH-1…PH-3 |
| 9 | [Character pasma](09-character.md) | CHR-1…CHR-5 |
| 10 | [Oversampling](10-oversampling.md) | OS-1…OS-5 |
| 11 | [Analizator, wizualizacja i podpowiedzi rezonansów](11-analizator-wizualizacja.md) | VIS-1…VIS-8 |
| 12 | [Odczyty fazy i ringingu](12-odczyty-faza-ringing.md) | PR-1…PR-4 |
| 13 | [Mierniki THD+N i aliasingu](13-mierniki-thd-aliasing.md) | MTR-1…MTR-4 |
| 14 | [Funkcje globalne](14-funkcje-globalne.md) | GLB-1…GLB-5 |
| 15 | [UX/UI: grupowanie, hierarchia, przyciski i animacje](15-ux-ui.md) (propozycja po M8) | VIS-7, GLB-5, BAND-6, BAND-7 |

Wspólne pojęcia używane w notatkach:

- **fs** — częstotliwość próbkowania hosta; **fs_os** — częstotliwość wewnątrz oversamplingu.
- **Lane** — pojedynczy sygnał przetwarzany przez pasmo: kanał głośnika albo składowa Mid/Side pary kanałów (zob. §4).
- **ERB** — Equivalent Rectangular Bandwidth: ERB(f) = 24,7·(4,37·f/1000 + 1) Hz; skala ERB-number: 21,4·log10(4,37·f/1000 + 1) (Glasberg & Moore 1990).
- **SVF-TPT** — filtr state-variable dyskretyzowany całkowaniem trapezowym (topology-preserving transform; Zavalishin, Simper).
