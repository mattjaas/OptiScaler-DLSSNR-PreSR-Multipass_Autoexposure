# NR v26: Low36 w podstawowym Fast i połączony Fast + Cross5

Low36 został zatwierdzony przez użytkownika po pomiarze całego NR w The Last
of Us Part I i ocenie obrazu bez zauważonego pogorszenia. Od v26 podstawowy
**Inter-pass fast** zawiera Low36. Jedyną dodatkową opcją jest
**Fast + Cross5 (experimental)**, czyli cały nowy Fast z filtrem Low36 oraz
pięcioma sąsiadami zamiast dziewięciu. Wagi pozostają dokładnym `exp2`.
Usunięto osobne Low36 i Weights oraz dawny wariant Cross5 z filtrem 64 próbek.

| INI | Opcja | Filtr low między passami | Sąsiedzi guiding |
|---|---|---|---|
| 4 | Inter-pass fast | Low36, 6×6 | 3×3, dziewięciu |
| 7 | Fast + Cross5 (experimental) | Low36, 6×6 | środek i cztery osie |

Zachowane wartości 5/6 ze starego INI migrują do 4. Dawna wartość 7 wybiera
nowy wariant połączony. Nie przypisujemy Cross5 do starej wartości Low36.
Off, oba tryby referencyjne i dokładny Optimized pozostają dostępne;
domyślne ustawienie nadal to Off. Fast działa dla Area/radius 1/guide >0
przy rzeczywistych wymiarach 50–90% na obu osiach; pozostałe ustawienia
korzystają z dotychczasowej dokładnej polityki Optimized.

Zmiana dotyczy wyłącznie inter-pass. Zwykły i końcowy filtr Mode18 zachowuje
8×8 próbek. Low36 jest pomijany, kiedy shaping nie potrzebuje pola low,
w szczególności przy równych gain high/low. Podstawowy Fast ma ten sam
wynik co dawny zatwierdzony Low36; nie zmieniamy jego kernela ani guidingu.
Nie dodajemy dispatchy, tekstur, barier ani alokacji w każdej klatce.
PSO Low36 i Cross5 są tworzone leniwie, bez LDS, z fallbackiem na ten sam
tryb standardowego shadera. Kod i skompilowane shadery Weights usunięto.

## Benchmark w grze

Wybierz **Compare Fast / Fast + Cross5 at 50% / 59% / 65% (CSV)**.
Profil ustawia Temporal DLAA residual + P100-guided (Transfer 9), sześć
passów, Area/radius 1/guide 1 i zachowuje bieżący shaping oraz ustawienia
poszczególnych passów. Dokładnie P50/P59/P65, bez dodatkowej bieżącej skali.
Każda skala ma:

1. Off, Classic reference, Fused reference.
2. Optimized / Fast / Fast / Optimized.
3. Fast / Fast + Cross5 / Fast + Cross5 / Fast.

To 18 unikalnych konfiguracji / 33 okna. Domyślnie każde okno ma własne
90 klatek rozgrzewki i 160 zmierzonych próbek (5280 łącznie). Znaczniki GPU
obejmują cały NR. Osobne CSV zawierają podsumowanie, próbki i sześć par ABBA,
a TXT opisuje warunki. Prefiks nowego profilu: `NR-v26-fast-cross5-`;
istniejący profil Optimized/Fast również dostaje prefiks v26, aby nie mylić
nowej podstawy z Fast v21. Inne profile benchmarku zachowują swoje zestawy.

W `.pairs.csv` najważniejszy jest `symmetric_delta_ms`: średnia z median
dwóch okien wariantu minus średnia z median jego dwóch sąsiednich kontroli.
**Minus oznacza skrócenie czasu.** `first_delta_ms` i `last_delta_ms` pokazują
obie strony porównania. Zachowane kolumny `*_gain_ms` mają przeciwny znak:
plus oznacza zaoszczędzony czas. W podsumowaniu `delta_vs_fast_median_ms`
używa wszystkich okien Fast danej skali; do oceny Cross5 preferuj lokalną
parę ABBA. Nie sumuj dawnych oddzielnych zysków Low36 i Cross5 jako wyniku
nowego połączonego wariantu.

Zaznacz **Hold each experiment benchmark window for visual inspection**,
aby oglądać i przechwytywać obraz po każdym rozgrzanym pomiarze. Przejdź
dalej istniejącym przyciskiem benchmarku. Klatki zatrzymania nie wchodzą
do próbek. Do czasów zachowaj tę samą nieruchomą scenę, a wygląd sprawdź
także osobno w ruchu: ukośne krawędzie, włosy, roślinność, drobne wzory,
ciemne detale i narastanie różnic między passami. Zakończenie/anulowanie
przywraca wcześniejsze ustawienia.

## Kontrole lokalne

Testy arytmetyki inter-pass, benchmarku i polityki przeszły. DXC kompiluje
sześć shaderów: standard, RGB16, RGB20, Fast, Low36, Cross5; izolowane Fast,
Low36 i Cross5 nie alokują LDS. Test sprzętowy na RTX 4090 sprawdza także
połączenie rzeczywiście obliczonego Low36 z guidingiem, zgodność z v25,
zwykły Mode18, signed DC, krawędzie, FP16/FP32, dane HDR/nonfinite, alfę
i zgodność izolowanych PSO z fallbackiem. To kontrola poprawności,
nie ocena wizualna połączonego wariantu w grze.

Na RTX 4090 przeszło 216 kontroli połączonego toru, 72 kontrole Fast oraz
6528 porównań dokładnych ścieżek (53 543 552 komponentów RGBA).

## Zweryfikowana paczka

[Build 38130331565](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/actions/runs/38130331565)
zakończył się sukcesem: testy Python/C++, kompilacja shaderów i fixture GPU,
pełny build DLL, pakowanie i publikacja. Kod paczki:
`7cf72bb6e6a42687a8d708af5a5927ae4222947f`.

[Pobierz ZIP v26](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/releases/download/nr-interpass-v26-fast-cross5-20261011/OptiScaler-NR-nr-interpass-v26-fast-cross5-20261011.zip).
SHA256: `f8a3648f583e722f49a3dc44b1467d97b6ebb27476dbf2b72d3e0f481cce5096`.

Pobrany ZIP przeszedł CRC i weryfikację wszystkich 62 wpisów SHA256SUMS.
DLL zawiera sześć właściwych shaderów, identyfikator commita, nowe opcje
menu i kolumny raportu. Dawne opcje oraz bytecode Weights są nieobecne.
Dokument v26 jest dostępny w repozytorium; istniejący skrypt pakowania
nie dołącza go do ZIP. Paczka nie została automatycznie podmieniona w grze.

## Syntetyczny pomiar GPU — 11 października 2026

RTX 4090, sterownik NVIDIA 616.56. Rezydujące sinusoidy RGB 3840×2160,
residual 0,03; P50 1920×1080, P59 2266×1274, P65 2496×1404.
Wejścia FP32, wyjście i low FP16, Area/radius 1/guide 1, sigma range 0,015,
spatial 3, high 1,13/low 0,71, shadow gate wyłączony. Każde okno ma 90
warmup i 160 timestampów GPU; timestamp obejmuje osiem powtórzeń
low-filter + korekty i ich barier, wynik dzielony przez osiem.
Alokacje, upload i fence CPU poza pomiarem. Zegarów/temperatury nie kontrolowano.
**To pomiar dwóch shaderów bez NGX/NR, DLAA i final resolve, a nie wynik gry.**
Nie mnożymy zysków przez liczbę passów. Do oceny całego toru służy benchmark w grze.

| Skala | Kontrola → wariant | Mediana A / B ms | Zmiana ABBA ms (minus = szybciej) | SD A / B ms | P95 A / B ms | Mediany A1 / B1 / B2 / A2 ms |
|---|---|---:|---:|---:|---:|---|
| P50 | Fused optimized P50/general → Fast + Low36 (approximate) | 0.316928 / 0.303616 | -0.013856 | 0.031782 / 0.022071 | 0.399616 / 0.371584 | 0.316160 / 0.303552 / 0.303744 / 0.318848 |
| P50 | Fast + Low36 (approximate) → Fast + Low36 + Cross5 (approximate) | 0.303680 / 0.303360 | -0.000320 | 0.027486 / 0.022076 | 0.379648 / 0.373248 | 0.303616 / 0.303360 / 0.303360 / 0.303744 |
| P59 | Fused RGB20 → Fast + Low36 (approximate) | 0.552576 / 0.407680 | -0.145280 | 0.035316 / 0.030827 | 0.638464 / 0.483840 | 0.556288 / 0.407936 / 0.407552 / 0.549760 |
| P59 | Fast + Low36 (approximate) → Fast + Low36 + Cross5 (approximate) | 0.408256 / 0.405504 | -0.002816 | 0.026774 / 0.028850 | 0.483328 / 0.479488 | 0.407936 / 0.405312 / 0.405696 / 0.408704 |
| P65 | Fused RGB16 → Fast + Low36 (approximate) | 0.604544 / 0.526720 | -0.078240 | 0.039428 / 0.029725 | 0.701184 / 0.599296 | 0.607808 / 0.526720 / 0.526720 / 0.602112 |
| P65 | Fast + Low36 (approximate) → Fast + Low36 + Cross5 (approximate) | 0.526720 / 0.524224 | -0.002560 | 0.029235 / 0.031585 | 0.597120 / 0.615040 | 0.526848 / 0.524288 / 0.524160 / 0.526720 |

Źródło: [3840 próbek GPU](NR-v26-fast-cross5-GPU.samples.csv).
SHA256 `841cdb09f34a37ee53e712e09aea98597a46f6a9538a8741eabadf910632ef6a`.

Pojedynczy przebieg ABBA ma tylko dwa okna każdej strony. SD próbek nie
jest przedziałem ufności zysku. Wynik syntetyczny nie potwierdza jakości
ani uniwersalnej przewagi połączonego wariantu w grze.

## Pomiar całego NR w grze

[Raport 121917](NR-v26-20261011-ingame-analysis.md) z RTX 4090 / 4K / sześciu passów
potwierdza dodatkowe skrócenie czasu Fast + Cross5 względem Fast z Low36:
−0,049152 ms przy P59 i −0,111360 ms przy P65, po obu stronach lokalnego ABBA.
P50 daje praktyczny remis (+0,002304 ms). Użytkownik nie zauważył pogorszenia
obrazu połączonego wariantu, także przy pytaniu o ruch. Wynik dotyczy tego
przebiegu i ustawień; wariant pozostaje osobną opcją.
