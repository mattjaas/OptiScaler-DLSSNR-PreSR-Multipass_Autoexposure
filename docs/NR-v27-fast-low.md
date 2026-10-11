# NR v27: Fast poniżej P50 i Cross5 wyłączony przy P50

Aktualna integracja: [v30 — Cross5 w Fast przy każdym pomniejszeniu](NR-v30-full-range-fast.md).
Poniższy opis i wyniki dotyczą historycznych oddzielnych opcji v27.

**Wynik z gry:** [raport 130323](NR-v27-20261011-ingame-analysis.md),
RTX 4090, 4K, sześć passów: Fast względem Optimized skraca cały NR
o 1,142016 / 0,465920 / 1,223424 / 0,697088 / 0,707328 ms przy
P33/P40/P41/P42/P49. Cross5 dodatkowo oszczędza odpowiednio
0,006912 / 0,014848 / 0,001792 / 0,011776 / 0,022528 ms; obie strony
każdego ABBA wskazują przyspieszenie. P41 to praktycznie remis,
P49 ma różną wielkość zysku po obu stronach. Użytkownik nie zauważył
pogorszenia jakości, także po pytaniu o ruch. Wszystkie 12 800 próbek
uzgodniono z summary i 15 parami. To wynik tych pięciu punktów i sceny,
nie potwierdzenie całego zakresu 25–49%. Kod i polityka pozostają bez zmian.

Opcja **Fast + Cross5** przy 50% używa podstawowego **Fast z Low36**,
z dziewięcioma sąsiadami i dokładnymi wagami. Przy P59/P65 Cross5 nadal
działa. Zgodnie z poleceniem użytkownika rozszerzamy oba warianty Fast
także poniżej P50, do rzeczywistego P25 na obu osiach, jako eksperyment.
Dokładny Optimized zachowuje swoją dotychczasową politykę i pozostaje
punktem odniesienia. Domyślne ustawienie nadal to Off.

Nie dodajemy nowego trybu menu. INI 4 wybiera Fast z Low36, a 7 ten sam
Fast z Cross5, z wyjątkiem P50. Wagi pozostają dokładnym `exp2`.
Warunki Fast: Area, radius 1, guide >0, native większe od working,
rzeczywiste working/native 25–90% na każdej osi. Pozostałe ustawienia
zachowują dokładny fallback. Ocena jakości niższych skal z gry jest
zapisana w raporcie powyżej; inne sceny i niezmierzone skale wymagają oceny.

## Dlaczego warto zbadać niższe skale

Fast zastępuje rekonstrukcję guidingu w native i redukcję Area korektą
obliczoną raz na piksel working. Przy mniejszej siatce working redukuje
liczbę miejsc, w których trzeba liczyć guiding. Low36 upraszcza low field
tylko wtedy, gdy shaping go potrzebuje; Cross5 dodatkowo usuwa czterech
sąsiadów diagonalnych. To ta sama matematyka co w v26, teraz dopuszczona
przy niższych skalach. Nie dodajemy dispatchy, tekstur ani barier GPU.

Jeden piksel working obejmuje większy fragment native. Przybliżenie
guidingu próbką native w centrum może więc bardziej zmieniać krawędzie
i drobne detale. Cross5 może dodatkowo zmieniać ukośne wzory. To hipoteza
o możliwym kompromisie jakości, nie stwierdzony artefakt ani jego gwarancja.
Pomiar całego NR w grze wskazuje przyspieszenie Fast w pięciu punktach
opisanych powyżej; nie rozszerzamy tego wniosku na niezmierzone skale.

Zwykły i końcowy filtr Mode18 nadal ma 8×8 próbek. Oba Fast używają Low36
wyłącznie między passami i pomijają low field, gdy shaping go nie potrzebuje.
Final resolve i temporal DLAA pozostają w istniejącym torze.

## P50

Warunkiem wyłączenia Cross5 są rzeczywiste wymiary na obu osiach.
Rozpoznajemy dokładne P50 oraz zaokrąglenie nieparzystego native w górę
o pół texela working, np. 65×37 → 33×19. Wyjątek nie obejmuje wszystkich
skal poniżej P50. Przy P50 CSV raportuje `Fast + Low36 (approximate)`,
także po wybraniu INI 7.

Powód: [pomiar v26 w grze](NR-v26-20261011-ingame-analysis.md) na RTX 4090,
4K, sześć passów, Area/radius 1/guide 1 dał zmianę Cross5 względem Fast
P50 +0,002304 ms, z sąsiednimi −0,006144 / +0,010752 ms. To praktyczny remis.
P59/P65 wykazały dodatkową oszczędność 0,049152 / 0,111360 ms i obie strony
ABBA były zgodne. Użytkownik nie zauważył pogorszenia obrazu tego wariantu.
Nie przedstawiamy wyłączenia Cross5 przy P50 jako nowego pomiaru GPU.

## Benchmark poniżej P50

Wybierz **Compare Optimized / Fast / Fast + Cross5 below 50% (CSV)**.
Profil ustawia Transfer 9 (Temporal DLAA residual + P100-guided), sześć
passów, Area/radius 1/guide 1; zachowuje shaping i ustawienia passów.
Testuje P33/P40/P41/P42/P49 oraz bieżącą niższą skalę, jeśli jest inna.
P45 pomijamy jako powtórzenie dotychczasowego dokładnego toru RGB20.

Każda skala ma:

1. Off, Classic reference, Fused reference i alternatywny Classic/RGB20.
2. Znany wymuszony dokładny zwycięzca / Optimized / Optimized / zwycięzca.
3. Optimized / Fast / Fast / Optimized.
4. Fast / Fast + Cross5 / Fast + Cross5 / Fast.

To 40 unikalnych konfiguracji / 80 okien / 15 par ABBA, albo 48 / 96 / 18
z dodatkową bieżącą skalą. Domyślnie każde okno ma osobne 90 klatek
rozgrzewki i 160 próbek pomiarowych: 12 800 lub 15 360 próbek.
Wszystkie te znaczniki obejmują **cały NR**, nie sam shader inter-pass.
Nie przywracamy odrzuconego Generic Fused optimized poniżej P50.

Przy znanych 4K skalach wymuszone kontrole to Classic P33/P41 i RGB20
P40/P42/P49. Dodatkowa niezmierzona skala używa jawnie przewidywanej
kontroli, z brakiem historycznego zysku. Weryfikacja wymuszonej kontroli
przeciw automatowi i alternatywnego toru pozostaje częścią pomiaru.
Nowe Fast nie otrzymują historycznych wyników dokładnych ścieżek.

Prefiks: `NR-v27-fast-low-`; summary/raw/pairs CSV i metadata TXT.
W `pairs.csv` comparison 1 to dokładna kontrola/automat, 2 Optimized/Fast,
3 Fast/Cross5. Preferuj lokalne `symmetric_delta_ms`: minus oznacza krótszy
czas. `gain` ma przeciwny znak. Globalne summary pulują powtarzające się
okna o tej samej nazwie; nie zastępują lokalnych porównań ABBA.

Zaznacz **Hold each experiment benchmark window for visual inspection**,
aby oglądać i przechwytywać wyniki. Klatki zatrzymania nie trafiają do próbek.
Do pomiaru czasów utrzymuj stałą scenę. Osobno sprawdź obraz w ruchu:
drobne i ukośne tekstury, włosy, roślinność, ciemne krawędzie oraz narastanie
różnic między passami. Zakończenie/anulowanie przywraca ustawienia.

## Benchmark powyżej i przy P50

Profil **Compare Fast / Fast + Cross5 at 50% / 59% / 65% (CSV)** zachowuje
Off, obie referencje i Optimized/Fast/Fast/Optimized przy każdej skali.
Dodatkowe Fast/Cross5/Cross5/Fast wykonuje przy P59/P65. P50 nie powtarza
identycznego Fast przeciw Fast. To 17 konfiguracji / 29 okien / pięć par,
4640 próbek przy 160 na okno. Prefiks `NR-v27-fast-cross5-`.
Profil Optimized/Fast ma prefiks `NR-v27-fast-`. Dotychczasowe Regular,
Below50 i Boundary zachowują swoje zestawy i dokładne ścieżki.

## Kontrola

Test produkcyjnej polityki obejmuje zakres P25–P90, wyjątek P50, P59/P65,
nieparzyste i asymetryczne wymiary, wejścia poniżej P25, wielkie wymiary
bez overflow oraz dotychczasowe filtry, promienie i referencje.
Test benchmarku obejmuje oba nowe plany, przywracanie ustawień i raporty.
Fixture GPU sprawdza Fast/Low36/Cross5 również przy P25/P33/P40/P41/P42/P49.
Nie zastępuje to oceny obrazu w grze. Pełny build wykonuje istniejący workflow.

Na RTX 4090 przeszło 648 kontroli GPU (w tym nowe niższe skale), test
polityki, wszystkie testy arytmetyki inter-pass i test planów benchmarku.
Shaderów produkcyjnych nie zmieniano; pełny build ponownie je kompiluje.

## Zweryfikowana paczka v27

[Build 38132850846](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/actions/runs/38132850846)
przeszedł testy Python/C++, kompilację shaderów i fixture GPU, pełny build
DLL, pakowanie i publikację. Kod paczki:
`d63c28fc9c42560e6d5500f88a339ce34b5215e3`.

[Pobierz ZIP v27](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/releases/download/nr-interpass-v27-fast-cross5-20261011/OptiScaler-NR-nr-interpass-v27-fast-cross5-20261011.zip).
SHA256: `1f60293330c7b8477ea892914e09c033c8b47caf071a04139d2c1131fa1da021`.

Pobrany ZIP przeszedł CRC i wszystkie 62 wpisy SHA256SUMS. DLL zawiera
nowy przycisk i prefiksy benchmarku niższych skal, opis wyjątku P50,
identyfikator commita oraz sześć shaderów identycznych z v26.
Dawne opcje i bytecode Weights są nieobecne. Dokument jest dostępny
w repozytorium; istniejący skrypt pakowania nie dołącza go do ZIP.
Paczka nie została automatycznie podmieniona w grze.

## Syntetyczny pomiar GPU — 11 października 2026

RTX 4090, sterownik NVIDIA 616.56. Rezydujące sinusoidy RGB 3840×2160,
residual 0,03; P50 1920×1080, P59 2266×1274, P65 2496×1404.
Niższe working: P33 1267×713, P40 1536×864, P41 1574×886,
P42 1613×907, P49 1882×1058.
Wejścia FP32, wyjście i low FP16, Area/radius 1/guide 1, sigma range 0,015,
spatial 3, high 1,13/low 0,71, shadow gate wyłączony. Każde okno ma 90
warmup i 160 timestampów GPU; timestamp obejmuje osiem powtórzeń
low-filter + korekty i ich barier, wynik dzielony przez osiem.
Classic wykonuje rekonstrukcję native i redukcję osobno; Fast/RGB mają
połączoną korektę working. Syntetyczny fixture wiąże rzeczywiście obliczony
low field także dla kontroli Classic, a nie zastępczą teksturę source.
Alokacje, upload i fence CPU poza pomiarem. Zegarów/temperatury nie kontrolowano.
**To pomiar etapów inter-pass bez NGX/NR, DLAA i final resolve, a nie wynik gry.**
Nie mnożymy zysków przez liczbę passów. Do oceny całego toru służy benchmark w grze.

| Skala | Kontrola → wariant | Mediana A / B ms | Zmiana ABBA ms (minus = szybciej) | SD A / B ms | P95 A / B ms | Mediany A1 / B1 / B2 / A2 ms |
|---|---|---:|---:|---:|---:|---|
| P33 | Classic optimized → Fast + Low36 (approximate) | 0.399936 / 0.153344 | -0.242016 | 0.029413 / 0.016376 | 0.471296 / 0.214912 | 0.388608 / 0.153344 / 0.153344 / 0.402112 |
| P33 | Fast + Low36 (approximate) → Fast + Low36 + Cross5 (approximate) | 0.153216 / 0.152960 | -0.000256 | 0.021708 / 0.018739 | 0.215808 / 0.214528 | 0.153344 / 0.152960 / 0.153088 / 0.153216 |
| P40 | Fused RGB20 → Fast + Low36 (approximate) | 0.308928 / 0.196608 | -0.110112 | 0.024767 / 0.016973 | 0.377472 / 0.257536 | 0.300544 / 0.196608 / 0.196672 / 0.312960 |
| P40 | Fast + Low36 (approximate) → Fast + Low36 + Cross5 (approximate) | 0.196608 / 0.196480 | -0.000096 | 0.018247 / 0.026998 | 0.258176 / 0.262144 | 0.196608 / 0.196480 / 0.196480 / 0.196544 |
| P41 | Classic optimized → Fast + Low36 (approximate) | 0.461760 / 0.217088 | -0.241376 | 0.028793 / 0.031160 | 0.532480 / 0.297728 | 0.454784 / 0.217280 / 0.216896 / 0.462144 |
| P41 | Fast + Low36 (approximate) → Fast + Low36 + Cross5 (approximate) | 0.216704 / 0.216704 | -0.000096 | 0.017845 / 0.018080 | 0.278144 / 0.277888 | 0.216704 / 0.216704 / 0.216640 / 0.216832 |
| P42 | Fused RGB20 → Fast + Low36 (approximate) | 0.405056 / 0.226816 | -0.175840 | 0.030322 / 0.019890 | 0.480896 / 0.289152 | 0.394496 / 0.227072 / 0.226560 / 0.410816 |
| P42 | Fast + Low36 (approximate) → Fast + Low36 + Cross5 (approximate) | 0.226624 / 0.226688 | -0.000032 | 0.018642 / 0.019536 | 0.287872 / 0.288768 | 0.226560 / 0.226624 / 0.226688 / 0.226816 |
| P49 | Fused RGB20 → Fast + Low36 (approximate) | 0.449920 / 0.301824 | -0.142272 | 0.029022 / 0.021799 | 0.517248 / 0.363392 | 0.437184 / 0.301696 / 0.301952 / 0.451008 |
| P49 | Fast + Low36 (approximate) → Fast + Low36 + Cross5 (approximate) | 0.301440 / 0.301568 | +0.000192 | 0.023183 / 0.022138 | 0.363392 / 0.363264 | 0.301504 / 0.301568 / 0.301760 / 0.301440 |
| P50 | Fused optimized P50/general → Fast + Low36 (approximate) | 0.308416 / 0.290560 | -0.017504 | 0.025065 / 0.021613 | 0.375936 / 0.351744 | 0.307072 / 0.290688 / 0.290368 / 0.308992 |
| P59 | Fused RGB20 → Fast + Low36 (approximate) | 0.561280 / 0.416512 | -0.143584 | 0.031384 / 0.040529 | 0.629504 / 0.517376 | 0.563072 / 0.419200 / 0.415040 / 0.558336 |
| P59 | Fast + Low36 (approximate) → Fast + Low36 + Cross5 (approximate) | 0.411008 / 0.408064 | -0.003328 | 0.030522 / 0.030958 | 0.486400 / 0.481408 | 0.410240 / 0.407744 / 0.408256 / 0.412416 |
| P65 | Fused RGB16 → Fast + Low36 (approximate) | 0.611904 / 0.531072 | -0.080512 | 0.033101 / 0.025159 | 0.684928 / 0.589824 | 0.613632 / 0.531072 / 0.530624 / 0.609088 |
| P65 | Fast + Low36 (approximate) → Fast + Low36 + Cross5 (approximate) | 0.529408 / 0.525696 | -0.005024 | 0.025115 / 0.025921 | 0.589696 / 0.588544 | 0.532736 / 0.525632 / 0.526016 / 0.528960 |

Źródło: [9600 próbek GPU](NR-v27-fast-low-GPU.samples.csv).
SHA256 `52ec4651950b3012e54e55897858cfbc981c2b0d96d3c028f3a9931ca9a138ff`.

Pojedynczy przebieg ABBA ma tylko dwa okna każdej strony. SD próbek nie
jest przedziałem ufności zysku. Wynik syntetyczny nie potwierdza jakości
ani uniwersalnej przewagi połączonego wariantu w grze.
