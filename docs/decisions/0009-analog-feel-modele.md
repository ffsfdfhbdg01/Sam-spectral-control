# ADR-0009: Analog Feel — modele N/P/S i nazewnictwo

- **Status:** Proponowana (M0)
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

## Rozważane alternatywy
- **Emulacje 1:1 urządzeń** (stałe częstotliwości) — sprzeczne z koncepcją parametrycznego EQ; odrzucone.
- **Globalne etapy nieliniowe (transformatory na całej wtyczce)** — łamią CH-6; odrzucone.
