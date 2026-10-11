# NR v29: Cross5 w Fast również przy P50

**Fast (INI 4) zawsze zawiera Cross5 w swoim obsługiwanym zakresie,
również przy 50%.** Usuwamy wyjątek P50 zgodnie z poleceniem użytkownika:
remis w dostarczonym pomiarze i brak zauważonego pogorszenia jakości nie
uzasadniają osobnej reguły bez potwierdzonej przyczyny. Nadal jest jedna
opcja Fast, z Low36. Dawne wartości 5/6/7 migrują do 4 i są aliasami Fast.

Warunki kwalifikacji nadal są takie same: Area, radius 1, guide >0,
native większe od working, rzeczywiste working/native 25–90% na obu
osiach. „Zawsze” dotyczy tego zakresu. Pozostałe ustawienia wybierają
dokładny Optimized. Nie zmieniamy matematyki ani bytecode shaderów,
dispatchy, tekstur i barier. Zmiana dotyczy inter-pass.

## Dlaczego Fast nie używa specjalizacji dokładnego P50

Dokładny tor rekonstruuje korektę w native i integruje ją do working.
Mode28 przy idealnym podziale native/working = 2 ma specjalizację dla
czterech pikseli native z ponownym użyciem stencilów. Fast Mode36 zamiast
tej integracji liczy jedną przybliżoną korektę na piksel working, z próbką
native w centrum. Specjalizacja dokładnego P50 przyspiesza pracę, którą
Fast pomija; dołączenie jej przywróciłoby pominięte obliczenia. Nie jest
przyczyną remisu Cross5 przeciw Fast potwierdzoną pomiarem GPU.

Fast zachowuje wspólne usprawnienia: gotowy wynik working powstaje w jednym
przebiegu korekty z clamp/sanitize, bez pełnej tekstury korekty native i
oddzielnej redukcji, z ponownym użyciem zasobów i lazy PSO. Zachowuje
shaping i shadow gate, dokładne wagi `exp2`, ograniczenia krawędzi i alfę.
Low36 jest generowany tylko, gdy shaping potrzebuje low field; równe gainy
pomijają jego generowanie. Cross5 usuwa cztery rogi stencila 3×3.

Fast nie używa RGB16/RGB20 LDS ani cache stencilów dokładnej integracji:
to alternatywne algorytmy dla większej ilości dokładnej pracy. Poza
kwalifikacją Fast zachowana polityka Optimized dobiera dokładne P50,
RGB16/RGB20 albo Classic. Zwykły/końcowy Mode18 8×8, temporal DLAA i
final resolve zachowują dotychczasowe działanie.

## Pomiary i jakość

W [v26](NR-v26-20261011-ingame-analysis.md) Cross5 przy P50 dał zmianę
całego NR +0,002304 ms, z sąsiednimi −0,006144 / +0,010752 ms. Minus
oznacza przyspieszenie. To praktyczny remis, a nie dowód regresji czy
przyspieszenia. Użytkownik nie zauważył pogorszenia jakości v26, także po
pytaniu o ruch. Tę ocenę i decyzję o usunięciu wyjątku zachowujemy oddzielnie
od twierdzeń o wydajności: v29 nie ma jeszcze nowego pomiaru całego NR w grze.

Warunki tego przebiegu: RTX 4090, NVIDIA 616.56, TLOU Part I według folderu
źródłowego, scena `unspecified`, native 3840×2160 / working 1920×1080,
Transfer 9, sześć passów, Area/radius 1/guide 1, range 0,015, spatial 3,
shaping 2, shadow gate 0. Osobne 90 klatek rozgrzewki i 160 próbek na okno.
Lokalne mediany puli A/B 26,070528 / 26,066432 ms, SD 0,196573 / 0,179403 ms,
P95 26,420224 / 26,359808 ms. Średnia median okien ABBA daje inną miarę
niż mediana puli; żadna nie daje tu wyraźnego zysku. Zegarów/temperatury
nie kontrolowano. Te czasy obejmują cały NR, nie całą klatkę gry; nie
mnożymy ich przez liczbę passów.

[Pomiary niższych skal v27](NR-v27-20261011-ingame-analysis.md) i
[integracja v28](NR-v28-integrated-fast.md) opisują pozostałe zmierzone
punkty. P25 i ułamkowe skale nie są przez to potwierdzonymi zwycięzcami.

## Benchmark

**Compare integrated Fast / Fast without Cross5 at 50% / 59% / 65% (CSV)**
porównuje teraz przy każdej z trzech skal Off, obie referencje,
Optimized / Fast / Fast / Optimized, a następnie wymuszony Fast bez
Cross5 / Fast / Fast / kontrola. To 18 konfiguracji / 33 okna / sześć
bloków ABBA / 5280 próbek przy 160 próbkach na okno. P50 nie jest pomijane.

Kontrola bez Cross5 pozostaje wyłącznie w benchmarku, z Low36 i dziewięcioma
sąsiadami. Wymuszenie nie omija ograniczeń kwalifikacji Fast. Summary
`gain/delta_vs_fast_median_ms` odnosi się do aktualnego Fast z Cross5;
`.pairs.csv` zachowuje sąsiednią kontrolę. Opcjonalne manual hold pozwala
sprawdzić obraz po rozgrzewce bez wliczania klatek oglądania do pomiaru.
Ruch sprawdzamy oddzielnie od statycznych pomiarów. Ustawienia wracają
po zakończeniu/anulowaniu.

Podstawowy FastQuality nadal ma 15 konfiguracji / 21 okien; niższy profil
P33/P40/P41/P42/P49 + dodatkowa bieżąca niższa skala nadal ma 40 / 80
lub 48 / 96. Regular, Below50 i Boundary zachowują swoje dokładne kontrole,
Off i oba referencyjne. Nowe prefiksy: `NR-v29-fast-`, `NR-v29-fast-baseline-`,
`NR-v29-fast-low-`.

## Weryfikacja

Lokalnie przeszły sześć istniejących testów arytmetyki inter-pass i test
benchmarku. Nowo skompilowany test produkcyjnej polityki zachowuje
referencje i dokładne tory, sprawdza P25–P100, filtry/radii/guide, aliasy
trybów, dokładne/zaokrąglone P50 i duże wymiary, oraz 37 622 osie kafli
(21 z chronionym overflow). Na RTX 4090 przeszło 648 kontroli shaderów,
w tym Cross5 przy P50, rzeczywisty Low36 + guiding, Mode18, FP16/FP32,
HDR/nonfinite, signed DC, krawędzie, alfa i izolowane/fallback PSO.
To kontrola poprawności, nie nowy pomiar wydajności ani ocena percepcyjna.

Wynik GitHub Actions i weryfikacja paczki zostaną zapisane po zakończeniu buildu.
