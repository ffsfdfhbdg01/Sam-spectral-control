# Wydanie: instalatory, podpisy, workflow (REL-1…REL-3, TST-2)

## Przegląd

| Workflow | Kiedy | Co robi |
|---|---|---|
| `ci.yml` | pull request, push do `main`/`master`, ręcznie (*Run workflow*: `all` albo `linux`); nie startuje dla zmian samej dokumentacji ani na pushach do gałęzi roboczych | build (Linux, macOS universal, Windows x64), testy, pluginval (strictness 10), walidator VST3 Steinberga (tryb rozszerzony), RealtimeSanitizer, ASan/UBSan, clang-format |
| `release.yml` | tag `vX.Y.Z` albo ręcznie (*Run workflow*) | to samo dla macOS i Windows, potem instalatory: `SamSpectralControl-X.Y.Z-macOS.pkg` i `SamSpectralControl-X.Y.Z-Windows.exe`; przy tagu — opublikowane wydanie GitHub z instalatorami, archiwum źródeł `SamSpectralControl-X.Y.Z-source.tar.gz` (ze wszystkimi submodułami), `SHA256SUMS.txt` i notatkami (`packaging/release-notes.md` + sekcja wersji z CHANGELOG) |

Bez sekretów podpisu workflow wydania buduje **niepodpisane** instalatory: bundle macOS dostaje podpis ad hoc (Apple silicon wczytuje tylko podpisany kod), pakiet zostaje bez podpisu, a Windows bez Authenticode. Wydanie z tagu jest wtedy oznaczane jako **wstępne** (*pre-release*), a do notatek trafia `packaging/release-notes-unsigned.md` — jak otworzyć instalator mimo ostrzeżeń Gatekeepera (macOS) i SmartScreen (Windows). Z kompletem sekretów (Developer ID, notaryzacja, Azure) wydanie jest zwykłe.

## Bramka wydania

Dla tagu workflow odmawia wydania, dopóki:
- tag ≠ `v` + wersja z `CMakeLists.txt` (`project(SamSpectralControl VERSION X.Y.Z …)`),
- `CHANGELOG.md` nie ma sekcji `## [X.Y.Z]`,
- [ADR-0002](decisions/0002-licencja-juce.md) (licencja JUCE) lub [ADR-0016](decisions/0016-identyfikatory-nazwy.md) (identyfikatory) ma status „Otwarta”,
- `packaging/LICENSE.txt` jest tymczasowy (linia zaczynająca się od `PROVISIONAL`).

## Kroki wydania

1. Decyzje (od 1.0.0 podjęte, 2026-10-10): licencja AGPLv3 ([ADR-0002](decisions/0002-licencja-juce.md)) — `LICENSE` (pełny tekst), `packaging/LICENSE.txt` (nota, adres kodu źródłowego i pełny tekst; pokazywany i instalowany przez instalatory), sekcja JUCE w `THIRD-PARTY-NOTICES.txt` odsyła do niego i do kodu źródłowego; identyfikatory zamrożone ([ADR-0016](decisions/0016-identyfikatory-nazwy.md)).
2. Testy w hostach według [testing.md](testing.md) (TST-3) — przed wydaniem zwykłym; 1.0.0 i 1.1.0 są wstępne i ich jeszcze nie ma.
3. Wersja w `CMakeLists.txt` i sekcja w `CHANGELOG.md` z datą: `## [X.Y.Z] - RRRR-MM-DD`.
4. `git tag vX.Y.Z && git push origin vX.Y.Z` albo *Actions → Release → Run workflow* z zaznaczonym **publish** (workflow sam tworzy tag `vX.Y.Z` na tym commicie — tak powstały 1.0.0 i 1.1.0) → workflow „Release” (bramka, build, testy, pluginval, walidator VST3, instalatory, archiwum źródeł) → opublikowane wydanie GitHub → sprawdzenie instalatorów (instalacja, deinstalacja, `spctl`/właściwości pliku). Wydanie można potem edytować albo cofnąć do szkicu na GitHubie. Ręczne uruchomienie bez **publish** buduje i sprawdza instalatory (artefakty przebiegu) bez wydania.

Archiwum źródeł to kod odpowiadający binarkom w rozumieniu AGPLv3 (§6): wszystkie pliki z `git ls-files --recurse-submodules` (wtyczka, JUCE, pffft, Catch2), w katalogu `SamSpectralControl-X.Y.Z/`; buduje się z niego tak samo jak z repozytorium (`cmake -S SamSpectralControl-X.Y.Z -B build`).

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
- komponenty (*Customize*): VST3 → `/Library/Audio/Plug-Ins/VST3`, Audio Unit (gdy zbudowany) → `/Library/Audio/Plug-Ins/Components` i zawsze instalowany „Uninstaller, licences and manual” → `/Library/Application Support/Sam Spectral Control`;
- bundle nie są relokowalne (instalator zawsze pisze do folderów systemowych);
- deinstalator: `Uninstall Sam Spectral Control.command` usuwa bundle, swój folder i paragony `pkgutil` (presety użytkownika w `~/Documents/Sam Spectral Control` zostają);
- macOS 11+, Apple silicon i Intel (universal binary);
- `DRY_RUN=1` wypisuje wywołania narzędzi Apple zamiast je uruchamiać (sprawdzenie skryptu i `distribution.xml` poza macOS).

**Windows** — [`packaging/windows/SamSpectralControl.iss`](../packaging/windows/SamSpectralControl.iss) (Inno Setup 6):
- `iscc /DAppVersion=X.Y.Z /DArtefacts=<…\SamSpectralControl_artefacts\Release> /DOutputDir=<katalog> packaging\windows\SamSpectralControl.iss`;
- VST3 → `C:\Program Files\Common Files\VST3\Sam Spectral Control.vst3` (komponent stały), licencje → `C:\Program Files\Sam Audio\Sam Spectral Control` (zawsze), instrukcja obok (komponent do wyboru);
- deinstalator w *Aplikacje i funkcje*; Windows 10/11 x64; `AppId` jest stały (ADR-0016).

## Licencje zewnętrzne i instrukcja (REL-4)

- `THIRD-PARTY-NOTICES.txt` w katalogu głównym generuje `python3 tools/third-party-notices.py` z plików licencji w `external/` (wersje z `external/JUCE/JUCE.spdx.json` i commitów submodułów). Lista odpowiada temu, co jest w binarce: JUCE, pffft, zlib, HarfBuzz, SheenBidi, LunaSVG, PlutoVG, libpng, biblioteka JPEG IJG, VST 3 SDK, rozszerzenia PreSonus (domena publiczna) i — tylko w komponencie Audio Unit — AudioUnitSDK. Nieużywane kodeki JUCE (FLAC, Ogg Vorbis, Opus, MP3) i WebP są wyłączone w `src/CMakeLists.txt`, więc ich kod nie trafia do wtyczki (sprawdzone `nm` na binarce).
- Po aktualizacji JUCE lub pffft: `python3 tools/third-party-notices.py` i commit; `--check` (krok CI na Linuksie i `local-check.sh`) wykrywa nieaktualny plik.
- Instalatory zawsze instalują `LICENSE.txt` i `THIRD-PARTY-NOTICES.txt`; instrukcja [manual.md](manual.md) trafia obok jako `Manual.md` razem z obrazami, do których się odwołuje (`images/editor.png`, `editor-advanced.png`, `sidechain.png`, `solo.png`, `upward.png`; lista w `packaging/windows/SamSpectralControl.iss` i `packaging/macos/build-pkg.sh`) (? → *User Manual* w nagłówku otwiera ten plik). Zrzuty ekranu odświeża się po zmianach interfejsu (build z `-DSSC_BUILD_TOOLS=ON`, na Linuksie pod `xvfb-run -a`):

```bash
BANDS="b01_used=1 b01_shape=3 b01_freq=60 b01_slope=3 b02_used=1 b02_freq=250 b02_gain=-3 b02_q=1.2 \
    b03_used=1 b03_freq=3000 b03_gain=3 b03_q=0.9 b03_mode=1 b03_threshold=-30 b04_used=1 b04_shape=2 \
    b04_freq=10000 b04_gain=2.5 b05_used=1 b05_freq=6500 b05_q=2.5 b05_mode=2 b05_threshold=6 b05_threshold_mode=1"
ssc_screenshot docs/images/editor.png 1100 660 $BANDS select=3
ssc_screenshot docs/images/editor-advanced.png 1100 660 $BANDS select=3 advanced=1 globaladvanced=1
ssc_screenshot docs/images/editor-860x480.png 860 480 $BANDS select=3
```

`advanced=1` otwiera panel Advanced pasma, `globaladvanced=1` globalny, `motion=0|1|2` ustawia animacje (Full / Reduced / Off). `docs/images/ui-1.0-*.png` to zrzuty interfejsu 1.0 do porównania w research §15 — nie odświeża się ich.

## Walidacja (TST-2)

- **pluginval** 1.0.4, strictness 10, na trzech systemach (także test edytora pod Xvfb na Linuksie).
- **Walidator VST3 Steinberga** z VST3 SDK 3.8.1 (MIT): [`tools/ci/vst3-validator.sh`](../tools/ci/vst3-validator.sh) klonuje SDK, buduje tylko `validator` i uruchamia testy rozszerzone (`-e`) — lokalnie 537/537.
- **auval** — gdy budowany jest Audio Unit (`SSC_FORMAT_AU`).

## Minuty GitHub Actions i weryfikacja lokalna

Od wydania 1.0.0 repozytorium jest publiczne, a w repozytoriach publicznych standardowe runnery GitHub Actions (także macOS i Windows) są bezpłatne. W repozytorium prywatnym minuty macOS liczą się ×10, a Windows ×2: pełne CI to ok. 155 minut rozliczeniowych, sam Linux (`platforms: linux`) — ok. 35, a po wyczerpaniu miesięcznego limitu zadania w ogóle nie startują. Dlatego `ci.yml` nie uruchamia się na pushach do gałęzi roboczych ani dla zmian samej dokumentacji, tylko dla pull requestów, pushy do `main`/`master` i ręcznie (Actions → CI → *Run workflow*, z wyborem `all` albo `linux`). Workflow wydania startuje tylko dla tagów i ręcznie.

Te same kontrole działają lokalnie, bez minut:

```bash
tools/ci/local-check.sh          # format, licencje, build z -Werror, testy (stos 1 MB), pluginval, walidator VST3, RTSan, ASan/UBSan
tools/ci/local-check.sh --quick  # tylko format, licencje, build i testy
```

Skrypt pomija kroki, do których brakuje narzędzi (licencje: `python3`; pluginval: zmienna `PLUGINVAL` lub `pluginval` w `PATH`; RTSan: `clang++-20`; ASan/UBSan: `clang++`), i podaje to w podsumowaniu. Na macOS i Windows (Visual Studio) build sprawdza się tylko na tych systemach — lokalnie albo w CI po odnowieniu limitu. Pliki workflow można sprawdzić bez uruchamiania [actionlint](https://github.com/rhysd/actionlint): `actionlint .github/workflows/*.yml`.
