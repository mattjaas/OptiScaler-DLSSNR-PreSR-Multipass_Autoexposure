# Nowy transformer: 6x ogranicza ghosting, ale go nie usuwa

2026-10-31. Użytkownik przekazał komplet `nr-edit.dds`, depth, motion i JSON z gry Marvel's Spider-Man 2. Doprecyzował, że ghostingu prawdopodobnie nie da się tu całkiem usunąć: przy 6x jest mniejszy niż przy 2,25x, a dalsze zwiększanie już nie pomaga. **6x jest obserwowanym punktem malejących korzyści, nie etykietą „bez ghostingu” ani pierwszym dobrym progiem**. Nie dodano tej klatki do wcześniejszego binarnego dopasowania clear/bad.

[Wyniki liczbowe i hashe czterech źródeł](validation/nr-edit-transformer-plateau-audit.json). Oryginalnych obrazów nie publikujemy. Cała nowa analiza odbyła się na CPU, bez gry, uruchamiania modeli DLSS, testów GPU ani zmiany DLL.

## Czy źle odczytywany jest mnożnik lub czy opóźnia się EMA?

JSON: faktycznie użyte 6x, automat wyłączony, prywatny preset 11/K, IsHDR włączone, ekspozycja Native, edit mode, saturation 1x, depth colour off, siatka 2496×1404. Historia nie była resetowana, `held=false`. Podgląd zgłasza 2,25069904x, taki sam wynik raw i EMA, 1860 zaakceptowanych pomiarów, wiek ostatniego około 0,048 s. Użytkownik ustawił slow=5 s i fast=1,5 s. Pomiar ruchu jest aktywny.

Surowy edit przed mnożnikiem odtworzono przez podzielenie wyłącznie dodatnich kanałów DDS przez `positive_gain=6`; ujemne pozostawiono bez zmiany. Otrzymano:

- dokładny pełnoobrazowy próg S_RG: **2,250698805x**;
- produkcyjne koszyki histogramu na pełnym obrazie: **2,250699043x**;
- wszystkie 64 fazy siatki 256×144: **2,250699043x**, bez rozrzutu.

To nie jest błąd skali „6x w pliku”, sprzężenie pomiaru ze suwakiem ani zbyt wolna EMA. Tutaj nawet pełny obraz zamiast próbkowania daje ten sam zbyt niski w stosunku do obserwowanych korzyści cel. Zwiększenie próbek/zmiana EMA nie rozwiąże tego przypadku.

## Dlaczego obecny próg kończy się na 2,25x?

Obecny warunek liczy dodatnie składowe R i G przekraczające epsilon 0,0087917922 względem neutralnego nośnika, osobno normalizuje oba kanały i uśrednia odsetki. Cel transformera wynosi 97,2343313%. Nie mierzy odległości między obiektami, rozmieszczenia słabego sygnału ani obrazu po DLSS.

| Dodatni mnożnik | S_RG przed FP16 | S_RG po rekonstrukcji nośnika FP16 na CPU |
|---:|---:|---:|
| 2,250699 | 97,4497% | 91,7748% |
| 2,32 | 97,4497% | 97,4497% |
| 3 | 97,4671% | 97,4671% |
| 4 | 97,5434% | 97,5434% |
| 5 | 99,8902% | 99,8902% |
| 6 | 99,8944% | 99,8944% |
| 8 | 99,9070% | 99,9070% |

W dolnym ogonie dodatniego editu jest wyraźna kwantyzacja. Dużo próbek ma różnicę 1/256=0,00390625, kolejne mniejsze 1/512=0,001953125. Gdy mnożnik przekroczy około 2,250699, pierwsza grupa przeskakuje ponad epsilon i globalny cel jest już zaliczony. Pozostałe słabe edity są dla tego warunku za małym odsetkiem, choć ich dalsze wzmocnienie może poprawiać DLSS.

Od 6 do 8 S_RG rośnie tylko o około **0,0126 punktu procentowego**. To pasuje do małej dalszej zmiany tej statystyki, ale nie dowodzi przyczyny ustania poprawy ghostingu. Statystyka także bardzo słabo odróżnia 5x od 6x, więc **nie przewiduje dokładnie obserwowanego optimum**. Mocniejszy sygnał już ponad epsilon nadal zmienia amplitudę i kontrast, choć ta metryka przestaje go rozróżniać.

Dodatkowo po 6x w pasmie wokół 0,5 pozostaje 9,94% / 11,65% / 11,20% wszystkich składowych R/G/B. Z tego odpowiednio 9,92% / 11,56% / 11,18% pochodzi z zerowych lub ujemnych editów. Dodatni mnożnik nie odsuwa tych składowych od neutralnego poziomu. Nie twierdzimy, że są źródłem pozostałego ghostingu; pokazuje to tylko ograniczenie tego jednego regulatora.

### Dodatkowy problem na granicy FP16

Przy 2,250699x edity 1/256 dają przed zapisem nośnik około 0,50879179. Po FP16 to 0,5087890625, czyli odległość od 0,5 nadal **poniżej epsilon**. Następna wartość FP16 to 0,50927734375, a midpoint to 0,509033203125. Dla tych editów potrzeba nieco ponad 2,3125x (2,32x przechodzi), aby rzeczywisty nośnik przekroczył ten sam próg. To około 2,75% korekty względem 2,250699x, a nie 2,67-krotne podniesienie do 6x. Nie wyjaśnia głównego braku dopasowania. Nie wykonano odczytu rzeczywistego bufora FP16 ani NGX; to odtworzenie numeryczne z zapisu przed clippingiem i roundingiem.

## Globalny wynik ukrywa nierównomierność obrazu

Poniższe maski są diagnostyką tej samej klatki, **nie proponowaną segmentacją obiektów ani nową regułą dla automatu**. Ich S_RG liczymy przed FP16:

| Obszar | Część pikseli | S_RG przy 2,25x | S_RG przy 6x | Mnożnik dla obecnego celu tylko w tej masce |
|---|---:|---:|---:|---:|
| depth = 0 | 21,67% | 100% | 100% | 2,25x |
| depth > 0 | 78,33% | 96,54% | 99,86% | 4,50x |
| MV poniżej 1 px | 3,18% | 84,09% | 95,32% | 9,00x |
| MV 1..10 px | 35,32% | 94,75% | 99,85% | 4,50x |
| MV 10..20 px | 32,51% | 98,77% | 99,96% | 2,25x |
| MV co najmniej 20 px | 28,99% | 99,23% | 99,98% | 2,25x |

Wszystkie depth/MV są skończone, ich wymiary zgadzają się z editem. To długości wektorów w pikselach siatki prywatnego DLSS. Zera głębi nie są automatycznie etykietowane jako tło/interfejs, a mały MV nie oznacza automatycznie problematycznej postaci. Nie znamy granic semantycznych obiektów ani miejsc ghostingu z samej etykiety całej klatki. Widać jednak, że globalny wynik może być wysoki przy znacznie słabszym sygnale w części obrazu. Nie wdrażamy ignorowania zerowej głębi ani wymuszania maksimum po maskach: w tej klatce wymuszenie wolnego fragmentu dałoby 9x, powyżej obserwowanych korzyści przy 6x.

## Dlaczego nie podnieść po prostu progu lub zmienić wag?

Aby ten obraz osiągał cel dopiero w pobliżu 6x, można by podnieść cel do około 99,89435%. Przeliczono taki kontrfaktyczny cel na czterech starych pierwszych dobrych zapisach transformera:

| Stary zapis | Obecny cel: mnożnik względem zapisanego dobrego | Cel dopasowany do nowej sceny |
|---|---:|---:|
| game1-transformer-noghosting | 1,002x | 7,202x |
| game1-transformer-noghosting2 | 1,125x | 18,006x |
| game2-transformer-noghosting1 | 1,014x | 20,732x |
| game2-transformer-noghosting2 | 1,125x | 2,251x |

To mnożniki **dotychczasowych pierwszych dobrych wartości**, nie absolutne wartości suwaków. Uniwersalne podniesienie tego progu bardzo mocno nadmiernie wzmocniłoby stare sceny. Nie używamy nowej klatki z pozostającym ghostingiem jako nowej kalibracji „clear”.

Osobno R/G/B przy dotychczasowym celu dają odpowiednio 2,251x / 3,001x / 2,251x. Dowolna nieujemna średnia ich odsetków przy tym samym celu osiągnie go najpóźniej przy około 3,001x. Same wagi kanałów też nie pozwolą wybrać tutaj 6x. To kolejny argument za badaniem lokalnego rozkładu i kontrastu sygnału, a nie samym przestawianiem globalnego percentyla. Z jednego zapisu nie wyznaczono jeszcze takiego lekkiego, zweryfikowanego regulatora.

## Odtwarzanie i zakres zmiany

`tools/analyze_nr_edit_capture.py <capture.json> --observed-plateau-gain 6 --residual-ghosting --output <report.json>` wykonuje audyt na CPU. Wymaga trzech DDS obok JSON, potwierdza formaty i siatki, odtwarza edit przed mnożnikiem i porównuje pełny obraz, koszyki i 64 fazy. Opcjonalne powtórzone `--clear-reference <same-family-noghosting.dds>` odtwarza wyłącznie kontrfaktyczne porównanie ze starymi dobrymi zapisami. Raport zawiera metadane, statystyki i hashe, bez pikseli i ścieżek źródłowych. `--residual-ghosting` jest jawną etykietą użytkownika; bez niego obecność ghostingu pozostaje nieznana.

Potwierdzono liczbową zgodność wszystkich 64 faz i produkcyjnych koszyków z zapisanym UI (błąd poniżej 1e-6), zakresy odsetków oraz zachowanie etykiety residual ghosting bez przypisania ghost-free gain. Nie zmieniono HLSL, binariów ani progów automatu. W tej scenie obecny Auto kończy podnoszenie zbyt wcześnie w stosunku do zaobserwowanej poprawy przy 6x; modyfikacja wymaga nowej walidacji, a nie samego dopasowania progu do tej jednej klatki.


## Następny test: sąsiedztwo różnych pól ruchu

Na propozycję użytkownika sprawdzono [granice i przeplot wolniejszych/szybszych MV](NR-edit-motion-boundaries-study.md), z parą pikseli zamiast globalnej klasy MV<1 px. Przy niezmienionym celu sześć wariantów sąsiedztwa daje w tej scenie 4,5014x zamiast 2,2507x. Maska obrysowuje postać i barierkę; nie znamy jeszcze zgodności z miejscami ghostingu. Wąski przeplot jest zbyt skąpo reprezentowany na siatce 256×144. Jest to badanie CPU, bez nowej reguły runtime ani zmiany DLL.
