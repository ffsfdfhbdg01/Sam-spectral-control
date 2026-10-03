# ADR-0002: Licencja JUCE — AGPLv3 czy komercyjna

- **Status:** **Otwarta** — decyzja użytkownika przed publikacją binarek
- **Wymagania:** PLT-3, REL-1…REL-3

## Kontekst
JUCE 9 (jak JUCE 8) jest dostępny na dwóch licencjach: **AGPLv3** albo **licencja komercyjna** (progi Starter / Indie / Pro zależne od przychodu — aktualne warunki na juce.com). Pozostałe zależności są permisywne: VST3 SDK 3.8 (MIT), pffft (BSD-podobna), Catch2 (BSL-1.0).

## Opcje
| Opcja | Co oznacza dla Sam Spectral Control |
|---|---|
| **AGPLv3** | Kod źródłowy wtyczki musi być udostępniony odbiorcom binarek na AGPLv3. Sprzedaż jest możliwa, ale kod pozostaje otwarty. |
| **Komercyjna JUCE** | Kod może pozostać zamknięty; obowiązują warunki wybranego progu (m.in. limity przychodu dla Starter/Indie). |

## Decyzja (tymczasowa)
- Rozwój przebiega identycznie niezależnie od wyboru.
- **Do czasu decyzji nie publikujemy binarek** poza artefaktami CI do testów wewnętrznych.

## Konsekwencje
- Brak blokady dla M1–M7; decyzja jest blokująca dla wydania (M8).
