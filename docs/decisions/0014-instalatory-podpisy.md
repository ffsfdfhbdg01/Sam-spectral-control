# ADR-0014: Instalatory i podpisy

- **Status:** Proponowana (M0)
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

## Rozważane alternatywy
- **WiX** — bardziej złożony; Inno Setup wystarcza dla wtyczek.
- **DMG z ręczną instalacją** — gorsze UX i brak kontroli ścieżek; odrzucone.
