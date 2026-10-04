# Testy w hostach — checklista (TST-3)

Testy ręczne, których nie da się wykonać w CI (Atmos, AAX, interakcja w DAW). Wypełniane od M5 (surround) i w M8 (wydanie). Legenda: ✅ działa · ⚠️ działa z uwagami · ❌ błąd · — nie dotyczy.

## Macierz hostów

| Host | System | Format | Stereo | 5.1 | 7.1.4 | 9.1.6 | Automatyzacja | Render offline | Uwagi |
|---|---|---|---|---|---|---|---|---|---|
| Cubase Pro / Nuendo | macOS / Windows | VST3 | | | | | | | Atmos (renderer wbudowany) |
| Reaper | macOS / Windows / Linux | VST3 | | | | | | | dowolna liczba kanałów |
| Ableton Live | macOS / Windows | VST3 | | — | — | — | | | tylko stereo |
| Studio One | macOS / Windows | VST3 | | | | | | | Atmos |
| Bitwig Studio | macOS / Windows / Linux | VST3 | | | | | | | |
| FL Studio | macOS / Windows | VST3 | | — | — | — | | | |
| DaVinci Resolve (Fairlight) | macOS / Windows | VST3 | | | | | | | Atmos |
| Logic Pro | macOS | AU (P1) | | | | | | | Atmos 7.1.4 |
| Pro Tools | macOS / Windows | AAX (P2) | | | | | | | Atmos do 9.1.6 |

## Scenariusze dla każdego hosta

1. Wstawienie wtyczki na ścieżkę mono, stereo i szynę surround (tam, gdzie host pozwala).
2. Zapis i ponowne otwarcie projektu — identyczny stan.
3. Automatyzacja: Frequency, Gain, Q, Threshold, Output, On/Off — bez trzasków.
4. Zmiana trybu fazy, Quality, oversamplingu i dodanie pasma Spectral — host poprawnie kompensuje latencję (test impulsem z równoległą ścieżką bez wtyczki).
5. Render offline vs odtwarzanie — identyczny wynik (null test).
6. Bypass z hosta i przyciskiem On/Off — bez trzasków, stała latencja.
7. Surround: cel pasma na wybranych głośnikach; kanały nieobjęte pasmami nietknięte (null test).
   - Panel „Speakers” w panelu pasma (widoczny przy > 2 kanałach): kliknięcie głośnika, szybkie wybory All/None/Bed/Tops/Front/Surround, LFE; każda zmiana to jeden krok Undo.
   - Preset/projekt zapisany w stereo z pasmem „All + Side” otwarty na szynie 7.1.4 działa na Side każdej pary (L/R, Lss/Rss, Lrs/Rrs, Ltf/Rtf, Ltr/Rtr), C i LFE pozostają nietknięte.
   - CH-2 (Atmos): ścieżka/szyna bed 7.1.2 i 7.1.4 w Nuendo/Cubase, 7.1.4 w Resolve Fairlight, 9.1.6 w Nuendo — wtyczka się ładuje, przetwarza, a latencja jest kompensowana.
8. Skalowanie okna, HiDPI, kilka instancji jednocześnie, zamykanie i otwieranie edytora podczas odtwarzania.
