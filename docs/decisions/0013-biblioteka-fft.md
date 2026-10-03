# ADR-0013: Biblioteka FFT

- **Status:** Zaakceptowana (M0, 2026-10-03) — bramka benchmarku FFT do wykonania
- **Wymagania:** DYN-9, AG-3, VIS-1, NFR-1

## Decyzja
- **pffft** (licencja BSD-podobna, SIMD SSE/NEON) jako podstawowa FFT dla STFT, Auto Gain i analizy — submoduł w `external/pffft`.
- **`juce::dsp::FFT`** jako implementacja zapasowa za tym samym interfejsem `IFft`.
- **Bramka M1**: benchmark rozmiarów 1024–16384 (real FFT forward+inverse) na macOS (arm64, x86_64), Windows x64 i Linuksie; jeśli inna opcja jest > 1,5× szybsza przy zgodnej licencji — nowy ADR.

## Rozważane alternatywy
- **FFTW** (GPL albo licencja komercyjna) — odrzucone z powodu licencji.
- **KFR** (GPL albo komercyjna) — odrzucone z powodu licencji.
- **vDSP (Accelerate)** — tylko macOS; możliwy backend platformowy za `IFft`.
- **Intel IPP** — duża zależność; opcjonalny backend.
