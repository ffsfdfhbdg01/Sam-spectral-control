# ADR-0016: Identyfikatory i nazwy wtyczki

- **Status:** **Otwarta** — wymaga potwierdzenia przed pierwszą dystrybucją (także betą)
- **Wymagania:** PLT-6

## Kontekst
Identyfikatory (VST3 Class ID, kod producenta, kod wtyczki, bundle ID) trafiają do sesji użytkowników — po pierwszej dystrybucji nie wolno ich zmieniać. JUCE wymaga: kod producenta — 4 znaki, co najmniej jedna wielka litera; kod wtyczki — 4 znaki, dokładnie jedna wielka litera (dla AU/GarageBand pierwsza wielka, reszta małe).

## Propozycja (robocza)
| Pole | Wartość robocza |
|---|---|
| Nazwa wtyczki | Sam Spectral Control |
| Nazwa producenta | Sam Audio *(do potwierdzenia)* |
| Kod producenta | `SamA` *(do potwierdzenia)* |
| Kod wtyczki | `Sspc` |
| Bundle ID | `com.samaudio.samspectralcontrol` *(do potwierdzenia)* |
| VST3 Class ID | wyprowadzany przez JUCE z kodów powyżej; zapisany w repozytorium po zatwierdzeniu |

## Decyzja
- Rozwój (M1–M7) używa wartości roboczych.
- Przed pierwszą dystrybucją użytkownik potwierdza nazwę producenta i kody; po potwierdzeniu wartości są zamrażane, a ten ADR otrzymuje status „Zaakceptowana”.
