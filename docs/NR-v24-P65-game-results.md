# NR v24 — wyniki kontrolowanego porównania baseline P65

## Wniosek

Raport `NR-v24-p65-baseline-20261011-092513` nie potwierdza regresji
głównego standardowego shadera. Stare rzeczywiste binarium v13/v14 nie
obniża mediany Off ani Optimized względem obecnego w tym samym torze gry.
Nie ma podstaw do jego przywrócenia ani do zmiany produkcyjnej polityki.

To porównanie zmienia tylko standard PSO, a nie całą historyczną DLL.
Audyt CPU/NGX/zasobów i wcześniejsze porównania inter-pass są opisane w
[audycie baseline](NR-v24-P65-baseline-audit.md),
[wynikach v22](NR-v22-P65-game-results.md) i
[profilu v23](NR-v23-P65-guide16-benchmark.md).
Nie oznacza to dowodu identycznej wydajności wszystkich dawnych sesji.

## Warunki i kontrola danych

Źródła, zachowane bez zmian:
`F:/Gry/The Last of Us - Part I/OptiScaler-NR-Benchmarks/`
`NR-v24-p65-baseline-20261011-092513.{csv,samples.csv,txt}`.

The Last of Us Part I; NVIDIA GeForce RTX 4090, sterownik DXGI 32.0.16.1656.
3840×2160 → 2496×1404, P65, sześć modeli, Area, radius 1, guide 1.
Transfer 9, range sigma 0,015, spatial sigma 3, shaping 2, compound 1,
shadow gate 0, HDR transfer 0. Natural/style 1, intensity/structure/tone 1,
auto mask 1; skin pass 2 = -0, pozostałe = 1.
Pełne zapisane ustawienia modeli/transferu/shaping są zgodne z TXT v23.
Scena `unspecified`; nie ustalono identycznego kadru między sesjami.
Zegary/temperatura nie były kontrolowane.

Dziesięć okien, każde po 90 próbek rozgrzewki i 160 pomiarowych.
Off ABBA, obie obecne referencje, Optimized ABBA. Każdy z czterech
wariantów A/B ma 320 próbek; referencje po 160.
Oba Optimized używają bieżącego Fused RGB16, różni je wyłącznie standard PSO.
Mierzony jest cały przedział GPU NR, w tym NGX, przejścia i overlap.

Niezależny reader sprawdził wszystkie 1600 próbek: brak duplikatów i braków
indeksów 0..159, kolejność dziesięciu okien, rewizje, faktyczne ścieżki,
geometrię i sześć aktywnych modeli. Przeliczono mean/median/P95/min/max/SD,
mediany okien, spread oraz nowe kolumny matching Off i gain same mode.
Zgodność ze skróconym CSV do jego dokładności zapisu — PASS.

## Porównanie standard PSO

| Tryb / standard | Próbki | Mediana ms | Średnia ms | P95 ms | SD ms | Narzut nad własnym Off ms |
|---|---:|---:|---:|---:|---:|---:|
| Off, obecny | 320 | 35,623936 | 35,633782 | 35,904512 | 0,164496 | 0 |
| Off, v13/v14 | 320 | 35,624448 | 35,623859 | 35,893248 | 0,156085 | 0 |
| Optimized, obecny | 320 | 37,140992 | 37,153549 | 37,432320 | 0,172648 | 1,517056 |
| Optimized, v13/v14 + obecny RGB16 | 320 | 37,158400 | 37,165898 | 37,440512 | 0,163969 | 1,533952 |
| Classic reference, obecny | 160 | 38,665216 | 38,676128 | 38,906880 | 0,151999 | 3,041280 |
| Fused reference, obecny | 160 | 41,631744 | 41,618733 | 41,858048 | 0,164420 | 6,007808 |

Stary−obecny: Off **+0,000512 ms** mediany, choć średnia starego jest
o 0,009923 ms niższa. Optimized: stary **+0,017408 ms** mediany i
**+0,012349 ms** średniej. Narzut inter-pass z obecnym standardem jest
o **0,016896 ms** niższy. To małe różnice i nie dowód stałego zwycięstwa
obecnego standardu w każdej scenie, ale nie ma tu odzyskania 0,1–0,26 ms
przez przywrócenie starego binarium.

## ABBA i zmienność

| Blok | A1 obecny ms | B1 stary ms | B2 stary ms | A2 obecny ms |
|---|---:|---:|---:|---:|
| Off | 35,591680 | 35,622400 | 35,629056 | 35,638784 |
| Optimized | 37,134336 | 37,169152 | 37,138432 | 37,143552 |

Różnice **stary−obecny** median w dwóch parach:

- Off: +0,030720 / −0,009728 ms.
- Optimized: +0,034816 / −0,005120 ms.

Obie zmieniają znak. Rozstęp median obecnego Off między A1/A2 to
0,047104 ms, starego 0,006656 ms. Przy Optimized: 0,009216 / 0,030720 ms.
Średnie obu par Optimized są po stronie obecnego (+0,010611 / +0,014086 ms
stary−obecny), przy Off średnie są po stronie starego
(−0,001523 / −0,018323 ms). Nie utożsamiać SD próbek z przedziałem ufności
różnicy; kolejne klatki nie są niezależnymi powtórzeniami eksperymentu.

## Względem poprzednich sesji

Obecny Off v24 35,623936 ms jest o **0,204288 ms niższy niż w v23**,
bez przywracania starego standardu. Stary standard w tym samym v24 ma
35,624448 ms, praktycznie tyle samo. Różnica między sesjami v23/v24 jest
więc dużo większa niż zmierzony wpływ zmiany tego PSO.
Nie ustalono, czy powodują ją zegary, stan gry, historia NGX, obciążenie
zewnętrzne czy inne warunki; nie przypisujemy przyczyny bez pomiaru.

Obecny Off v24 jest o 0,025600 ms niższy niż historyczny v14 i o 0,057856 ms
wyższy niż v13. Optimized v24 37,140992 ms jest nadal o 0,120832 ms wyżej
niż RGB16 v14 37,020160 ms. Narzut względem własnego Off jest wyższy
o 0,146432 ms względem historycznego v14. Historyczne ustawienia/scena
nie są w pełni udokumentowane, więc nie jest to kontrolowany pomiar regresji.
Przywrócenie standardu w v24 tej różnicy nie zmniejsza.

## Decyzja i dalsza praca

Zostaje obecny standard i Optimized. Nie wprowadzono zmiany jakości ani
domniemanej poprawki. Nie powtarzać v22/v23/v24 bez konkretnej nowej hipotezy:
dotychczasowe kontrolowane porównania nie odzyskały historycznej przewagi
przez zmianę standardu, starego RGB16 ani Guide16.
Jeśli audyt będzie kontynuowany, pozostałą różnicę historycznego narzutu
traktować jako nierozstrzygniętą, a nie potwierdzony błąd shadera.
Ten zakres obejmuje analizę i dokumentację, nie wymaga nowego buildu.

## Identyfikacja źródeł

SHA256 oryginalnych plików:

- CSV: `067ad978ec2fe28bc14184d3d677dbaa7760f0b1c9f1537c96af3f4118efa55e`.
- Raw samples CSV: `625d9730f2889619ff1173ba56a0dd3dbd1f2fe774a9657f0a9e4e41706c28b2`.
- TXT: `74c995d91c45a9ee7c24083d10bd436d3489ba9c1fa303c2c5b21e1549bbe1c8`.

Lokalny reader: `artifacts/nr-v24/analyze_game.py`;
przeliczone wyniki: `artifacts/nr-v24/game_analysis.json`.
Nie zmieniono źródeł użytkownika ani shaderów w paczce v24.
