# NR v21: eksperymentalny Fast inter-pass

Zgłoszenie czasu dokładnego Optimized około 37,3 ms przy P65 oraz porównanie
z historycznym RGB16 około 37,0 ms opisano w [audycie P65](NR-v21-P65-optimized-audit.md).

W menu **Model → Inter-pass reconstruction**, po **Inter-pass optimized**, jest
**Inter-pass fast (experimental)** (`InterPassReconstruction=4`). Dotychczasowy
Optimized (`3`) zachowuje dokładną matematykę i politykę v20. Domyślna opcja pozostaje Off.

Fast jest świadomym kompromisem jakości, który wymaga sprawdzenia w grze.
W DX12, przy Area, radius 1, dodatnim guide strength oraz rzeczywistych wymiarach
50–90% na obu osiach, wykonuje jedną estymację residualu na piksel roboczy.
Pozostałe ustawienia korzystają z dotychczasowego dokładnego Optimized.
Nominalne 50% przy małych nieparzystych wymiarach może wypaść poniżej progu
na jednej osi; decydują rzeczywiste wymiary tekstur.

Dokładna ścieżka oblicza `Area(P100 + shaped Guided(N - B, P100))`.
Fast przybliża ją przez `B + shaped Guided(N - B, P100 w środku piksela roboczego)`.
`B` jest niezmienną oryginalną bazą Area, a `N` wynikiem bieżącego passa.
P100 prowadzi korektę przez liniową próbkę w środku; sąsiedztwo residualu ma nadal
3×3 próbki, pełne wykładnicze wagi bilateralne, dotychczasowy guide strength,
kształtowanie częstotliwości i opcjonalny shadow gate.

Oszczędność pochodzi z pominięcia osobnych estymacji dla wszystkich texeli P100
objętych Area: jedna estymacja zamiast czterech przy dokładnym P50, a przy skalach
ułamkowych zamiast nawet dziewięciu. Nie jest to przeliczenie na gwarantowany czas GPU.
Nie powstaje powierzchnia P100 ani dodatkowy dispatch, kopia, bariera czy alokacja
w każdej klatce. Osobny PSO powstaje dopiero przy pierwszym użyciu i nie ma LDS.
Jeżeli jego utworzenie zawiedzie, standardowy PSO wykonuje tę samą matematykę Mode33
i raportuje fallback. Inne tryby nie tworzą PSO Fast.

Przybliżenie zmienia przestrzenne rozłożenie drobnych korekt przy krawędziach,
cienkich detalach i przejściach do cienia. Shaping i shadow gate są teraz oceniane
w środku zamiast osobno dla każdego texela P100. Różnice mogą narastać przez wiele
passów. Zachowany jest kodek HDR, zakres i sanitizacja wejścia NR, clamp na brzegach,
alfa z tego samego środkowego texela P100 oraz dotychczasowy końcowy resolve/DLAA.
To nie jest potwierdzenie, że utrata jakości jest niewidoczna.

## Benchmark i ocena obrazu

W sekcji statusu NR wybierz **Compare reference / Optimized / Fast at 50% / 59% / 65% (CSV)**.
Możesz wpisać nazwę sceny w **Fast benchmark scene**. Profil wymusza
**Temporal DLAA residual + P100-guided**, sześć passów, Area, radius 1, guide 1.
Pozostawia bieżące ustawienia stylu, intensywności, shaping i shadow gate.

Dla każdej z dokładnie trzech skal mierzy Off, Classic reference, Fused reference
oraz **Optimized / Fast / Fast / Optimized** (ABBA). To 15 konfiguracji i 21 okien,
każde z osobną rozgrzewką; domyślnie 90 próbek rozgrzewki i 160 pomiarowych.
Nie dopisuje bieżącej skali i nie zmienia pozostałych profili benchmarku.

Do oceny obrazu zaznacz **Hold each Fast benchmark window for visual inspection**.
Po pomiarze konfiguracja pozostaje aktywna aż do **Next comparison window / finish report**.
Zamknij menu, obejrzyj obraz, otwórz menu i przejdź dalej. Możesz użyć
**Capture this configuration (before/after)**; istniejący mechanizm zapisuje
obrazy wejścia/wyjścia w `dlssnr-capture` obok OptiScaler. Zanotuj skalę i nazwę
konfiguracji z ekranu; przechwycenie nie uruchamia drugiego renderera jednocześnie.
Próbki podczas zatrzymania i przechwytywania nie trafiają do ukończonego okna pomiaru.

Porównuj wynik końcowy Optimized i Fast przy tej samej kamerze, najpierw w bezruchu,
potem także podczas ruchu. Sprawdź włosy, roślinność, drobne napisy, cienkie krawędzie,
ciemne powierzchnie i jasne detale HDR. Dla czystego pomiaru czasu uruchom osobny
przebieg bez zatrzymywania, screenshotów i ruchu kamery. Różne czasy ręcznego
oglądania osłabiają odporność ABBA na dryft temperatury i zegarów GPU.

Po pełnym zakończeniu zapisuje `NR-v21-fast-*.csv`, `.samples.csv` i `.txt`
w `OptiScaler-NR-Benchmarks` w folderze gry. CSV zawiera rzeczywiste wymiary,
wybraną ścieżkę, medianę/P95/SD i rozrzut median okien. Kolumna
`gain_vs_optimized_median_ms` to mediana Optimized minus mediana wariantu:
wartość dodatnia oznacza skrócenie czasu. Pomiary obejmują cały przedział NR na GPU,
z NGX i zależnościami; nie sam shader. Metadata zapisuje GPU, scenę i ustawienia.
Zakończenie/anulowanie przywraca poprzedni transfer, skalę, passy i tryb inter-pass.

W profilu Fast `delta_vs_expected_path_ms` odnosi się do Optimized,
`path_matches_control` oznacza wyłącznie identyczną nazwę ścieżki, a historyczne
zyski są `NaN`: nie ma historycznych pomiarów tego przybliżenia w grze.
Nie używaj porównania nazw PSO jako oceny jakości czy wydajności.

## Weryfikacja

Workflow `build_nr_detail_quality_experiments.yml` kompiluje standardowy shader,
dwa zachowane PSO RGB i Fast; sprawdza brak LDS w Fast, politykę selekcji,
istniejącą arytmetykę i zestawy benchmarków. Fixture `interpass_fast_quality.cpp`
sprawdza rzeczywiście skompilowane Fast i standardowy Mode33: identyczne wyniki
obu PSO, nieparzyste wymiary, brzegi, FP16/FP32, wejścia HDR/non-finite, shaping,
shadow gate, skończony zakres RGB i zachowanie alfy. Nie wymaga równości Fast
z filtrem dokładnym i nie ocenia jakości percepcyjnej.

`interpass_fast_timing.cpp` porównuje zachowane zwycięskie PSO Optimized z Fast
na rezydujących teksturach 4K: 50/59/65%, ABBA, 90 warmup + 160 próbek na okno.
Uploady, alokacje i oczekiwania CPU są poza timestampami. Ten syntetyczny pomiar
nie obejmuje NR/NGX, DLAA ani końcowego resolve i nie zastępuje benchmarku w grze.

## Lokalny pomiar syntetyczny — 10 października 2026

GPU: NVIDIA GeForce RTX 4090. Scena: rezydujące syntetyczne sinusoidy RGB i residual 0,03,
native 3840×2160; wejścia FP32, wyjście FP16. Area, radius 1, sigma range 0,015,
sigma spatial 1,2, guide 1; shaping neutralny. Jedna korekta inter-pass,
90 warmup + 160 próbek na okno, ABBA, 320 próbek na wariant/skala.
Timestamp obejmuje 8 dispatchy i bariery UAV, wynik dzielony przez 8.
Pomiar nie obejmuje NGX/NR/DLAA ani końcowego resolve; nie jest wynikiem z gry.

| Skala / working | Optimized mediana ms | Fast mediana ms | Zysk ms | SD Optimized / Fast ms | Mediany okien Optimized / Fast ms |
|---|---:|---:|---:|---:|---|
| P50 / 1920×1080 | 0.22810 | 0.22758 | 0.00051 | 0.00179 / 0.00535 | 0.22810/0.22822 / 0.22758/0.22746 |
| P59 / 2266×1274 | 0.35302 | 0.26125 | 0.09178 | 0.01374 / 0.01334 | 0.35021/0.36109 / 0.26131/0.26125 |
| P65 / 2496×1404 | 0.38451 | 0.28723 | 0.09728 | 0.04197 / 0.02665 | 0.38246/0.39322 / 0.28723/0.28710 |

Surowe próbki: `docs/NR-v21-fast-GPU.samples.csv`. Mediany i SD populacyjne
odtworzone z 1920 próbek; indeksy sprawdzone bez luk i duplikatów.
Zegarów/temperatur nie kontrolowano. Oszczędności całego toru w grze nie należy
wyliczać przez przemnożenie tych wyników przez liczbę przejść.
Przy P50 różnica 0,00051 ms jest mniejsza od zmienności obu wariantów: ten pomiar
nie potwierdza przyspieszenia przy 50%. Zysk P59/P65 także trzeba sprawdzić w pełnym torze w grze.

Lokalnie przeszły wszystkie dotychczasowe testy Python i polityka C++.
RTX 4090: 6528 porównań zachowanych dokładnych shaderów z bazą v20,
53 543 552 komponentów RGBA identycznych bitowo (NaN według kontraktu fixture).
Fast: 72 fixture GPU przeszły kontrolę równoważności PSO, zakresu i alfy.
Lokalny WARP (`Microsoft Basic Render Driver`) kończy fixture błędem systemowym
`0xc0000374`; wyników tego sterownika nie uznajemy za pozytywną weryfikację.
Ocena percepcyjna i czas całego toru Temporal DLAA w grze pozostają do sprawdzenia.

Kompilacja źródła Vulkan jest zablokowana przez istniejące odwołania `gAux2`/`gAux`
w kodzie temporalnego carriera. Te same trzy błędy występują w czystej bazie v20;
nowy Mode33 jest ograniczony do DX12. Zachowane binaria Vulkan nie zostały zmienione.
Workflow kompiluje fixture Fast do uruchomienia na docelowym GPU; nie wymaga
wykonania go na WARP hosta GitHub Actions. Kontrola lokalna odbyła się na RTX 4090.
