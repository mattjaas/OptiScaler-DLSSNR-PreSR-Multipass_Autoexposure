# NR v37 — współdzielenie próbek Low36

Nowy wariant **Model → Inter-pass reconstruction → Fast cached low (experimental)**
(`InterPassReconstruction=8`) przyspiesza filtr low używany przez Fast między
modelami. Zwykły Fast (4), dokładny Optimized, referencje i domyślny Off pozostają
dostępne. Polityka dokładnych ścieżek i harmonogram z v36 nie zmieniają się.

## Zmiana i koszt

Grupa 8×8 pikseli pola low współdzieli 34×34 próbki signed residualu N−B
w pamięci LDS. Sąsiednie filtry Low36 nakładają się na tej samej siatce o kroku
0,25 footprint. Pełna grupa pobiera 1156 par próbek zamiast 2304, zachowując
wszystkie 36 dodatnich wag `11,15,17,17,15,11`, kolejność sumowania,
normalizację 7396, nakładanie filtra i sanitizację residualu. To liczba próbek,
a nie procent przyspieszenia całego NR. Krawędzie nadal mają clamp.

Koszt dodatkowy: 13 872 bajty LDS/grupę, odczyty LDS i jedna bariera grupy.
Wszystkie wątki uczestniczą w barierze przed odrzuceniem wątków poza ostatnią
częściową grupą. Nie ma nowych tekstur, dispatchy, kopii, barier zasobów ani
alokacji w każdej klatce. Nowy PSO powstaje leniwie tylko dla tego wariantu.
Brak PSO używa starego standardowego Low36 i zgłasza fallback.

Wariant działa tam, gdzie Fast, a zysk występuje tylko, gdy shaping potrzebuje
pola low. Wyłączony shaping lub równe high/low nadal pomijają filtr. Cross5,
końcowy filtr Mode18 i pozostałe istniejące binaria shaderów są identyczne
z v36; nowy shader jest osobnym PSO DX12. Binaria Vulkan nie zmieniają się.
Raport ścieżki cached nie dowodzi wykonania LDS, gdy filtr low jest pominięty.

## Pomiar GPU — 12 października 2026

RTX 4090, NVIDIA 616.56, native 3840×2160. Resident synthetic RGB sinusoidy
z residualem 0,03, wejścia FP32, low/wyjście FP16. Working P50 1920×1080,
P59 2266×1274, P65 2496×1404. Area, radius 1, guide 1, range sigma 0,015,
spatial sigma 3, high 1,13, low 0,71, shadow gate wyłączony.

Każde okno ABBA ma osobne 90 rozgrzewek i 160 timestampów GPU. Timestamp
obejmuje 8 powtórzeń Low36 + Cross5 z barierami, podzielonych przez 8.
Alokacje, upload i CPU fence są poza timestampem. To czas jednej korekcji,
**bez NGX, temporal DLAA i final resolve — nie pomiar całego NR w grze**.
Nie mnożymy go przez liczbę passów. Zegary, temperatura i obciążenie innych
procesów nie były kontrolowane; SD uwzględnia też pojedyncze wysokie próbki.

Wiersze podają średnią median okien danej strony. Minus oznacza szybciej.
Obie próby używają identycznego bytecode nowego Low36. Pierwsza była pomiarem
prototypu, druga powtórzeniem po integracji; rozrzut P65 ogranicza pewność zysku.

| Próba / skala | Fast / cached ms | Delta ABBA ms | SD Fast / cached ms | Mediany A1 / B1 / B2 / A2 ms |
| --- | --- | --- | --- | --- |
| 1 / P50 | 0.303040 / 0.270944 | -0.032096 | 0.044062 / 0.045109 | 0.303104 / 0.270848 / 0.271040 / 0.302976 |
| 1 / P59 | 0.405728 / 0.344256 | -0.061472 | 0.051553 / 0.045420 | 0.405504 / 0.344192 / 0.344320 / 0.405952 |
| 1 / P65 | 0.559552 / 0.399680 | -0.159872 | 0.051766 / 0.047726 | 0.553984 / 0.399616 / 0.399744 / 0.565120 |
| 2 / P50 | 0.288704 / 0.270016 | -0.018688 | 0.010305 / 0.002706 | 0.289408 / 0.270080 / 0.269952 / 0.288000 |
| 2 / P59 | 0.409216 / 0.346752 | -0.062464 | 0.012974 / 0.008561 | 0.409472 / 0.346624 / 0.346880 / 0.408960 |
| 2 / P65 | 0.496320 / 0.404832 | -0.091488 | 0.022944 / 0.090470 | 0.496448 / 0.405888 / 0.403776 / 0.496192 |

Źródła: [1920 próbek prototypu](NR-v37-low36-cache-prototype.samples.csv)
i [1920 próbek powtórzenia](NR-v37-low36-cache-GPU.samples.csv).
Fixture: `tests/nr_gpu/interpass_low_cache_timing.cpp`.
Wstępny kafel Cross5 dał tylko około 0,001–0,002 ms i remis przy P50/shaping;
nie został włączony. Nowy wariant zmienia wyłącznie low.

## Różnice obrazu i ograniczenia

Filtr matematycznie zachowuje próbki i wagi, ale współdzielenie używa innej
kolejności działań przy wyznaczaniu współrzędnych FP32. Wynik nie jest bitowo
identyczny. Nie ma niezależnej oceny percepcyjnej ani pomiaru w grze.

GPU audit porównuje nowy PSO z oryginalnym Low36 i zasila Cross5 każdym polem low.
Sprawdza FP16/FP32, małe/nieparzyste wymiary, częściowe grupy, 12 skal,
signed residual, NaN, losowe duże wartości HDR, stały ±0,08 i alpha.

- 480 porównań małych tekstur: maksymalna różnica wyjścia Fast 0,000488281,
  RMS 0,000000210855. Maksymalna różnica FP32 0,0000152588; alpha niezmienione.
- 24 porównania native 4K przy P50/P59/P65: maksymalna różnica wyjścia Fast
  0,0131836, RMS 0,000000967925. Maksymalna różnica FP32 0,0000152588.
  Największe pojedyncze różnice FP16 wystąpiły w stresie z dużym losowym HDR,
  przy zaokrąglaniu pola low; nie należy przedstawiać ich jako gwarantowanie
  niewidocznych w każdym obrazie. Alpha i signed DC pozostały poprawne.

Miary dotyczą jednej korekcji na danych syntetycznych. Kilka modeli i ruch
mogą kumulować różnice. Dlatego wariant jest opt-in, a obecny Fast pozostaje
bez zmiany. Dane wejścia FP32 fixture też nie reprezentują kosztu każdej gry.

## Porównanie w grze

Przycisk **Compare Fast / cached low at 50% / 59% / 65% (CSV)** mierzy cały NR:
Off, obie referencje, Optimized, potem Fast/cached/cached/Fast w ABBA.
To 18 konfiguracji / 24 okna, osobna rozgrzewka, paired CSV i opcjonalny Hold
do oględzin. Nie dopisuje bieżącej skali; pozostałe profile pozostają bez zmian,
z limitem 65%. Wyniki mają prefix `NR-v37-cached-low-`.

Benchmark zachowuje wybrane shaping/gainy. Gdy nie potrzeba low, nie należy
oczekiwać zysku. Wpisz scenę, zachowaj kamerę i ustawienia podczas pomiaru;
obejrzyj osobno drobne tekstury, ciemne detale, HDR, krawędzie i ruch.
Zapisz GPU, rozdzielczość, medianę/P95/SD i różnice ABBA. Finish/cancel
przywraca pierwotne ustawienia. Dopiero wynik całego NR i oględziny mogą
uzasadnić zastąpienie zwykłego Fast w danej scenie.
