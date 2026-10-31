# Edit NR a ghosting: analiza ośmiu zapisów

Analiza z 2026-10-31. **Znaleziono tani wskaźnik rozdzielający wszystkie przekazane przykłady. Nie znaleziono dowodu, że pojedynczy edit pozwala bezbłędnie przewidywać ghosting DLSS w dowolnej grze.** Wskaźnik nadaje się do eksperymentalnej regulacji siły sygnału, a próg wymaga walidacji na nowych sekwencjach w ruchu. W tej zmianie powstały analiza, narzędzie CPU i przetestowany prototyp GPU; kod renderowania, DLL i automatyczny mnożnik pozostają bez zmian.

## Dane i zakres wniosku

Użytkownik oznaczył cztery zapisy jako ghosting i cztery jako brak ghostingu. `game1` to Marvel's Spider-Man 2, 2240×1264; `game2` to The Last of Us Part I, 2496×1404. Obie pary na grę pokazują zbliżone sceny, z różnicami pozycji kamery/postaci i ustawień. Według użytkownika zmieniały się tylko Positive NR edit transport multiplier oraz Model input brightness. Ich wartości nie są znane; pozostałe opcje nie zmieniały się. Nie znamy z samych DDS konkretnego presetu/wersji DLSS, ekspozycji ani stanu pozostałych opcji.

Wszystkie osiem plików to poprawne, skończone RGBA FP32, z ciasno zapisanymi wierszami. Zachowano ich SHA-256 w raporcie liczbowym. Etykiety pochodzą od użytkownika, nie z oglądania obrazu po DLSS: takich obrazów ani sekwencji czasowych nie dostarczono. Oryginalne DDS i podglądy nie są publikowane w repozytorium.

Zgodnie z produkcyjnym trybem 43 zapis zawiera:

```text
d = NR - proxy
E = min(d, 0) + m × max(d, 0)
```

Jest to edit **po dodatnim mnożniku**, ale przed dodaniem 0,5, nasyceniem, kolorowaniem głębi i przycięciem. Neutralny zapis wynosi 0. Nie jest to końcowe wejście prywatnego DLSS, szczególnie przy włączonych kolorach lub trybie pełnej odpowiedzi NR.

## Najlepszy znaleziony prosty wskaźnik

Dla każdego piksela obliczamy:

```text
A = max(0, E.r, E.g, E.b)
Q = P90(A)                     # 90. percentyl po WSZYSTKICH pikselach
C = fraction(A > 0.14)         # pokrycie dodatnim sygnałem powyżej 0,14
```

Piksele neutralne oraz wyłącznie przyciemniane wchodzą do mianownika i mają A=0. Nie liczymy percentyla wyłącznie wśród dodatnich pikseli. Największa dodatnia składowa nie pozwala ujemnym kanałom anulować dodatniego sygnału, jak mogłaby to robić luminancja podpisanego editu.

| Plik (bez `.dds`) | Etykieta | Q: P90 dodatniej amplitudy | C: piksele powyżej 0,14 |
|---|---|---:|---:|
| game1-transformer-ghosting | ghosting | 0,084595 | 1,227% |
| game1-transformer-ghosting2 | ghosting | 0,111084 | 4,370% |
| game1-transformer-noghosting | brak | 0,399780 | 57,592% |
| game1-transformer-noghosting2 | brak | 0,153809 | 11,643% |
| game2-transformer-ghosting1 | ghosting | 0,085144 | 1,648% |
| game2-transformer-ghosting2 | ghosting | 0,121094 | 6,761% |
| game2-transformer-noghosting1 | brak | 0,258307 | 32,631% |
| game2-transformer-noghosting2 | brak | 0,167969 | 15,779% |

W dostarczonym zbiorze przypadki z ghostingiem mają Q≤0,121094, a bez ghostingu Q≥0,153809. Przykładowy próg Q≈0,14 rozdziela te przypadki. Równie tani test `C < 10%` wskazuje wszystkie cztery próbki z ghostingiem, a `C ≥ 10%` wszystkie cztery bez niego. Przy atomach rozkładu i dokładnych równościach percentyl oraz test pokrycia nie muszą być identyczne; w tych danych oba dają te same etykiety.

**Interpretacja:** koreluje poziom mocniejszych dodatnich zmian rozłożonych na istotnej części obrazu, a nie sama średnia jasność czy ustawienie mnożnika. Jest to zgodne z obserwacją użytkownika, że różne gry wymagają różnych mnożników: ten sam mnożnik wzmacnia różnie silny bazowy edit. Nie stanowi dowodu mechanizmu wewnątrz modelu NVIDIA ani istnienia w nim twardego progu 0,14.

## Porównanie kandydatów i sprawdzenie między grami

Próg każdego wskaźnika wyznaczano jako środek między największą wartością przy ghostingu a najmniejszą bez ghostingu w grze treningowej. Następnie sprawdzano drugą grę i odwracano role. Kierunek testu jest wspólny: mniejszy dodatni sygnał ma wskazywać większe ryzyko.

| Wskaźnik | Separacja wszystkich 8 przez jeden próg | Trafienia w dwóch próbach z odłożoną grą |
|---|---|---:|
| Średnia dodatnich kanałów | nie | 6/8 |
| RMS dodatnich kanałów | tak | 7/8 |
| P90 największego dodatniego kanału | tak | 8/8 |
| Pokrycie A>0,14 | tak | 8/8 |
| Średni lokalny gradient dodatniego editu | nie | 5/8 |
| Odsetek kanałów neutralnych po RN FP16(0,5+E) | nie | 3/8 |

P90: próg wyznaczony na game1 wynosi 0,132446 i poprawnie rozdziela game2. Próg wyznaczony na game2 wynosi 0,144531 i poprawnie rozdziela game1. Wariant z RMS nie przenosi się równie dobrze: próg z game2 mylnie zalicza `game1-transformer-noghosting2` do ghostingu.

To **analiza eksploracyjna po obejrzeniu etykiet**, a nie niezależny pomiar trafności na nowych danych. Wybór cechy P90 i progu 0,14 korzystał z tych samych ośmiu zapisów. Przesunięcia próbkowania oraz dwie gry nie tworzą setek nowych scen. Nie należy przedstawiać wyniku 8/8 jako deklaracji 100% skuteczności przyszłego automatu.

Hipoteza o prostym zanikaniu sygnału do neutralnego FP16 nie wyjaśnia całego zbioru: w `game2-transformer-ghosting2` do 0,5 zaokrągla się tylko około 0,000874% dodatnich kanałów, a w odpowiadającym mu `noghosting2` około 0,001015%. To diagnostyka RN z NumPy; nie jest testem każdego sposobu zapisu FP16 przez sterownik. Dane nie uzasadniają automatu opartego wyłącznie na liczbie utraconych zmian.

## Ile trzeba próbkować

Użyto punktowych próbek na równomiernej siatce, bez filtrowania i bez redukcji całego obrazu. Sprawdzono 8×8 faz wewnątrz komórki siatki; CPU i GPU mają identyczne całkowitoliczbowe współrzędne. Każda faza osobno daje jeden pomiar.

| Siatka | Odczyty na pomiar | Poprawne etykiety w 8 plikach × 64 fazach |
|---|---:|---:|
| 32×18 | 576 | 502/512 |
| 64×36 | 2304 | 512/512 |
| 128×72 | 9216 | 512/512 |

Dla 64×36 największe zmierzone C w pliku z ghostingiem wyniosło 7,5955%, a najmniejsze w pliku bez ghostingu 10,4167%. Siatka 32×18 myli się w `game1-transformer-noghosting2`, którego wynik leży najbliżej granicy. **64×36 jest najmniejszą z przetestowanych siatek zachowującą wszystkie etykiety we wszystkich fazach.** Nie oznacza to gwarancji dla innych obrazów ani dowodu, że każda siatka poniżej 2304 próbek będzie niewystarczająca.

Próbkowanie wciąż może ominąć cienki obiekt. Globalny P90 może także zostać zdominowany przez jasne tło, podczas gdy ghosting występuje na małej ciemnej postaci. Do wykrywania takiego przypadku potrzebne byłyby wyniki lokalne lub dane czasowe; te zapisy nie walidują takiego detektora.

## Prototyp GPU i koszt

Sprawdzono pięć rzeczywistych shaderów obliczeniowych, z pełną redukcją i wymaganymi barierami UAV. Prototypy nie są podłączone do ścieżki renderowania. Wszystkie korzystają z 2304 punktowych odczytów.

* `count64`, `count256`: pojedyncza grupa 64/256 wątków, lokalne liczniki i redukcja.
* `tiled64`: 36 grup po 64 wątki, potem redukcja 36 liczników. Dwa dispatche.
* `hist256`: pojedyncza grupa 256 wątków, histogram 256 przedziałów, równolegle licznik C. Jeden dispatch.
* `tiledhist64`: 36 histogramów lokalnych i osobna redukcja. Dwa dispatche.

Histogram używa bitów dodatniego FP32: 16 przedziałów na wykładnik, bez logarytmów, sortowania i obliczania luminancji. Zwraca dolną i górną granicę P90; szerokość względna zwykłego przedziału wynosi najwyżej 6,25%. Przedział zerowy obejmuje wartości poniżej 2^-12; ostatni jest przepełnieniem. Te skrajne przedziały nie pozwalają wyznaczyć precyzyjnej korekty mnożnika. `hist256` używa 2 KiB pamięci grupowej (histogram i liczniki). Gęste zliczenia obejmują zera i ujemne edity; nie odrzucamy ich z denominatora.

**Pomiar GPU:** RTX 4090, sterownik 616.56, D3D12, źródła to residentne rzeczywiste zapisy RGBA FP32. DXC 1.9.2602.17, cs_6_0, -O3. Osobna rozgrzewka po 64 powtórzenia dla A i B, 4 bloki × 16 kwartetów ABBA, 128 okien na wariant, 8 powtórzeń w oknie. A oznacza brak pomiaru; B to wskazany pomiar. Stemple czasu GPU obejmują dispatche i bariery UAV. Mediana A wynosi 0,000000 ms przy tej rozdzielczości znaczników; P90 A wynosi 0,000128 ms. To brak pracy w zakresie pomiaru, nie czas całej bazowej klatki.

| Rozmiar źródła | Metoda | A mediana ms | B mediana ms (P10–P90) | B−A ms |
|---|---|---:|---:|---:|
| 2240×1264 | count64 | 0 | 0,005600 (0,005376–0,005888) | +0,005600 |
| 2240×1264 | count256 | 0 | 0,004144 (0,003968–0,004352) | +0,004144 |
| 2240×1264 | tiled64 | 0 | 0,003122 (0,002944–0,003328) | +0,003122 |
| 2240×1264 | hist256 | 0 | 0,006054 (0,005888–0,006272) | +0,006054 |
| 2240×1264 | tiledhist64 | 0 | 0,006956 (0,006784–0,007168) | +0,006956 |
| 2496×1404 | count64 | 0 | 0,005624 (0,005376–0,005888) | +0,005624 |
| 2496×1404 | count256 | 0 | 0,004300 (0,004096–0,004480) | +0,004300 |
| 2496×1404 | tiled64 | 0 | 0,003116 (0,003034–0,003328) | +0,003116 |
| 2496×1404 | hist256 | 0 | 0,005988 (0,005760–0,006272) | +0,005988 |
| 2496×1404 | tiledhist64 | 0 | 0,006954 (0,006784–0,007168) | +0,006954 |

W tym pomiarze najtańszy jest `tiled64`, mimo dwóch dispatchy. Do bezpośredniego oszacowania mnożnika wybrałbym `hist256`: kosztuje około 0,006 ms, a dostarcza przedział percentyla zamiast samego werdyktu powyżej/poniżej progu. Rozproszony histogram przegrał; nie ma podstaw do używania go w runtime na tej karcie.

To pomiar izolowanego etapu na ciepłych danych, **bez** kosztu NR, prywatnego DLSS, odczytów dwóch żywych tekstur model/proxy, zastosowania mnożnika i sterowania jego stanem. Upload, alokacja, CPU, kopiowanie wyników i oczekiwanie na fence są poza oknami. Nie zmierzono całego toru gry. Podanych liczb nie należy przedstawiać jako potwierdzonego narzutu finalnej integracji ani oszczędności czasu klatki.

Niezależny test CPU używa scalarnych odczytów RGB i `nth_element`, a nie histogramu. Na RTX 4090 oraz WARP przeszło po **4160 przypadków**: 8 rzeczywistych obrazów i 5 syntetycznych × 64 fazy × 5 metod. Sprawdzane asercją są dokładna liczba przekroczeń i mianownik, a dla obu histogramów dodatkowo objęcie niezależnego P90 przez zwrócony przedział. Syntetyki obejmują zera, wartości ujemne, HDR/overflow, sub-2^-12, NaN/Inf i obraz mniejszy niż siatka. Jest to walidacja pomiaru, nie ghostingu modelu.

## Jak ten pomiar może sterować mnożnikiem

Znajomość historycznych suwaków nie jest potrzebna. Na żywo dostępne są bieżące NR, proxy i mnożnik. Dla dodatniego mnożnika zachodzi dokładna zależność w arytmetyce rzeczywistej:

```text
q = P90(max(0, NR.r-proxy.r, NR.g-proxy.g, NR.b-proxy.b))
Q_po_wzmocnieniu = m × q
m_docelowe = target / q
```

Jeśli pomiar wykonywany jest już po mnożniku, równoważnie `m_docelowe = m_bieżące × target / Q`. Zmiana Model input brightness automatycznie wpływa na nowy NR/proxy, a więc na q. Poziom `target≈0,15` jest **propozycją kalibracyjną z tego zbioru**, nie uniwersalnym minimum bez ghostingu. Przykładowo plik z Q≈0,085 wymagałby około 1,76× obecnego mnożnika, żeby osiągnąć taki poziom sygnału. Nie jest to potwierdzony wynik obrazu po tej korekcie.

Praktyczny wariant do sprawdzenia w integracji:

1. Mierzyć bazowy podpisany edit na siatce 64×36, przed mnożnikiem, kolorami i clippingiem. Nie tworzyć pełnej tekstury editu tylko dla pomiaru: czytać istniejące NR/proxy w poprawnych współrzędnych aktywnej siatki prywatnego wejścia.
2. Histogram na GPU wyznacza `[q_low, q_high)`. Dla zwykłego przedziału `target/q_low` konserwatywnie osiąga zakładany poziom sygnału i może go przestrzelić najwyżej o 6,25% z powodu kwantyzacji histogramu. To gwarancja poziomu tej statystyki, nie jakości DLSS.
3. Stosować histerezę oraz ograniczać szybkość zmiany mnożnika; zwiększać szybciej, zmniejszać wolniej. Przy braku mierzalnego dodatniego editu, pustym/niepoprawnym źródle i skrajnych przedziałach histogramu nie dzielić przez zero ani nie uruchamiać wzrostu do 1024×. Utrzymywać bezpieczny stan i sygnalizować brak podstaw do regulacji.
4. Ten sam zastosowany mnożnik musi być użyty przy przygotowaniu oraz odwróceniu editu w danej ewaluacji. Stan kontrolera powinien należeć do widoku, z jawnym resetem po zmianie sesji/urządzenia i poprawnym zachowaniem przy DRS. Nie resetować historii DLSS przy każdej małej korekcie regulatora.
5. Wynik i mnożnik przechowywać na GPU; bez oczekiwania CPU/GPU i odczytu w każdej klatce. Używać ponownie zasobów, a przy wyłączonym eksperymencie nie wykonywać dispatchy ani alokacji. Diagnostykę UI można odczytywać asynchronicznie i rzadko.

Próg trzeba sprawdzić osobno przy innych presetach/wersjach DLSS, ekspozycji, transporcie kolorów i trybie pełnej odpowiedzi NR. W tym ostatnim wejściem jest proxy+edit, więc poziom samego dodatniego editu nie opisuje całego obrazu widzianego przez DLSS. Pomiar nie zastępuje kontroli tych warunków. Ujemny clipping, zły depth/MV czy ghosting istniejący już w NR również nie muszą reagować na zwiększanie dodatniej części.

## Co jeszcze jest potrzebne do przewidywania ghostingu

Są możliwe dwa przebiegi z identycznym bieżącym editem, ale różną historią, wektorami ruchu, głębią lub ekspozycją. Mogą dawać różne wyniki czasowe. Każda funkcja wyłącznie tego pojedynczego editu zwróci dla nich tę samą prognozę. Dlatego nie da się uzasadnić obietnicy dokładnego, uniwersalnego detektora na podstawie samych tych danych. Dokumentacja NVIDIA opisuje DLSS jako korzystający z poprzednich klatek i wektorów ruchu oraz wymaga koloru, depth/MV i konfiguracji ekspozycji: [NVIDIA: temporal accumulation and motion vectors](https://www.nvidia.com/en-us/geforce/news/nvidia-image-scaler-dlss-rtx-november-2021-updates/), [NVIDIA Streamline DLSS integration](https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md).

Następną niezależną walidacją powinien być eksperyment w ruchu z kilkoma mnożnikami wokół przewidzianego poziomu. Rejestrować automatycznie ustawienia i wartości q/m, a następnie porównywać wyjście po DLSS, szczególnie ruchomą postać i odsłanianie tła. Potrzebne są przypadki przejścia ghosting→brak ghostingu i powrotu, po ustabilizowaniu historii, w nowych scenach. Jeden punkt „źle” i jeden „dobrze” nie określają najniższego skutecznego mnożnika. Nie trzeba odtwarzać z pamięci wartości obecnych ośmiu zapisów.

Najpierw należy potwierdzić, że target 0,15 rzeczywiście zapobiega obserwowanemu ghostingowi, zamiast tylko zapewniać odpowiednio duży edit. Dopiero taki wynik uzasadni opis automatu jako regulatora ghostingu. Obecnie poprawne określenie to **eksperymentalny regulator amplitudy dodatniego editu**.

## Odtwarzanie

Python 3.11+, NumPy (pomiar wykonano na 2.4.6):

```powershell
python tools/analyze_nr_edit_ghosting.py `
  'E:/Gry/Marvels SpiderMan 2/dlssnr-edit/game*-transformer-*.dds' `
  'F:/Gry/The Last of Us - Part I/dlssnr-edit/game*-transformer-*.dds' `
  --output artifacts/edit-analysis/study.json

$files = @(Get-ChildItem -LiteralPath `
  'E:/Gry/Marvels SpiderMan 2/dlssnr-edit', `
  'F:/Gry/The Last of Us - Part I/dlssnr-edit' `
  -Filter 'game*-transformer-*.dds' | Sort-Object FullName | ForEach-Object FullName)
./tools/test_nr_edit_signal_probe.ps1 -DdsPaths $files `
  -TimingCsv artifacts/edit-analysis/gpu-timing.csv
./tools/test_nr_edit_signal_probe.ps1 -Warp -DdsPaths $files
# Bez -DdsPaths uruchamiane są tylko niezależne syntetyki.
```

Raporty: [cechy i hashe](validation/nr-edit-ghosting-study.csv), [podsumowanie, próbkowanie i progi](validation/nr-edit-ghosting-study.json), [surowe okna GPU](validation/nr-edit-signal-probe-timing.csv). Pełne wyniki poszczególnych faz odtwarza skrypt analizy; w repozytorium są ich granice i liczby trafień. Shadery tej analizy są wyłącznie testowe, kompilowane skryptem powyżej; nie zmieniają produkcyjnych blobów ani ABI.
