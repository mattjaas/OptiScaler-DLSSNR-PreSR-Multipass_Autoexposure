# NR v25: trzy niezależne eksperymenty inter-pass

W **Model → Inter-pass reconstruction** są trzy nowe opcje po Fast v21.
Każda zmienia tylko jeden element Fast v21. Nie łączą się ze sobą i nie zastępują
dotychczasowego Optimized ani Fast. Domyślnie pozostaje Off.

| INI / opcja | Zmiana względem Fast v21 | Co oglądać |
|---|---|---|
| 5 / Fast low36 (experimental) | Filtr low residualu 6×6 zamiast 8×8, tylko między passami | Ruchome wzory na drobnych teksturach, powolne zmiany tonów przy różnym low/high gain |
| 6 / Fast weights (experimental) | Dziewięciu sąsiadów, przybliżone wagi wykładnicze | Kontrastowe i kolorowe krawędzie, ciemne detale |
| 7 / Fast cross5 (experimental) | Środek i czterech sąsiadów; dokładne wagi wykładnicze | Ukośne krawędzie, włosy, drobne napisy, roślinność w ruchu |

**Zmiany dotyczą wyłącznie inter-pass.** Końcowy filtr niskich częstotliwości
Mode18, kodek HDR, temporal DLAA i final resolve zachowują obecne działanie.
Niezmienna baza B i cumulative residual N−B są zachowane. Nie ma nowych dispatchy,
tekstur, kopii, barier ani alokacji w każdej klatce. PSO nowych wariantów są tworzone
leniwie i nie używają LDS; standardowy shader ma te same warianty jako fallback.

Jak Fast v21, eksperymenty działają w DX12 przy Area, radius 1, dodatnim guide strength
i rzeczywistych wymiarach 50–90% na obu osiach. Inne ustawienia używają dokładnej
polityki Optimized v20. Nominalna skala przy nieparzystych wymiarach nie gwarantuje
spełnienia warunku na obu osiach. Binariów Vulkan nie zmieniono.

## Czy Low36 cofa dawną poprawkę artefaktów?

Dotyczy tego samego etapu, w którym prosty, nienakładający się box 8×8 powodował
składanie wysokich częstotliwości do szerokiej korekty. Poprawiony filtr Mode18
używa nakładającego się okna 8×8, liniowych próbek i dodatnich wag
`7,11,15,17,17,15,11,7` na każdej osi.

Low36 zachowuje odstęp `0,25 × footprint`, środkowe sześć pozycji i wagi
`11,15,17,17,15,11`, renormalizowane przez `86²=7396`.
Pozostaje nakładającym się filtrem o pełnej szerokości 1,25 piksela low field.
Usuwa zewnętrzne próbki, które miały 14% masy wag na każdej osi; nie przywraca boxa.
Ma 36 pozycji z dwoma odczytami bilinear zamiast 64. To liczba próbek, nie procentowy
zysk czasu GPU. Końcowy low field nadal używa 64 pozycji.

Nie ma gwarancji, że Low36 będzie lepszy od dawnego boxa we wszystkich scenach.
Test fazowo uśrednionej energii pasma zaporowego dla footprint 8 dał:
box `0,02674886`, obecny 64 `0,02778529`, Low36 `0,03973635`.
To średnia mocy odpowiedzi na sinusoidy 1D ponad Nyquist pola low, z bilinear
i 32 fazami siatki źródłowej. Nie odpowiada konkretnej fazie, widmu residualu,
shaping ani ruchowi w grze i nie jest rankingiem jakości percepcyjnej.
Pokazuje, że nie wolno obiecywać globalnie lepszego tłumienia ani niewidocznych zmian.
Wstępny pomysł rozsunięcia sześciu próbek do szerokości filtra 64 został odrzucony:
tworzył nowy grzebień aliasingu (energia `0,06343874` w tym samym teście).

Low36 ma efekt tylko wtedy, gdy inter-pass shaping potrzebuje low field.
Przy nieaktywnym shaping lub jednakowym high/low gain korzysta z tej samej matematyki
i PSO co Fast v21. Benchmark pozostawia wybrane przez użytkownika shaping i ustawienia
passów; nie wymusza sztucznego pogorszenia obrazu, aby pokazać zysk.

## Wagi i pięciu sąsiadów

Weights rozkłada nieujemny wykładnik na część całkowitą i ułamkową.
Potęga dwóch jest konstruowana dokładnie, a `2^(−f)` na `[0,1]` przybliża wielomian
Hermite'a trzeciego stopnia z dopasowanymi końcami i pochodnymi.
Wagi są dodatnie i monotoniczne; maksymalny względny błąd interpolacji w teście
wynosi `0,061181%`. To błąd wagi, nie ograniczenie błędu końcowego obrazu po sześciu passach.
Zachowane są wszystkie dziewięć par odczytów, normalizacja i fallback środkowego residualu.
Zastąpienie instrukcji wykładniczej arytmetyką nie gwarantuje przyspieszenia GPU.

Cross5 pomija cztery narożne pary odczytów i ich wagi. Pozostałe wagi są dokładne,
a low field nadal ma 64 pozycje. Nie jest to 44% zysku czasu GPU — inne koszty,
transfery pamięci i cały NR pozostają. Różnice mogą narastać przez kolejne passy.

## Benchmark i oglądanie obrazu

Wybierz **Compare Fast / Low36 / Weights / Cross5 at 50% / 59% / 65% (CSV)**.
Profil wymusza Transfer 9, sześć passów, Area, radius 1, guide 1; zachowuje
styl/intensywność, shaping, shadow gate i ustawienia poszczególnych passów.
Możesz wpisać nazwę sceny w istniejącym polu **Fast benchmark scene**.

Dla każdej z dokładnie trzech skal kolejność jest następująca:

1. Off, Classic reference, Fused reference.
2. Optimized / Fast v21 / Fast v21 / Optimized.
3. Fast v21 / Low36 / Low36 / Fast v21.
4. Fast v21 / Weights / Weights / Fast v21.
5. Fast v21 / Cross5 / Cross5 / Fast v21.

To **24 unikalne konfiguracje i 57 okien**, z osobną rozgrzewką każdego okna.
Domyślnie 90 próbek rozgrzewki i 160 pomiarowych. Wcześniejsze profile są dostępne
osobno i mają dotychczasowy zestaw wariantów.

Zaznacz **Hold each experiment benchmark window for visual inspection**, aby po
pomiarze obejrzeć aktywną konfigurację. Zamknij menu, obejrzyj obraz, otwórz menu
i wybierz **Next comparison window / finish report**. Możesz przechwycić wejście/wyjście
przyciskiem **Capture this configuration (before/after)**; zapisy są w `dlssnr-capture`.
Zanotuj skalę, wariant i numer okna. Zatrzymane klatki nie wchodzą do pomiaru.
Ruch i długie postoje pogarszają porównywalność czasów: wykonaj osobno statyczny
benchmark bez zatrzymań oraz oglądanie jakości w ruchu.

W folderze gry `OptiScaler-NR-Benchmarks` powstają `NR-v25-fast-experiments-*`:

- `.csv`: 24 wiersze konfiguracji, rzeczywiste rozdzielczości/ścieżki, mediana/P95/SD,
  mediany okien i zyski względem Optimized oraz Fast. Fast jest tu pulą wszystkich jego okien.
- `.samples.csv`: 9120 surowych próbek przy domyślnych ustawieniach, numer okna i `comparison`.
- `.pairs.csv`: 12 niezależnych porównań ABBA, cztery mediany okien, zysk symetryczny
  `(A1+A2−B1−B2)/2`, oba zyski sąsiednich par i rozrzut median A/B. **To główny raport
  do oceny nowych wariantów względem pobliskiej kontroli Fast.** Dodatni zysk oznacza szybciej.
- `.txt`: GPU, scena, ustawienia i interpretacja raportów.

Timestamp obejmuje **cały NR na GPU**, z NGX i zależnościami, a nie tylko inter-pass
ani cały czas klatki gry. Zakończenie/anulowanie przywraca wcześniejsze ustawienia,
w tym transfer i wybór inter-pass. Wyniki nie zmieniają automatycznie polityki ani ustawień.

## Weryfikacja lokalna

Kompilacja DXC siedmiu shaderów (standardowy, RGB16/RGB20, Fast i trzy eksperymentalne)
zakończyła się powodzeniem; eksperymentalne PSO nie mają LDS. Fast v21 ma identyczny izolowany
bytecode. Oddzielono jego funkcję od pętli eksperymentalnej, aby zachować także
zaokrąglenia standardowego fallbacku.

Na RTX 4090 przeszły: 6528 porównań zachowanych ścieżek dokładnych / 53 543 552
komponenty RGBA; 72 istniejące fixture Fast; 288 kontroli eksperymentów, v21/Mode18,
nieparzystych wymiarów, brzegów, FP16/FP32, danych HDR/nonfinite, signed DC i alfy.
Wyniki v21 Fast i zwykłego Mode18 są sprawdzane dokładnie z zachowanym baseline.
Nowe izolowane i standardowe PSO mają ograniczenie różnicy zaokrągleń: RGB FP32
`2e−6`, FP16 1 ULP; Low36 jest sprawdzany dokładnie. To nie test jakości percepcyjnej.

Przeszły również testy polityki, istniejące `test_nr_interpass_*.py`, benchmarków,
interpolacji wag i zakresu zastosowania Low36. Workflow kompiluje fixture GPU,
ale uruchomienie na sprzętowym GPU wykonano lokalnie; runner Actions nie jest
używany jako test jakości ani pomiar GPU w grze.

## Lokalny pomiar GPU — 11 października 2026

RTX 4090, rezydujące sinusoidy RGB 4K i residual 0,03. Working: P50 1920×1080,
P59 2266×1274, P65 2496×1404. Wejścia FP32, wyjście/low field FP16, Area/radius 1,
guide 1, sigma range 0,015, spatial 3,0, high gain 1,13, low gain 0,71, shadow gate wyłączony.
Każde okno: 90 warmup + 160 timestampów; timestamp obejmuje osiem powtórzeń
low-filter + głównej korekty oraz ich bariery, wynik podzielony przez osiem.
Alokacje/upload/fence CPU poza pomiarem. Zegarów i temperatury nie kontrolowano.
**To pomiar shaderów, bez NGX/NR, DLAA i final resolve; nie wynik z gry.**

| Skala | Kontrola → wariant | Mediana A / B ms | Zysk median ms | Zysk symetryczny ABBA ms | SD A / B ms | Mediany A1 / B1 / B2 / A2 ms |
|---|---|---:|---:|---:|---:|---|
| P50 | Fused optimized P50/general → Fast guided (approximate) | 0.306432 / 0.305792 | 0.000640 | 0.000896 | 0.016361 / 0.015689 | 0.305920 / 0.305920 / 0.305536 / 0.307328 |
| P50 | Fast guided (approximate) → Fast low36 (approximate) | 0.305792 / 0.289792 | 0.016000 | 0.015936 | 0.016500 / 0.014802 | 0.305664 / 0.289792 / 0.289792 / 0.305792 |
| P50 | Fast guided (approximate) → Fast weights (approximate) | 0.305792 / 0.305600 | 0.000192 | 0.000160 | 0.017971 / 0.016941 | 0.305664 / 0.305536 / 0.305728 / 0.305920 |
| P50 | Fast guided (approximate) → Fast cross5 (approximate) | 0.305920 / 0.305536 | 0.000384 | 0.000448 | 0.016995 / 0.015960 | 0.306048 / 0.305536 / 0.305408 / 0.305792 |
| P59 | Fused RGB20 → Fast guided (approximate) | 0.525440 / 0.428928 | 0.096512 | 0.094880 | 0.024663 / 0.019636 | 0.520192 / 0.428800 / 0.429184 / 0.527552 |
| P59 | Fast guided (approximate) → Fast low36 (approximate) | 0.429888 / 0.410880 | 0.019008 | 0.018816 | 0.020113 / 0.018621 | 0.428544 / 0.410880 / 0.410880 / 0.430848 |
| P59 | Fast guided (approximate) → Fast weights (approximate) | 0.430336 / 0.430464 | -0.000128 | -0.000160 | 0.030153 / 0.020290 | 0.430336 / 0.430336 / 0.430592 / 0.430272 |
| P59 | Fast guided (approximate) → Fast cross5 (approximate) | 0.429952 / 0.426304 | 0.003648 | 0.003968 | 0.029209 / 0.038084 | 0.430336 / 0.425728 / 0.426496 / 0.429824 |
| P65 | Fused RGB16 → Fast guided (approximate) | 0.577152 / 0.479744 | 0.097408 | 0.096128 | 0.029170 / 0.002735 | 0.573376 / 0.479552 / 0.479744 / 0.578176 |
| P65 | Fast guided (approximate) → Fast low36 (approximate) | 0.480896 / 0.497920 | -0.017024 | -0.036576 | 0.029280 / 0.033664 | 0.481152 / 0.497664 / 0.537408 / 0.480768 |
| P65 | Fast guided (approximate) → Fast weights (approximate) | 0.480768 / 0.481024 | -0.000256 | -0.000352 | 0.001566 / 0.001727 | 0.480768 / 0.481152 / 0.480960 / 0.480640 |
| P65 | Fast guided (approximate) → Fast cross5 (approximate) | 0.480768 / 0.475968 | 0.004800 | 0.004736 | 0.001342 / 0.001222 | 0.480640 / 0.476032 / 0.475904 / 0.480768 |

Surowe 7680 próbek: `docs/NR-v25-fast-experiments-GPU.samples.csv`.
Sprawdzono wszystkie 48 okien i indeksy 0–159. Driver NVIDIA 616.56.
Low36 skracał medianę w P50/P59 o 0,016/0,019 ms, ale w P65 był wolniejszy;
duża różnica B1/B2 w P65 uzasadniała osobne powtórzenie tego przypadku.

Powtórzenie P65 Low36, ten sam shader/fixture i ABBA 90/160:
Fast mediana `0,509184 ms`, Low36 `0,524032 ms`, zysk `−0,014848 ms`;
SD `0,031211 / 0,031892 ms`. Mediany A1/B1/B2/A2:
`0,506112 / 0,525056 / 0,523648 / 0,509888 ms`.
Zysk symetryczny `−0,016352 ms`, sąsiednie pary `−0,018944 / −0,013760 ms`.
Oba sąsiednie porównania również wskazują regresję; nie promujemy Low36 jako
zwycięzcy P65. Surowe 640 próbek: `docs/NR-v25-low36-P65-repeat-GPU.samples.csv`.

Weights w tych pomiarach nie ma przekonującego zysku; różnice są rzędu
`−0,000256..+0,000192 ms`. Cross5 ma mały zysk shaderowy P59/P65
`0,003648/0,004800 ms`; P50 jest niemal remisem i ma większy rozrzut próbek.
Pozostawiono wszystkie trzy jako jawne opcje do testów zgodnie z prośbą użytkownika.
Nie są domyślne ani automatycznie wybierane. Pomiar całego NR w grze może dać inne
wyniki; nie mnożymy shaderowych oszczędności przez liczbę passów i nie twierdzimy,
że te eksperymenty gwarantują szybszy cały tor albo niewidoczną zmianę obrazu.

## Zweryfikowana paczka do testów

[Release v25](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/releases/tag/nr-interpass-v25-fast-experiments-20261011)
zawiera `OptiScaler-NR-nr-interpass-v25-fast-experiments-20261011.zip`.
Źródłowy commit: `a678dfd558e8c8151c42788269b9813329bb25a6`.
[GitHub Actions 38124083818](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/actions/runs/38124083818)
zakończył się sukcesem, łącznie z kompilacją DLL, pakowaniem i publikacją release.

SHA256 ZIP: `5ece70c9aeca254b744158aa9f291c75c6330d6d031715332d0a71dd1239ed9e`.
Pobrana paczka przeszła CRC, wszystkie 62 wpisy `SHA256SUMS.txt`, kontrolę commita
oraz obecności siedmiu dokładnych blobów shaderów i nowych opcji/raportów w DLL.
Suma zgadza się również z digestem assetu GitHub. Raport ten i surowe pomiary są
w repozytorium; paczka używa dotychczasowego zestawu dokumentacji instalacyjnej.
Jakość percepcyjna oraz wydajność całego NR nowych eksperymentów nadal wymagają
sprawdzenia w konkretnej scenie gry.
