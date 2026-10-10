
### To wydanie nie jest podpisane cyfrowo

Instalatory nie mają podpisu Apple Developer ID ani podpisu Authenticode, dlatego system ostrzega przed ich pierwszym uruchomieniem. To wydanie wstępne (*pre-release*).

- **macOS**: po dwukliku pojawi się komunikat, że Apple nie może sprawdzić pliku. Zamknij go (*Gotowe*), otwórz **Ustawienia systemowe → Prywatność i ochrona**, w sekcji **Ochrona** kliknij **Otwórz mimo to** przy `SamSpectralControl-@VERSION@-macOS.pkg` i potwierdź hasłem. W macOS 15 i nowszych otwieranie przez Control-klik już nie omija tej blokady.
- **Windows**: SmartScreen pokaże okno „System Windows ochronił ten komputer”. Kliknij **Więcej informacji → Uruchom mimo to**.

Przed uruchomieniem możesz porównać sumę SHA-256 pobranego pliku z `SHA256SUMS.txt` (macOS: `shasum -a 256 <plik>`, Windows: `certutil -hashfile <plik> SHA256`).
