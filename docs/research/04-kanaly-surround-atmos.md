# §4 Kanały, surround i Dolby Atmos (CH-1…CH-6)

## a) Literatura i normy

- **ITU-R BS.775** — referencyjny układ 5.1 (wielokanałowy dźwięk stereofoniczny).
- **ITU-R BS.2051** — zaawansowane (immersyjne) układy głośników do produkcji, m.in. z warstwą wysokościową.
- **Dolby Atmos** — bed do **7.1.2**, obiekty (mono/stereo) pozycjonowane przez renderer; monitorowanie i re-rendering do **9.1.6** w produkcji muzycznej i domowej. Nazewnictwo głośników Dolby: L, R, C, LFE, Lss, Rss, Lrs, Rrs, Lw, Rw, Ltf, Rtf, Ltm, Rtm, Ltr, Rtr.
- **VST3 SDK** — `Steinberg::Vst::SpeakerArrangement` to 64-bitowa maska głośników (m.in. `kSpeakerL/R/C/Lfe/Ls/Rs/Sl/Sr/Tfl/Tfr/Trl/Trr/Tsl/Tsr/Lw/Rw/Lfe2`). Predefiniowane układy obejmują m.in. 7.1.4 (`k71_4`, 12 kanałów), 9.1.6 (16 kanałów) i 22.2 (`k222`).
- **Macierz Mid/Side** (Blumlein; opis w: U. Zölzer (red.), *DAFX: Digital Audio Effects*, 2. wyd., Wiley 2011): M = (L + R)/2, S = (L − R)/2; L = M + S, R = M − S. Rekonstrukcja jest dokładna z dokładnością do zaokrągleń zmiennoprzecinkowych.
- Znane pułapki mapowania kanałów JUCE ↔ VST3 (wątki na forum JUCE: „VST3 Speaker types to/from Juce Channel types wrapping mistakes”) — wymagają testów w hostach, a nie tylko w jednostkowych testach układów.

## b) Wtyczki referencyjne

- **FabFilter Pro-Q 4** — surround do Dolby Atmos 9.1.6 (zależnie od DAW i formatu). Panel głośników per pasmo: domyślnie zaznaczone wszystkie głośniki **poza LFE**; przycisk „All” przełącza dołączenie LFE; kliknięcie rzędu wybiera np. tylko Lss/Rss albo Center; L/R + C można łączyć (krzywa działa na L/C/R). Stereo placement (L/R/M/S) działa na wybranych głośnikach.
- **oeksound soothe3** — obsługa multichannel do 9.1.6.

## c) Wybrane podejście

### Obsługiwane układy
- `isBusesLayoutSupported`: wejście i wyjście mają ten sam `AudioChannelSet`; akceptujemy mono, stereo, wszystkie nazwane układy surround/immersive z JUCE (w tym 7.1.2, 7.1.4, 7.1.6, 9.1.4, 9.1.6, 22.2) oraz układy **discrete** (kanały bez nazw) do **32 kanałów** (stała kompilacji `kMaxChannels = 32`, prealokacja).
- **„7.2.4”** nie istnieje jako standardowy układ Atmos. Nie zamykamy listy — każdy układ z rozpoznanymi typami kanałów (także z `LFE2`) jest obsługiwany przez model topologii poniżej.
- **Ambisonics — poza v1.0.** Przetwarzanie per kanał niszczy obraz przestrzenny ambisonii; układy ambisoniczne są odrzucane w `isBusesLayoutSupported` (P2: tryb „All, linked”).

### Model topologii kanałów
Z `AudioChannelSet` budujemy w `prepareToPlay` (poza wątkiem audio, bez alokacji w trakcie przetwarzania) strukturę `ChannelTopology`:

- **Kanały** z typem semantycznym (np. `Left`, `Centre`, `LFE`, `LeftSurroundSide`, `TopFrontLeft`, `Discrete(n)`).
- **Grupy**: Front (L/R), Center, LFE (LFE, LFE2), Side surround, Rear surround, Wides, Top front, Top middle, Top rear, Other.
- **Pary symetryczne**: (L,R), (Lss,Rss), (Lrs,Rrs), (Lw,Rw), (Ltf,Rtf), (Ltm,Rtm), (Ltr,Rtr), (Lc,Rc)…
- **Single**: C, LFE, LFE2, Cs, Tc, kanały discrete.

### Cel pasma (Target)
```
struct BandTarget {
    SpeakerMask speakers;     // semantyczne ID głośników (nie indeksy!), domyślnie All bez LFE
    Component   component;    // All | Left | Right | Mid | Side
};
```
- Cel zapisujemy **semantycznie**, więc preset przenosi się między układami: np. „All + Side” zapisane w stereo działa w 7.1.4 na składowej Side każdej wybranej pary.
- Reguły dla składowych:
  - **Left/Right** — lewy/prawy kanał każdej wybranej pary; single są pomijane.
  - **Mid/Side** — składowe M/S każdej wybranej pary; dla singli „Mid” = sam kanał, „Side” = brak działania.
  - **All** — każdy wybrany kanał niezależnie.
- **Lane** = sygnał przetwarzany przez pasmo (kanał albo składowa M/S pary). Pasmo ma własny stan filtrów dla każdej lane. Kodowanie/dekodowanie M/S odbywa się lokalnie w paśmie (kilka dodawań na próbkę), więc różne pasma mogą mieć różne cele bez kopiowania całego bufora.
- Zmiana celu w trakcie odtwarzania: nowy zestaw lane startuje z wyzerowanym stanem i krótkim crossfade’em (≈ 10 ms) z poprzednim.

### Linkowanie detekcji (pasma Dynamic/Spectral)
- Tryby: **Independent** (każda lane osobno), **Pair**, **Group**, **All** (domyślnie **Group**).
- Wspólny poziom detekcji = **maksimum** poziomów lane w zbiorze linkowania (zachowuje szczyty — standard w zlinkowanych kompresorach, Giannoulis i in. 2012), a z niego jedno wzmocnienie dla wszystkich lane w zbiorze.

### LFE
- „All” domyślnie **bez** LFE/LFE2; przełącznik w panelu dołącza je (jak w Pro-Q 4).

### Kanały nieobjęte pasmami (CH-6)
- Kanał, którego nie obejmuje żadne aktywne pasmo, omija cały tor przetwarzania (także oversampling i etapy nieliniowe) i jest jedynie opóźniany o latencję wtyczki.
- Etapy nieliniowe Analog Feel i Character są przypisane do pasm (działają na ich lane), a nie do całej wtyczki — dzięki temu nie barwią kanałów, których użytkownik nie przetwarza.
- Globalny Auto Gain mierzy i kompensuje głośność tylko kanałów objętych co najmniej jednym pasmem. Output gain i globalny Mix są jawnymi ustawieniami użytkownika i działają na wszystkie kanały.

### Odrzucone alternatywy
- **Cel zapisany jako indeksy kanałów** — preset nie przenosiłby się między układami; odrzucone.
- **Globalna konwersja całego bufora do M/S** — wymusza ten sam tryb na wszystkich pasmach; odrzucone na rzecz lokalnego M/S per pasmo.
- **Średnia (zamiast maksimum) w linkowaniu** — przepuszcza szczyty w pojedynczych kanałach; odrzucone jako domyślne (może wrócić jako opcja P2).

## d) Kryteria akceptacji

| ID | Kryterium | Metoda |
|---|---|---|
| CH-1 | `isBusesLayoutSupported` przyjmuje: mono, stereo, LCR, 5.0, 5.1, 7.1, 7.1.2, 7.1.4, 7.1.6, 9.1.6, 22.2, discrete 1–32; odrzuca ambisonię i układy różne na wejściu/wyjściu | test jednostkowy |
| CH-2 | Wtyczka ładuje się i przetwarza 7.1.4 i 9.1.6 w Nuendo/Cubase Pro oraz 7.1.4 w Resolve Fairlight | macierz hostów (`docs/testing.md`) |
| CH-3 | M/S: |L − (M+S)| i |R − (M−S)| ≤ 1 ulp skali sygnału (float/double) | test jednostkowy |
| CH-4 | Preset „All + Side” ze stereo działa w 7.1.4 na Side każdej wybranej pary; „Mid” na singlu = sam kanał | test jednostkowy |
| CH-5 | Link = Pair/Group/All → identyczne wzmocnienie w lane zbioru (różnica ≤ 1e-6 dB) | test jednostkowy |
| CH-6 | Przy Output = 0 dB i Mix = 100 % kanały nieobjęte żadnym pasmem są bitowo identyczne z wejściem opóźnionym o latencję — także przy włączonym oversamplingu, Analog Feel i Auto Gain | null test (porównanie bit-exact) |
