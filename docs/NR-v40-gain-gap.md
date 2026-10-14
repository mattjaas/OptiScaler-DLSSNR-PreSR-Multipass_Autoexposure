# NR v40 — interpass zależny od różnicy gainów

Stały harmonogram co 1/2/3 modele zastępuje przewidywanie następnego modelu.
Po bieżącym modelu, przed następnym, korekcja występuje wyłącznie gdy:

```
abs(next_high_mid - next_low) > 0.25
&& abs(next_high_mid - next_low) > abs(current_high_mid - current_low)
```

Oba warunki są ścisłe. Różnica dokładnie 0,25, niezmienna lub malejąca
nie uruchamia interpassu. Przykład high=0,7 i low=0,8 przy P50:
kolejny model daje high=0,49, low=0,8; 0,31 > 0,25 i 0,31 > 0,1,
więc korekcja występuje przed drugim modelem. Przy różnicy 0,4 → 0,3
korekcja jest pomijana, mimo że obie różnice przekraczają 0,25.

To przewidywanie gainów, bez analizy pikseli, readbacku czy autotuningu GPU.
Nie gwarantuje rzeczywistej różnicy pasm obrazu poniżej 0,25: modele i filtracja
nie są liniową funkcją gainu, a jeden model może sam przekroczyć próg.
Nie wykonujemy kolejnych korekcji pomiędzy tą samą parą modeli.

## Gainy i zakresy

Każdy model używa własnego stylu i ustawień Auto/Manual. Obliczenia są
identyczne jak w interpass shaping: kalibracja signed working-space transfer 7,
potem istniejąca korekcja rozdzielczości:

```
gain = p50_gain ^ log2(1 / clamp(working_scale, 0.25, 1))
high = 1 + (gain_high - 1) * clamp(configured_Structure, 0, 1)
```

Najpierw korygujemy gainy osobno dla każdego modelu. High/mid jest iloczynem
gainów od ostatniej udanej korekcji, jeśli mnożenie jest włączone; inaczej
gainem ostatniego modelu. Low nie jest mnożone ani modyfikowane przez Structure.
Skala to ta sama żądana skala przed zaokrągleniem tekstur, której używa shaping.
Końcowy resolve nadal używa swojego skonfigurowanego transferu i kalibracji.

| Keep inter-pass low gain at 1 | Low w przewidywaniu | Faktyczny low w interpass | Końcowy low |
| --- | --- | --- | --- |
| wyłączony | średnia gainów modeli w bieżącym bloku | średnia bieżącego bloku | średnia po ostatniej udanej korekcji |
| włączony | średnia gainów wszystkich wykonanych/przewidywanych modeli do tego miejsca | 1 | średnia wszystkich modeli |

Przewidywane low po następnym modelu uwzględnia także jego gain.
Z checkboxem nie porównujemy high do wymuszonego 1: przewidujemy stan
bez korekcji, z low docelowo rozliczanym dla całej serii. High nadal obejmuje
tylko nierozliczony blok. Dotychczasowe zasady średniej low z v39 pozostają.

Off, spatial compression, pojedynczy model oraz skale powyżej 90% pozostają
nieaktywne. Shaping Off daje neutralne gainy i zero korekcji.
Nie ma już przerwy 84–85%, wynikającej ze starego harmonogramu.
Nie wykonujemy interpassu po ostatnim modelu. Każde pominięte przejście
używa dotychczasowego ClampProxy, a końcowe przetwarzanie nadal działa.
Nieudana korekcja nie przesuwa zakresu; kolejna decyzja uwzględnia faktycznie
nierozliczone modele. Zmiana maski harmonogramu resetuje historię modeli.

## Koszt i diagnostyka

Gainy do przewidywania są liczone raz dla każdego modelu na klatkę na CPU,
w stałej tablicy, bez alokacji. Gdy tryb jest nieaktywny, nie wyliczamy gainów.
Nie dodano shaderów, PSO, zasobów GPU, kopiowania ani pomiarów w ścieżce domyślnej.
Liczba istniejących dispatchy i barier zależy teraz od decyzji o korekcji;
może zarówno spaść, jak i wzrosnąć względem starego harmonogramu.
Pomijany interpass nie buduje pola low ani nie wykonuje rekonstrukcji.

Menu pokazuje oczekiwaną liczbę korekcji i końcowe gainy, korzystając z tych
samych helperów. Mnożenie high można zmienić przy każdym harmonogramie.
CSV benchmarku zamiast `interpass_stride` zawiera `interpass_expected_mask`,
`interpass_mask` i `interpass_corrections`. Bit N oznacza korekcję po modelu
o indeksie N liczonym od zera; maska 10 oznacza korekcje po modelach 2 i 4.
Pomiar przyjmuje poprawny harmonogram bez korekcji, wymaga zgodności masek
przewidzianej i wykonanej oraz przerywa okno, gdy harmonogram zmieni się w nim.
Historyczne benchmarki v37/v39 nie są pomiarem v40. Zestawy referencji,
Off, ABBA i przywracanie ustawień zachowano.

## Weryfikacja

Dziesięć testów Python NR/benchmarku oraz lokalny test produkcyjnej polityki
C++ przechodzą. C++ obejmuje próg i jego sąsiedztwo, malejącą i równą różnicę,
obie strony low, zmienne low, oba zakresy średniej, wyłączone mnożenie,
scale/Structure, pusty i pojedynczy zakres, maksymalną pojemność oraz nieudane
korekcje. Porównuje harmonogram z niezależnym przeliczeniem bloków dla
1–32 modeli, 24 zestawów gainów i obu ustawień mnożenia/zakresu low.
Workflow v40 kompiluje DLL, istniejące shadery i fixture pomiarowy.

## Kontrolny pomiar GPU — 14 października 2026

RTX 4090, NVIDIA 616.56. Resident synthetic RGB sinusoidy, native 3840×2160,
wejścia FP32, pole low i wyjście FP16. Sześć modeli reprezentowanych przez
niezmienne wejścia fixture: pięć przejść, każde Cached Low36 + Cross5
albo zwykły ClampProxy. Area/radius 1/guide 1, residual 0,03, range sigma 0,015,
spatial 3, shadow gate wyłączony, Structure 1, mnożenie high włączone,
neutral low wyłączone. Wszystkie modele mają bazowe P50 high=0,7 i low=0,8,
gainy są przeliczone według skali przed wyborem harmonogramu i korekcjami.

A: harmonogram v39; B: v40. Te same zasoby i PSO przez całe ABBA; stałe
uploadowane przed pomiarem. Osobna rozgrzewka 90 sekwencji i 160 próbek
w każdym oknie. Timestamp obejmuje osiem powtórzeń całej sekwencji pięciu
przejść z barierami, podzielonych przez osiem. Nie obejmuje alokacji, uploadu,
fence CPU, modeli NGX, temporal DLAA ani końcowego resolve. Wejścia nie
przechodzą przez NGX pomiędzy przejściami; ten fixture mierzy dispatchy,
nie jakość obrazu ani czas całego NR. Zegary/temperatura niekontrolowane.

| Skala / work | Korekcje A / B | A / B ms (średnia median okien) | Delta B−A ms | SD A / B ms | Mediany A1 / B1 / B2 / A2 ms |
| --- | --- | --- | --- | --- | --- |
| P50 / 1920×1080 | 5 / 5 | 1.416512 / 1.417920 | +0.001408 | 0.044867 / 0.050647 | 1.416576 / 1.418944 / 1.416896 / 1.416448 |
| P65 / 2496×1404 | 5 / 2 | 2.064960 / 1.052320 | −1.012640 | 0.066445 / 0.031683 | 2.063936 / 1.062720 / 1.041920 / 2.065984 |
| P84 / 3226×1814 | 2 / 1 | 1.700480 / 1.214848 | −0.485632 | 0.058171 / 0.048603 | 1.713920 / 1.213952 / 1.215744 / 1.687040 |
| P85 / 3264×1836 | 1 / 1 | 1.232064 / 1.233024 | +0.000960 | 0.039336 / 0.046932 | 1.231232 / 1.232768 / 1.233280 / 1.232896 |

SD jest odchyleniem standardowym wszystkich próbek obu okien danego wariantu.
Przy mniejszej liczbie korekcji sekwencja dispatchy przyspiesza; przy tej samej
liczbie koszt jest podobny. Nie ekstrapolujemy tych wartości na grę ani inne
gainy. [2560 próbek](NR-v40-gain-gap-GPU.samples.csv), źródło fixture:
`tests/nr_gpu/interpass_gain_schedule_timing.cpp`.

W grze porównać v39/v40 w ABBA ze stałą sceną, zestawem modeli, rozdzielczością
i ustawieniami, po osobnej rozgrzewce, z timestampami całego NR i sprawdzeniem
raportowanych masek. Ocenić szczególnie detale, ruch, krawędzie i HDR.
Jakość oraz pełny czas GPU w grze nie zostały zweryfikowane dla v40.
