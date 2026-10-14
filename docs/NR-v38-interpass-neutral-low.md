# NR v38 — neutralny gain low między modelami

> Wersja historyczna. [v39](NR-v39-low-mean.md) zmienia low na średnią gainów modeli:
> z checkboxem final low obejmuje całą serię, bez checkboxa odpowiedni blok.

Checkbox **Model → Inter-pass reconstruction → Keep inter-pass low gain at 1**
ustawia dokładnie `low=1` podczas korekcji interpass. INI:
`InterPassNeutralLow=true`. Domyślnie jest wyłączony, aby zachować dotychczasowy
wybór użytkownika. Działa z aktywnym frequency shaping, we wszystkich trybach
interpass, również przy odstępach 2 i 3 modeli.

High/mid zachowuje gainy stylu/Structure i dotychczasową regułę mnożenia
w obrębie bloku. **Końcowy resolve zachowuje skonfigurowany low gain ostatniego
modelu, zastosowany raz. Low nigdy nie jest mnożone przez liczbę modeli.**
Przy Off lub pominiętych korekcjach końcowy blok nadal obejmuje cały właściwy
zakres modeli. Zmiana aktywnej opcji resetuje historie bez odtwarzania NGX.

To neutralizacja gainu pasma low, a nie usunięcie filtra low. Istniejąca
matematyka to `high × (edit − lowField) + lowGain × lowField`. Po włączeniu
otrzymujemy `high × (edit − lowField) + lowField`. Przy high innym niż 1
pole low nadal jest potrzebne. Optimized/Fast może pominąć jego dispatch
przez istniejącą optymalizację dopiero przy równych gainach high i low.
Shaping Off i shadow confidence zachowują swoje działanie.

Nie zmieniają się shadery/HLSL/bytecode, PSO, geometria, filtry, harmonogram
ani dokładna polityka ścieżek. Nie dodaje nowego etapu GPU, ale może uruchomić istniejący filtr low, jeśli
wcześniej był pomijany przy równych gainach high/low. Celowo zmienia
siłę low shaping między modelami; jakość trzeba ocenić w grze. Benchmark
zachowuje/przywraca flagę i zapisuje ją wraz z liczbowymi gainami trzech stylów.

Test integracji sprawdza izolację od final resolve, reset historii i INI.
512 testów GPU na RTX 4090 sprawdziło constant signed low ±0,08, gain 1 i 0,71,
niezależność od high 0/0,454/1/1,5, alpha, FP16/FP32 oraz klasyczne/fused/Fast
implementacje z oryginalnym i cached Low36. To sprawdzenie matematyki,
a nie percepcyjna ocena obrazu ani pomiar całego NR.

[Analiza dostarczonego v37 z gry](NR-v37-20261014-ingame-analysis.md) potwierdza
przyspieszenie cached Low36 przy P50/P59/P65. Pomiary poprzedzają nowy checkbox.


## Kontrolny pomiar GPU opcji

RTX 4090, NVIDIA 616.56, resident synthetic RGB sinusoidy native 3840×2160,
signed residual 0,03, wejścia FP32, pole low/wyjście FP16. P50 1920×1080,
P59 2266×1274, P65 2496×1404. Area/radius 1/guide 1, sigma range 0,015,
spatial 3, high 1,13. A ma low 0,71, B low 1; shadow gate wyłączony.
Obie strony używają cached Low36 + Cross5 i potrzebują filtra low.

Osobne 90 rozgrzewki i 160 timestampów na każde okno ABBA. Timestamp obejmuje
8 powtórzeń low+korekcji i barier, podzielonych przez 8. Alokacje/upload/fence
CPU są poza pomiarem. Zegary i temperatura niekontrolowane.
To koszt shaderów, bez NGX i final resolve, a nie wynik całego NR w grze.

| Skala | A / B ms (średnia median okien) | Delta B−A ms | SD A / B ms | Mediany A1 / B1 / B2 / A2 ms |
| --- | --- | --- | --- | --- |
| P50 | 0.273088 / 0.272992 | -0.000096 | 0.028872 / 0.022765 | 0.273152 / 0.273024 / 0.272960 / 0.273024 |
| P59 | 0.349600 / 0.350176 | +0.000576 | 0.030194 / 0.023522 | 0.349568 / 0.349696 / 0.350656 / 0.349632 |
| P65 | 0.407104 / 0.411328 | +0.004224 | 0.033768 / 0.036514 | 0.407552 / 0.406400 / 0.416256 / 0.406656 |

Brak spójnego przyspieszenia. P65 ma zmianę median wariantu o 0,009856 ms:
obie sąsiednie delty mają różny znak (−0,001152 / +0,009600 ms).
Nie przypisujemy tego wahania stałemu kosztowi checkboxa. Przy high innym
niż 1 samo low=1 zachowuje potrzebę filtrowania. Nie testowano w tej próbie
przejścia od równych gainów do różnych ani pominięcia filtra przy high=low=1.

[1920 próbek](NR-v38-neutral-low-GPU.samples.csv),
fixture tests/nr_gpu/interpass_neutral_low_timing.cpp.
Weryfikacja wpływu opcji na cały NR wymaga osobnego ABBA w tej samej scenie
w grze, z jedyną różnicą stanu checkboxa i osobną rozgrzewką.
