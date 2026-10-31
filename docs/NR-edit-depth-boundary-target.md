# Globalny mnożnik z kontrastu na granicach głębi

2026-10-31. Sprawdzono propozycję użytkownika: ustalać **jeden globalny mnożnik dodatniego editu**, tak żeby kontrast na granicach bliskiej/dalekiej powierzchni osiągał poziom zmierzony między głową a wodą przy 6×. To inne kryterium niż dotychczasowa liczba dodatnich pikseli oddalonych od 0,5.

**W tej scenie kandydat daje około 6,17×; rzadkie próbkowanie daje 6,18× po połączeniu momentów 64 faz.** To wynik dopasowania do celu pochodzącego z tej samej sceny, nie niezależna walidacja ghostingu ani uniwersalny próg. Użytkownik nadal obserwował ghosting przy 6×, choć słabszy niż przy 2,25×; powyżej 6× nie widział dalszej poprawy właśnie w tej scenie.

Badanie jest wyłącznie CPU. Nie zmieniono DLL, ustawień ani automatu; nie uruchamiano gry, GPU lub NGX. [Skrypt](../tools/analyze_nr_depth_boundary_target.py), [pełne statystyki i hashe](validation/nr-edit-depth-boundary-target.json), [poprzednia analiza głowy/wody](NR-edit-head-water-contrast.md).

## Cel i obliczenie mnożnika

Z poprzednich 213 par głowa–woda wyznaczono cel średniej bezwzględnej różnicy kanałów R/G: **0,1429128154**, czyli `(mean(abs(ΔR))+mean(abs(ΔG)))/2` na nośniku przy 6×. To numeryczny kontrast wejścia przed prywatną ekspozycją/NGX, nie fotometryczna jasność sceny. Nie mieszamy R/G w jeden podpisany kolor przed różnicą: zmiany kanałów mogłyby się wzajemnie znosić.

Dla każdego końca pary i kanału rozdzielamy **surowy, jeszcze niepomnożony edit**:

```
N = max(min(edit, 0), -0.5)
P = max(edit, 0)
A = abs(Na-Nb)
B = abs(Pa-Pb)
contrast(g) = A + g*B
```

Obie różnice mają ten sam znak w danym kanale, ponieważ min/max są monotoniczne. Dlatego wzór jest dokładny dla nieujemnego nośnika `0.5 + N + g*P`, dopóki nie ma górnego clippingu/FP16. Skrypt niezależnie porównuje go z bezpośrednim transportem obu końców dla dodatnich, ujemnych i mieszanych editów. W dostarczonym zapisie aż do 1024× nie występuje przekroczenie 65504; uwzględniono dolne ograniczenie do zera.

Dla średniej po parach i R/G:

```
recommended = max(1, (target - mean(A)) / mean(B))
```

Jeśli kontrast przy 1× już wystarcza, wynik to 1×. Zbyt mało par, brak kontrolowalnej części B lub cel nieosiągalny do 1024× oznacza **brak estymaty**, zamiast podnoszenia do maksimum. To nadal steruje wyłącznie dodatnim mnożnikiem; nic nie podnosi ujemnego editu ani neutralnego 0,5. Pomiar przed gain daje wartość bezwzględną, niezależną od aktualnego ręcznego/automatycznego mnożnika.

## Automatyczne granice bez maski postaci

Cel wyznaczono na ręcznie sprawdzonych parach głowa–woda. Następnie selekcja na całym ekranie **nie używa ziarna postaci, jej ROI ani rozpoznawania wody**:

1. Dwa sąsiednie depth wyznaczają potencjalny skok głębi. Używamy proximity: reversed Z = depth, zwykłe Z = 1-depth. Stosunek bliższej/dalszej proximity co najmniej 1,25 / 2 / 4; to parametry w przestrzeni projekcyjnej, nie deklarowane odległości fizyczne. NaN/Inf, 0 i wartości poza zakresem odpadają.
2. Próbki koloru odsuwamy od środka granicy: radius 1 oznacza dwa sąsiadujące piksele, radius 2 daje końce odległe o 3 px, radius 4 o 7 px, radius 8 o 15 px. Każdy koniec musi zachować głębię swojej strony z tolerancją 25%. Odrzuca to pary, które przeskoczyły przez cienki obiekt z powrotem na to samo tło.
3. Wymagamy ruchu co najmniej jednego końca >0,1 px. Osobno porównano brak filtra względnego ruchu z `length(MVa-MVb)>2 px`. Są to jednostki prywatnego wejścia.
4. Średnią liczymy wyłącznie po wybranych parach i kanałach R/G. Mnożnik nadal obowiązuje cały obraz.

To selekcja nieciągłości w obrazie, nie semantyczne rozpoznawanie obiektów ani potwierdzona maska disocclusion. Zmiana pozycji postaci względem ściany/wody może zmieniać zestaw granic; stosunek proximity nie jest odporną miarą odległości w metrach. Test ma 24 warianty ustalone z góry, nie wybiera wyłącznie jednego wyniku najbliższego 6×.

## Wyniki

Stosunek proximity ≥2, różnica MV >2 px, cel średniego kontrastu R/G = 0,1429128154:

| Radius | Liczba par | Kontrast przy 2,25× | Przy 6× | Proponowany gain |
|---:|---:|---:|---:|---:|
| 1 | 20704 | 0,0496644 | 0,1055096 | 8,5116× |
| 2 | 18467 | 0,0725513 | 0,1398311 | **6,1718×** |
| 4 | 16122 | 0,0683439 | 0,1341768 | 6,4976× |
| 8 | 14454 | 0,0635470 | 0,1322214 | 6,5838× |

Dla radius 2: `mean(A)=0,0321833253`, `mean(B)=0,0179413019`. Zatem `(0,1429128154-0,0321833253)/0,0179413019 = 6,1717645`.

Dla stosunków proximity 1,25 / 2 / 4 i radius 2..8, z filtrem ΔMV>2, średnia proponuje **6,1077..6,5838×**. Bez tego filtra ten sam zakres wariantów daje **6,6928..9,7102×**. Bez odsunięcia od granicy propozycje są wyższe (z ΔMV>2: 8,4520..8,8115×); piksele krawędzi mają inny kontrast niż wnętrza powierzchni. Nie należy uznawać arbitralnego radius za uniwersalną stałą.

Sprawdzono także odmienne agregacje, z osobnym celem każdej z nich wyznaczonym z głowy/wody przy 6×:

| Agregacja, radius 2 / ratio 2 / ΔMV>2 | Propozycja |
|---|---:|
| Średnia | 6,1718× |
| Mediana | 10,0000× |
| Średnia po odrzuceniu stałych 10% najniższych/najwyższych par | 7,6562× |

Mediana nie poprawia tu doboru. Średnia może być natomiast podatna na nieliczne silne edity; jej korzystny wynik w jednym obrazie nie stanowi dowodu przewagi we wszystkich scenach. Nie wybrano mediany lub progu z dawnego miernika pojedynczych pikseli jako celu nowej statystyki par.

### Czy wynik pochodzi tylko z głowy?

Wycięto diagnostyczny prostokąt wokół głowy i policzono to samo kryterium na reszcie ekranu. Dla radius 2: lokalny prostokąt daje 6,8777× (877 par), reszta **6,1381×** (17590 par). Dla radius 4: lokalnie 6,5096×, reszta 6,4971×. Dla radius 8 lokalny fragment daje już 10,0528× — rośnie wpływ doboru końców, mimo że cały ekran daje około 6,58×.

Bliskość wyniku całego ekranu do 6× nie dowodzi więc, że algorytm wykrył dokładnie problem głowy/wody. Mało widoczny obiekt może zostać zdominowany przez inne granice. Wymuszanie progu na każdej pojedynczej parze także nie jest rozwiązaniem: identycznych editów po obu stronach nie da się rozdzielić dodatnim mnożnikiem, a słaba sterowalność może prowadzić do bardzo dużych globalnych wartości.

## Rzadkie próbkowanie i koszt

Symulacja **rzeczywistej siatki 256×144 z 64 fazami 8×8**, na której testujemy sąsiedni skok depth i dopiero po jego akceptacji czytamy końce editu. Nie jest to koszt pełnoobrazowego wykrywania lub listy wszystkich krawędzi. Osobno w JSON zachowano diagnostyczne próbkowanie listy krawędzi; nie utożsamiamy go z tanim przebiegiem rastrowym.

| Radius | Pary na fazę min..max | Propozycje pojedynczych faz min..max | Gain ze średnich momentów 64 faz |
|---:|---:|---:|---:|
| 1 | 170..256 | 7,0738..10,6011× | 8,4800× |
| 2 | 145..228 | **5,0387..7,9679×** | **6,1815×** |
| 4 | 129..208 | 5,2711..7,8313× | 6,5382× |
| 8 | 119..185 | 5,1998..8,2844× | 6,4924× |

Wszystkie 64 fazy mają co najmniej 64 pary. Pojedyncze fazy są zbyt zmienne do bezpośredniego ustawiania gain. Sensowny eksperyment runtime powinien wygładzać **mean(A), mean(B)** przez EMA i dopiero z nich obliczać gain, z zachowaniem dotychczasowego gate ruchu/zmiany MV, ograniczenia częstości, histerezy i szybszego nadganiania po trwałej zmianie. Łączenie 64 faz nieruchomego zapisu nie symuluje EMA rzeczywistej ruchomej sceny. Nie deklarujemy stabilności temporalnej na podstawie tej tabeli.

Potrzebne są trzy sumy: A, B i liczba par, zamiast histogramu 1024 koszyków. Selekcja wymaga dodatkowych punktowych odczytów depth i MV; kolor tylko dla przyjętych granic. Można wykonywać ją w jednym rzadkim dispatchu, bez tekstury maski, bez renderowania dwóch mnożników i bez wielokrotnego DLSS. **Koszt GPU nie został zmierzony ani zaimplementowany**; nie podajemy teoretycznego ms. Na razie jest to narzędzie CPU i kandydat na odrębny eksperyment, nie zamiana obecnego automatu.

## Kwantyzacja i ograniczenia danych

Zaokrąglenie całej głębi na CPU do UNORM16/24 oraz minimalny skok 2 LSB daje dla ratio 2 / ΔMV>2:

| Radius | Oryginalne R32 | UNORM16 | UNORM24 |
|---:|---:|---:|---:|
| 2 | 6,171765× | 6,173295× | 6,171765× |
| 4 | 6,497625× | 6,498697× | 6,497625× |
| 8 | 6,583809× | 6,584954× | 6,583809× |

To syntetyczna kwantyzacja liczb na CPU, nie test widoków D16/D24 ani zachowania shaderów D3D12/NGX. Wcześniejsze 16 zapisów CNN/Transformer nie ma odpowiadających depth/MV, więc nie da się sprawdzić na nich tej selekcji ani dobrać osobnego progu CNN. Kontrast może również zależeć od prywatnej ekspozycji, presetu i innej treści sceny. Jeden cel z tej klatki nie daje gwarancji braku ghostingu lub optymalnej stabilności w innych grach.

Sprawdzono syntetycznie wzór A+gB, rozwiązanie dla znanego gain, brak sterowalności, poprawną stronę Z, warunki na sztucznej granicy 5/20 px, brak granic na stałej głębi oraz odrzucanie NaN. Hashe wszystkich źródeł odpowiadają poprzedniej analizie głowy/wody. Same źródła są mapowane tylko do odczytu. Powtórzenie:

```powershell
python tools/analyze_nr_depth_boundary_target.py '<capture>.json' --output '<report>.json'
```
