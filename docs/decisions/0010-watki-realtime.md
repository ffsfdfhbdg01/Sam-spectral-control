# ADR-0010: Wątki, komunikacja i real-time safety

- **Status:** Zaakceptowana (M0, 2026-10-03)
- **Wymagania:** ARC-4, PLT-5, NFR-3, PR-2, MTR-4

## Decyzja
- **Determinizm**: wszystko, co wpływa na audio, liczy się w wątku audio (także Auto Gain, korektor Natural Phase, tabele gainów). Wątki tła liczą wyłącznie dane do wyświetlania.
- **Wątki**: audio (host), analiza (własne wątki tła: analizator z detektorem rezonansów, odczyty pasm, mierniki THD+N/aliasingu), UI (wątek komunikatów).
- **Komunikacja**: SPSC ring buffers (prealokowane; producent nadpisuje najstarsze dane), atomics dla skalarów (gain pasm, mierniki, CPU), triple buffers dla tablic (widma, maski, odczyty).
- **Prealokacja** w `prepareToPlay` dla konfiguracji maksymalnej (32 kanały, 24 pasma, 8× OS, Max).
- **Weryfikacja**: RealtimeSanitizer (Clang 20+, atrybut `[[clang::nonblocking]]` na `processBlock` i ścieżce DSP) w CI na Linuksie; ASan/UBSan w testach; TSan dla testów komunikacji między wątkami.
- Wątek analizy śpi, gdy edytor jest zamknięty.

## Konsekwencje
- Utrata ramek wizualizacji przy przeciążeniu jest akceptowalna; audio nigdy nie czeka.
- Odczyty (§12) liczone tym samym kodem DSP w osobnych instancjach (bez współdzielenia stanu z wątkiem audio).
- Mierniki (§13, M7): wątek audio tylko nagrywa wejścia nieliniowości pasm w oknie na końcu okresu (zmienne lokalne w pętli, migawka przez triple buffer; narzut ≈ 1,5–5 % czasu DSP przy otwartym edytorze, zero bez niego); analiza w wątku `MeterService`, który przy przeciążeniu pomija migawki (uśrednianie uwzględnia upływ czasu).

## Rozważane alternatywy
- **Mutexy z try_lock w wątku audio** — niedeterministyczne pominięcia; odrzucone.
- **Auto Gain liczony w wątku tła** — łamie determinizm renderu offline; odrzucone.
