# ADR-0016: Identyfikatory i nazwy wtyczki

- **Status:** Zaakceptowana (2026-10-10) — wartości zamrożone przed pierwszym wydaniem
- **Wymagania:** PLT-6

## Kontekst
Identyfikatory (VST3 Class ID, kod producenta, kod wtyczki, bundle ID) trafiają do sesji użytkowników — po pierwszej dystrybucji nie wolno ich zmieniać. JUCE wymaga: kod producenta — 4 znaki, co najmniej jedna wielka litera; kod wtyczki — 4 znaki, dokładnie jedna wielka litera (dla AU/GarageBand pierwsza wielka, reszta małe).

## Decyzja
Właściciel projektu zatwierdził wartości robocze (2026-10-10). Od wydania 1.0.0 są niezmienne:

| Pole | Wartość |
|---|---|
| Nazwa wtyczki | Sam Spectral Control |
| Nazwa producenta | Sam Audio |
| Kod producenta | `SamA` |
| Kod wtyczki | `Sspc` |
| Bundle ID | `com.samaudio.samspectralcontrol` |
| VST3 Class ID — procesor (*Audio Module Class*) | `ABCDEF01-9182FAEB-53616D41-53737063` |
| VST3 Class ID — kontroler (*Component Controller Class*) | `ABCDEF01-1234ABCD-53616D41-53737063` |
| Identyfikatory pakietów macOS | `com.samaudio.samspectralcontrol.vst3` / `.au` / `.uninstaller` |
| `AppId` instalatora Windows | `{BBE8FCC0-05F6-4CCE-ACA0-867AE7F01824}` (Inno Setup) |

- Class ID wyprowadza JUCE z kodów producenta i wtyczki (dwie ostatnie grupy to „SamA” i „Sspc” w ASCII); wartości odczytane z `Contents/Resources/moduleinfo.json` zbudowanej wtyczki 1.0.0.
- Źródła prawdy: `juce_add_plugin` w `src/CMakeLists.txt`, `packaging/macos/build-pkg.sh` (identyfikatory pakietów), `packaging/windows/SamSpectralControl.iss` (`AppId`).

## Konsekwencje
- Zmiana któregokolwiek pola po wydaniu sprawia, że hosty widzą inną wtyczkę: zapisane sesje i presety hostów jej nie znajdą. Nowa wtyczka (np. wersja 2 niezgodna wstecz) dostaje nowy kod wtyczki, nie zmienia się istniejącego.
- Nazwa producenta i adres projektu (`COMPANY_WEBSITE`) są widoczne w hostach; adres można zmieniać, bo nie jest identyfikatorem.
