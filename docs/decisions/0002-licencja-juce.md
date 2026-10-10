# ADR-0002: Licencja JUCE — AGPLv3 czy komercyjna

- **Status:** Zaakceptowana (2026-10-10) — **AGPLv3**, kod otwarty
- **Wymagania:** PLT-3, REL-1…REL-3

## Kontekst
JUCE 9 (jak JUCE 8) jest dostępny na dwóch licencjach: **AGPLv3** albo **licencja komercyjna** (progi Starter / Indie / Pro zależne od przychodu — aktualne warunki na juce.com). JUCE deklaruje `AGPL-3.0-only OR LicenseRef-JUCE-Commercial` (`external/JUCE/JUCE.spdx.json`). Pozostałe zależności są permisywne: VST3 SDK 3.8 (MIT), pffft (BSD-podobna), Catch2 (BSL-1.0, tylko testy).

## Opcje
| Opcja | Co oznacza dla Sam Spectral Control |
|---|---|
| **AGPLv3** | Kod źródłowy wtyczki musi być udostępniony odbiorcom binarek na AGPLv3. Sprzedaż jest możliwa, ale kod pozostaje otwarty. |
| **Komercyjna JUCE** | Kod może pozostać zamknięty; obowiązują warunki wybranego progu (m.in. limity przychodu dla Starter/Indie). |

## Decyzja
Właściciel projektu wybrał **AGPLv3** (2026-10-10).
- Sam Spectral Control jest wydawany na **GNU Affero General Public License, wersja 3** (`AGPL-3.0-only`, jak JUCE). Prawa autorskie: Sam Audio.
- Pełny tekst licencji: `LICENSE` w katalogu głównym (wykrywany przez GitHub). Instalatory pokazują i instalują `packaging/LICENSE.txt` — nota o prawach autorskich, wyłączenie gwarancji, adres kodu źródłowego i pełny tekst AGPLv3 — oraz `THIRD-PARTY-NOTICES.txt`.
- **Kod źródłowy dla odbiorców binarek** (AGPLv3 §6): repozytorium https://github.com/ffsfdfhbdg01/Sam-spectral-control jest publiczne, a każde wydanie ma tag `vX.Y.Z` i archiwum źródeł ze wszystkimi submodułami (JUCE, pffft, Catch2), budowane przez workflow wydania. Archiwum nie zależy od dostępności repozytoriów zewnętrznych.
- §13 (interakcja przez sieć) nie dotyczy wtyczki: nie udostępnia ona funkcji użytkownikom przez sieć.

## Konsekwencje
- Wydania mogą być publikowane (bramka `release.yml` wymaga zaakceptowanego ADR i licencji bez linii `PROVISIONAL`).
- Każdy, kto rozpowszechnia zmodyfikowaną wersję, musi udostępnić jej kod na tej samej licencji. Zmiana licencji na zamkniętą w przyszłości wymagałaby licencji komercyjnej JUCE i zgody wszystkich autorów kodu.
- Publiczne repozytorium ma darmowe minuty GitHub Actions na standardowych runnerach (także macOS i Windows), więc CI i wydania nie zależą od limitu minut konta.
