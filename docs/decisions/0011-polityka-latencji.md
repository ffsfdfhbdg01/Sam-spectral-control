# ADR-0011: Polityka latencji

- **Status:** Zaakceptowana (M0, 2026-10-03) — doprecyzowuje ARC-3
- **Wymagania:** ARC-3, DYN-8, GLB-1, OS-3

## Kontekst
ARC-3 wymaga, by latencja zmieniała się tylko po zmianie trybu fazy, Quality, oversamplingu lub Max Lookahead. Silnik STFT (pasma Spectral) wnosi latencję ≈ N_fft (40–170 ms). Gdyby była doliczana zawsze, instancja bez pasm Spectral straciłaby zerową latencję.

## Decyzja
- Latencja = OS + Natural Phase (D) + Max Lookahead + STFT (tylko gdy istnieje ≥ 1 pasmo w trybie Spectral).
- Latencja zmienia się wyłącznie przy **jawnych zmianach konfiguracji przez użytkownika**: tryb fazy, Quality, OS, Max Lookahead, **włączenie/wyłączenie silnika STFT** (pierwsze/ostatnie pasmo Spectral).
- Parametr **Mode** pasma jest **nieautomatyzowalny** (VST3 bez flagi `kCanAutomate`), żeby automatyzacja nie zmieniała latencji w trakcie odtwarzania.
- Zmiana latencji: `setLatencySamples` → host (`restartComponent(kLatencyChanged)`); UI pokazuje aktualną latencję i krótki komunikat przy zmianie.
- Linie opóźniające toru suchego (bypass, Mix, kanały nieobjęte pasmami) prealokowane dla maksymalnej możliwej latencji.

## Konsekwencje
- Zero latencji w Minimum Phase bez pasm Spectral i bez lookahead.
- Dodanie pierwszego pasma Spectral zmienia latencję — zachowanie udokumentowane w manualu.

## Rozważane alternatywy
- **Stała latencja STFT zawsze** — utrata trybu zero-latency; odrzucone.
- **Globalny przełącznik „Spectral engine”** — dodatkowa komplikacja UI; odrzucone na rzecz automatycznego włączania przy pierwszym paśmie Spectral.
- **Spectral w Minimum Phase tylko w trybie Low Latency** — zależne od P1; rozważyć po M3.
