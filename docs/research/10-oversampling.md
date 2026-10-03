# §10 Oversampling (OS-1…OS-5)

## a) Literatura i normy

- J. Kahles, F. Esqueda, V. Välimäki, „Oversampling for Nonlinear Waveshaping: Choosing the Right Filters”, JAES, 2019 — porównanie filtrów FIR/IIR do oversamplingu nieliniowości: tłumienie zaporowe, pasmo przejściowe, koszt, latencja.
- F. Esqueda, *Aliasing Reduction in Nonlinear Audio Signal Processing*, rozprawa doktorska, Aalto University, 2018.
- J. Parker, V. Zavalishin, E. Le Bivic, DAFx-16, 2016; S. Bilbao i in., IEEE SPL 24(7), 2017 — ADAA pozwala zmniejszyć wymagany współczynnik oversamplingu.
- R. A. Valenzuela, A. G. Constantinides, „Digital Signal Processing Schemes for Efficient Interpolation and Decimation”, IEE Proc. G 130(6), 1983 — półpasmowe filtry polifazowe z sekcjami all-pass (podstawa HIIR).
- L. de Soras, HIIR — biblioteka polifazowych półpasmowych filtrów IIR.

## b) Wtyczki i biblioteki referencyjne

- **juce::dsp::Oversampling** — kaskady 2× z filtrami `filterHalfBandFIREquiripple` (liniowa faza) albo `filterHalfBandPolyphaseIIR` (niska latencja); raportuje latencję.
- **soothe2** — uwaga terminologiczna: tam „oversampling” oznacza rozdzielczość częstotliwościową analizy spektralnej, a nie oversampling czasowy. W UI Sam Spectral Control „Oversampling” zawsze oznacza oversampling czasowy, a rozdzielczość STFT należy do Quality.

## c) Wybrane podejście

- **Ustawienie globalne**: Off (1×, domyślnie) / 2× / 4× / 8× — kaskada stopni 2×.
- **Zakres** (ARC-2): łańcuch pasm czasowych (filtry, dynamika, nieliniowości Character/Analog). Silnik STFT pracuje na bazowym fs.
- **Typ filtrów** zależny od trybu fazy:
  - Minimum Phase → polifazowy półpasmowy IIR (niska latencja; niewielkie zniekształcenia fazy blisko Nyquista),
  - Natural Phase / Analog Feel → półpasmowy FIR equiripple (liniowa faza, stała latencja),
  - P1: ręczny wybór typu filtra w ustawieniach zaawansowanych.
- **Specyfikacja filtrów** (cel startowy, weryfikacja wg Kahles i in. 2019):
  - Normal: pasmo przepustowe do 0,43·fs (≈ 19 kHz przy 44,1 kHz), zafalowania ≤ 0,05 dB, tłumienie ≥ 90 dB,
  - Max: pasmo do 0,45·fs (≈ 20 kHz), zafalowania ≤ 0,01 dB, tłumienie ≥ 120 dB.
- **Stała latencja**: gdy OS > 1×, oversampling działa zawsze (także gdy chwilowo nie ma aktywnej nieliniowości), żeby włączanie Character nie zmieniało latencji.
- **Implementacja**: na start `juce::dsp::Oversampling`; jeśli benchmark M4 pokaże przewagę HIIR (CPU) przy tej samej specyfikacji — podmiana za wspólnym interfejsem `IOversampler`.
- **Render offline (OS-4, P1)**: osobny współczynnik dla `isNonRealtime()`. Żeby nie zmieniać latencji, wszystkie warianty są dopełniane opóźnieniem do wspólnej, maksymalnej latencji (wymaga włączenia opcji przed startem renderu).
- **Podpowiedź UI (OS-5)**: gdy aktywne są nieliniowości, a miernik aliasingu (§13) przekracza −80 dB, w dolnym pasku pojawia się subtelna sugestia zwiększenia OS.

### Odrzucone alternatywy
- **Oversampling całej wtyczki (z STFT)** — mnoży koszt FFT bez korzyści; odrzucone.
- **Automatyczne włączanie OS tylko przy aktywnych nieliniowościach** — zmienia latencję w trakcie pracy; odrzucone.

## d) Kryteria akceptacji

| ID | Kryterium | Metoda |
|---|---|---|
| OS-1 | Aliasing (Warm, +12 dB, ton 10 kHz, 0 dBFS, 48 kHz): 1× ≤ −80 dB (ADAA), 2× ≤ −95 dB, 4× ≤ −110 dB, 8× ≤ −120 dB (cele do potwierdzenia w M4) | test pomiarowy |
| OS-2 | Zafalowania pasma przepustowego ≤ 0,05 dB do 19 kHz przy 44,1 kHz (Normal); ≤ 0,01 dB do 20 kHz (Max) | test jednostkowy |
| OS-3 | Latencja zgłaszana = zmierzona dla każdego współczynnika i typu filtra | test jednostkowy |
| OS-3 | Kanały nieobjęte pasmami omijają OS (CH-6) | null test |
| OS-5 | Sugestia pojawia się tylko przy aktywnej nieliniowości i aliasingu > −80 dB | test UI |
| — | Tabela CPU dla 1×/2×/4×/8× w `docs/measurements/` | benchmark |
