# ADR-0014: Instalatory i podpisy

- **Status:** Zaakceptowana (M0, 2026-10-03)
- **Wymagania:** REL-1…REL-3

## Decyzja
**macOS**
- Universal binary (arm64 + x86_64); hardened runtime; podpis `codesign` certyfikatem **Developer ID Application**.
- Instalator `.pkg`: `pkgbuild` (komponenty: VST3, AU gdy włączony) + `productbuild` z `distribution.xml` (wybór komponentów), podpis **Developer ID Installer**.
- Notaryzacja `xcrun notarytool submit --wait` + `xcrun stapler staple`.
- Ścieżki: `/Library/Audio/Plug-Ins/VST3/`, `/Library/Audio/Plug-Ins/Components/`.
- Deinstalacja: skrypt usuwający bundle i receipts (`pkgutil --forget`).

**Windows**
- Bundle VST3 (format katalogowy Steinberga) w `C:\Program Files\Common Files\VST3\`.
- Instalator **Inno Setup** z deinstalatorem; podpis Authenticode instalatora i binarek: **Azure Artifact Signing** (dawniej Trusted Signing) albo certyfikat OV. Reputacja SmartScreen buduje się z czasem (także dla certyfikatów EV od 2024).

**CI**
- `release.yml`: budowanie, testy, podpisy i notaryzacja z sekretów GitHub (certyfikaty, Apple ID / klucz API App Store Connect, dane Azure). Bez sekretów workflow tworzy niepodpisane artefakty (do testów wewnętrznych).

## Wymagania po stronie użytkownika
- Konto Apple Developer (certyfikaty Developer ID Application/Installer, klucz API do notaryzacji).
- Konto Azure Artifact Signing albo certyfikat OV do podpisu na Windows.

## Realizacja (M8)
- `packaging/macos/build-pkg.sh` (pkgbuild z nierelokowalnymi bundle, productbuild z `distribution.xml` — komponenty VST3 / AU / deinstalator z licencjami; codesign z hardened runtime, notarytool z kluczem API, stapler, spctl), `packaging/macos/uninstall.command`.
- `packaging/windows/SamSpectralControl.iss` (Inno Setup 6: VST3 w `{commoncf64}\VST3`, komponent z instrukcją i licencjami, deinstalator, stały `AppId`).
- `.github/workflows/release.yml`: tag `vX.Y.Z` albo ręcznie; podpisy tylko przy obecnych sekretach (Developer ID z `.p12`, klucz API notaryzacji; Azure Artifact Signing przez `azure/artifact-signing-action@v2`), bez nich niepodpisane artefakty; szkic wydania z CHANGELOG i SHA-256; bramka: wersja = tag, sekcja CHANGELOG, ADR-0002 i ADR-0016 zaakceptowane, licencja nie tymczasowa. Szczegóły i lista sekretów: [docs/release.md](../release.md).

## Rozważane alternatywy
- **WiX** — bardziej złożony; Inno Setup wystarcza dla wtyczek.
- **DMG z ręczną instalacją** — gorsze UX i brak kontroli ścieżek; odrzucone.
