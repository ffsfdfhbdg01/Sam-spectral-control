## Pobieranie i instalacja

| System | Plik |
|---|---|
| macOS 11 lub nowszy (Apple silicon i Intel) | `SamSpectralControl-@VERSION@-macOS.pkg` |
| Windows 10 lub 11, 64-bit | `SamSpectralControl-@VERSION@-Windows.exe` |

Instalator umieszcza wtyczkę VST3 w folderze systemowym: `/Library/Audio/Plug-Ins/VST3` (macOS) albo `C:\Program Files\Common Files\VST3` (Windows). Po instalacji wtyczka jest w DAW jako **Sam Audio → Sam Spectral Control** (jeśli jej nie widać, przeskanuj wtyczki ponownie). Deinstalacja: na macOS `Uninstall Sam Spectral Control.command` w `/Library/Application Support/Sam Spectral Control`, na Windows *Ustawienia → Aplikacje*. Instrukcja obsługi jest instalowana razem z wtyczką i otwiera się z menu **?** w jej nagłówku.

Sumy kontrolne SHA-256 wszystkich plików są w `SHA256SUMS.txt`.

**Kod źródłowy.** Sam Spectral Control jest wolnym oprogramowaniem na licencji GNU AGPLv3. Archiwum `SamSpectralControl-@VERSION@-source.tar.gz` zawiera pełny kod tej wersji razem z JUCE i pozostałymi bibliotekami (submoduły); to samo jest w repozytorium pod tagiem `v@VERSION@`.
