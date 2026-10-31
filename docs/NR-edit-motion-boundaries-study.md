# Sąsiedztwo wolnych i szybkich MV — postać na ruchomym tle

2026-10-31. Hipoteza użytkownika dotyczy sąsiadujących/przeplatających się pikseli o różnych wektorach ruchu: kamera śledzi postać, a otoczenie przesuwa się szybciej. To inny wybór niż wszystkie piksele z długością MV poniżej 1 px. Taką granicę może tworzyć także para 5 px i 20 px, gdy oba obszary mają niezerowy ruch.

Analiza dotyczy wyłącznie tego samego kompletu `nr-edit` ze Spider-Man 2, preset K, rzeczywisty mnożnik 6x. Użytkownik obserwuje tu poprawę do około 6x, z pozostającym ghostingiem; nie ustanawiamy limitu 6x dla innych scen ani etykiety clear. [Poprzedni audyt całego obrazu](NR-edit-transformer-plateau-audit.md). Nowe [wyniki liczbowe](validation/nr-edit-motion-boundaries.json) i [narzędzie CPU](../tools/analyze_nr_motion_boundaries.py). Nie zmieniono DLL, HLSL, progów ani automatu i nie wykonywano GPU/NGX.

## Definicja maski

Dla pary pikseli p,q na tej samej siatce wejścia prywatnego DLSS:

- różnica pełnych wektorów `|MV(p)-MV(q)| > 2 px` (osobno sprawdzono 4 px);
- szybszy wektor ma co najmniej 2 px, a wolniejszy najwyżej połowę jego długości;
- pary poziome i pionowe, odległość 1, 4 albo 8 pikseli; bez zawijania na krawędziach.

Obie strony takich par tworzą maskę granic. Osobno zliczono stronę wolniejszą i szybszą. Ten sam piksel może należeć do kilku par, ale histogram editu z maski liczy go raz. Warunki opierają się na kwadratach długości — do selekcji nie jest potrzebny sqrt. Głębia nie uczestniczy w wyborze. Progi 2/4 px są wariantami diagnostycznymi, nie wybranymi uniwersalnymi progami runtime; jednostki na klatkę zależą od rozdzielczości i ruchu/FPS.

Z jednej klatki MV wykrywamy **nieciągłość przestrzenną**, nie potwierdzone odsłonięcie powierzchni ani przyczynę ghostingu. NVIDIA wymienia odsłaniane obszary o dużych MV jako jeden z problematycznych przypadków śledzenia cech: [DLSS Programming Guide, 31 marca 2026, sekcja 3.15](https://raw.githubusercontent.com/NVIDIA/DLSS/main/doc/DLSS_Programming_Guide_Release.pdf). To kontekst dla hipotezy, a nie rekomendacja tej konkretnej maski pomiarowej przez NVIDIA. Nie używamy jej jako BiasCurrentColor; ta sekcja ogranicza obsługę tego parametru i nie rekomenduje go dla najnowszych modeli.

## Wyniki na przekazanej scenie

Dodatni edit przed mnożnikiem odzyskano przez podział dodatnich składowych DDS przez 6. Na wybranych pikselach ponownie liczono dokładnie dotychczasowe S_RG przy tym samym epsilon i celu 97,2343313%. Zmienia się wyłącznie obszar pomiaru.

| Odległość pary | Minimalna różnica MV | Część wszystkich pikseli | Wynik z całej maski |
|---:|---:|---:|---:|
| 1 px | 2 px | 0,4710% | 4,5014x |
| 1 px | 4 px | 0,4509% | 4,5014x |
| 4 px | 2 px | 1,8151% | 4,5014x |
| 4 px | 4 px | 1,7357% | 4,5014x |
| 8 px | 2 px | 3,4270% | 4,5014x |
| 8 px | 4 px | 3,2688% | 4,5014x |

Dla porównania cały obraz dawał 2,2507x. We wszystkich sześciu wariantach zarówno wolniejsza, jak i szybsza strona osobno dają 4,5014x. Nie dobierano progu, aby wymusić 6x. Wynik jest większy i bliższy wartości korzystnej według użytkownika w tej scenie, ale nie przewiduje dokładnie tego punktu.

Przy odległości 1 px i różnicy >2 px są 9862 pary oraz 16505 unikalnych pikseli maski. S_RG przy 2,250699x wynosi 95,0739% na obu stronach, 94,4047% na stronie wolniejszej i 95,6629% na szybszej. Przy 6x odpowiednio 99,7286% / 99,5781% / 99,8613%. Globalny obraz osiągał próg, mimo niższego wyniku na tych granicach.

Lokalny podgląd mapy MV i maski obrysowuje sylwetkę postaci w środku oraz elementy barierki. Obserwacja kształtu nie potwierdza lokalizacji ghostingu. Podgląd powstaje opcjonalnie przez `--preview`; żółty oznacza obie strony granicy, linie są poszerzone wyłącznie na ilustracji, a liczbowe maski zachowują oryginalną szerokość. Obrazy pochodne pozostają w lokalnych artefaktach, bez publikacji pikseli źródłowych.

## Przeplot w kilku kierunkach

Zliczono, ilu z czterech bezpośrednich sąsiadów spełnia warunek różnych pól ruchu. Prosta krawędź zwykle daje jednego takiego sąsiada; cienki element, narożnik lub przeplot może mieć kilku. Nie utożsamiamy tej liczby z klasą obiektu.

| Minimalna liczba różnych sąsiadów | Piksele | Część obrazu | Dodatnie próbki R/G w pełnej masce | Wynik |
|---:|---:|---:|---:|---:|
| 1 | 16505 | 0,4710% | 8519 / 8970 | 4,5014x |
| 2 | 3179 | 0,0907% | 1806 / 1781 | 4,5014x |
| 3 | 35 | 0,0010% | 16 / 12 | zbyt mało danych |
| 4 | 5 | 0,00014% | 3 / 2 | zbyt mało danych |

Maska co najmniej dwóch różnych sąsiadów nadal daje 4,5014x w pełnym obrazie. Dla 3/4 sąsiadów nie interpretujemy matematycznego kwantyla jako użytecznej sugestii: obie liczby dodatnich próbek są mniejsze od dotychczasowego minimum 64.

## Czy wystarczy lekki pomiar 256×144?

Przesunięto tę samą siatkę przez 64 fazy, bez losowania i bez wykonywania DLSS. Przykład delta>2:

| Odległość | Dodatnie próbki R/G na fazę | Zakres pojedynczych propozycji | Średnia z osobno normalizowanych rozkładów 64 faz |
|---:|---|---:|---:|
| 1 px | R: 70–106; G: 73–112 | 2,2507–4,5014x | 4,5014x |
| 4 px | R: 304–370; G: 340–399 | 2,2507–4,5014x | 4,5014x |
| 8 px | R: 586–653; G: 647–724 | 2,2507–4,5014x | 4,5014x |

Każda faza podstawowej maski ma >=64 dodatnich R i G. Węższy przeplot >=2 sąsiadów ma tylko 9–26 R i 10–27 G na fazę; **żadna faza nie spełnia minimum**. Nie można po prostu podłączyć tak wąskiej maski do dotychczasowego miernika i zakładać identycznej stabilności. Uśrednienie 64 statycznych rozkładów pokazuje potencjał akumulacji, ale nie odtwarza rzeczywistej EMA poruszającej się sceny.

Punktowy warunek może wymagać czterech dodatkowych sąsiednich odczytów MV na próbkę, bez pełnoobrazowej maski i drugiego dispatchu. Selekcja przed odczytem editu pozwala pomijać kolor dla niewybranych próbek. Jest to rozważanie kosztu operacji, **nie pomiar ms ani potwierdzenie implementacji GPU**. Odległość 4/8 zwiększa ilość danych w tej klatce, lecz zmienia pasmo analizowanego obszaru. Nie wybrano jej jeszcze na podstawie testów jakości ani GPU.

## Kontrast po obu stronach

Osobno zapisano różnicę editu pomiędzy końcami par, po dodatnim mnożeniu, per kanał. Przy braku górnego clippingu:

`abs(Tg(Ea)-Tg(Eb)) = abs(negativePart(Ea)-negativePart(Eb)) + g * abs(positivePart(Ea)-positivePart(Eb))`.

Część ujemna nośnika jest ograniczona od dołu do -0,5. W tym pliku nie osiąga tego ograniczenia. Dla par, których kontrast da się zmienić dodatnim mnożnikiem, przy radius=1 odsetek R/G przekraczający epsilon wynosi 89,5848% przy 2,250699x oraz 99,1373% przy 6x. To odrębna statystyka, nie przeszkolono dla niej celu „bez ghostingu”; nie podstawiamy bez walidacji starego celu z rozkładu pojedynczych pikseli. Kontrole porównują wzór z niezależnym transportem obu końców, zachowując ujemny edit bez mnożenia.

## Weryfikacja i ograniczenia

Narzędzie CPU sprawdza syntetycznie: stały duży MV i płynny gradient nie tworzą fałszywej granicy; dwa pola 5/20 px wyznaczają dokładnie dwie kolumny granicy; szachownica 5/20 daje cztery różnych sąsiadów we wnętrzu. Potwierdzono zgodność siatek, skończoność MV, hashe i brak nadpisania źródeł (memmap tylko read). Zbadano sześć z góry opisanych wariantów bez wybierania wyłącznie najbliższego 6x.

W folderach starych 16 zapisów nie ma odpowiadających im plików `game*-*.motion.dds`, więc nie zweryfikowano tej selekcji na ich etykietach ani osobno na CNN. Nie ma nowego potwierdzonego regulatora. Różnica MV jest sensownym kandydatem na wybór obszaru pomiaru, nie dowodem ghostingu; 6x pozostaje wyłącznie obserwacją tej sceny. Progów i DLL nie zmieniono.

Odtworzenie: `python tools/analyze_nr_motion_boundaries.py <nr-edit.json> --observed-plateau-gain 6 --output <report.json> --preview <local.png>`. NumPy jest wymagane, Pillow tylko do opcjonalnej ilustracji. Nie jest potrzebne GPU.


## Fragment głowy na tle wody

Po doprecyzowaniu użytkownika, że ghosting występował na wodzie, sprawdzono [kontrast głowy i pobliskiej wody](NR-edit-head-water-contrast.md) po wydzieleniu masek z głębi i wizualnie wskazanych obszarów. Różnica średnich Y rośnie 0,0931→0,1793. Lokalna różnica przy obrysie głowy rośnie 0,0670→0,1651 (2,46x), a kontrast samej wody także rośnie (około 2,33x dla różnicy sąsiadów). To osobna analiza wejścia, bez dowodu przyczyny ghostingu lub nowego progu automatu.
