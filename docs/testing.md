# Testy w hostach (TST-3) i testy obciążeniowe (TST-4)

## Testy w hostach — checklista (TST-3)

Testy ręczne, których nie da się wykonać w CI (Atmos, AAX, interakcja w DAW). Wypełniane od M5 (surround) i w M8 (wydanie). Legenda: ✅ działa · ⚠️ działa z uwagami · ❌ błąd · — nie dotyczy.

### Macierz hostów

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

### Scenariusze dla każdego hosta

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

## Testy obciążeniowe — automatyczne (TST-4)

`tests/plugin/TestStress.cpp`, tag `[TST-4]`, ok. 15 s w Release. Wtyczka jest wywoływana tak jak przez hosta (`setBusesLayout`, `prepareToPlay`, `processBlock`). Ustawienie „ciężkie”: 8 pasm (Low Cut 24 dB/okt., dwa pasma Dynamic z lookahead i Auto Gain pasma, Bell Warm na Mid, High Shelf Sharp na Side, pasmo Spectral, Notch Q 20, High Cut 48 dB/okt.), globalny Auto Gain, Max Lookahead 5 ms. Każdy przebieg sprawdza: wyjście skończone, bez wartości subnormalnych i ograniczone; żaden blok nie został wyciszony przez ochronę NaN/Inf; **zero alokacji** w każdym wywołaniu `processBlock` (podmieniony `operator new`, `tests/AllocationCounter.cpp`).

| Scenariusz | Zakres | Dodatkowo sprawdzane |
|---|---|---|
| Układy kanałów | 20 akceptowanych układów: mono, stereo, LCR, LCRS, quad, 5.0–7.1, 5.1.2–9.1.6, discrete 1 / 24 / 32; Natural Phase, 2×, High | LFE (żadne pasmo go domyślnie nie wybiera) = wejście opóźnione o latencję, bit w bit (CH-6) |
| Częstotliwości próbkowania | 44,1 / 48 / 88,2 / 96 / 176,4 / 192 / 352,8 / 384 kHz × Minimum/Natural Phase × OS Off/2×/8× × Quality Normal/Max (przy Max także pasmo Spectral) | środek ciężkości odpowiedzi impulsowej = raportowana latencja (faza liniowa dokładnie, IIR ±0,5/k próbki); OS ograniczony do 400 kHz; Mix 0 % = wejście opóźnione o latencję; ustawienie ciężkie we wszystkich trybach fazy, w tym Analog Feel |
| Bufory | bloki 1, 2, 3, 7, 16, 32, 64, 100, 128, 256, 441, 1000, 1024, 2048, 4096, 8192 (`prepareToPlay` z tym samym rozmiarem); losowe 0–8192 (puste bloki i bloki większe niż zapowiedziane); float i double | wyjście bit w bit równe renderowi blokami 512 (NFR-3: render offline = czas rzeczywisty) |
| Szybka automatyzacja | wszystkie automatyzowalne parametry 8 pasm i globalne zmieniane losowo w każdym bloku (1–256 próbek, 1500 bloków); co 25 bloków parametry nieautomatyzowalne (tryb pasma, faza, Quality, OS, Max Lookahead), Solo; stereo i 5.1 | po zatrzymaniu automatyzacji i ciszy na wejściu sygnał zanika — filtry pozostały stabilne przy szybkiej modulacji |
| Zmiana układu w trakcie sesji | stereo → 7.1.4 → mono → 5.1 → 9.1.6 → 32 kanały → LCR → stereo, przy 44,1–384 kHz i blokach 32–8192, z `releaseResources` i bez; analizator, mierniki i odczyty pracują w tle jak przy otwartym edytorze | wybór głośników pasma (Centre) zachowany; LFE dokładnym opóźnieniem; latencja stała |
| Błędy hosta | `processBlock` przed `prepareToPlay`, puste bloki float i double | bez awarii; przed przygotowaniem bufor zostaje nietknięty |

### Real-time safety — RealtimeSanitizer (PLT-5)

Preset `rtsan` (Clang 20, `-fsanitize=realtime`): `SscAudioProcessor::processImpl` i `Engine::process` są oznaczone `SSC_REALTIME` (`[[clang::nonblocking]]`), więc każda alokacja, blokada lub wywołanie blokujące w ich trakcie przerywa test z raportem. Pod RTSan przechodzą wszystkie testy rdzenia i wtyczki (181, w tym TST-4); w CI robi to zadanie „Real-time safety”. Lokalnie:

```bash
CC=clang-20 CXX=clang++-20 cmake --preset rtsan
cmake --build --preset rtsan
ctest --preset rtsan
```

### Pamięć — ASan/UBSan

Preset `sanitize` (także w `tools/ci/local-check.sh`) obejmuje testy rdzenia DSP (127). Testy wtyczki, łącznie z interfejsem (`[ui]`, `[UX]`), sprawdza się pod ASan/UBSan na żądanie, w buildzie Debug z instrumentacją także kodu JUCE (na 4 rdzeniach budowa ok. 5 min, testy ok. 4 min):

```bash
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake -S . -B build/asan-plugin -G Ninja -DCMAKE_BUILD_TYPE=Debug -DSSC_BUILD_PLUGIN=ON \
  -DCMAKE_CXX_FLAGS="$SAN" -DCMAKE_C_FLAGS="$SAN" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" -DCMAKE_SHARED_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build/asan-plugin --target ssc_plugin_tests
ulimit -s 65536
build/asan-plugin/tests/plugin/ssc_plugin_tests_artefacts/Debug/ssc_plugin_tests
```

Ostatni przebieg (interfejs z research §15): 54 z 54, bez zgłoszeń ASan, UBSan i LeakSanitizer. Test podpowiedzi rezonansów podaje dane w tempie wątku analizy, a nie zegara — wolniejszy build nie gubi próbek w pełnym FIFO.

