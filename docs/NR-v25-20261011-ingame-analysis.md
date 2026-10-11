# NR v25: wynik z gry, 11 października 2026, raport 110644

W dostarczonym przebiegu **Low36 jest najlepszym nowym wariantem**: względem lokalnych
kontroli Fast v21 skrócił cały przedział NR na GPU o 0,073472 / 0,142080 / 0,187648 ms
przy P50/P59/P65. Oba sąsiednie porównania ABBA były dodatnie przy każdej skali.
Użytkownik na pytanie o jakość v25, również w ruchu, odpowiedział:
**„Nie zauważyłem pogorszenia”**. To obserwacja dotycząca tego testu, a nie dowód
identycznego obrazu ani ocena wszystkich scen/HDR/ustawień.

Cross5 ma mniejszy dodatni wynik P59/P65. P50 jest mniej stabilne: jeden sąsiedni
zysk jest ujemny, a dryft kontroli większy od zysku symetrycznego.
Weights nie daje ogólnego przyspieszenia: prawie remis P50, mały dodatni wynik P59,
mała regresja P65. Żaden wniosek nie zmienia automatycznie kodu, ustawień domyślnych
ani polityki Optimized.

## Warunki i źródła

- Gra: The Last of Us Part I, według folderu raportów. Scena/kamera: `unspecified`.
- GPU: RTX 4090, DXGI driver 32.0.16.1656 (NVIDIA 616.56).
- Native 3840×2160; working P50 1920×1080, P59 2266×1274, P65 2496×1404.
- Transfer 9: Temporal DLAA residual + P100-guided; sześć efektywnych passów.
- Area, radius 1, guide 1, range sigma 0,015, spatial sigma 3,0.
- Shaping 2, shadow gate wyłączony. Dokładnych gainów i stylów poszczególnych passów
  oraz stanu HDR raport nie podaje; nie da się ich niezależnie odtworzyć z plików.
- Każde okno: 90 próbek rozgrzewki, 160 pomiarowych; manual hold wyłączony.
- 24 konfiguracje / 57 okien / 9120 próbek i 12 osobnych par ABBA.
- Mierzony jest **cały NR na GPU**, z NGX i zależnościami, nie sam inter-pass ani cała
  klatka gry. Zegarów/temperatur nie kontrolowano. Brak czasu absolutnego próbek.

Archiwalne kopie czterech dostarczonych plików są identyczne bajtowo ze źródłami:

| Plik w `docs/` | SHA256 |
|---|---|
| NR-v25-fast-experiments-20261011-110644.csv | 77e45b55c2d3be1c132feac9ffc5d8ed5385bf43f5a950d43428e9f9929396e6 |
| NR-v25-fast-experiments-20261011-110644.pairs.csv | 2038b96ba019a90ccdef459ac1423fbbd424012749070ef9dd1ce86b03ebbab6 |
| NR-v25-fast-experiments-20261011-110644.samples.csv | 9f99046fa3b74e75bb01f42bed4331b52d659bf9db94c22e065d2e66c21230da |
| NR-v25-fast-experiments-20261011-110644.txt | d2d9f8684ccbbec41c17c44aa765392466199d2f57f96bee64bcb569dd3b33e9 |

## Nowe warianty względem pobliskiej kontroli Fast v21

Główną miarą jest zysk symetryczny `(mediana A1 + mediana A2 − mediana B1 − mediana B2)/2`.
Nie używa się tu globalnej puli wszystkich okien Fast, ponieważ kolejne eksperymenty
odbywały się w innym czasie. Procent odnosi się do średniej dwóch median okien kontroli.

| Skala | Low36 zysk ABBA ms / % | Weights zysk ABBA ms / % | Cross5 zysk ABBA ms / % |
|---|---:|---:|---:|
| P50 | +0,073472 / +0,2815% | −0,001536 / −0,0059% | +0,032256 / +0,1234% |
| P59 | +0,142080 / +0,4332% | +0,024832 / +0,0756% | +0,072448 / +0,2207% |
| P65 | +0,187648 / +0,5123% | −0,013568 / −0,0370% | +0,103168 / +0,2818% |

Przed/po poniżej to mediany puli wyłącznie 320 próbek A i 320 próbek B danego bloku.
Różnica tych dwóch median może różnić się od zysku symetrycznego czterech okien.
SD jest populacyjne; P95 używa nearest rank, tak jak raport gry.

| Skala / wariant B | Fast A / wariant B mediana ms | SD A / B ms | P95 A / B ms | Zysk pierwszej / ostatniej pary ms |
|---|---:|---:|---:|---:|
| P50 Low36 | 26,102272 / 26,034176 | 0,187763 / 0,187214 | 26,449920 / 26,351616 | +0,055296 / +0,091648 |
| P50 Weights | 26,142720 / 26,142720 | 0,189605 / 0,210391 | 26,480640 / 26,491904 | +0,014848 / −0,017920 |
| P50 Cross5 | 26,140160 / 26,100224 | 0,184583 / 0,179767 | 26,460160 / 26,376192 | −0,004608 / +0,069120 |
| P59 Low36 | 32,810496 / 32,656896 | 0,149479 / 0,157578 | 33,022976 / 32,856064 | +0,132096 / +0,152064 |
| P59 Weights | 32,828416 / 32,809472 | 0,128209 / 0,129619 | 32,975872 / 33,011712 | +0,021504 / +0,028160 |
| P59 Cross5 | 32,822784 / 32,752128 | 0,122405 / 0,162034 | 32,982016 / 32,935936 | +0,061440 / +0,083456 |
| P65 Low36 | 36,625920 / 36,442624 | 0,180253 / 0,184314 | 36,940800 / 36,867072 | +0,182272 / +0,193024 |
| P65 Weights | 36,631040 / 36,651008 | 0,174593 / 0,208093 | 36,956160 / 37,100544 | −0,024064 / −0,003072 |
| P65 Cross5 | 36,612096 / 36,512768 | 0,176771 / 0,175862 | 36,940800 / 36,854784 | +0,113152 / +0,093184 |

Low36 ma także niższy P95 od swojej kontroli przy każdej skali. W P50 różnica
sąsiednich zysków 0,055296/0,091648 ms wskazuje większą niepewność wielkości zysku,
ale oba kierunki są zgodne. W P65 sąsiednie wyniki 0,182272/0,193024 ms są zbliżone.

Dla Cross5 P50 kontrola A zmieniła medianę o 0,057856 ms, przy zysku ABBA 0,032256 ms.
Nie traktujemy dodatniej średniej jako równie stabilnego wyniku co Low36 lub Cross5 P59/P65.
Dla Weights P59 obie sąsiednie pary są dodatnie, ale zysk mały, a P95 B wyższy o 0,035840 ms.
To nie wskazuje ogólnej przewagi Weights ani poprawy wszystkich części rozkładu czasów.

## Zachowany Fast v21 względem Optimized

| Skala | Optimized / Fast mediana ms | Zysk ABBA ms / % | SD Optimized / Fast ms | P95 Optimized / Fast ms |
|---|---:|---:|---:|---:|
| P50 | 26,352128 / 26,126336 | +0,226560 / +0,8597% | 0,190923 / 0,184605 | 26,735616 / 26,454016 |
| P59 | 33,444864 / 32,819200 | +0,620544 / +1,8557% | 0,220853 / 0,123095 | 33,757184 / 32,986112 |
| P65 | 37,292032 / 36,659200 | +0,631808 / +1,6942% | 0,165194 / 0,177703 | 37,622784 / 36,977664 |

Fast v21 pozostaje szybszy po obu stronach ABBA przy wszystkich trzech skalach.
Nie sumujemy zysków z różnych bloków jako nowego niezależnie kontrolowanego porównania
Low36 przeciw Optimized. Nie mnożymy zysków całego NR przez liczbę passów.

## Dlaczego wniosek P65 Low36 różni się od syntetycznego?

Wcześniejszy syntetyczny test wykazał regresję P65 Low36, również w powtórzeniu.
Tam badano stałe sinusoidy, gain high/low 1,13/0,71 i dwa shadery, bez NGX/DLAA/final resolve.
Obecne dane pochodzą z całego toru w grze z rzeczywistymi teksturami i historią NR.
Te warunki nie są równoważne. Dostarczony pomiar w grze jest właściwą podstawą
wyboru dla tego zestawu ustawień, ale nie wyjaśnia sam z siebie przyczyny różnicy.
Bez pomiarów etapów, liczników lub profilerów nie przypisujemy jej cache, occupancy,
konkretnemu dispatchowi ani interakcji NGX. Raport ścieżki Low36 nie podaje oddzielnie,
w których przejściach był potrzebny low field.

Low36 jest dobrym kandydatem do pozostawienia dla przetestowanej konfiguracji,
zwłaszcza P59/P65, przy zgłoszonym braku zauważalnego pogorszenia obrazu.
Cross5 daje mniejszy zysk względem swoich lokalnych kontroli Fast niż Low36.
Nie wykonano osobnego bezpośredniego ABBA Low36/Cross5.
Nie przesądzamy wyników dla innych skal, scen, GPU, gainów, HDR ani liczby passów.

## Kontrola spójności

Odtworzono wszystkie 24 wiersze summary z 9120 próbek: liczebności, średnie, mediany,
nearest-rank P95, min/max, SD, mediany i rozrzuty okien oraz wszystkie kolumny różnic.
Maksymalny błąd uzgodnienia `0,000004919812 ms` mieści się w zaokrągleniu do pięciu miejsc.
Wszystkie 12 wierszy `.pairs.csv` odtworzono z właściwych czterech okien:
mediany, zysk symetryczny, oba zyski sąsiednie i rozrzuty. Błąd poniżej `8e−15 ms`.
Indeksy 0–159 są kompletne w każdym oknie; kolejność P50/P59/P65 i wszystkich bloków
ABBA zgadza się z profilem. Potwierdzono sześć efektywnych passów, rzeczywiste wymiary,
mode 0–7, zachowane ścieżki Optimized oraz nazwy PSO eksperymentów, bez fallbacku.

To pojedynczy przebieg z dwoma oknami każdej strony danego bloku, nie 320 niezależnych
powtórzeń eksperymentu. SD pojedynczych klatek nie służy tu jako próg rozstrzygający
samą różnicę median. Nie wyliczano pozornie precyzyjnych przedziałów ufności ani p-value
z potencjalnie skorelowanych kolejnych klatek.
