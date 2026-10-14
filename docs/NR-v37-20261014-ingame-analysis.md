# NR v37 — cached Low36 w grze, 14 października 2026

Cached Low36 skraca czas całego NR względem oryginalnego Fast przy wszystkich
trzech zmierzonych skalach: **P50 o 0,101632 ms, P59 o 0,286720 ms,
P65 o 0,197120 ms**. Obie strony każdego ABBA potwierdzają kierunek zysku.
P59 ma większą zmienność wielkości przyspieszenia. To pomiar z gry, a nie
ekstrapolacja syntetycznego kosztu shadera. Nie dostarczono oceny obrazu.

## Warunki i zakres

- The Last of Us Part I według folderu źródłowego; scena/kamera `unspecified`.
- RTX 4090, DXGI 32.0.16.1656 (NVIDIA 616.56), native 3840×2160.
- P50 1920×1080, P59 2266×1274, P65 2496×1404, sześć modeli.
- Transfer 9, Temporal DLAA residual + P100-guided, Area/radius 1, guide 1.
- Range sigma 0,015, spatial sigma 3, shaping Manual, high/mid compounding 1,
  shadow gate 0. Wszystkie modele Natural, preset 0, Structure 1.
- Raport nie zapisuje liczbowych manualnych gainów ani stanu HDR. Nie
  zakładamy ich wartości domyślnych. v38 dodaje gainy do metadanych.
- 18 konfiguracji / 24 niezależnie rozgrzane okna / 3840 próbek.
- Każde okno: 90 rozgrzewek, 160 pomiarów, bez ręcznego Hold.
- Timestamp obejmuje cały NR wraz z NGX i zależnościami, nie całą klatkę gry.
- Zegary i temperatura niekontrolowane. Dwa okna na wariant nie są dużą
  liczbą niezależnych powtórzeń; SD kolejnych klatek nie jest błędem zysku.

## Lokalne ABBA

Kontrola A = oryginalny Fast (4), B = Fast cached low (8).
Przed/po to średnia median okien danej strony: `(A1+A2)/2` i `(B1+B2)/2`.
Delta to wariant minus kontrola; ujemna oznacza szybciej.

| Skala | Fast / cached ms | Delta ms | Delta % | Pierwsza / ostatnia delta ms |
| --- | --- | --- | --- | --- |
| P50 | 25.876480 / 25.774848 | -0.101632 | -0.3928% | -0.099328 / -0.103936 |
| P59 | 32.465152 / 32.178432 | -0.286720 | -0.8832% | -0.245760 / -0.327680 |
| P65 | 36.190208 / 35.993088 | -0.197120 | -0.5447% | -0.198144 / -0.196096 |

| Skala | SD Fast / cached ms | Rozrzut median Fast / cached ms | Delta pooled P95 ms | Mediany A1 / B1 / B2 / A2 ms |
| --- | --- | --- | --- | --- |
| P50 | 0.191557 / 0.207465 | 0.005120 / 0.009728 | -0.102400 | 25.879040 / 25.779712 / 25.769984 / 25.873920 |
| P59 | 0.171007 / 0.208664 | 0.012800 / 0.069120 | -0.264192 | 32.458752 / 32.212992 / 32.143872 / 32.471552 |
| P65 | 0.155155 / 0.180465 | 0.005120 / 0.007168 | -0.131072 | 36.187648 / 35.989504 / 35.996672 / 36.192768 |

P50/P65 mają bardzo podobne zyski po obu stronach. Przy P59 mediana cached
zmienia się o 0,069120 ms, a kontrola o 0,012800 ms. Daje to zgodny kierunek,
ale nie równie stabilny szacunek wielkości zysku. P95 jest niższe we wszystkich
trzech punktach, choć SD pojedynczych próbek cached jest wyższe. Nie twierdzimy,
że każdy wskaźnik zmienności poprawił się ani że test dowodzi istotności
statystycznej lub zwycięstwa we wszystkich scenach/skalach.

Nie mnożymy tych wyników przez pięć korekcji ani sześć modeli: już dotyczą
całego NR. Są ważniejsze dla tej konfiguracji niż wcześniejszy shader-only ABBA.
Cached nie był szybszy od Off: pooled median był wyższy o 0,185856 / 0,244736 /
0,288768 ms. Pojedyncze okna Off/Optimized nie mają osobnego ABBA z cached,
więc nie traktujemy ich jako równie mocnego porównania lokalnego.

PSO raportuje właściwe ścieżki Fast i cached, bez fallbacku. Optimized używa
P50 specialization / RGB20 / RGB16. Raport ścieżki sam nie dowodzi użycia LDS
w każdym przypadku; tutaj ustawienie Manual pozwala na aktywny filtr low,
ale dokładne gainy nie zostały zapisane. Ocena czasu nie zastępuje oględzin
detali, HDR i ruchu. Cached pozostaje opcjonalnym wariantem.

## Audyt źródeł

Odtworzono 18 podsumowań z 3840 próbek: pełne indeksy 0–159 w 24 oknach,
liczebności, geometrie, wybory trybów/ścieżek, stride 1 i pięć korekcji poza Off,
mean/median/P95/min/max/SD, mediany i rozrzuty okien oraz pochodne różnice.
Odtworzono wszystkie trzy ABBA, obie sąsiednie różnice i wszystkie kolumny par.
Największy błąd summary to 0,000004812 ms, zgodny z zaokrągleniem CSV;
błędy par mieszczą się w 0,00000051 ms. Nie wykryto niespójności raportu.

Kopie są identyczne bajtowo z przekazanymi plikami. Źródła:

| Plik | SHA256 |
| --- | --- |
| [Summary](NR-v37-cached-low-20261014-000730.csv) | `69c7831119bd6d3db9a25c0370a722413b2aae094e0223ccbab6437d61b42629` |
| [Pary ABBA](NR-v37-cached-low-20261014-000730.pairs.csv) | `5651a1278ecdf9cc2b511419fcbbf9a4a1897f4700d490a0801279325b86e870` |
| [Surowe próbki](NR-v37-cached-low-20261014-000730.samples.csv) | `d9f089018618e48bc6eb95099daebe735a96a37f9752b0657e5b894d4dbd31a3` |
| [Metadane](NR-v37-cached-low-20261014-000730.txt) | `2a6f02655be406952b1baa4c14bbb9b9c4be05512883d96b344bb1a2ca1342f8` |

Wyniki pochodzą z v37, przed checkboxem neutralnego low z v38. Nie dowodzą
przyspieszenia ani jakości nowej opcji, która celowo zmienia shaping low
między modelami, zachowując konfigurację końcowego resolve.
