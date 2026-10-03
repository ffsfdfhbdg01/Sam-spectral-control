# ADR-0007: Model głośności dla Auto Gain

- **Status:** Proponowana (M0)
- **Wymagania:** AG-1…AG-6

## Kontekst
Tryb „Fletcher” ma uwzględniać krzywe jednakowej głośności. Krzywe Fletchera–Munsona (1933) są historyczne; obowiązującą normą jest ISO 226:2023. Tabele 2023 są dostępne w płatnej normie; formuła i tabele 2003 są szeroko publikowane, a różnice 2003↔2023 nie przekraczają 0,6 dB (Suzuki, Takeshima, Kurakata 2024).

## Decyzja
- **Fletcher**: pasma 1 ERB → kalibracja SPL (domyślnie K-20; P1 parametr Listening level) → poziom głośności z odwrotnej formuły ISO 226 → sony → suma → różnica wejście/wyjście w dB.
- Formuła i tabele **ISO 226:2003** w pliku danych wymienialnym na tabele **2023** po zakupie/licencji normy. W UI tryb nazywa się „Fletcher” (zgodnie z wymaganiem), a manual wyjaśnia, że używa współczesnych krzywych ISO.
- **Digital**: nieważone RMS.
- **BS.1770 (K-weighting)** — tylko jako kontrola krzyżowa w testach (AG-6).
- Obliczenia w wątku audio (FFT 4096, hop 1024 dla wejścia i wyjścia) — deterministycznie (NFR-3).
- Szybkości, tryb Stable (krzywa EQ × LTAS) i make-up pasm — jak w research §7.

## Konsekwencje
- Zgodność z ISO 226:2023 w granicach 0,6 dB do czasu podmiany tabel.
- Koszt: 2 FFT 4096 na 1024 próbki dla grupy kanałów przetwarzanych.

## Rozważane alternatywy
- **Pełny ISO 532-3** — dokładniejszy dla dźwięków zmiennych w czasie, ale kosztowny; P1 jako wariant High/Max.
- **K-weighting jako „Fletcher”** — nie zależy od poziomu i nie odwzorowuje krzywych jednakowej głośności; odrzucone.
