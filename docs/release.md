# Wydanie: instalatory, podpisy, workflow (REL-1…REL-3, TST-2)

## Przegląd

| Workflow | Kiedy | Co robi |
|---|---|---|
| `ci.yml` | pull request, push do `main`/`master`, ręcznie (*Run workflow*: `all` albo `linux`); nie startuje dla zmian samej dokumentacji ani na pushach do gałęzi roboczych | build (Linux, macOS universal, Windows x64), testy, pluginval (strictness 10), walidator VST3 Steinberga (tryb rozszerzony), RealtimeSanitizer, ASan/UBSan, clang-format |
| `release.yml` | tag `vX.Y.Z` albo ręcznie (*Run workflow*) | to samo dla macOS i Windows, potem instalatory: `SamSpectralControl-X.Y.Z-macOS.pkg` i `SamSpectralControl-X.Y.Z-Windows.exe`; przy tagu — szkic wydania GitHub z notatkami z CHANGELOG i sumami SHA-256 |

Bez sekretów podpisu workflow wydania buduje **niepodpisane** instalatory — wyłącznie do testów wewnętrznych (macOS Gatekeeper je blokuje, Windows SmartScreen ostrzega).

## Bramka wydania

Dla tagu workflow odmawia wydania, dopóki:
- tag ≠ `v` + wersja z `CMakeLists.txt` (`project(SamSpectralControl VERSION X.Y.Z …)`),
- `CHANGELOG.md` nie ma sekcji `## [X.Y.Z]`,
- [ADR-0002](decisions/0002-licencja-juce.md) (licencja JUCE) lub [ADR-0016](decisions/0016-identyfikatory-nazwy.md) (identyfikatory) ma status „Otwarta”,
- `packaging/LICENSE.txt` jest tymczasowy (linia zaczynająca się od `PROVISIONAL`).

## Kroki wydania

1. Decyzje: ADR-0002 i ADR-0016 → „Zaakceptowana”; ostateczne warunki licencji w `packaging/LICENSE.txt` (bez linii `PROVISIONAL`).
2. Testy w hostach według [testing.md](testing.md) (TST-3).
3. Wersja w `CMakeLists.txt` i sekcja w `CHANGELOG.md` z datą: `## [X.Y.Z] - RRRR-MM-DD`.
4. `git tag vX.Y.Z && git push origin vX.Y.Z` → workflow „Release” → szkic wydania na GitHubie → sprawdzenie instalatorów (instalacja, deinstalacja, `spctl`/właściwości pliku) → *Publish release*.

## Sekrety i zmienne (Settings → Secrets and variables → Actions)

**macOS** (konto Apple Developer Program):

| Sekret | Zawartość |
|---|---|
| `MACOS_CERTIFICATES_P12` | plik `.p12` (base64) z certyfikatami **Developer ID Application** i **Developer ID Installer** wraz z kluczami prywatnymi |
| `MACOS_CERTIFICATES_PASSWORD` | hasło pliku `.p12` |
| `NOTARY_KEY_P8` | treść klucza API App Store Connect (`AuthKey_….p8`) do `notarytool` |
| `NOTARY_KEY_ID`, `NOTARY_ISSUER_ID` | identyfikator klucza i wystawcy (Users and Access → Integrations) |

Workflow tworzy tymczasowy keychain, sam znajduje obie tożsamości, podpisuje bundle (`codesign --options runtime --timestamp`), buduje pakiet (`productbuild --sign`), wysyła go do notaryzacji (`notarytool submit --wait`), zszywa bilet (`stapler staple`) i sprawdza go (`spctl --assess --type install`). Na koniec usuwa keychain i klucz.

**Windows** (Azure Artifact Signing, dawniej Trusted Signing):

| Sekret | Zawartość |
|---|---|
| `AZURE_TENANT_ID`, `AZURE_CLIENT_ID`, `AZURE_CLIENT_SECRET` | rejestracja aplikacji (service principal) z rolą podpisującego profil certyfikatu konta Artifact Signing |
| `AZURE_SIGNING_ENDPOINT` | adres regionu konta, np. `https://eus.codesigning.azure.net/` |
| `AZURE_SIGNING_ACCOUNT`, `AZURE_CERTIFICATE_PROFILE` | nazwa konta i profilu certyfikatu |

Akcja `azure/artifact-signing-action@v2` podpisuje binarkę wtyczki (`Contents/x86_64-win/Sam Spectral Control.vst3`) przed budową instalatora, a potem instalator (SHA-256, znacznik czasu RFC 3161). Deinstalator wbudowany w instalator Inno Setup nie jest podpisywany osobno.

**Zmienna** `SSC_FORMAT_AU` = `ON` (Variables): workflow buduje też Audio Unit, uruchamia `auval -strict` i dodaje komponent AU do pakietu macOS.

## SmartScreen (REL-2)

Microsoft Defender SmartScreen ocenia pobierane pliki według reputacji, którą wydawca zdobywa z czasem, w miarę pobrań i instalacji. Nowy wydawca — także z certyfikatem EV (od 2024 r. EV nie daje już reputacji od razu) czy z Artifact Signing — przez pierwsze tygodnie może widzieć ostrzeżenie „System Windows ochronił ten komputer” (*Więcej informacji → Uruchom mimo to*). Pomaga: ta sama tożsamość wydawcy przy każdym wydaniu (ten sam profil certyfikatu), podpisanie wszystkich plików wykonywalnych, publikacja z jednego, stałego adresu; przy fałszywych alarmach plik można zgłosić do analizy w portalu Microsoft Security Intelligence. Warto to opisać w notatkach pierwszego wydania.

## Instalatory

**macOS** — [`packaging/macos/build-pkg.sh`](../packaging/macos/build-pkg.sh) `<artefakty Release> <wersja> <katalog wyjściowy>`:
- komponenty do wyboru (*Customize*): VST3 → `/Library/Audio/Plug-Ins/VST3`, Audio Unit (gdy zbudowany) → `/Library/Audio/Plug-Ins/Components`, „Uninstaller and licences” → `/Library/Application Support/Sam Spectral Control`;
- bundle nie są relokowalne (instalator zawsze pisze do folderów systemowych);
- deinstalator: `Uninstall Sam Spectral Control.command` usuwa bundle, swój folder i paragony `pkgutil` (presety użytkownika w `~/Documents/Sam Spectral Control` zostają);
- macOS 11+, Apple silicon i Intel (universal binary);
- `DRY_RUN=1` wypisuje wywołania narzędzi Apple zamiast je uruchamiać (sprawdzenie skryptu i `distribution.xml` poza macOS).

**Windows** — [`packaging/windows/SamSpectralControl.iss`](../packaging/windows/SamSpectralControl.iss) (Inno Setup 6):
- `iscc /DAppVersion=X.Y.Z /DArtefacts=<…\SamSpectralControl_artefacts\Release> /DOutputDir=<katalog> packaging\windows\SamSpectralControl.iss`;
- VST3 → `C:\Program Files\Common Files\VST3\Sam Spectral Control.vst3` (komponent stały), licencje i instrukcja → `C:\Program Files\Sam Audio\Sam Spectral Control` (komponent do wyboru);
- deinstalator w *Aplikacje i funkcje*; Windows 10/11 x64; `AppId` jest stały (ADR-0016).

## Walidacja (TST-2)

- **pluginval** 1.0.4, strictness 10, na trzech systemach (także test edytora pod Xvfb na Linuksie).
- **Walidator VST3 Steinberga** z VST3 SDK 3.8.1 (MIT): [`tools/ci/vst3-validator.sh`](../tools/ci/vst3-validator.sh) klonuje SDK, buduje tylko `validator` i uruchamia testy rozszerzone (`-e`) — lokalnie 537/537.
- **auval** — gdy budowany jest Audio Unit (`SSC_FORMAT_AU`).

## Minuty GitHub Actions i weryfikacja lokalna

W repozytorium prywatnym minuty macOS liczą się ×10, a Windows ×2: pełne CI to ok. 155 minut rozliczeniowych, sam Linux (`platforms: linux`) — ok. 35. Po wyczerpaniu miesięcznego limitu zadania w ogóle nie startują. Dlatego `ci.yml` nie uruchamia się na pushach do gałęzi roboczych ani dla zmian samej dokumentacji, tylko dla pull requestów, pushy do `main`/`master` i ręcznie (Actions → CI → *Run workflow*, z wyborem `all` albo `linux`). Workflow wydania startuje tylko dla tagów i ręcznie.

Te same kontrole działają lokalnie, bez minut:

```bash
tools/ci/local-check.sh          # format, build z -Werror, testy (stos 1 MB), pluginval, walidator VST3, RTSan, ASan/UBSan
tools/ci/local-check.sh --quick  # tylko format, build i testy
```

Skrypt pomija kroki, do których brakuje narzędzi (pluginval: zmienna `PLUGINVAL` lub `pluginval` w `PATH`; RTSan: `clang++-20`; ASan/UBSan: `clang++`), i podaje to w podsumowaniu. Na macOS i Windows (Visual Studio) build sprawdza się tylko na tych systemach — lokalnie albo w CI po odnowieniu limitu. Pliki workflow można sprawdzić bez uruchamiania [actionlint](https://github.com/rhysd/actionlint): `actionlint .github/workflows/*.yml`.
