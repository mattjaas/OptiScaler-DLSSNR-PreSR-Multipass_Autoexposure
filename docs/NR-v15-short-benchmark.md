# Krótki benchmark v15 na podstawie wyników v14

Źródło: `NR-v14-20261010-134037.csv`, `.samples.csv`, `.txt` z benchmarków
The Last of Us Part I. [Pełny audyt](NR-v14-20261010-analysis.md): 63 konfiguracje,
10 080 próbek, RTX 4090, native 3840×2160, sześć passów, Area radius 1,
guide strength 1, 90 warmup + 160 pomiarów. Jedna konfiguracja mierzona raz.
Scena/taktowanie/temperatura bez osobnego rejestru. Flagi CSV to ustawienia
żądane; nie otrzymano logu gry potwierdzającego każdy PSO. Podane czasy
obejmują cały przedział GPU NR, nie izolowany inter-pass.

## Wybór 59% jako jedynej dodatkowej skali

P50 zachowuje specjalizację dokładnego 2× downsample; P65 Compact16.
P59 ma w tej sesji rzeczywiste wymiary 2266×1274: na obu osiach poniżej 60%,
więc bada inną ścieżkę Linear20. Jest blisko granicy, lecz pozostaje
arbitralną skalą fractional, w odróżnieniu od regularnego stosunku P60.
P61 powiela kwalifikację Compact16 już reprezentowaną w P65. P55 również
bada Linear20, lecz ma większą zmienność i mniejszy dodatkowy zysk RGB.

| Skala | Mediana Linear bez Weights | Legacy Linear20 Weights | Izolowany Linear20 Mode28 | Linear20 Mode28 + RGB | SD wariantu RGB |
|---:|---:|---:|---:|---:|---:|
| 55% | 30,62579 | 33,74285 | 30,46605 | 30,40461 | 0,30126 |
| 59% | 33,61638 | 37,10976 | 33,39571 | 33,31072 | 0,17367 |
| 60% | 34,09306 | 37,59053 | 33,95430 | 33,87341 | 0,17115 |
| 61% | 36,60902 | 40,21043 | 36,54298 | 36,41651 | 0,15981 |

Wszystkie wartości w ms. Kolumna bez Weights korzysta z automatycznej
kwalifikacji Compact16: w P55/P59 efektywnie Linear20, w P60/P61 Linear16.
Pozostałe kolumny to jawnie Linear20. Nie należy traktować tej tabeli jako
porównania identycznego PSO we wszystkich skalach.

P59: izolowany Linear20 Mode28 skraca medianę o 3,71405 ms względem
legacy Linear20 Weights. RGB dodatkowo skraca medianę o 0,08499 ms,
średnią o 0,06392 ms i P95 o 0,10240 ms. Wobec Linear bez Weights
łączny zysk RGB wynosi 0,30566 ms mediany, 0,25839 ms średniej i 0,37888 ms
P95. Duża strata starego Weights poniżej 60% powtarza się w P55/P59;
nie trzeba jej ponownie mierzyć w każdym regularnym sweepie.

P59 „Boundary legacy weights ON” i jawny „Linear20 legacy weights ON”
prowadzą przy tych wymiarach do tego samego PSO, lecz ich mediany w sweepie
różnią się o 0,08602 ms. To pokazuje skalę wpływu kolejności/dryfu sesji:
małych różnic nie traktujemy jako gwarantowanego zysku. Redukcja manifestu
usuwa także tę redundantną parę. Duża strata legacy pozostaje wyraźna
w obu pomiarach; korzyść RGB względem Mode28 należy potwierdzić ponownie.

## P65 i usunięte warianty

RGB v14: mediana 37,02016 ms, średnia 37,02808 ms, P95 37,30739 ms,
SD 0,18477 ms. Wobec Mode28 v13 mediana poprawia się o 0,11110 ms,
średnia o 0,11445 ms i P95 o 0,10240 ms. Wobec Weights v10 mediana
poprawia się o 0,27699 ms, a wobec Linear16 bez Weights o 0,26061 ms.
To najbardziej użyteczny wariant do dalszego mierzenia.

- Source/model cache: około +0,89 ms względem Weights w P65; Classic cache
  również wolniejszy od Classic optimized w P50/P65. Usunięte ze sweepu.
- Strided: mediany P65 37,53370 / 37,42669 ms wobec Linear16 37,28077 ms.
  Quad Fill i Both v11: 37,49990 ms. Usunięte.
- Spatial v11 oraz v12: małe, niepowtarzalne korzyści między sesjami,
  żaden nie zbliża się do Mode28/RGB. Usunięte.
- Area v13 + Mode28: 37,10413 ms, wolniejsze od RGB o 0,08397 ms.
  Sam Area daje mały zysk wobec Weights, lecz gorszy P95. Usunięte.
- Guide-one P65: mediana 37,03347 ms i P95 37,43949 ms wobec RGB
  37,02016 / 37,30739 ms. RGB + guide-one: 37,07085 / 37,61459 ms.
  W P59 połączenie różni się od RGB tylko o -0,00205 ms mediany;
  nie ma podstaw do mierzenia obu za każdym razem. Usunięte.
- Classic optimized pozostaje dostępny jako tryb, ale nie jest wariantem
  optymalizacji aktualnego Fused. Regularny sweep koncentruje się na Fused;
  jego nieoptymalizowany odpowiednik pozostaje bazą jakości/funkcjonalności.

Wykluczenie dotyczy tylko benchmarku, nie implementacji, menu ani testów
poprawności shaderów. Zachowano starsze PSO i przywracanie wszystkich opcji
użytkownika, również tych nieużywanych podczas krótkiego sweepu.

## Zestaw 14 konfiguracji

| Skala | Konfiguracje |
|---:|---|
| 50% — 3 | No inter-pass; Fused reference; Fused optimized |
| 59% — 5 | No inter-pass; Fused reference; Linear20 bez Weights; izolowany Linear20 Weights + Mode28; ten sam wariant + RGB |
| 65% — 6 | No inter-pass; Fused reference; Linear16 bez Weights; Weights v10; Mode28 v13; Mode28 + RGB v14 |

Off mierzy koszt NR bez rekonstrukcji między przebiegami. Fused reference
mierzy tę samą funkcję bez optymalizacji. Etapy pośrednie pozwalają rozdzielić
wpływ tiled, Weights, specjalizacji Mode28 i RGB. Weights v10 pozostaje tu
kontrolą, choć jego samodzielny zysk w tej sesji nie był przekonujący.

Manifest `DlssNr_BenchmarkCases.h` jest jedynym źródłem konfiguracji.
Przy domyślnych 90/160 próbkach liczba mierzonych klatek spada
z 10 080 do 2240, a z rozgrzewką z 15 750 do 3500: **o 77,8%**.
To redukcja liczby klatek, nie zmierzony czas całego benchmarku.
Zachowano okno pomiarowe i rozgrzewkę, bez utraty liczby próbek na wariant.
Menu i pliki noszą nazwę v15. Wyjściowe shadery, domyślne flagi i routing
optymalizacji pozostają bez zmian; to skrócenie pomiaru.

P59 Fused reference nie był mierzony w dostarczonym szerokim v14. Dodano go
do nowego zestawu jako wymaganą bazę; jego zysku/kosztu nie estymowano.
Pojedynczy sweep nie dowodzi powtarzalności małych różnic. Wnioski dla tej
RTX 4090 nie są automatycznie uniwersalne dla wszystkich GPU i scen.
