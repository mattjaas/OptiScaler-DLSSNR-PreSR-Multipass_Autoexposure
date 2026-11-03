# Rodziny prywatnego DLSS i cykliczny reset przy niskim MV

Aktualizacja 2026-10-31. W **DLSS to temporally stabilize NR** dostępne są CNN, Transformer oraz **Transformer 2-gen (L/M)**. Ta ostatnia ma **Private Transformer 2-gen preset override**: Automatic M, L, M. Numery L=12 i M=13 są kontrolowane względem SDK. Wskazówki presetów dotyczą prywatnego SR/DLAA; sukces tworzenia NGX nie potwierdza faktycznie użytego wewnętrznego modelu. Jawny override nie przechodzi po błędzie na Default. Automatic zachowuje istniejący fallback. Zmiana rodziny/override odtwarza prywatną instancję przez dotychczasowy mechanizm bezpiecznego zwalniania GPU. Wybór jest zachowany podczas DRS, a latch instancji czyszczony podczas shutdown NR/NGX.

## Osobny cel kontrastu

**Depth-boundary contrast target** i **Exact boundary contrast target** zapisują wartość tylko dla wybranej rodziny: CNN (A/C/D/E), Transformer (J/K) lub Transformer 2-gen (L/M). Przełączenie przywraca jej własny cel. Nie ma nowych kalibracji jakości dla L/M: początkowa wartość to dotychczasowe 0,1429128154. Stary niestandardowy `SuperiorGainPreviewBoundaryTarget` jest fallbackiem dla każdej jeszcze nieustawionej rodziny. Ustawienie jednej nie nadpisuje pozostałych. Metoda coverage zachowuje dotychczasową klasyfikację CNN/Transformer; L/M używają dotychczasowej kalibracji Transformer bez potwierdzenia jakości w grze.

## Ciągłość historii podczas Auto

Automatyczne zmiany dodatniego mnożnika nie ustawiają Reset prywatnego NGX ani nie zerują historii meteringu ekspozycji. Dotyczy to również rozgrzewki i automatycznego minimum 1×. Ręczna zmiana, powrót do ręcznego mnożnika po wyłączeniu Auto i inne zmiany transportu zachowują właściwe resety. Pozostałe przyczyny resetu (np. zmiana instancji lub reset gry) nadal obowiązują. Nie przeskalowujemy nieudokumentowanej historii NGX. Test CPU obejmuje 200 kolejnych automatycznych zmian i oddzielnie reset przy manual/zmianie transformacji; jakość temporalna wymaga testu gry.

## Powtarzany reset przy małym ruchu

Opcja **Reset private DLSS after sustained low motion** jest domyślnie wyłączona i niezależna od Auto/preview. **Low-motion MV threshold (1080p reference)** ustala próg maksymalnej długości próbkowanego MV w pikselach odniesienia przy wysokości obrazu 1080p na klatkę (domyślnie 0,25); **Low-motion reset delay** czas (domyślnie 2 s). Dokładne pola pozwalają na 0,001..64 px oraz 0,05..60 s.

Gdy wszystkie 576 punktów stałej siatki 32×18 są poprawne i poniżej progu przez zadany czas, resetowana jest prywatna historia DLSS oraz meteringu ekspozycji. Licznik czasu od razu zaczyna kolejny okres. Przy ciągle małym ruchu reset powtarza się co zadany okres, z dokładnością częstotliwości próbkowania i opóźnienia odczytu. Nie wymaga wcześniejszego wzrostu MV. Ruch powyżej progu zeruje odliczanie. Hold, zewnętrzny reset, zmiana kontekstu, niepoprawne dane lub brak świeżego wyniku przez 0,5 s również przerywają okres. Wielokrotne odczytanie tego samego wyniku nie przyspiesza zegara. W UI widać maksymalne MV, bieżący czas i liczbę żądań resetu. NR oraz DLSS gry nie dostają tego resetu.

Jest to próbkowany warunek spokoju: drobny ruch może ominąć punkty siatki. Nie jest detektorem ghostingu ani filmików. Nie wymaga zmiany MV między klatkami; poprawny stały ruch nadal może przekraczać próg.

## Koszt i czas życia GPU

Detektor niskiego ruchu uruchamia małą niezależną sondę MV najwyżej 8 razy/s: jeden dispatch 256 wątków, 576 odczytów MV i wynik 2×1 RGBA32_UINT (32 B danych, readback z wyrównaniem D3D12). Sonda korzysta z asynchronicznego runtime/puli oraz odroczonego zwalniania; bez oczekiwania CPU na GPU. Czyta oryginalne MV gry, z uwzględnieniem subrect/origin, jednostek wektorów, render-resolution MV i docelowego rozmiaru obrazu. Nie wykonuje dodatkowego przeskalowania pełnego obrazu. Po rozgrzaniu używa puli; wyłączona opcja nie tworzy PSO ani zasobów sondy.

Pomiar syntetyczny, NVIDIA GeForce RTX 4090, D3D12, syntetyczne MV, Off vs osobna sonda, 16 par rozgrzewki, następnie 16 ABBA / 32 próbki na wariant, GPU timestamps obejmujące dispatch, bariery i kopię wyniku:

| Rozdzielczość | Off średnia ± SD ms | Sonda średnia ± SD ms | Różnica ms |
|---|---:|---:|---:|
| 2240×1264 | 0,000416 ± 0,000503 | 0,006441 ± 0,000281 | +0,006025 |
| 2496×1404 | 0,000672 ± 0,000486 | 0,006379 ± 0,000286 | +0,005707 |

[Surowe próbki](validation/private-low-motion-watch-timing.csv). To koszt pojedynczego pomiaru w osobnym harnessie, bez NGX i pozostałych etapów gry; nie dowodzi kosztu całego toru. Automat wygaszania i reset współdzielą pojedynczą sondę. Włączenie samego resetu korzysta również z tej sondy. Weryfikacja całego toru: ta sama scena, rozdzielczość i preset, rozgrzane ABBA Off/On oraz GPU timestamps NR+DLSS+composition. Nie uruchomiono testu NGX w grze dla tych zmian.

## Konfiguracja i weryfikacja

Nowe klucze `[DlssNr]` mają default, odczyt/zapis, apply/reset profilu i dokładnie jeden wpis `kKeptKeys`:

```ini
SuperiorScalingTransformer2PresetOverride=0
SuperiorGainPreviewBoundaryTargetCnn=0.1429128154
SuperiorGainPreviewBoundaryTargetTransformer=0.1429128154
SuperiorGainPreviewBoundaryTargetTransformer2=0.1429128154
SuperiorLowMotionReset=false
SuperiorLowMotionResetThreshold=0.25
SuperiorLowMotionResetSeconds=2
```

Rodzina `SuperiorScalingPreset`: 0=No DLSS, 1=CNN, 11=Transformer, 13=Transformer 2-gen. Override 0=Automatic M, 12=L, 13=M. JSON zapisuje aktywną rodzinę/cel, override 2-gen, konfigurację i stan low-motion oraz `automatic_gain_changes_reset_private_history=false`.

Walidacja lokalna: profile z aktualnym Config.cpp, serializacja JSON, polityki rodzin/presetów, niezależne cele i migracja, cykliczny timer (7 kolejnych żądań bez wzrostu MV, potem przerwanie ruchem i nowe odliczanie), przerwy/NaN/stare wyniki. Testy WARP i RTX 4090 obejmują produkcyjny asynchroniczny runtime, replay/reset command lists, ograniczoną pulę, zmianę programu, współdzielenie MV oraz 18 przypadków sondy z niezależną referencją dla origin/jednostek/NaN/Inf i serial 64-bit. Zachowano 336 przypadków referencji depth-boundary. Kompilacja/check 57 shaderów obejmuje nowy MotionWatch; zamrożony v59 oraz dotychczasowe shadery coverage/boundary nie zmieniły się. Build Release i regresje WARP uruchamia również workflow eksperymentu.

**No DLSS** zachowuje dopasowanie guides, korektę między przebiegami i końcową rekonstrukcję Superior, również na skompresowanej siatce. Dla rekonstrukcji kierowanej istniejący shader wylicza podpisany edit `odpowiedź NR − proxy` bezpośrednio na siatce roboczej z mnożnikiem 1×. Prywatny SR/DLAA oraz jego ustawienia transportu i pomiarów są wtedy pomijane. **Keep NR history when switching private DLSS** (`SuperiorKeepNrHistory`, domyślnie wyłączone) zachowuje historię modelu NR przy zmianie prywatnego presetu lub przejściu do/z No DLSS. Nowa instancja prywatnego DLSS zaczyna z własną świeżą historią. Zmiana rozdzielczości modelu NR i resety zgłaszane przez grę nadal obowiązują. Przy włączaniu lub zmianie presetu, podczas oczekiwania na utworzenie prywatnego DLSS Superior nakłada bieżący edit tym samym torem przestrzennym co No DLSS. Pierwsza gotowa ewaluacja używa wyniku nowego DLSS. Przy kompresji odczyt mapy warp w innej liście poleceń nadal wymaga zgłoszenia listy z jej inicjalizacją.

Na RTX 4090 zmierzono izolowany dispatch ekstrakcji editu do FP16 wraz z barierami, względem pustego okna timestamp. Po 64 próbach rozgrzewki na wariant zebrano 128 próbek na wariant w kolejności ABBA. Przy 2496×1404 koszt wyniósł 0,0289 ± 0,0005 ms wobec 0,0001 ± 0,0003 ms, przy 3840×2160 0,1038 ± 0,0287 ms wobec 0,0002 ± 0,0004 ms (średnia ± SD). Test syntetyczny nie obejmuje zmiany całego toru, w szczególności korekty między przebiegami i resolve. Pomiar można powtórzyć przez `private_exposure_quality.exe --extract-timing`. W grze potrzebny jest osobny pomiar NR i rekonstrukcji przy tych samych ustawieniach, po rozgrzewce i z tym samym ruchem kamery.

## Wygaszanie prywatnego DLSS i reakcja na niski ruch

**Private DLSS fade frames** ustala długość liniowego przejścia od editu po DLSS do bieżącego editu NR. Domyślnie to 6 ewaluacji NR, zakres 0..240. **Use time for private DLSS fades** przełącza na czas zegara monotonicznego. **Private DLSS fade duration** oraz **Exact private DLSS fade seconds** ustawiają 0..10 s, początkowo 0,15 s. Wartość 0 daje natychmiastowe przejście. Wygaszanie następuje przy zmianie rodziny/override presetu oraz wyborze No DLSS.

Podczas wygaszania poprzedni DLSS nadal przetwarza bieżące wejście. Mieszanie następuje na podpisanym edicie po odwróceniu transportu, przed końcowym frequency shaping i guided reconstruction. Korzysta z istniejącego decode, z dodatkowymi odczytami bieżącego proxy i odpowiedzi NR tylko przy wadze poniżej 100%. Nie dodaje dispatchu ani osobnej tekstury mieszania. Przy wadze 0 wynik jest bieżącym editem NR.

Po wygaszeniu instancja jest odraczana do bezpiecznego zwolnienia przez tracker GPU. Nowa instancja NGX powstaje dopiero po zwolnieniu poprzedniej. W oczekiwaniu działa tor No DLSS z bieżącym editem. Nowy preset zaczyna od świeżej historii i pełnego udziału, bez programowego fade-in. Zmiany geometrii, urządzenia lub flag tworzenia wymagające odtworzenia zasobów nie czekają na estetyczne wygaszenie. Opcja Keep NR history pozostaje niezależna.

**Private DLSS low-motion action** ma trzy wartości.

- **Off** pozostawia sterowanie ręczne.
- **Stop evaluations at low motion** po wygaszeniu wstrzymuje ewaluacje NGX, zachowując instancję i alokacje. Po powrocie ruchu pierwsza ewaluacja otrzymuje Reset przed zapisem wyniku. Dopiero jej nowy wynik trafia do decode i rekonstrukcji, z pełną wagą. Historia ekspozycji prywatnej instancji również zaczyna od nowa.
- **Keep DLSS running in background** zachowuje ewaluacje i temporalną historię przy wadze 0. Powrót ruchu płynnie zwiększa udział do 100%, z tą samą długością przejścia co wygaszanie.

Automat używa progu **Low-motion MV threshold (1080p reference)** i czasu **Low-motion reset delay**. Działają także przy wyłączonym checkboxie resetu historii. Używany jest ten sam kontroler czasu i shader sondy 576 MV co w low-motion reset, najwyżej 8 pomiarów/s, asynchronicznie. Przy aktywnym automacie wynik tej sondy jest również używany przez opcjonalny reset, co zapobiega drugiemu odczytowi sondy. Podczas wstrzymania NGX sonda nadal obserwuje oryginalne pole MV gry w jednostkach pikseli odniesienia 1080p na klatkę. Brak świeżych poprawnych MV przywraca DLSS. Dodatkowe widoki korzystają z ręcznego sterowania, automat zasila główny widok.

UI pokazuje **Private DLSS mix**, maksymalne próbkowane MV, czas niskiego ruchu oraz stan wstrzymania/pracy w tle. Czas powrotu po ruchu obejmuje częstotliwość sondy i opóźnienie asynchronicznego odczytu. Mały obiekt może ominąć punkty siatki. Włączenie osobnego automatycznego resetu historii pozostaje dodatkową przyczyną resetów również w trybie pracy w tle.

Nowe klucze mają pełny default/read/write/apply/reset profilu i występują raz w kKeptKeys.

```ini
SuperiorFadeByTime=false
SuperiorFadeFrames=6
SuperiorFadeSeconds=0.15
SuperiorMotionMode=0
```

SuperiorMotionMode ma wartości 0=Off, 1=Stop evaluations, 2=Background. Preferencje są zachowywane w INI/profilu, stany wygaszania należą do generacji i nie przetrwają shutdown NR/NGX.

Weryfikacja obejmuje CPU dla dokładnych końców przejścia, zegara czasu, niskiego ruchu, natychmiastowego powrotu po wstrzymaniu i płynnego powrotu w tle. Na WARP porównano blend z niezależną referencją dla obu sposobów transportu NR. Łącznie test ekspozycji/carrier obejmuje 1507 przypadków. Przy wadze 100% 48 przypadków ma identyczne bity jak poprzedni shader. Pozostałe shadery Superior, guide i interpass zachowują dotychczasowe bloby.

Pomiar GPU timestamp na RTX 4090, syntetyczne FP16 wejścia, 64 próby rozgrzewki na wariant i 128 próbek/variant w ABBA porównał encode+decode. Dla mieszania 50% względem 100% przy 1920×1080 mediana wyniosła 0,033792/0,033792 ms, przy 3840×2160 0,166400/0,201728 ms. Średnie ± SD to odpowiednio 0,381560 ± 2,500499 / 0,391160 ± 2,432035 ms oraz 0,968840 ± 3,568383 / 1,650344 ± 4,982462 ms. Tak duży rozrzut uniemożliwia wiarygodne określenie kosztu na podstawie tej sesji. [Surowy pomiar mieszania](validation/private-fade-timing.csv) oraz [porównanie starego i nowego shadera przy 100%](validation/private-fade-default-timing.csv) zachowują wszystkie próbki, także długie odstępstwa. Potrzebne jest rozgrzane ABBA całego toru w grze, osobno dla 100%, mieszania, pracy w tle i wstrzymania. Testy syntetyczne nie potwierdzają jakości przejść ani działania NGX w grze.

## Niezależność progu od rozdzielczości

Próg jest teraz proporcją wysokości końcowego obrazu, prezentowaną jako piksele przy 1080p. Dla danego punktu liczymy `MV_final = MV_game × game_scale × final_extent/reference_extent`, a następnie obie osie mnożymy przez `1080/final_height`. Referencja źródła to render subrect dla low-resolution MV albo output dla high-resolution MV. Przy brakującym rozmiarze referencyjnym używana jest poprawna wielkość aktywnego regionu. Brak końcowego rozmiaru obrazu lub niepoprawna skala daje nieważny wynik i przywraca aktywność DLSS.

Przykładowe 0,25 px@1080p odpowiada 0,25 końcowego piksela w 1080p, 0,3333 piksela w 1440p i 0,5 piksela w 4K. Stanowi zawsze około 0,02315% wysokości obrazu na klatkę. Próg pozostaje metryką na klatkę, więc FPS wpływa na odpowiadającą mu prędkość na sekundę. Skala NR 65%/100%, kształt packed grid i nierówne skale obu osi nie uczestniczą w tej konwersji.

Siatka 32×18 zawsze wybiera względne pozycje w aktywnym regionie oryginalnego bufora MV. Zapobiega to zależności od nieliniowego upakowania wektorów przy kompresji. Guides wykorzystywane przez NR i prywatny DLSS nadal powstają swoim dotychczasowym torem. Sonda służy tylko decyzji o ruchu. Coverage i depth-boundary używane do doboru mnożnika zachowują własne dotychczasowe jednostki i algorytmy.

Nazwy kluczy INI pozostają takie same, jednak stara liczba progu jest teraz interpretowana jako px@1080p. Może wymagać ponownego dostrojenia. JSON zawiera `low_motion_reset_units=pixels_at_1080p_height_per_frame`. Maksymalny wynik w UI ma oznaczenie px@1080p.

Test CPU obejmuje pięć rozdzielczości 720p..8K, render/output MV, wejściowe wektory UV i pikselowe, origins oraz brakujące/niepoprawne metadane. Sonda WARP porównuje ten sam ruch przy wyjściu 1080p i 4K oraz trzech różnych siatkach NR z niezależnym wynikiem liczbowym. Łącznie ma 24 przypadki origin/jednostek/poprawności. HLSL i bloby nie zmieniają się. W dotychczasowym wariancie współdzielenia z coverage sam reset mógł korzystać wyłącznie z istniejącego readbacku. W tej wersji reset i automat ruchu korzystają wspólnie z osobnej sondy dla spójności jednostek. Koszt pojedynczej sondy z wcześniejszego testu syntetycznego wynosił około 0,006 ms na RTX 4090 przy częstotliwości najwyżej 8 Hz; nie jest to nowy pomiar całego toru w grze.
