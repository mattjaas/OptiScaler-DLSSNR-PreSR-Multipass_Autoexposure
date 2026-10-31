# Kontrast głowy postaci i wody: 2,25x → 6x

2026-10-31. Użytkownik doprecyzował, że ghosting pojawiał się na wodzie podczas obracania kamerą. Zapis wykonał trochę za wcześnie: na tle wody jest dopiero głowa, nie cała sylwetka. Dlatego podstawowy lokalny pomiar dotyczy głowy i pobliskiej wody. Z tego zapisu nie odtwarzamy przebiegu smużenia ani nie oznaczamy 6x jako ustawienia bez ghostingu.

[Pełne dane liczbowe, maski opisane parametrami i hashe](validation/nr-edit-head-water-contrast.json), [narzędzie CPU](../tools/analyze_nr_head_water_contrast.py). Nie uruchamiano gry, GPU ani NGX; DLL pozostaje bez zmian w tej analizie.

## Jak wydzielono obszary z głębi

To analiza tej konkretnej sceny z ziarnem i wzorcowym fragmentem powierzchni, nie automatyczne rozpoznawanie postaci/wody z samej mapy głębi:

- w centralnym obszarze wybrano ziarno postaci (u=0,48, v=0,55), wokół którego wydzielono spójny składnik zbliżonej głębi;
- wzorzec głębi podłoża dopasowano na pustym fragmencie chodnika i odjęto przy selekcji, aby stopy nie połączyły sylwetki z podłogą;
- uzyskany składnik ma bbox **[1103, 519, 1395, 1277)** i 91747 pikseli;
- górne 12% wysokości składnika (do y<609,96), sprawdzone na ilustracji, określa roboczą maskę głowy. Osobno sprawdzono szersze 15%, obejmujące część szyi;
- dla wskazanego fragmentu wody dopasowano zależność głębi od pozycji ekranowej. Tolerancja wokół tej powierzchni odrzuca barierkę, podłoże i niebo. Wybrano widoczną wodę do 64 pikseli od wnętrza głowy;
- maski są stałe i identyczne przy 2,25x i 6x. Wizualnie sprawdzono czerwony obszar głowy i niebieski fragment wody.

Po odsunięciu o 2 piksele od granic, aby ograniczyć mieszane piksele krawędzi, pomiar średnich obejmuje 6111 pikseli głowy i 18171 pikseli pobliskiej wody. Dokładne ROI, ziarno, współczynniki i tolerancje są w skrypcie/raporcie. Głębokość odwrócona jest zgodna z JSON. Nie przeliczano jej na metry.

## Co porównano

Signed edit w DDS jest już po mnożniku 6x. Dodatnie wartości podzielono przez 6, ujemne pozostawiono bez zmiany. Dla obu wariantów odtworzono zwykły nośnik:

`C(g) = FP16(clamp(0.5 + min(E,0) + g*max(E,0), 0, 65504))`.

Pomiary zachowują HDR powyżej 1. Nie stosują automatycznej ekspozycji, gamma ani normalizacji między wariantami. Jasność liczbowa Y = 0,2126R + 0,7152G + 0,0722B jest umowną projekcją koloru nośnika, a nie pomiarem fotometrycznym sceny. Nie uwzględnia działania prywatnej ekspozycji/modelu NGX. Dodatkowo policzono różnice osobno R/G/B, bez konwersji do Y.

## Średnie obszarów

| Wielkość | 2,25x | 6x |
|---|---:|---:|
| Średnia Y wnętrza głowy | 0,461544 | 0,476133 |
| Średnia Y pobliskiej wody | 0,554644 | 0,655393 |
| Bezwzględna różnica tych średnich | 0,093099 | 0,179260 |
| Kontrast Michelsona tych dwóch średnich | 9,1616% | 15,8423% |

Różnica średnich rośnie około **1,9255x**. Wnętrze głowy w tej reprezentacji rośnie słabiej niż pobliska woda. Zastąpienie tego pomiarem całej postaci mieszałoby fragmenty na tle chodnika i barierki z istotnym tu fragmentem na tle wody.

## Lokalna granica głowa–woda

Dla obrysu odsuniętego o 2 piksele do wnętrza głowy znaleziono najbliższy czysty piksel wody, maksymalnie 32 piksele dalej. Uzyskano **213 par**. Każdy piksel obrysu liczy się raz; piksel wody może powtórzyć się jako najbliższy dla kilku punktów. Nie są to niezależne obserwacje temporalne.

Liczone są `mean(abs(Yhead-Ywater))` oraz `mean(abs(Yhead-Ywater)/(Yhead+Ywater))`. Średniej kontrastu poszczególnych par nie utożsamiamy z kontrastem średnich całych obszarów.

| Lokalna miara | 2,25x | 6x | Iloraz |
|---|---:|---:|---:|
| Średnia bezwzględna różnica Y | **0,067048** | **0,165069** | **2,462x** |
| Średni kontrast Michelsona par | **6,1120%** | **12,7486%** | **2,086x** |
| Średnia bezwzględna różnica R/G | 0,062211 | 0,142913 | 2,297x |
| Średnia bezwzględna różnica R | 0,049401 | 0,101205 | 2,049x |
| Średnia bezwzględna różnica G | 0,075021 | 0,184621 | 2,461x |
| Średnia bezwzględna różnica B | 0,089791 | 0,233155 | 2,597x |

Na samym obrysie występuje jasny kontur editu, więc średnie par mogą mieć odwrotną kolejność jasności głowa/woda niż średnie wnętrza. Stosujemy wartości bezwzględne. To zmierzone zwiększenie separacji sygnału, nie potwierdzenie przyczyny ghostingu.

## Wrażliwość wyboru próbek

Dla par do 32 px, po odsunięciu od krawędzi:

| Odsunięcie | Liczba par | Różnica Y przy 2,25x | Przy 6x | Iloraz |
|---:|---:|---:|---:|---:|
| 2 px | 213 | 0,067048 | 0,165069 | 2,462x |
| 4 px | 209 | 0,054191 | 0,122127 | 2,254x |
| 8 px | 200 | 0,068589 | 0,139521 | 2,034x |

Szerszy górny fragment obejmujący kawałek szyi (15% zamiast 12%, 272 par) daje 0,067368→0,162790, czyli 2,416x. To wartość podana we wcześniejszym komentarzu roboczym; ostateczny podstawowy wynik 213 par ogranicza maskę ciaśniej do głowy. W każdym wariancie kierunek zmiany jest ten sam, lecz nie ma jednego uniwersalnego poziomu kontrastu niezależnego od maski.

## Kontrast samej wody

Ponieważ użytkownik wskazał ghosting na wodzie, porównano również sąsiednie piksele wyłącznie wewnątrz tej samej pobliskiej maski wody, bez par z głową:

- 35750 par poziomych/pionowych w odległości 1 px: średnia różnica Y **0,015571→0,036349**, czyli **2,334x**; średni Michelson **1,4330%→2,8722%**, czyli **2,004x**;
- pary oddalone o 4 px dają podobny wzrost różnicy Y (około 2,37x).

Mnożnik wzmacnia więc także teksturę samej wody. Z jednej statycznej klatki nie można przypisać całej obserwowanej poprawy ani wyłącznie separacji głowa–woda, ani wyłącznie wzmocnieniu fal. Nie ustalono progu przewidującego optimum 6x, a ten punkt nadal dotyczy tylko obserwacji użytkownika w tej scenie.

## Weryfikacja i odtwarzanie

Kontrole sprawdzają zgodność wymiarów, rozłączne maski, skończone dane, zachowanie wszystkich niedodatnich składowych editu oraz zgodność odtworzonego nośnika przy 6x z niezależnym `FP16(0.5+savedDDS)`. Sprawdzono dziewięć ustawień odsunięcia/maksymalnej odległości, wariant zakresu głowy, zakresy miar i niezmienione hashe źródeł. Nie mierzono GPU; jest to wyłącznie analiza CPU.

`python tools/analyze_nr_head_water_contrast.py <nr-edit.json> --output <report.json> --preview <local.png>` wymaga signed editu, depth i JSON oraz NumPy/SciPy (Pillow tylko dla podglądu). Podgląd przedstawia maski i oba warianty z tą samą skalą liczbową; wartości >1 są przycinane wyłącznie do PNG. Nie publikujemy pikseli źródłowych i nie zmieniamy DLL/HLSL/regulatora na podstawie tego pomiaru.

## Następny test: globalny gain z kontrastu granic

[Badanie celu kontrastu na granicach głębi](NR-edit-depth-boundary-target.md) wykorzystuje poziom R/G z głowy/wody przy 6× jako jawnie sceniczny cel. Automatyczne granice na całym ekranie, odsunięte próbki i ΔMV>2 dają około 6,17×; symulacja rzadkiej siatki po połączeniu 64 faz daje 6,18×. To kandydat pomiaru, nie niezależna kalibracja, implementacja w DLL ani gwarancja usunięcia smużenia.
