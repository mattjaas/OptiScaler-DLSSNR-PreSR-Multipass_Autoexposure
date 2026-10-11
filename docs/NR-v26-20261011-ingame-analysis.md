# NR v26: wynik z gry, 11 października 2026, raport 121917

**Fast + Cross5 z Low36 daje dodatkowe przyspieszenie przy P59 i P65.**
Zmiana czasu całego NR względem nowego Fast z Low36 wynosi +0,002304 ms
przy P50 (praktycznie remis), −0,049152 ms przy P59 i −0,111360 ms przy P65.
Oba sąsiednie porównania P59/P65 wskazują przyspieszenie. P65 ma bardzo
zbliżone wyniki po obu stronach i przekracza istotne dla projektu 0,1 ms.
Użytkownik na pytanie o jakość połączonego wariantu v26, także w ruchu,
odpowiedział **„Nie zauważyłem pogorszenia”**. To ocena przetestowanego
obrazu, a nie gwarancja identyczności ani wynik dla wszystkich scen.

Dla tej konfiguracji warto korzystać z Fast + Cross5 przy P65; przy P59
zysk jest mniejszy, ale zgodny po obu stronach ABBA. P50 nie daje dowodu
dodatkowej oszczędności względem podstawowego Fast. Zachowujemy wariant
połączony jako osobną opcję; analiza nie zmienia kodu, domyślnego Off
ani polityki dokładnego Optimized.

## Warunki pomiaru

- The Last of Us Part I według folderu źródłowego; scena/kamera `unspecified`.
- RTX 4090, DXGI 32.0.16.1656 (NVIDIA 616.56), native 3840×2160.
- Working: P50 1920×1080, P59 2266×1274, P65 2496×1404.
- Transfer 9, Temporal DLAA residual + P100-guided, sześć efektywnych passów.
- Area, radius 1, guide 1, range sigma 0,015, spatial sigma 3,0.
- Shaping 2, shadow gate 0. Raport nie podaje dokładnych gainów/stylów
  poszczególnych passów ani stanu HDR; nie odtwarzamy ich z samych nazw ścieżek.
- 18 konfiguracji / 33 okna / 5280 próbek / sześć par ABBA.
- Osobne 90 klatek rozgrzewki i 160 próbek na okno, manual hold 0.
- Mierzony jest cały przedział NR na GPU, z NGX i zależnościami,
  nie sam inter-pass ani cała klatka gry. Zegary i temperatura niekontrolowane.
- Każda para ma dwa okna kontroli i dwa wariantu. Próbki kolejnych klatek
  nie stanowią 320 niezależnych powtórzeń eksperymentu.

## Lokalny wynik ABBA

A oznacza kontrolę, B wariant. Główną miarą jest zmiana czasu
`(mediana B1 + mediana B2 − mediana A1 − mediana A2)/2`.
**Minus oznacza przyspieszenie.** Procent odnosimy do średniej dwóch
median kontroli. Zachowane kolumny `gain` mają przeciwny znak.

| Skala | Porównanie A / B | Zmiana ABBA ms | Zmiana % | Pierwsza / ostatnia zmiana ms |
|---|---|---:|---:|---:|
| P50 | Optimized / Fast z Low36 | -0,374528 | -1,4173% | -0,360448 / -0,388608 |
| P50 | Fast z Low36 / Fast + Cross5 z Low36 | +0,002304 | +0,0088% | -0,006144 / +0,010752 |
| P59 | Optimized / Fast z Low36 | -0,749568 | -2,2391% | -0,721408 / -0,777728 |
| P59 | Fast z Low36 / Fast + Cross5 z Low36 | -0,049152 | -0,1503% | -0,069120 / -0,029184 |
| P65 | Optimized / Fast z Low36 | -0,837888 | -2,2437% | -0,834560 / -0,841216 |
| P65 | Fast z Low36 / Fast + Cross5 z Low36 | -0,111360 | -0,3050% | -0,107008 / -0,115712 |

Przy P50 zmiany −0,006144 / +0,010752 ms są niewielkie i przeciwne.
Średnia +0,002304 ms nie jest użytecznym dowodem regresji ani przyspieszenia.
Kontrola zmieniła medianę o 0,014336 ms, wariant o 0,031232 ms.
Przy P59 obie strony skracają czas, ale wielkość zysku różni się
(0,069120 / 0,029184 ms). Przy P65 obie strony są zbliżone
(0,107008 / 0,115712 ms), przy rozrzucie kontroli 0,007168 ms
i wariantu 0,001536 ms.

## Mediany, SD, P95 i okna

Poniżej pulujemy wyłącznie 320 próbek każdej strony danej lokalnej pary.
SD jest populacyjne; P95 nearest rank. Różnica median puli może mieć
inną wartość, a przy prawie remisie nawet inny znak niż symetryczny ABBA.
W P50 mediana puli Cross5 jest niższa od kontroli o 0,004096 ms, podczas
gdy zmiana z czterech median okien to +0,002304 ms. To dwie różne miary,
nie błąd raportu. Globalne summary używa wszystkich czterech okien Fast
danej skali, więc nie zastępuje kontroli z sąsiedniego bloku.

| Skala / porównanie | Mediana A / B ms | SD A / B ms | P95 A / B ms | A1 / B1 / B2 / A2 ms |
|---|---:|---:|---:|---|
| P50 Optimized / Fast | 26,423296 / 26,055680 | 0,189718 / 0,190607 | 26,757120 / 26,405888 | 26,405888 / 26,045440 / 26,056704 / 26,445312 |
| P50 Fast / Fast + Cross5 | 26,070528 / 26,066432 | 0,196573 / 0,179403 | 26,420224 / 26,359808 | 26,058752 / 26,052608 / 26,083840 / 26,073088 |
| P59 Optimized / Fast | 33,481216 / 32,728064 | 0,302772 / 0,129011 | 33,980416 / 32,903168 | 33,458176 / 32,736768 / 32,717312 / 33,495040 |
| P59 Fast / Fast + Cross5 | 32,705536 / 32,651776 | 0,108123 / 0,114997 | 32,870400 / 32,837632 | 32,709120 / 32,640000 / 32,671744 / 32,700928 |
| P65 Optimized / Fast | 37,343232 / 36,504576 | 0,184755 / 0,183612 | 37,702656 / 36,873216 | 37,342208 / 36,507648 / 36,504064 / 37,345280 |
| P65 Fast / Fast + Cross5 | 36,512256 / 36,398592 | 0,189341 / 0,185615 | 36,870144 / 36,774912 | 36,505600 / 36,398592 / 36,397056 / 36,512768 |

W tych lokalnych parach Cross5 ma niższy P95 przy każdej skali, także P50.
Nie zmienia to wniosku o praktycznym remisie median ABBA P50.
Nowy podstawowy Fast jest szybszy od Optimized po obu stronach każdej skali.
Nie sumujemy zysków oddzielnych par jako nowego bezpośredniego porównania
Cross5 z Optimized i nie mnożymy czasu całego NR przez liczbę passów.

Wyniki v25 dotyczyły Cross5 z filtrem low 64 próbek i innego przebiegu.
V26 mierzy Cross5 z Low36 przeciw Fast z tym samym Low36. Nie traktujemy
odejmowania wyników między tymi przebiegami jako kontrolowanego pomiaru
interakcji optymalizacji. Syntetyczny wynik shaderów v26 także dotyczył
innych danych i nie obejmował NGX/DLAA/final resolve.

## Uzgodnienie i archiwum

Odtworzono wszystkie 18 wierszy summary i sześć par z 5280 próbek:
liczebności, kolejność 33 okien, indeksy próbek, geometrie i ścieżki bez
fallbacków, mean/median/P95/min/max/SD, rozrzuty oraz wszystkie delty i gains.
Maksymalny błąd summary 0,0000048 ms mieści się w zaokrągleniu do pięciu
miejsc. Błąd pairs poniżej 0,000000000000009 ms odpowiada precyzji arytmetyki.
Ścieżka nie dowodzi, w których przejściach shaping potrzebował pola Low36.

Cztery archiwalne kopie w `docs/` są identyczne bajtowo ze źródłami:

| Źródło | SHA256 |
|---|---|
| [NR-v26-fast-cross5-20261011-121917.csv](NR-v26-fast-cross5-20261011-121917.csv) | `6c736db12f2a2969cf0c5946e7331141403aeadd4bdfd68709aa678eace44191` |
| [NR-v26-fast-cross5-20261011-121917.pairs.csv](NR-v26-fast-cross5-20261011-121917.pairs.csv) | `cdef465da7dd8a5f65cc532402b0b3280374ab0fea32c343539cd843b514b91b` |
| [NR-v26-fast-cross5-20261011-121917.samples.csv](NR-v26-fast-cross5-20261011-121917.samples.csv) | `ad537046bf001cd81beed2aa0a572e3ba1d3971229418697a9c4a01ed0ef8e95` |
| [NR-v26-fast-cross5-20261011-121917.txt](NR-v26-fast-cross5-20261011-121917.txt) | `cae1562c7f809a4b3499de4c0c0fdc5a6335d7e4a4cbe14009214dc1fcd6afeb` |
