# ADR-0003: Hybrydowa architektura DSP

- **Status:** Proponowana (M0)
- **Wymagania:** ARC-1, DYN-1…DYN-3, PH-1

## Kontekst
Wtyczka łączy wymagania sprzeczne dla jednego typu przetwarzania: zerowa latencja i analogowa faza (Minimum Phase), przetwarzanie per częstotliwość w paśmie (Spectral) i modelowanie analogowe z nieliniowościami.

## Decyzja
1. **Łańcuch czasowy** dla pasm Static i Dynamic: filtry SVF-TPT parametryzowane projektami dopasowanymi (ADR-0004), z nieliniowościami Character/Analog w obrębie pasm. Działa w domenie oversamplingu.
2. **Silnik spektralny** (STFT/WOLA) dla pasm Spectral: jedna wspólna maska wzmocnień na lane (ADR-0006).
3. **Korektor Natural Phase**: krótki FIR na wyjściu łańcucha czasowego (ADR-0005).
4. Kolejność: łańcuch czasowy → korektor → STFT → Auto Gain → Output/Mix/Bypass.

## Konsekwencje
- Zerowa latencja jest możliwa, dopóki nie ma pasm Spectral (ADR-0011).
- Koszt STFT nie rośnie z liczbą pasm Spectral.
- Dwa „światy” przetwarzania wymagają spójnych definicji kształtu pasma — kształt pasma w STFT (ważenie w_b[k]) liczony z tej samej funkcji przenoszenia co w łańcuchu czasowym.

## Rozważane alternatywy
- **Całość w dziedzinie częstotliwości** (jak DSEQ) — duża latencja i pre-ringing także dla pasm statycznych; brak trybu zero-latency. Odrzucone.
- **Całość w dziedzinie czasu z bankiem filtrów** dla trybu Spectral — słaba selektywność i trudna rekonstrukcja. Odrzucone.
