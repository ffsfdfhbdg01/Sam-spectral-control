# ADR-0009: Analog Feel — modele N/P/S i nazewnictwo

- **Status:** Zaakceptowana (M0, 2026-10-03)
- **Wymagania:** PH-1, PH-2, CHR-4, CH-6

## Decyzja
1. **Trzy warstwy modelu**: krzywe (parametryczne modyfikacje funkcji przenoszenia pasm), faza (prototyp analogowy; w High/Max z korektorem jak w Natural Phase), nieliniowości (z ADAA i oversamplingiem).
2. **Nieliniowości przypisane do pasm** (działają na lane pasma), nie do całej wtyczki — kanały nieobjęte pasmami pozostają nietknięte (CH-6).
3. **Kalibracja**: −18 dBFS (sinus) = +4 dBu (0 VU) dla wszystkich modeli.
4. **Złożoność wg Quality**: Eco — krzywe + saturacja bez pamięci; Normal — + interakcje zależne od poziomu; High — modele z pamięcią (state-space/WDF), ADAA-2, wewnętrzne 2×; Max — pełne modele nieliniowe (Newton–Raphson, Jiles–Atherton dla transformatorów), wewnętrzne 4×.
5. **Walidacja**: krzywe porównywane z modelami referencyjnymi — dla P: otwarty model WDF EQP-1A (SMC 2024; repozytorium `ABSounds/EQP1A-WDF` — **przed wykorzystaniem kodu sprawdzić licencję**; domyślnie używamy go wyłącznie jako referencji pomiarowej); dla N i S: dane producentów i pomiary opublikowane w literaturze.
6. **Nazewnictwo**: w UI „Analog N / Analog P / Analog S” z opisami bez znaków towarowych:
   - N — „Class-A transformer console EQ”,
   - P — „Passive LC program EQ with tube make-up”,
   - S — „1980s console EQ with proportional Q”.
   Nazwy Neve, Pultec i SSL mogą pojawić się w manualu/marketingu wyłącznie jako „inspired by” i dopiero po konsultacji prawnej.

## Konsekwencje
- Modele są „charakterem” dla w pełni parametrycznych pasm, a nie emulacjami 1:1 urządzeń z ich przełączanymi częstotliwościami.
- P1: parametry specyficzne dla modelu (np. „P-Atten” w półce Low dla „Pultec trick”).

## Aktualizacja (M6, 2026-10-04)

Realizacja i zmiany względem punktów 1–5 (szczegóły i pomiary: research/08 „Realizacja w M6”, research/09 „Realizacja w M6”):

1. **Krzywe** — reguły parametryczne `filters::analogCurve` (N: łagodnie proporcjonalne Q, „ściąganie” wysokich belli ku 0 dB, bump półek; P: rezonansowa półka LC przy boost, szersza półka bez rezonansu przy cut (ten sam rząd — pasmo Dynamic zachowuje topologię przy przejściu przez 0 dB), szerokie belle; S: Q proporcjonalne do |gain|). Model jest globalny (parametr *Analog Model*) i dotyczy wszystkich pasm; ta sama krzywa trafia do audio, korektora, Auto Gain i UI.
2. **Nieliniowości — zmiana**: zamiast modeli z pamięcią (state-space/WDF), Newtona–Raphsona i Jiles–Atherton w High/Max zastosowano wielomianowe nieliniowości bez pamięci w strukturze Character (dodawane tylko produkty wkładu pasma, ADAA 1. rzędu we wszystkich Quality), z podbiciem basu przed nieliniowością w N (nasycenie rośnie w basie). Uzasadnienie:
   - struktura „produkty wkładu pasma” gwarantuje, że odpowiedź liniowa jest dokładnie krzywą modelu przy każdym poziomie, pasmo 0 dB nic nie dodaje, a kanały poza pasmami są nietknięte (CH-6) — modele fizyczne całych obwodów (histereza J–A, sieć pasywna jako WDF) zmieniają też odpowiedź liniową z poziomem i nie rozkładają się na pasma parametrycznego EQ;
   - koszt i determinizm: brak iteracji Newtona–Raphsona w wątku audio (stały czas na próbkę), wynik niezależny od bloku hosta (NFR-3);
   - zachowanie jakościowe z punktu 3 research/08 §c jest spełnione pomiarowo (H2/H3 zależnie od modelu, wzrost w basie dla N, czystość S przy +4 dBu).
   Modele z pamięcią mogą wrócić jako opcja (P1), jeśli odsłuch w M8 wykaże potrzebę.
3. **Quality w Analog Feel**: Eco/Normal — krzywe + nieliniowości, faza minimalna, 0 latencji; High — + korektor fazy (D = 64) i wewnętrzny oversampling co najmniej 2×; Max — + korektor (D = 128) i co najmniej 4× (filtry FIR, łącznie ≤ 400 kHz). ADAA 2. rzędu pominięte z powodu opisanego w research/09 (mały zysk wobec oversamplingu).
4. **Kalibracja** (−18 dBFS = +4 dBu) — zrealizowana; wartości THD wariantów miękkich/twardych w research/08, pilnowane testem `[CHR-4]` (±3 dB).
5. **Walidacja** względem modeli referencyjnych (WDF EQP-1A, dane N i S) z tolerancją ±0,5 dB — **otwarta, przeniesiona na M8**; obecne testy `[PH-1]` sprawdzają reguły modeli. Kod repozytorium `ABSounds/EQP1A-WDF` nie został użyty.
6. Nazewnictwo bez zmian: „Analog N / P / S”, opisy z punktu 6 w podpowiedzi przełącznika modelu. „P-Atten” (P1) — nie zrealizowany; efekt boost + cut uzyskuje się dwoma pasmami P.

## Rozważane alternatywy
- **Emulacje 1:1 urządzeń** (stałe częstotliwości) — sprzeczne z koncepcją parametrycznego EQ; odrzucone.
- **Globalne etapy nieliniowe (transformatory na całej wtyczce)** — łamią CH-6; odrzucone.
