# ADR-0001: Framework — JUCE 9 + CMake + C++20

- **Status:** Proponowana (M0)
- **Wymagania:** PLT-1, PLT-2, PLT-3

## Kontekst
PROMPT.md wskazuje JUCE 8. W trakcie researchu (10.2026) dostępne są już wydania **JUCE 9.0.0–9.0.3**. Według ogłoszenia wydania JUCE 9 skupia się na wydajności UI (szybszy renderer programowy), SVG (lunasvg), czcionkach zmiennych, nowej implementacji CoreAudio na macOS i ulepszeniach CMake dla środowisk bez GUI (istotne dla CI). Licencjonowanie i ceny pozostały takie jak w JUCE 8 (AGPLv3 albo licencja komercyjna). JUCE zawiera potrzebne nagłówki VST3 SDK; sam VST3 SDK jest od wersji 3.8 na licencji MIT.

## Decyzja
- **JUCE 9.0.3** jako submoduł git w `external/JUCE`, przypięty do taga.
- **C++20**, **CMake ≥ 3.25**, `CMakePresets.json`.
- Formaty przełączane opcjami CMake: VST3 (domyślnie włączony), AU (P1), AAX (P2; wymaga AAX SDK i konta Avid).
- Aktualizacje patch JUCE po zielonym CI; zmiana wersji major — nowy ADR.

## Konsekwencje
- Świadome odstępstwo od PROMPT.md (JUCE 8 → 9) — nowsze poprawki, ta sama licencja.
- Ryzyko regresji świeżej wersji major ograniczamy przypięciem taga i testami (pluginval, hosty). Awaryjnie: JUCE 8.0.15 (ostatni tag gałęzi 8).

## Rozważane alternatywy
- **JUCE 8.0.x** — dojrzały, ale starszy; rezerwowa opcja.
- **iPlug2** — mniejsza społeczność, słabsze wsparcie surround/AAX w praktyce.
- **nih-plug (Rust)** — brak AU/AAX; odrzucone.
- **DPF** — brak AAX i ograniczone GUI; odrzucone.
