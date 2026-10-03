# ADR-0006: Silnik spektralny STFT

- **Status:** Proponowana (M0)
- **Wymagania:** DYN-3, DYN-9, DYN-10, PH-2

## Decyzja
- **STFT/WOLA**: okna √Hann analizy i syntezy; N_fft (48 kHz): Eco 2048, Normal 4096, High 8192, Max 8192; hop N/4 (Max: N/8); skalowanie N z fs.
- **Wspólna maska** na lane: G[k] = Σ_b g_b[k] (dB) dla wszystkich pasm Spectral; lane bez pasm Spectral omija silnik.
- **Detekcja**: poziom pasma 1 ERB wokół binu (dBFS); próg Absolute albo Relative (obwiednia: mediana w oknie ⅓–1 oktawy z wyłączeniem piku).
- **Komputer wzmocnienia**: ten sam co w Dynamic (research §6), ważony kształtem pasma w_b[k].
- **Wygładzanie**: w czasie (attack/release per bin, α z czasu hopu) i w częstotliwości (ERB; szerokość = Selectivity).
- **Antyzawijanie**: wygładzanie maski w częstotliwości; w Max dodatkowo okienkowanie odpowiedzi impulsowej maski.
- **FFT**: pffft (ADR-0013).
- **Latencja**: ≈ N_fft (dokładnie z implementacji), zgłaszana hostowi (ADR-0011).
- **Low Latency (P1)**: filtr minimalnofazowy z maski (cepstrum, 256–1024 tapy), splot z zerowym opóźnieniem (Gardner 1995), crossfade między ramkami.

## Konsekwencje
- Rozdzielczość w basie przy Eco jest ograniczona (≈ 23 Hz/bin przy 2048) — tryb Spectral dla niskich częstotliwości wymaga Normal+; UI to sygnalizuje.
- Koszt: 2 FFT na hop na lane z pasmami Spectral (+ 2 FFT w Max na okienkowanie maski).

## Rozważane alternatywy
- **sliCQ (stały Q)** — lepsza rozdzielczość w basie, większa złożoność i latencja; do oceny w M3 jako wariant High/Max.
- **Osobny STFT na pasmo** — koszt rośnie z liczbą pasm; odrzucone.
