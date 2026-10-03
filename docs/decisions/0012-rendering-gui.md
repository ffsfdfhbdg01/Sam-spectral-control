# ADR-0012: Rendering GUI

- **Status:** Zaakceptowana (M0, 2026-10-03)
- **Wymagania:** VIS-2, VIS-3, VIS-6, VIS-7

## Decyzja
- **Natywne renderery JUCE 9** (Direct2D na Windows, CoreGraphics na macOS; ulepszony renderer programowy) zamiast własnej ścieżki GPU na start.
- Odświeżanie zsynchronizowane z ekranem przez `juce::VBlankAttachment`; rysowanie tylko gotowych danych z triple bufferów.
- **Glow**: rendering do `juce::Image` w połowie rozdzielczości, separowalne rozmycie Gaussa (cache kerneli i buforów), złożenie z wypełnieniem pod krzywą widma.
- Skalowalne okno (HiDPI), zapamiętywany rozmiar; tokeny kolorów i typografii w jednym miejscu (`ui/Theme`).
- **Bramka wydajności (M1/M7)**: p95 czasu klatki ≤ 16,7 ms przy 1920×1080 na maszynach referencyjnych. Jeśli bramka nie przejdzie — ścieżka OpenGL (`juce::OpenGLContext`) dla wyświetlacza widma jako osobny ADR.

## Konsekwencje
- Brak zależności od przestarzałego OpenGL na macOS, dopóki nie okaże się konieczny.
- Efekty glow muszą być tanie (niska rozdzielczość, cache) — weryfikowane profilowaniem.

## Rozważane alternatywy
- **OpenGL od początku** — dodatkowa złożoność i przestarzałe API na macOS; zostaje jako plan B.
- **Biblioteka visage (GPU)** — dodatkowa zależność i integracja z JUCE; do ewentualnej oceny, jeśli plan B nie wystarczy.
