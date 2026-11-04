# Rodziny prywatnego DLSS i cykliczny reset przy niskim MV

Aktualizacja 2026-11-04: cztery odrzucone transformacje transportu zostały wycofane. Dawne wartości INI/profili są ignorowane; szczegóły i bieżąca weryfikacja: [sekcja wycofania](#wycofanie-odrzuconych-transformacji-transportu). Starsze opisy i pomiary dokumentują wskazane rewizje historyczne.


Aktualizacja 2026-10-31. W **DLSS to temporally stabilize NR** dostępne są CNN, Transformer oraz **Transformer 2-gen (L/M)**. Ta ostatnia ma **Private Transformer 2-gen preset override**: Automatic M, L, M. Numery L=12 i M=13 są kontrolowane względem SDK. Wskazówki presetów dotyczą prywatnego SR/DLAA; sukces tworzenia NGX nie potwierdza faktycznie użytego wewnętrznego modelu. Jawny override nie przechodzi po błędzie na Default. Automatic zachowuje istniejący fallback. Zmiana rodziny/override odtwarza prywatną instancję przez dotychczasowy mechanizm bezpiecznego zwalniania GPU. Wybór jest zachowany podczas DRS, a latch instancji czyszczony podczas shutdown NR/NGX.

## Przejście z Finished picture na After SR

W logu Spider-Man 2 z sesji 2026-11-03 przejścia o 23:55:09 i 23:56:35 zatrzymywały prywatny DLSS na `Finishing old private DLSS GPU work; using current NR edit.` Powrót do Finished picture o 23:56:32 umożliwiał ponowne tworzenie instancji. NR nadal pracował w After SR, ale używał rekonstrukcji bieżącego editu bez prywatnego DLSS.

Przyczyną były własne listy poleceń Finished picture pozostawione przy życiu po anulowaniu trybu. Tracker poprawnie zatrzymywał zasoby i NGX tak długo, jak zapis mógł zostać ponownie wykonany; samo zakończenie GPU nie zamyka takiego zapisu. Powrót do Finished picture resetował te listy, dlatego odblokowywał zwolnienie. `Cancel` zwalnia teraz własne, anulowane listy po sprawdzeniu istniejącego fence. Lista w toku lub oczekująca na zgłoszenie pozostaje chroniona. Fence i allocator są zachowane do ponownego użycia, a powrót do Finished picture odtwarza tylko brakującą listę. Listy należące do gry zachowują dotychczasowe zasady obsługi replay.

Zmiana nie dodaje dispatchy, kopii, sygnałów ani oczekiwania CPU/GPU. Sprawdzanie fence trwa tylko do zwolnienia listy. Test WARP korzystający z produkcyjnego GpuLifetime i tego samego helpera zwalniania sprawdza dwukrotne przejście, wymuszone polecenia GPU w toku, brak przedwczesnego zwolnienia, blokowanie przez ukończony zapis możliwy do replay oraz zwolnienie starej generacji po anulowaniu. Nie tworzy instancji NGX; uruchomienie prywatnego DLSS po przełączeniu wymaga potwierdzenia w grze.

## Osobny cel kontrastu

**Depth-boundary contrast target** i **Exact boundary contrast target** zapisują wartość tylko dla wybranej rodziny: CNN (A/C/D/E), Transformer (J/K) lub Transformer 2-gen (L/M). Przełączenie przywraca jej własny cel. Nie ma nowych kalibracji jakości dla L/M: początkowa wartość to dotychczasowe 0,1429128154. Stary niestandardowy `SuperiorGainPreviewBoundaryTarget` jest fallbackiem dla każdej jeszcze nieustawionej rodziny. Ustawienie jednej nie nadpisuje pozostałych. Metoda coverage zachowuje dotychczasową klasyfikację CNN/Transformer; L/M używają dotychczasowej kalibracji Transformer bez potwierdzenia jakości w grze.

## Ciągłość historii podczas Auto

Automatyczne zmiany dodatniego mnożnika nie ustawiają Reset prywatnego NGX ani nie zerują historii meteringu ekspozycji. Dotyczy to również rozgrzewki i automatycznego minimum 1×. Ręczna zmiana, powrót do ręcznego mnożnika po wyłączeniu Auto i inne zmiany transportu zachowują właściwe resety. Pozostałe przyczyny resetu (np. zmiana instancji lub reset gry) nadal obowiązują. Nie przeskalowujemy nieudokumentowanej historii NGX. Test CPU obejmuje 200 kolejnych automatycznych zmian i oddzielnie reset przy manual/zmianie transformacji; jakość temporalna wymaga testu gry.

## Powtarzany reset przy małym ruchu

Opcja **Reset private DLSS after sustained low motion** jest domyślnie wyłączona i niezależna od Auto/preview. **Low-motion MV threshold (1080p reference)** ustala próg długości pojedynczego próbkowanego MV w pikselach odniesienia przy wysokości obrazu 1080p na klatkę (domyślnie 0,25); **Low-motion reset delay** czas (domyślnie 2 s). Dokładne pola pozwalają na 0,001..64 px oraz 0,05..60 s.

Gdy wszystkie 576 punktów stałej siatki 32×18 są poprawne, ale mniej niż wymagany odsetek przekracza próg przez zadany czas, resetowana jest prywatna historia DLSS oraz meteringu ekspozycji. Licznik czasu od razu zaczyna kolejny okres. Przy ciągle małym ruchu reset powtarza się co zadany okres, z dokładnością częstotliwości próbkowania i opóźnienia odczytu. Nie wymaga wcześniejszego wzrostu MV. Ruch powyżej progu zeruje odliczanie. Hold, zewnętrzny reset, zmiana kontekstu, niepoprawne dane lub brak świeżego wyniku przez 0,5 s również przerywają okres. Wielokrotne odczytanie tego samego wyniku nie przyspiesza zegara. W UI widać odsetek ruchomych próbek, bieżący czas i liczbę żądań resetu. NR oraz DLSS gry nie dostają tego resetu.

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

UI pokazuje **Private DLSS mix**, odsetek ruchomych próbek, czas niskiego ruchu oraz stan wstrzymania/pracy w tle. Czas powrotu po ruchu obejmuje częstotliwość sondy i opóźnienie asynchronicznego odczytu. Mały obiekt może ominąć punkty siatki. Włączenie osobnego automatycznego resetu historii pozostaje dodatkową przyczyną resetów również w trybie pracy w tle.

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

Test CPU obejmuje pięć rozdzielczości 720p..8K, render/output MV, wejściowe wektory UV i pikselowe, origins oraz brakujące/niepoprawne metadane. Sonda WARP porównuje ten sam ruch przy wyjściu 1080p i 4K oraz trzech różnych siatkach NR z niezależnym wynikiem liczbowym. Łącznie ma 24 przypadki origin/jednostek/poprawności. Ta część zmiany działa po stronie hosta; nowy licznik w shaderze sondy opisano poniżej. W dotychczasowym wariancie współdzielenia z coverage sam reset mógł korzystać wyłącznie z istniejącego readbacku. W tej wersji reset i automat ruchu korzystają wspólnie z osobnej sondy dla spójności jednostek. Koszt pojedynczej sondy z wcześniejszego testu syntetycznego wynosił około 0,006 ms na RTX 4090 przy częstotliwości najwyżej 8 Hz; nie jest to nowy pomiar całego toru w grze.

## CNN E utrzymywany jako druga instancja

**Use resident CNN E at low motion** jest osobną, domyślnie wyłączoną opcją dla **Stop evaluations at low motion**. Wymaga CNN A/C/D jako głównego presetu. Dla Transformerów, No DLSS, głównego E oraz trybu Background kontrolka jest wyszarzona. Przy Automatic A/E liczy się faktyczny zatrzaśnięty wybór A albo E.

Po włączeniu powstaje druga instancja NGX z osobną mapą parametrów i presetem E. Współdzieli z głównym presetem bufory carrier, wynik, depth/MV i ekspozycję zarządzane przez OptiScaler. Każda instancja NGX ma własne wewnętrzne zasoby i historię. E czeka na potwierdzenie zgłoszenia poleceń utworzenia, zanim może zostać wybrane. Jeśli NGX odrzuci utworzenie E lub zwróci alias głównego uchwytu, główny preset kontynuuje pracę. UI pokazuje ten błąd. Wyłączenie i ponowne włączenie opcji ponawia próbę.

Po utrzymaniu niskiego ruchu przez ustawiony czas główny preset wygasa do bieżącego editu NR. Następnie E dostaje Reset przed pierwszą ewaluacją i przejmuje wynik z pełną wagą. Powrót ruchu wygasza E i w ten sam sposób wybiera główny preset. Nieaktywna instancja pozostaje zaalokowana i pomija ewaluacje. Renderowana klatka ma najwyżej jedną ewaluację prywatnego DLSS. Zmiana kierunku podczas wygaszania płynnie przywraca aktualny preset, bez zmiany aktywnej instancji.

Włączenie opcji może spowodować jednorazowe przycięcie przy tworzeniu E. Pierwsze użycie gotowej instancji może również wymagać pracy wewnętrznego runtime. Obsługi dwóch instancji i kosztu przełączenia w grze jeszcze nie zmierzono. Sukces CreateFeature nie potwierdza, że zamknięty runtime respektuje hint E.

Przy wyłączeniu opcji, zmianie trybu lub wyborze niezgodnego głównego presetu E jest zwalniane przez tracker po zakończeniu używających go zapisów GPU. Dla E ustawiamy udokumentowane FreeMemOnReleaseFeature=1, żeby ograniczyć zatrzymywanie jego alokacji w wewnętrznym cache NGX. Zwolnienie może być odroczone, a pozostały cache oraz inne instancje gry nadal wpływają na całkowite zużycie VRAM. UI pokazuje liczbę aktualnie zaalokowanych uchwytów oraz numer wybranego presetu; fizyczne zużycie należy porównać w tej samej scenie po rozgrzewce i zakończeniu odroczonych zwolnień. Nie podano niezmierzonej liczby MB.

Preferencja `SuperiorMotionCnnIdleE=false` ma pełny obieg INI i profili, z pojedynczym wpisem w kKeptKeys. Preferencja pozostaje zapisana również po wyszarzeniu kontrolki, ale druga instancja powstaje tylko w zgodnym trybie i dla zgodnego głównego presetu. Obie instancje są czyszczone przy NR/NGX shutdown. JSON zapisuje `resident_cnn_e_enabled`, `active_private_preset` i `resident_private_instances`.

Testy CPU sprawdzają eligibility głównego A/C/D, wykluczenie E i Transformerów, opt-in, tryb Off/Background, trzyklatkowe wygaszenie w obie strony, stabilny wybór przez 120 klatek oraz anulowanie przejścia. Testy JSON i profili obejmują nowe pola. Ta część zmiany działa po stronie hosta; nowy licznik w shaderze sondy opisano poniżej. Dwa uchwyty NGX oraz ich pamięć i zachowanie wymagają weryfikacji w grze; testy WARP nie tworzą instancji NVIDIA DLSS.

## Ruch kamery szacowany odsetkiem próbek

Decyzja używa teraz **Moving samples required for camera motion**, domyślnie 75%, zakres 1..100%. Najpierw każda z 576 próbek jest przeliczana do px@1080p. Sonda zlicza próbki o długości ściśle większej od progu MV. Przy co najmniej wymaganym odsetku klasyfikuje ruch jako aktywny. Dokładne 75% to 432 próbki. 431 pozostaje poniżej wymaganej liczby. Jedna szybko poruszająca się postać nie wystarczy, jeśli nie obejmuje wymaganej części siatki.

Ten odsetek jest przybliżeniem ruchu kamery. Samo MV nie rozstrzyga przyczyny ruchu. Duży ruchomy obiekt albo animowane tło może przekroczyć 75%, a ruch kamery obejmujący mniejszą część obrazu może pozostać poniżej. Suwak pozwala dostroić kompromis. Kamera obracająca się lub przemieszczająca do przodu może zostać wykryta bez wymogu wspólnego kierunku wektorów.

Ten sam warunek zasila automat No DLSS/Background/CNN E i opcjonalny okresowy reset. Czas niskiego ruchu nadal działa przed wygaszeniem; wzrost odsetka do wymaganego progu przerywa odliczanie. Każda próbka musi być poprawna, w przeciwnym razie wynik jest nieważny i automat przywraca aktywność.

Klucz `SuperiorLowMotionMinimumPercent=75` ma default, read/write, apply/reset profilu i pojedynczy wpis kKeptKeys. JSON zawiera `camera_motion_required_sample_percent` i `moving_sample_percent`. Oryginalny maksymalny MV nadal jest zapisywany diagnostycznie, ale nie rozstrzyga już decyzji.

Wynik pozostaje 32-bajtowy, a rozmiar siatki i częstotliwość sondy są te same. Liczniki poprawnych i ruchomych próbek są pakowane do jednego słowa podczas istniejącej redukcji. Dzięki ograniczeniu do 576 elementów pola 16-bitowe nie przepełniają się.

WARP sprawdza 29 przypadków sondy, w tym 0/1/431/432/576 ruchomych punktów, granicę 75%, rozdzielczości 1080p/4K i różne siatki NR. Kontroler CPU sprawdza także timer z jednym ruchomym punktem i przerwanie przy 75%. Na RTX 4090 pomiar starej sondy vs nowa przy syntetycznym polu MV, 16 par rozgrzewki i 16 ABBA / 32 próbki na wariant, wyniósł 0,007539 ± 0,000484 vs 0,007449 ± 0,000564 ms przy 2240×1264 oraz 0,007367 ± 0,000579 vs 0,007372 ± 0,000631 ms przy 2496×1404. Różnice -0,000090 i +0,000005 ms są mniejsze od rozrzutu. Nie jest to pomiar całego toru gry. [Surowe próbki starej i nowej sondy](validation/camera-motion-coverage-timing.csv).


## Eksperymentalne lokalne odrzucanie ghostingu: metoda legacy 3×3

Poniższy opis dotyczy **Ghost rejection method = Local range 3x3 (legacy)**. Nowe metody opisano na końcu dokumentu; samo odrzucanie pozostaje domyślnie wyłączone.

**Experimental DLSS ghost rejection** jest domyślnie wyłączone. Działa w D3D12 z prywatnym CNN, Transformer i 2-gen, zarówno na edicie, jak i pełnej odpowiedzi NR. Przy No DLSS i Vulkanie kontrolka jest wyszarzona. **Ghost rejection threshold** (default 0.01, zakres 0..0.25), **Ghost rejection softness** (0.02, 0.0001..0.25) oraz **Ghost rejection strength** (1, 0..1) sterują maską. Niższy próg odrzuca więcej różnic, większa miękkość rozciąga przejście, a siła 1 w pełni zastępuje odrzucone piksele bieżącym editem NR. Siła 0 wybiera zwykły shader.

W istniejącym decode najpierw cofane są mnożnik dodatniego editu, nasycenie, kolor głębi oraz rozjaśnianie według głębi. Otrzymany podpisany edit DLSS porównujemy osobno w R/G/B z min/max bieżącego editu NR w sąsiedztwie 3×3 na siatce pracy. Odległość poza tym zakresem to maksimum z trzech kanałów. Maska jest równa `strength * smoothstep(threshold, threshold + softness, distance)`. Jeden wspólny współczynnik RGB miesza wynik DLSS z `NR answer - NR input proxy` w bieżącym pikselu. Potem działa istniejące globalne wygaszanie, frequency shaping i guided reconstruction. Pełna maska usuwa wpływ prywatnego DLSS na edit w tym pikselu przed dalszą obróbką.

To heurystyka odrzucania wyniku niepopartego lokalnym editem, nie pewny detektor ruchomej smugi. Ghosting mieszczący się w lokalnym zakresie może pozostać; poprawna rekonstrukcja czasowa wychodząca poza zakres może zostać odrzucona. Skutkiem może być mniejsza stabilność lub odsłonięcie szumu NR. Nie naprawia ghostingu obecnego już w samej odpowiedzi NR ani DLSS gry. Nie używa dodatkowej historii, MV ani depth do decyzji, nie zmienia automatycznego mnożnika i nie resetuje historii. Promień to jeden piksel siatki NR, więc jego zakres na ekranie zależy od skali pracy i packed warp.

Implementacja ma osobny opcjonalny PSO `private_carrier_ghost`. Zwykły `private_carrier` pozostaje identyczny bajtowo. Brak dodatkowych tekstur, dispatchy, kopii lub barier; włączona funkcja czyta 9 par proxy/answer w istniejącym decode. PSO powstaje dopiero przy użyciu. NaN/Inf w próbkach NR korzystają z istniejących zabezpieczeń per kanał, sąsiedztwo jest ograniczone do granic obrazu, parametry są sprawdzane zarówno na CPU, jak i w HLSL. Ustawienia mają pełny obieg INI/profili: `SuperiorGhostRejection`, `SuperiorGhostThreshold`, `SuperiorGhostSoftness`, `SuperiorGhostStrength`. Zapis pre-DLSS DDS pozostaje przed maskowaniem; JSON zawiera stan i parametry `ghost_rejection_*`.

Weryfikacja obejmuje 456 przypadków niezależnej referencji numerycznej RGB, wyjście FP32/FP16 i zmienne proxy: obrazy 1×1 i 17×9, brzegi, dodatnie/ujemne edity, gain 0.5/1/6, pełną odpowiedź, połączone nasycenie/kolorowanie głębi/mnożenie według głębi/rozjaśnianie oraz siłę i wygaszanie 0/0.5/1. Osobne przypadki omijają sanityzację hosta, by sprawdzić fallback parametrów HLSL. Testy nie zawierają prywatnej ewaluacji NGX ani obserwacji smugi w grze.

Pomiar syntetyczny zmienionego etapu na NVIDIA RTX 4090: wejścia/wyjście FP16, gain 1, nasycenie 1, bez innych transformacji i globalnego wygaszania; falowe pola editów zamiast sceny gry. 16 par rozgrzewki, następnie 16 ABBA, 32 próbki na wariant. Dekodowanie bez/z maską: 2496×1404 **0.030304 ± 0.000503 / 0.075488 ± 0.000527 ms**, różnica **+0.045184 ms**; 3840×2160 **0.095040 ± 0.009873 / 0.192384 ± 0.009433 ms**, różnica **+0.097344 ms**. To średnie i odchylenia standardowe z timestampów GPU. Całego toru NR + NGX + kompozycja w grze nie zmierzono; należy porównać Off/On w tej samej scenie po rozgrzewce w ABBA. [Surowe próbki](validation/private-ghost-timing.csv). Powtórzenie: `private_exposure_quality.exe --ghost-timing` po kompilacji jak w workflow eksperymentalnym.


## Zielony podgląd maski ghostingu

**Show DLSS ghost rejection mask** (domyślnie off) zaznacza na zielono obszary zastępowane czystym editem. Wymaga aktywnego **Experimental DLSS ghost rejection**, siły większej od zera oraz aktywnej ewaluacji prywatnego DLSS. Intensywność odpowiada masce odrzucenia pomnożonej przez aktualną wagę DLSS po wygaszaniu. Pełna maska to 75% zielonej nakładki, aby pod spodem było widać scenę. Maska zanika przy wyłączaniu, a podczas braku DLSS, tworzenia instancji i uśpienia nie używa starego wyniku. Zwykłe debug views obrazu mają pierwszeństwo. Podgląd nie resetuje historii ani nie zmienia podpisanego editu.

Istniejący opcjonalny decode zapisuje maskę do nieużywanego kanału alpha tekstury editu, po uwzględnieniu globalnego wygaszania. RGB pozostaje identyczne. Odrębne, tworzone na żądanie PSO kompozycji `host_ghost_hdr/sdr/packed_hdr/packed_sdr` odczytują alpha i mieszają wynik z zielenią w końcowej domenie koloru, po frequency shaping i ochronie skóry, przed kodowaniem do formatu gry. Jasność zieleni odpowiada bieli roboczej. Alpha obrazu gry, oryginalna połowa porównania, divider i letterbox pozostają zachowane. Packed mapuje maskę przez ten sam warp; cache UV zawiera również wymagany niezmienny wiersz metadanych. Wszystkie zwykłe PSO kompozycji i carrier pozostają identyczne bajtowo. Bez dodatkowej tekstury, dispatchu, kopii i bariery. Podgląd jest wykluczony z Edit cache, aby zieleń nie stała się częścią przenoszonego editu.

Klucz INI/profilu `SuperiorShowGhostMask=false` ma read/write i reset/apply oraz pojedynczy wpis w kKeptKeys. Zapis pre-DLSS DDS pozostaje bez nakładki. JSON zapisuje `show_ghost_rejection_mask`.

WARP: 456 przypadków dekodowania sprawdza teraz również alpha i jej skalowanie przez fade. 56 przypadków kompozycji porównuje nakładkę z niezależnym obliczeniem RGB względem dotychczasowego shadera: SDR/HDR, packed/zwykła siatka, stała maska 0/0.5/1, gradient, NaN/Inf i ujemna alpha, wipe oraz zachowanie alpha obrazu. Testy profili i JSON obejmują nowy klucz. Nie potwierdzono jeszcze podglądu w grze.

Syntetyczny pomiar końcowej kompozycji na RTX 4090, FP16, liniowy HDR, mapa 1:1 (NR 100%), falowy edit i pełna maska, 16 par rozgrzewki i 16 ABBA / 32 próbki na wariant: zwykły faktycznie wybierany `host_normal_hdr0` vs podgląd `host_ghost_hdr`. 2496×1404: **0.068064 ± 0.000572 / 0.094208 ± 0.000443 ms**, różnica **+0.026144 ms**; 3840×2160: **0.160960 ± 0.002194 / 0.218944 ± 0.001129 ms**, różnica **+0.057984 ms**. To timestampy GPU zmienionego etapu, nie cały tor gry. Pomiar uwzględnia przejście na ogólny wariant diagnostyczny; bez podglądu pozostają dotychczasowe specjalizacje. Cały tor należy porównać w grze Off/On po rozgrzewce w ABBA. [Surowe próbki](validation/private-ghost-mask-timing.csv); powtórzenie: `private_exposure_quality.exe --ghost-mask-timing`.


## Sparowany zrzut ghostingu przed i po DLSS

**Save DLSS ghosting sample (before + after + guides)** używa pola **Pre-DLSS edit filename** i zapisuje sześć plików obok DLL w `dlssnr-edit`. Cały zestaw dostaje wspólny wolny numer, bez nadpisywania również osieroconych `.raw.dds` / `.after.dds`.

- `nazwa.dds`: dotychczasowy signed edit po dodatnim mnożniku (także zależnym od głębi), przed nasyceniem, kolorem głębi, dodanym rozjaśnieniem i clippingiem. Starsze przyciski zachowują ten format.
- `nazwa.raw.dds`: bieżący czysty edit NR, `answer - proxy`, przed wszelkimi transformacjami transportu. Neutralny edit = 0.
- `nazwa.after.dds`: wynik tego samego Evaluate prywatnego DLSS, z cofniętym mnożnikiem, nasyceniem, kolorem i biasem głębi. Bez odrzucania ghostingu, globalnego wygaszania, zielonego podglądu i końcowego frequency shaping / guided reconstruction. Neutralny edit = 0; można bezpośrednio porównywać z `.raw.dds`.
- `nazwa.depth.dds`, `nazwa.motion.dds`: głębia R32F i MV RG32F z siatki wejścia prywatnego DLSS tej samej klatki. MV są w pikselach siatki pracy; konwencja głębi i wymiary są w JSON.
- `nazwa.json`: dotychczasowe ustawienia i dane podglądu oraz `paired_before_after_capture`, `paired_same_evaluation`, jednostki i nazwy dodatkowych editów. Przy okazji poprawiono przecinki wokół `low_motion_reset_units`; oba warianty JSON są parsowane w CI.

Kolorowe edity są FP32 DDS ze znakiem, bez obcinania do 0..1. Zrzut nie jest końcową klatką gry ani kopią bufora historii NGX. Głębia i MV pomagają analizować granice obiektów, dysokluzje i kierunek ruchu; pojedynczy zestaw nie pokazuje całej ewolucji smugi w czasie. Warto zapisywać widoczną smugę w ruchu kamery, bez zmiany ustawień tuż przed zapisem. Zielony podgląd i samo odrzucanie mogą pozostać włączone — `.after.dds` zawsze je omija.

Przycisk wymaga prywatnego DLSS oraz wyłączonego eksperymentu Original guides, tak jak dotychczasowy zrzut guides. Gdy DLSS jest uśpiony, żądanie czeka na następną aktywną ewaluację. Nie wybudza ani nie resetuje historii. Przy błędzie Evaluate/ekspozycji/dispatchu pakiet nie jest publikowany jako kompletna próbka. Wszystkie pięć tekstur powstaje w jednej liście poleceń. Własna lista kopii jest uruchamiana dopiero po rzeczywistym zgłoszeniu tej listy na właściwą kolejkę; osobny fence chroni readbacki. Ponowne wykonanie listy gry nie nadpisuje wcześniej pobranych danych. Snapshots pozostają chronione przez GpuLifetime aż replay będzie niemożliwy.

Bez żądania nie ma nowych dispatchy, tekstur, kopii, barier ani odczytów GPU. Zapis jednorazowy ma dwa dodatkowe dispatchy względem edit+guides i dwie dodatkowe kopie do readbacku. Limit snapshots + readback + wcześniej zachowane snapshots wynosi 1 GiB dla pary; starsze zrzuty zachowują 512 MiB. W 4K pliki DDS zajmują łącznie około 498 MB, a pamięć przejściowa około 996 MB, plus wyrównania. Przy braku pamięci lub przekroczeniu limitu UI zgłasza błąd. GPU, mapowanie i zapis na dysk mogą chwilowo przyciąć obraz; kosztu całej operacji w grze nie zmierzono. Porównanie wymaga timestampów GPU dla klatki bez żądania i z żądaniem, z osobnym pomiarem czasu zapisu CPU; nie traktować tego jako stałego kosztu renderowania.

Testy WARP rozszerzają produkcyjny ReadbackCopy do 1/3/5 różnych tekstur na DIRECT i COMPUTE, sprawdzają formaty i payload DDS, zakończenie bez resetowania listy, ponowny replay z innymi danymi i niezmienność readbacków. Testy CPU obejmują rezerwację wspólnej nazwy, osierocone pliki par, rollback oraz JSON. Istniejące niezależne testy carrier/decode obejmują unit gain przed DLSS i cofanie transformacji po DLSS bez maski. Weryfikacja nie zawiera prywatnego NGX w grze; poprawność i jakość próbki należy potwierdzić z nowej DLL.

## Dysokluzje z historią MV i alternatywa przestrzenna

**Ghost rejection method** ma trzy podstawowe warianty opisane w tej sekcji oraz czwarty zoptymalizowany wariant opisany dalej. Nowym domyślnym wyborem pola jest **Recent disocclusion window (temporal MV)**, ale **Experimental DLSS ghost rejection** pozostaje domyślnie wyłączone. Progi legacy są zachowane przy powrocie do **Local range 3x3 (legacy)**. Wszystkie warianty działają po odwróceniu transportu, przed globalnym wygaszaniem, frequency shaping i guided reconstruction. **Ghost rejection strength** jest wspólne; przy 1 pełna maska daje bieżący `answer - proxy`. **Show DLSS ghost rejection mask** pokazuje faktyczną maskę na zielono. Błąd przygotowania historii omija odrzucanie i nie pokazuje fikcyjnej maski. Funkcja nie resetuje historii NR ani NGX.

### Recent disocclusion window (temporal MV)

Ta metoda szacuje, czy obecnie widoczna powierzchnia była wcześniej zasłonięta. Nie porównuje kolorów editu i nie rozpoznaje postaci. Tymczasowo oddaje odsłonięty fragment czystemu NR, również gdy DLSS działałby na nim poprawnie: to świadomy kompromis stabilności i odrzucania smug, nie pewny klasyfikator ghostingu.

- **Disocclusion masking window (frames)**: 1–120, default 16. Liczy aktywne ewaluacje prywatnego DLSS, nie czas ani klatki FG. Licznik nowej dysokluzji jest przenoszony MV razem z powierzchnią i maleje o jeden na ewaluację.
- **Disocclusion return fade (frames)**: 0–N, default 3. Przywraca DLSS podczas ostatnich klatek okna. Zero oznacza pełną maskę do wygaśnięcia; nie maskuje powierzchni z zerowym licznikiem.
- **Disocclusion depth separation**: 0.001–0.5, default 0.05. Separacja względna w dodatniej domenie bliskości NDC, z marginesem kwantyzacji; nie metry. Niższy próg zwiększa czułość i ryzyko fałszywego maskowania.
- **Ghost relative motion gate (1080p)**: 0.1–16, default 2 px przy wysokości 1080, skalowane do siatki pracy. Dotyczy pomocniczego wykrywania warstw ruchu przy małej różnicy głębi; nie jest obowiązkowym warunkiem głównego testu dysokluzji.

Trzy przebiegi na połowie wymiarów siatki NR: zerowanie R32_UINT, rozrzucenie bieżącej bliskości do współrzędnych poprzedniej klatki przez current→previous MV (`InterlockedMax`), aktualizacja historii. Każdy punkt wybiera najbliższy poprawny depth z 2×2 i jego MV. Rekonstrukcja poprzednich współrzędnych używa głębi tej samej bieżącej klatki, aby ograniczyć mylenie zmian NDC od kamery z zasłanianiem. Ogólną technikę opisuje m.in. [dokumentacja FSR temporal, Reconstruct & dilate / Depth clip](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-temporal/); tutaj jest osobnym shaderem, bez integracji FSR.

Dodatkowe próby poprzedniej warstwy MV i bliższej głębi wykrywają część kontaktów but–podłoże i znikający occluder. Wymagają sąsiedniej warstwy pasującej do bieżącego ruchu **i głębi**, więc sama zmiana prędkości nie wystarcza. Nowy wyraźnie bliższy obiekt zeruje licznik zamiast przejmować maskę tła. Decode pobiera biliniowo licznik z zgodnej powierzchni, uwzględniając także sąsiadów z zerowym wiekiem, i miesza wynik z czystym editem. Nowa historia zaczyna bez maski. Reset NGX/NR lub nowa generacja czyści jej ważność. Odrzucona niezgłoszona lista powoduje odtworzenie zasobów zamiast użycia niezrealizowanych przejść.

Dwa RGBA32F (bliskość, wiek, MV.xy) i R32_UINT na siatce ceil(work/2) zajmują nominalnie około 9 bajtów/piksel pracy: 30.1 MiB przy 2496×1404, 71.2 MiB przy 3840×2160, plus wyrównania. Powstają na żądanie i mają odroczone zwolnienie po wyłączeniu/zmianie metody. Bez funkcji nie ma tych alokacji ani trzech dispatchy, kopii czy readbacków. Zwykłe dopasowane guides nie wymagają osobnego przebiegu przygotowania. Eksperyment Original guides nadal przekazuje oryginalne guides do NGX, ale maska wymaga równocześnie dopasowanych guides na siatce NR. Packed korzysta z dopasowanej siatki i jej jednostek MV.

Bez macierzy kamery i historii identyfikatorów powierzchni jest to estymacja. Warstwy o niemal identycznej głębi i ruchu są niejednoznaczne; cienkie elementy mogą zniknąć przy połowie wymiarów. Szybkie zmiany głębi kamery, niepoprawne MV, przezroczystości i odbicia mogą dać błędną decyzję. Wejście z brzegu obrazu bez dowodu wcześniejszego zasłonięcia nie jest automatycznie dysokluzją. Niepoprawne/brakujące dane nie uruchamiają maski. Funkcja nie usuwa ghostingu już obecnego w NR ani w DLSS gry. Nie gwarantuje zamaskowania wszystkich butów ani braku false positives.

### Depth + motion + coherent foreground match

Alternatywa przestrzenna bez historii i dodatkowych dispatchy. W decode szuka bliższego obiektu o innym MV i zgodnego wkładu jego koloru w różnicę `DLSS edit - raw edit`. Spójność tej różnicy na krzyżu pięciu punktów oraz lokalna amplituda raw ograniczają odrzucanie zwykłego odszumiania. Do 24 prób depth/MV wzdłuż ruchu i w wachlarzu, z wczesnym odrzuceniem. Przy małym MV bieżącego punktu wyszukuje również ruchomy foreground, więc nie wymaga ruchu kamery.

**Ghost search radius (1080p reference)**: 8–512, default 192; **Ghost foreground depth ratio**: 1.02–4, default 1.2; **Ghost relative motion gate (1080p)** jest wspólne z temporal. **Ghost rejection threshold / softness** mają tu osobne wartości 0.004 / 0.01. Bliższy aktualny obiekt nie jest odrzucany na podstawie dalszego kandydata. Metoda może przegapić smugi o innym kolorze, małej separacji głębi lub poza próbkowanymi kierunkami. W sześciu zapisach zmniejsza część powielonych sylwetek, ale nadal zostawia widoczne smugi przy butach; nie przedstawiamy jej jako rozwiązania tego problemu.

### Weryfikacja i koszt nowych metod

WARP: 16 przypadków przestrzennych (wstrzyknięta kopia foreground, nieruchoma kamera/ruchomy obiekt, ta sama głębia, ten sam MV, poprawne tłumienie szumu, dalszy kandydat, niepoprawne guides; obie konwencje Z). Temporal: 75 kontroli znanego odsłonięcia, ochrony occludera, dokładnego N i przenoszenia licznika MV, resetu, NaN/Inf, przyspieszenia kamery, kwantyzacji D16/D24 przed wejściem R32F, obu konwencji Z, 1×1/nieparzystych wymiarów oraz niezależnej referencji biliniowego decode, zerowych sąsiadów, fade=0/2, siły, gain=0.5/6 i trybu editu/pełnej odpowiedzi. Zachowane 456 przypadków legacy, 56 zielonego podglądu i 1507 dotychczasowych przypadków ekspozycji/transportu. To nie ewaluacja NGX w grze.

[Raport sześciu sparowanych zapisów](validation/nr-paired-ghost-study.json) zawiera hashe i pochodne statystyki, bez obrazów. `tools/analyze_nr_paired_ghost.py` nie utożsamia różnicy RGB z ghostingiem. Pokrycie maski przestrzennej >0.5 wynosi 1.34–2.38% obrazu; to nie precyzja/recall. Zapisy dzieli kilkaset klatek i **nie weryfikują historii temporal**. Test numeryczny nie zastępuje obserwacji postaci i butów w grze.

Timestampy GPU: RTX 4090, 2496×1404, dane scen nr-edit-001 / 004, FP32, podpisany edit w znormalizowanej domenie, pełna odpowiedź z zerowym proxy, gain/saturation 1, bez pozostałych transformacji, podglądu i fade. 16 par rozgrzewki, 16 ABBA (32 próbki/wariant). Średnia ± odchylenie standardowe:

| Próba | Sam decode [ms] | 3 etapy temporal + decode [ms] | Różnica [ms] |
| --- | ---: | ---: | ---: |
| 001 | 0.146112 ± 0.001411 | 0.286880 ± 0.003442 | +0.140768 |
| 004 | 0.146112 ± 0.001570 | 0.301856 ± 0.004110 | +0.155744 |

Pomiar obejmuje bariery między etapami i decode UAV→SRV, bez alokacji w interwale GPU. Guides są powtarzane: wynik mierzy koszt, nie jakość czasową. [ABBA 001](validation/private-ghost-temporal-001-timing.csv), [004](validation/private-ghost-temporal-004-timing.csv). Powtórzenie: `private_exposure_quality.exe --temporal-ghost-timing <capture stem>`. Spatial na tych samych wejściach (sam dispatch decode): 001 0.145760±0.002618 → 0.269824±0.001379 (+0.124064 ms); 004 0.145088±0.001502 → 0.286048±0.000606 (+0.140960 ms). Względem legacy przy tych samych progach 0.004/0.01: +0.046656 / +0.063072 ms. [Spatial ABBA 001](validation/private-ghost-guided-001-timing.csv), [004](validation/private-ghost-guided-004-timing.csv). Nie porównywać bezpośrednio z wcześniejszym syntetycznym benchmarkiem FP16 legacy.

Całego toru NR+NGX+kompozycja w grze nie zmierzono. Weryfikacja: ta sama scena i ruch kamery, GPU, rozdzielczość, presety/transport, osobna rozgrzewka, Off/temporal w ABBA, następnie spatial/legacy; timestampy całego toru i etapów, średnia i zmienność, bez zrzutów podczas pomiaru. Osobno ocenić false positives na poprawnie stabilizowanych powierzchniach i pozostałe smugi przy stopach, z podglądem i bez niego.

Nowe klucze INI mają read/write, reset/apply i kKeptKeys profili: `SuperiorGhostMethod`, `SuperiorGhostGuidedThreshold`, `SuperiorGhostGuidedSoftness`, `SuperiorGhostRadius`, `SuperiorGhostDepthRatio`, `SuperiorGhostMotionGate`, `SuperiorGhostHistoryFrames`, `SuperiorGhostHistoryFade`, `SuperiorGhostHistoryDepth`. JSON dopisuje metodę, promień, separację i parametry okna. Parametry mają sanitizację CPU i fallback HLSL oraz asercje ABI. Zwykłe bloby carrier, kompozycji i legacy pozostają identyczne bajtowo.

## Opcja 4: zoptymalizowana historia dysokluzji

**Recent disocclusion window (optimized mixed precision)** jest czwartą pozycją **Ghost rejection method** (`SuperiorGhostMethod=3`). Dotychczasowa trzecia metoda (`=2`), jej źródła shaderów i bloby pozostają bez zmian. Domyślny wybór metody nadal wynosi 2; użytkownik sam wybiera opcję 4. Obie metody współdzielą istniejące ustawienia okna, powrotu DLSS i separacji głębi/MV. Zmiana między nimi rozpoczyna nową historię samej maski, bez resetu NR lub NGX. Zmiany automatycznego mnożnika nadal nie resetują historii DLSS.

Opcja 4 zachowuje trzy przebiegi połowy wymiarów NR, lecz łączy wybór guides 2×2 z rozrzutem głębi: zapisuje wybrane guides do aktualizowanego następnie UAV historii. Etap historii nie musi ponownie odczytywać czterech wartości głębi i MV z pełnej siatki. To samo UAV przechodzi przez poprawne bariery między etapami. W potwierdzonej dysokluzji kończy pracę od razu; w braku różnicy warstw pomija poszukiwanie sąsiadów. W pozostałych przypadkach minimalizuje kwadraty długości MV, a pierwiastki liczy dopiero po wyborze minimum, zamiast osobno dla każdego sąsiada.

Historia ma dwa `R32G32_UINT`, po 8 bajtów na punkt: pierwsze słowo zawiera głębię z wykładnikiem FP32 i 16 bitami mantysy oraz dokładny 7-bitowy licznik 0–120, drugie dwa MV zakodowane jako FP16. Pomocniczy `R32_UINT` rekonstrukcji pozostaje. Łączny nominalny payload spada z 9 do 5 bajtów na piksel NR, czyli o 44.4%: około 16.7 zamiast 30.1 MiB przy 2496×1404, 39.6 zamiast 71.2 MiB w 4K, plus wyrównania. Nie są to pomiary zużycia całego procesu. Guides przekazywane do NR/NGX pozostają w dotychczasowych formatach; kompresowana jest wyłącznie prywatna historia maski. Bez opcji nie dochodzi nowa praca GPU, a przejściowe alokacje i odroczone zwolnienia korzystają z istniejącego czasu życia generacji.

Decode odczytuje z historii tylko głębię i licznik; nie rozpakowuje MV. Wagi interpolacji i sumowanie wieku wykonuje natywnym FP16, a RGB, współrzędne, porównania głębi i końcowy blend nadal FP32. DXC używa `cs_6_2`, HLSL 2018 i `-enable-16bit-types`; generator sprawdza faktyczne instrukcje `fmul fast half` w DXIL. Nie jest to tylko deklaracja `half` kompilowana do FP32. Dla opcji 4 nie ma automatycznego wariantu FP32 maskowania; użytkownik zachował opcję 3 jako niezależny punkt odniesienia. [Opis natywnych typów 16-bit w DXC](https://github.com/microsoft/DirectXShaderCompiler/wiki/16-Bit-Scalar-Types).

**Disocclusion soft mask expansion**: 0–1, domyślnie 0.5, tylko opcja 4. Łagodnie zwiększa wagę maski na jej istniejącym brzegu, szczególnie na narożnikach, mieszając iloczyn dwóch wag liniowych z ich minimum. Zero zachowuje biliniowy wariant, jedynka najszerszy wariant tych samych czterech próbek. Każda próbka przechodzi kontrolę zgodności głębi. To lekkie poszerzenie miękkiej otoczki połowy siatki, nie dowolna dylatacja o podaną liczbę pikseli. Nie ma osobnego dispatchu ani tekstury. Przy zerowym ustawieniu czasowego fade maska pozostaje twarda do wygaśnięcia, zgodnie z dotychczasowym znaczeniem tego parametru. Zwiększanie otoczki może odrzucić więcej poprawnej stabilizacji DLSS; cienkie powierzchnie i niejednoznaczne guides nadal mogą powodować błędy. Zielony podgląd pokazuje faktyczną końcową wagę.

Klucz `SuperiorGhostSoftExpansion` ma deklarację/default, odczyt/zapis INI, reset/apply i pojedynczy wpis w `kKeptKeys`; JSON dopisuje `ghost_disocclusion_soft_expansion`. Parametry mają sanitizację CPU i fallback wartości w HLSL. Nie mylić fallbacku niepoprawnego parametru z usuniętym wariantem arytmetyki FP32.

### Precyzja i testy

[Audyt kwantyzacji sześciu źródłowych zapisów](validation/nr-compact-ghost-precision.json): największy błąd pojedynczej składowej MV po zapisie FP16 wyniósł 0.03125 piksela pracy, względny błąd zapisu bliskości około 0.00001514. Wynik dotyczy tych danych, nie wszystkich możliwych wektorów; krok FP16 rośnie z wartością. `tools/analyze_nr_compact_precision.py` wykonuje ten audyt bez pracy GPU. Margines porównań głębi uwzględnia pakowanie oraz oryginalną kwantyzację. Licznik jest przechowywany dokładnie, bez zaokrąglania FP8.

Testy GPU zawierają 102 kontrole sekwencji widoczności dla dotychczasowej i kompaktowej historii: kontakt but–podłoże, occluder, kontynuacja MV, upływ N, reset, invalid guides, przyspieszenie kamery, D16/D24 na wejściu R32F, reversed/conventional Z, 1×1 i nieparzyste wymiary. Dodatkowe 72 przypadki natywnego decode mają niezależną referencję skalarną, RGB i maskę z asercjami błędu (RGB do 0.00025, alpha do 0.0015): ekspansja 0/0.5/1, fade 0/3, neutralna historia, inna powierzchnia, obie konwencje Z, edit i pełna odpowiedź, gain 6 i globalny fade. Dotychczasowe 456/16/56/1507 przypadków legacy/spatial/podglądu/transportu pozostają. WARP oraz RTX 4090 przechodzą testy; nie jest to potwierdzenie jakości opcji 4 w grze. Użytkownik potwierdził działanie **opcji 3**, nie nowej optymalizacji.

### Pomiar i odrzucone próby

Średnie i odchylenia standardowe końcowego wariantu:

| Próba | Opcja 3 [ms] | Opcja 4 [ms] | Oszczędność etapu |
| --- | ---: | ---: | ---: |
| 001 | 0.124704 ± 0.001488 | 0.104096 ± 0.002042 | 0.020608 ms / 16.5% |
| 004 | 0.134720 ± 0.002080 | 0.112192 ± 0.001763 | 0.022528 ms / 16.7% |

RTX 4090, siatka NR 2496×1404, zapisane sceny 001 i 004. Kolor FP16, guides R32F/RG32F, unit gain/saturation, pełna odpowiedź w znormalizowanej domenie signed edit z zerowym proxy, bez podglądu i globalnego fade. 16 par rozgrzewki, 16 ABBA, 32 próbki/wariant. Mierzony jest cały zmieniony region GPU: clear, scatter/cache, historia, decode oraz bariery. Żadne alokacje ani odczyty CPU nie wchodzą w interwał timestampów. Powtarzane guides mierzą koszt, nie jakość czasową. Surowe wyniki: [001](validation/private-ghost-compact-001-timing.csv), [004](validation/private-ghost-compact-004-timing.csv), [podsumowanie](validation/private-ghost-compact-summary.json). Powtórzenie: `private_exposure_quality.exe --compact-ghost-timing <capture stem>` po kompilacji jak w workflow eksperymentalnym.

Odrzucono dodatkowy przebieg poszerzania z maską FP8 E5M2 (spakowaną razem z głębią w R32_UINT) oraz analogiczny bufor FP16: nie dawały przewagi całej ścieżki. FP8 nie zmniejszał 4-bajtowego payloadu tego bufora względem RG16F, wymagał konwersji i dodatkowego etapu. Nie przedstawiamy tego jako dowodu, że każda operacja FP8 jest wolniejsza; to odrzucony konkretny projekt. [Próby 001](validation/private-ghost-rejected-fp8-001-timing.csv), [004](validation/private-ghost-rejected-fp8-004-timing.csv) porównują całą ścieżkę z dodatkowym etapem FP8 do metody 3 i kompaktowego wariantu bez tego etapu. Do wersji produkcyjnej nie trafia martwy shader FP8. FP8 nie jest zwykłym typem arytmetycznym skalarnego shadera w używanym SM 6.2; [nowsze API DirectX dla FP8 dotyczą operacji algebry liniowej](https://microsoft.github.io/DirectX-Specs/d3d/D3D12LinearAlgebraRuntimeFeatureSupport.html).

Sprawdzono także natywne FP16 dla wyboru/porównań głębi i różnic MV, z oddzieloną całkowitą współrzędną reprojekcji. Nie dało zysku: kompaktowy etap około 0.105376/0.113984 ms vs dodatkowe FP16 guides 0.108704/0.114944 ms. Druga różnica jest bliska zmienności. [001](validation/private-ghost-half-guides-001-timing.csv), [004](validation/private-ghost-half-guides-004-timing.csv). Timestampy nie ustalają konkretnej przyczyny sprzętowej; koszty konwersji są hipotezą, nie wynikiem profilera. Dlatego geometryczne FP32 jest świadomym wyborem wydajności, a nie fallbackiem FP16.

Rozdzielenie pól historii na dwie płaszczyzny jednej tekstury R32_UINT oraz grupy 16×8 zamiast 8×8 również nie poprawiły tych scen. [Planar 001](validation/private-ghost-rejected-planar-001-timing.csv), [004](validation/private-ghost-rejected-planar-004-timing.csv); [16×8 001](validation/private-ghost-rejected-wide-001-timing.csv), [004](validation/private-ghost-rejected-wide-004-timing.csv). Zostaje interleaved uint2 i grupy 8×8. Odrzucone warianty nie są dodatkowymi pozycjami UI ani częścią zwykłego benchmarku.

Cały tor gry NR+NGX+kompozycja wymaga osobnego pomiaru po rozgrzewce, w porównywalnej scenie i ABBA 3/4, z timestampami całego toru oraz tego etapu. Oszczędności tego etapu nie przeliczać wprost na procent FPS. Osobno sprawdzić resztki smug, poprawną stabilizację i podgląd przy butach w ruchu.

## Dalsza optymalizacja opcji 4: decode i bariery (checkpoint 9f2cc97c)

Kolejna wersja nie zmienia ustawień, precyzji historii, liczników ani kształtu maski. Opcje 1–3 pozostają bez zmian. Dla opcji 4 wprowadza:

- Pojedynczy `GatherRed` zamiast czterech `Load` przy pobieraniu głębi/licznika historii. Pobierana jest ta sama czwórka texeli, w tej samej kolejności sumowania. Współrzędna gather jest środkiem wyznaczonego wcześniej footprintu, aby ograniczyć wpływ dokładności współrzędnych samplera. Clamp na brzegach pozostaje. [Dokumentacja Microsoft Gather](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-to-gather).
- Bariery UAV zamiast zbędnych par przejść UAV→SRV→UAV pomiędzy clear i scatter oraz scatter i aktualizacją historii. Zasób nadal jest synchronizowany, ale pozostaje w stanie właściwym dla kolejnego zapisu/odczytu UAV. Przejścia do SRV pozostają tam, gdzie faktycznie zaczyna się odczyt SRV. Zmiana jest ograniczona do opcji 4; wspólny helper ma domyślne zachowanie identyczne z poprzednim.
- Stały Mode 38 w kompaktowym PSO decode, zgodny z warunkiem hosta. Kompilator nie przechowuje już gałęzi kodowania/zrzutu w tym PSO.
- Dwa dodatkowe natywne warianty FP16 dla nasycenia 1× i wyłączonych depth colour/depth brightness: osobno edit wokół 0.5 i pełna odpowiedź NR. `SimpleCompactGhost` sprawdza rzeczywiste wartości i aktywne bity. Dodatni mnożnik nadal jest dowolny, również <1. Po włączeniu nasycenia lub transformacji głębi wybierany jest ogólny kompaktowy PSO, z ich pełnym działaniem. To specjalizacje tych samych obliczeń, bez nowych opcji UI i bez resetu historii.

Liczba przebiegów, tekstury i pamięć historii pozostają takie jak w poprzedniej opcji 4. Nowe PSO są tworzone na żądanie. Normalny carrier i shadery metody 3 pozostają identyczne bajtowo. Generator buduje 72 bloby, w tym oba faktycznie wybierane warianty tożsamościowego transportu; nie ma niepodłączonych produkcyjnych entry pointów.

WARP: 432 przypadki niezależnej referencji maski dla obu wariantów tożsamościowych i ogólnego decode, m.in. 1×1, 17×9, 1281×7, krawędzie, zerowy wiek, inna powierzchnia, obie konwencje Z, gain 6, oba wejścia NR i fade/ekspansja. 64 dodatkowe przypadki porównują wynik z niezmienionym zwykłym carrierem i niezależnie znaną wagą surowego editu: gain 0.5/6, nasycenie 1/4, depth colour on/off, depth brightness on/off, oba wejścia i Z. Osobne asercje chronią warunki wyboru PSO przed pominięciem aktywnego ustawienia. Dotychczasowe testy 456/16/102/56/1507 pozostają. Nie zmienia to ograniczeń estymacji dysokluzji ani nie dowodzi poprawy jakości w grze.

### Pomiar względem ostatniej DLL opcji 4

Referencja to **niezmienny commit d419c5e7032f0c8b37aa274fc831f3d1471849bb**, a nie bieżący shader pod inną nazwą. `python tools/prepare_nr_compact_reference.py` odtwarza trzy oryginalne bloby z obiektów Git i zapisuje ich SHA256; brak commita jest błędem, bez zastępowania referencji wersją bieżącą. Następnie `private_exposure_quality.exe --compact-revision-timing <capture stem>` mierzy oba tryby wejścia, porównuje całe słowa końcowej historii i wszystkie piksele decode FP16. W czterech porównaniach na scenach 001/004 **historia i piksele są bitowo identyczne**. Guides są powtarzane podczas benchmarku; nie jest to sekwencja NGX w grze.

RTX 4090, siatka NR 2496×1404, kolory FP16, rzeczywiste zapisane depth/MV. Dodatni mnożnik 1, nasycenie 1, depth colour/brightness off, okno 16, powrót 3, ekspansja 0.5; zielony podgląd i globalny fade off. Pełna odpowiedź używa znormalizowanej domeny signed edit z zerowym proxy; tryb editu dodaje neutralne 0.5 do tych samych danych przed konwersją FP16. 16 par rozgrzewki, następnie 64 ABBA = 128 próbek/wariant. Timestampy obejmują wszystkie przebiegi maski, decode i bariery, bez alokacji i mapowania CPU w interwale. Poza timestampem dochodzi readback do porównania bitowego; produkcja nie wykonuje tego porównania ani readbacku.

| Wejście | Scena | Poprzednia opcja 4 [ms] | Nowa opcja 4 [ms] | Oszczędność etapu |
| --- | --- | ---: | ---: | ---: |
| Edit NR | 001 | 0.099512 | 0.091832 | 0.007680 ms / 7.7% |
| Edit NR | 004 | 0.107160 | 0.099336 | 0.007824 ms / 7.3% |
| Pełna odpowiedź NR | 001 | 0.104176 | 0.098160 | 0.006016 ms / 5.8% |
| Pełna odpowiedź NR | 004 | 0.112752 | 0.107104 | 0.005648 ms / 5.0% |

Odchylenia standardowe i parowane różnice ABBA są w [podsumowaniu](validation/private-ghost-compact-v2-summary.json); surowe wyniki: [001 edit](validation/private-ghost-compact-v2-001-edit.csv), [004 edit](validation/private-ghost-compact-v2-004-edit.csv), [001 pełna odpowiedź](validation/private-ghost-compact-v2-001-answer.csv), [004 pełna odpowiedź](validation/private-ghost-compact-v2-004-answer.csv). Nie sumować tych procentów z wcześniejszymi procentami ani utożsamiać ich z FPS gry. Cały tor NR+NGX+kompozycja nie był mierzony w grze; potrzebne jest ABBA obu DLL w tej samej scenie z timestampami całego toru i etapu po rozgrzewce.

Odrzucono `GatherRed` w wyborze guides i w odczycie rekonstrukcji: nie dawał spójnego zysku w obu scenach. Odrzucono także znacznik klatki w 64-bitowym depth i `InterlockedMax` zamiast clear w każdej klatce. Wynik był bitowo poprawny, ale wzrósł koszt całego etapu: około 0.104200→0.111528 ms i 0.112088→0.117984 ms. [Surowe próby 001](validation/private-ghost-rejected-tagged-001.csv), [004](validation/private-ghost-rejected-tagged-004.csv). Przebieg mniej nie zrekompensował droższego wariantu; timestampy nie izolują kosztu samej instrukcji. Eksperymentalne PSO 64-bit nie są częścią paczki produkcyjnej.

## Wycofanie odrzuconych transformacji transportu

Wycofano **NR edit transport saturation**, **Depth colour transport**, **Depth-weighted positive edit multiplier** i **Add near-camera brightness before DLSS**, razem z parametrami. Usunięto kontrolki, pola Config, odczyt/zapis i apply/reset profili. Dwanaście dawnych kluczy należy do `kLeftOutKeys`; `kRetiredCarrierKeys` usuwa je przy zapisie INI, także z wcześniej istniejących plików. Stare wpisy nie wpływają na klatki ani historię.

Usunięto rzeczywiste obliczenia encode/decode i fused guides/carrier, odczyty głębi dla tych efektów oraz zbędny ogólny PSO kompaktowego decode. Opcja 4 zawsze wybiera jeden z dwóch natywnych wariantów FP16: edit lub pełna odpowiedź NR. Generator buduje 71 blobów. Wspólny ABI stałych pozostaje; dawne sloty są ignorowane. Pomocnicze settery i czysta matematyka CPU pozostają tylko dla testów starych pól i archiwalnych metadanych, bez wywołań przy renderowaniu. Nowe zrzuty zachowują neutralne dawne pola JSON dla kompatybilności narzędzi.

**Positive NR edit transport multiplier** nadal działa jednolicie na dodatnich składowych i jest cofany po DLSS. Ujemne składowe nie są mnożone, neutralny edit daje carrier 0.5. Ręczne wartości <1 pozostają; automat nie schodzi poniżej 1× i nie resetuje historii przy swoich zmianach. Nie wraca limit -1..2: carrier ma zakres 0..65504. Głębia/MV dla NR, interpass, rekonstrukcji, detekcji ruchu, kontrastu granic i dysokluzji pozostają. Usunięte opcje nie blokują podglądu/automatu; ograniczenia pełnej odpowiedzi i original guides pozostają.

Kontrola obejmuje test profili z aktualnym Config.cpp, 507 przypadków ekspozycji/transportu, 456 legacy maskowania, 16 guided, 102 sekwencji widoczności, 432 niezależnej referencji compact, 64 łączone i 56 zielonego podglądu. Testy wpisują dawne nieobojętne parametry i wymagają wyniku jednolitego transportu. 768 przypadków fused/separate sprawdza kolor, głębię i MV. Liczba testów transportu zmalała przez usunięcie scenariuszy odrzuconych funkcji; ekspozycja, HDR, gain <1, pełna odpowiedź i niepoprawne dane pozostają pokryte.

`python tools/prepare_nr_compact_reference.py` odtwarza też niezależne bloby checkpointu 9f2cc97c sprzed usunięcia transformacji. `private_exposure_quality.exe --compact-retirement-timing <capture stem>` porównuje checkpoint z bieżącą opcją 4 w obu trybach wejścia, po 16 parach rozgrzewki i 64 ABBA, sprawdzając bitową zgodność historii i pikseli. Referencji nie zastępuje się shaderem bieżącym.

RTX 4090, zapisane sceny 001/004, NR 2496×1404, kolor FP16, depth R32F / MV RG32F, gain 1, okno 16, fade 3, ekspansja 0.5, zielony podgląd i globalny fade off. Timestampy obejmują całą maskę, decode i bariery; poza oknem są upload, alokacje i odczyt CPU. W obu trybach wyniki historii i decode są bitowo zgodne z checkpointem. Guides są powtarzane, to pomiar etapu na zapisach, bez NGX i bez gry.

| Scena / wejście | Przed [ms ± SD] | Po [ms ± SD] | Po − przed [ms] |
|---|---:|---:|---:|
| 001 / edit | 0.091816 ± 0.001221 | 0.091904 ± 0.002452 | +0.000088 |
| 001 / answer | 0.098320 ± 0.001252 | 0.098128 ± 0.001260 | -0.000192 |
| 004 / edit | 0.099576 ± 0.001508 | 0.099336 ± 0.001288 | -0.000240 |
| 004 / answer | 0.107104 ± 0.001249 | 0.107448 ± 0.001214 | +0.000344 |

Drobne różnice kompaktowego etapu nie są podstawą deklaracji dodatkowego przyspieszenia całej gry. Usunięcie odrzuconej matematyki obejmuje także zwykłe encode/decode i fused preparation; ich osobny zysk nie został tu zmierzony. [Podsumowanie](validation/private-carrier-retirement-summary.json), surowe próby: [001 edit](validation/private-carrier-retirement-001-edit.csv), [001 answer](validation/private-carrier-retirement-001-answer.csv), [004 edit](validation/private-carrier-retirement-004-edit.csv), [004 answer](validation/private-carrier-retirement-004-answer.csv). Weryfikacja gry nadal wymaga rozgrzanego ABBA obu DLL z timestampami całego NR+NGX+kompozycji, tą samą sceną i ustawieniami, oraz oceny stabilizacji/ghostingu w ruchu.

## Opcja 5: fast blend i łączenie zapisów historii

**Recent disocclusion window (fast blend, experimental)** jest piątą pozycją w **Ghost rejection method** (`SuperiorGhostMethod=4`). Domyślny wybór nie zmienia się; opcje 1–4 pozostają dostępne, wszystkie ich dotychczasowe 71 blobów pozostają bajtowo niezmienione. Zapis/odczyt INI, reset/apply i filtr profili używają istniejącego klucza; sanitizacja akceptuje teraz również 4. JSON zapisuje tę wartość w istniejącym `ghost_rejection_method`.

Nowy wariant łączy dwie zmiany:

- **Łączenie splatów w wave.** Jeśli prawy zapis jednego aktywnego wątku trafia w dokładnie ten sam texel co lewy zapis następnej, wykonywane jest jedno `InterlockedMax` z maksimum obu głębi. Sprawdzane są rzeczywiste współrzędne, aktywność i maski pokrycia, bez założenia kolejności lane/piksel. Pozostałe zapisy działają osobno. Dla dodatnich skończonych głębi to dokładnie ta sama redukcja maksimum, bez zmiany wyniku rekonstrukcji ani timerów. Nie dodaje LDS, barier ani dispatchy. Jeśli urządzenie nie utworzy PSO z operacjami wave, host używa zwykłego scatter tej samej metody i pamięta wynik próby; nie powtarza tworzenia PSO co klatkę.
- **Wektorowe wagi FP32.** Zamiast kolejnych sumowań FP16 używa czteroskładnikowych iloczynów skalarnych FP32. Zmienia to zaokrąglenia na miękkim brzegu, bez zmiany kontroli głębi. Dla parzystych osi współrzędne wykorzystują dokładne ćwiartki texela; dla nieparzystych osobno wyznaczają małą poprawkę `1/(2N)`, ograniczając utratę precyzji przy odejmowaniu dużych współrzędnych. Siatka jest nadal `ceil(work/2)`, z tym samym footprintem i clampem. Arytmetyka RGB pozostaje FP32.

Detekcja dysokluzji, oba pierścienie sąsiadów przy kontakcie, historia MV FP16, wiek 1–120, bufor 8 bajtów na texel połowy siatki, kontrola foreground, fade i miękka ekspansja zachowują znaczenie opcji 4. Nie ma rzadszych klatek, pomijania butów, większych progów ani bardziej zgrubnej siatki. Pamięć historii i liczba przebiegów pozostają takie same; trzy nowe PSO są tworzone na żądanie tylko dla nowej opcji. Generator buduje 74 bloby.

### Pomiar GPU i błąd

Referencja: niezmienne bloby commita **ac9540b3e09f88369fa7c5a7ab5c97e3b6984491**, odtwarzane przez `python tools/prepare_nr_compact_reference.py`. Powtórzenie: `private_exposure_quality.exe --compact-fast-timing <capture stem>`. RTX 4090, sześć dostarczonych zapisów 001–006, NR 2496×1404, kolor FP16, depth R32F / MV RG32F. Gain 1, okno 16, fade 3, ekspansja 0.5; zielony podgląd i globalny fade off. Pełna odpowiedź jest mierzona w znormalizowanej domenie signed edit z zerowym proxy; tryb editu dodaje neutralne 0.5. Po 16 parach rozgrzewki następują 64 ABBA, 128 próbek na wariant. Timestampy obejmują clear, scatter, historię, decode i bariery; upload, alokacje i readback są poza oknem. Guides są powtarzane, a nie odtwarzane jako sekwencja gry/NGX.

| Scena / wejście | Opcja 4 [ms ± SD] | Opcja 5 [ms ± SD] | Oszczędność etapu |
|---|---:|---:|---:|
| 001 / edit | 0.092296 ± 0.003418 | 0.085504 ± 0.004768 | 7.4% |
| 001 / answer | 0.098000 ± 0.001242 | 0.092784 ± 0.002163 | 5.3% |
| 002 / edit | 0.094104 ± 0.001481 | 0.087784 ± 0.002103 | 6.7% |
| 002 / answer | 0.103368 ± 0.001872 | 0.097952 ± 0.002171 | 5.2% |
| 003 / edit | 0.096296 ± 0.001565 | 0.090008 ± 0.001747 | 6.5% |
| 003 / answer | 0.105032 ± 0.001289 | 0.100208 ± 0.001111 | 4.6% |
| 004 / edit | 0.099416 ± 0.001185 | 0.092080 ± 0.001829 | 7.4% |
| 004 / answer | 0.107328 ± 0.001162 | 0.101608 ± 0.001343 | 5.3% |
| 005 / edit | 0.089952 ± 0.001846 | 0.084912 ± 0.002430 | 5.6% |
| 005 / answer | 0.093832 ± 0.001757 | 0.089552 ± 0.002064 | 4.6% |
| 006 / edit | 0.094856 ± 0.001803 | 0.088056 ± 0.002201 | 7.2% |
| 006 / answer | 0.098704 ± 0.001522 | 0.093224 ± 0.002880 | 5.6% |

[Podsumowanie z jakością i parowanymi różnicami ABBA](validation/private-ghost-fast-summary.json); surowe CSV mają nazwy `validation/private-ghost-fast-001-edit.csv` itd. W każdym z 12 porównań całe słowa historii są bitowo identyczne. Maksymalna różnica wagi maski to 0.000488281, RGB 0.00012207; zmienione są 45–124 piksele maski z 3504384 na zapis (0.0013–0.0036%). Maksymalna różnica maski i RGB oraz liczba zmienionych pikseli są osobno zapisane dla każdego wejścia. To małe różnice numeryczne na tych zapisach, nie dowód niewidoczności w każdej grze. Nie przeliczać oszczędności etapu na procent FPS.

WARP i RTX 4090: dotychczasowe testy transportu/ekspozycji, green overlay i metod 1–4 oraz 432 niezależne przypadki maski + 64 transportu dla opcji 5. Dodatkowe 120 przypadków porównuje z referencją double mieszane powierzchnie, NaN/Inf, D16/D24, HDR, gain 0.01–1024 i szerokie nieparzyste siatki. Granice: maska 0.000003, RGB 0.00001 + 0.000003 × maksimum modułów raw/filtered. 72 przypadki splatów wymagają dokładnej zgodności uint z niezależnym skalarnym splatem i zachowanym PSO opcji 4: małe/nieparzyste wymiary, częściowe waves, niejednorodne MV, dane niepoprawne i odległe cele. 129 kontroli czasowych obejmuje także nowy scatter, w tym kontakt but–podłoże i powtarzalne timery. Narzędzie odczytu testowego poprawiono, aby R32_UINT miało jeden kanał, nie cztery.

### Odrzucone dalsze próby

[Podsumowanie prób](validation/private-ghost-fast-trials.json) opisuje wektorowe sumowanie FP16, dodatkowe mieszanie RGB FP16 oraz wcześniejsze wyjścia i zastępowanie dwóch pierwiastków w historii jednym lub nierównościami kwadratowymi. Nie wykazały dodatkowej przewagi całej ścieżki uzasadniającej włączenie ich do produkcji; FP16 RGB zmieniało więcej kanałów bez wyraźnego zysku. Kod/PSO tych prób nie trafiają do pakietu ani rutynowego benchmarku. Pozostają pełne obliczenia pierwiastków i oba pierścienie kontaktu.

Cały tor NR+NGX+kompozycja nie został zmierzony w grze. Weryfikacja użytkowa: rozgrzane ABBA opcji 4/5 przy tym samym ruchu, GPU, rozdzielczości i ustawieniach, z timestampami etapu oraz całego toru. Ocenić smugi przy butach, poprawne wygładzanie, HDR i zielony podgląd. Nowy wariant jest eksperymentalny; nie ma deklaracji poprawy jakości w grze ani gwarancji całkowicie niewidocznej różnicy.

## Dalsze uproszczenie matematyki: pełna odpowiedź i jednoczesne wygaszanie

Aktualizacja opcji 5, bez nowego przełącznika i bez zmiany domyślnej metody. Dwa mieszania z tym samym surowym editem są zastąpione jednym:

```
old = lerp(lerp(filteredEdit, rawEdit, rejection), rawEdit, globalRaw)
rawWeight = globalRaw + rejection * (1 - globalRaw)
new = lerp(filteredEdit, rawEdit, rawWeight)
previewWeight = rejection * (1 - globalRaw)
```

Tożsamość zachodzi w liczbach rzeczywistych; FP32/FMA może zmienić zaokrąglenia. Gdy `rawWeight >= 1`, nie jest pobierany ani dekodowany wynik prywatnego DLSS; pozostaje bieżący surowy edit. Maska jest obliczana przed pobraniem koloru. Kontrole głębi, historia, wiek, footprint i miękka ekspansja nie zmieniają się. Zielony podgląd zachowuje dotychczasową wagę, bez doliczania globalnego wygaszenia jako ghostingu.

Specjalizacja jest wybierana dla **pełnej odpowiedzi NR**, również bez wygaszania, oraz dla **editu z dodatnim globalnym udziałem raw**. Zwykły edit bez globalnego fade zachowuje identyczny bajtowo shader z a13e2274, ponieważ próba z dodatkową gałęzią bywała wolniejsza. Metody 1–4, scatter, timery i cała pozostała historia pozostają bajtowo takie jak wcześniej. Jest jeden dodatkowy lazy PSO dla edit+fade, bez dodatkowych dispatchy, tekstur, resetu NR/DLSS czy nowych kluczy INI. Przejście między istniejącymi PSO nie odtwarza instancji NGX. Generator buduje 75 blobów. Użytkownik wskazał, że zwykle używa maskowania i wygaszania osobno; zysk ich kombinacji **nie jest** zyskiem zwykłego maskowania editu.

### Pomiar porównawczy

Referencja **a13e22743e6d7e6324aacc581c64edcc403b4258**, odtwarzana z Git przez `tools/prepare_nr_compact_reference.py`. Powtórzenie: `private_exposure_quality.exe --compact-math-timing <stem> <gain> <globalRawFade>`. RTX 4090, sześć zapisów 001–006, 2496×1404, kolor FP16, depth R32F i MV RG32F, okno 16, fade timera 3, ekspansja 0.5, podgląd off. Gain 1 i 6; globalny raw 0 i 0.35. Zapisany signed wynik jest przekształcany jednolitym dodatnim gain do domeny transportu (tryb edit dodaje 0.5); to syntetyczne wejście kosztu GPU, bez ponownego wykonania NGX. Dla pełnej odpowiedzi proxy jest zerowe. Guides są powtarzane, nie są sekwencją gry.

Po 16 parach rozgrzewki: 64 ABBA, 128 próbek/wariant. Timestampy obejmują cały etap clear/scatter/historia/decode/bariery; upload, alokacje i odczyt CPU są poza oknem. [Pełne podsumowanie z SD, różnicami ABBA i błędem](validation/private-ghost-math-summary.json), surowe CSV `validation/private-ghost-math-g1-b0-001-edit.csv` itd.

| Gain | Global raw | Wejście | Zakres zmiany czasu etapu na sześciu zapisach |
|---|---:|---|---:|
| 1 | 0 | edit | -0.54% do +0.20% oszczędności |
| 1 | 0 | answer | -0.61% do +3.97% oszczędności |
| 1 | 0.35 | edit | +0.56% do +2.64% oszczędności |
| 1 | 0.35 | answer | +3.20% do +5.99% oszczędności |
| 6 | 0 | edit | -0.64% do +0.65% oszczędności |
| 6 | 0 | answer | -0.10% do +4.81% oszczędności |
| 6 | 0.35 | edit | +1.73% do +3.45% oszczędności |
| 6 | 0.35 | answer | +3.28% do +5.46% oszczędności |

Drobne ujemne wyniki i wyniki kontroli ze **tym samym blobem po obu stronach** pokazują rozrzut pomiaru. Nie ma deklaracji przyspieszenia każdego zapisu. Historia i waga maski pozostały bitowo identyczne w 48 porównaniach; bez globalnego fade także RGB było identyczne. Z fade 0.35 maksymalna różnica RGB wyniosła 0.0000610352. To wyniki na dostarczonych danych i FP16 wyjściu, nie gwarancja niewidoczności w każdej grze/HDR.

Testy WARP i RTX 4090 zachowują niezależne referencje masek, HDR, niepoprawnych danych, gain 0.01–1024 i nietypowych siatek. Stress test zawiera teraz globalny udział raw 0, 0.35, 1, NaN, Inf i wartość ujemną; sprawdza też właściwą specjalizację PSO. Test CPU chroni wybór dla skończonego dodatniego fade. Test profili jest wykonywany z aktualnym Config.cpp. Pełny pomiar NR+NGX+kompozycji w grze nie był wykonywany: wymaga rozgrzanego ABBA tej samej sceny, opcji i obu DLL oraz timestampów całego toru.

### Próby celowo zmieniające maskę

[Odrzucone próby i statystyki maski](validation/private-ghost-math-rejected.json) obejmują skrócone wzory gain, wspólną normalizację fade, wagi całkowite, przybliżone normy MV, przedziały głębi, 1/2 tapów, jeden pierścień oraz siatki 3/4 piksele. Samo zmniejszenie liczby działań często nie przyspieszało całego etapu. Nie dołączono niezmierzonych wariantów do rutynowego benchmarku.

Siatka co 4 piksele dawała w początkowych próbach około 20–28% oszczędności etapu, ale zmieniała 11.1%/13.7% pikseli maski w 001/004; ponad połowa skali wagi zmieniała się dla 6.5%/10.7% całego obrazu. Prawie pełne maskowanie (>0.9) stawało się prawie zerowe (<0.1) dla 1.6%/4.7% pikseli. To **nie jest** wyłącznie rozszerzenie lub prawie niewidoczna zmiana. Krok 3 też usuwał część maskowania (2.6%/5.2% pikseli z >0.9 do <0.1). Nie dodano tych wariantów do DLL.

Na życzenie użytkownika zbadano także zmianę wyłącznie wzmacniającą maskę, z miękką krawędzią. Przy zgodności wszystkich czterech głębi uproszczona górna obwiednia wynosi `max_i(age_i * max(4*bilinearWeight_i, minTent_i))`; przy braku zgodności wszystkich bierze maksimum z dopuszczonych głębi. Matematycznie dominuje dotychczasową średnią i ekspansję; nie ma normalizacji przez sumę wag. W 001/004 zwiększała maskę na 4.1%/2.7% pikseli, nigdzie jej nie zmniejszając, oraz zachowywała ułamkowe wagi na brzegu. Zarówno wariant FP32, całkowite wagi parzystej siatki, jak i prostszy bitowy test głębi nie wykazały mierzalnego przyspieszenia względem opcji 5. Samo maksimum czterech wieków też nie dawało przewagi zwykłego editu i usztywniało brzeg. Tych prób nie przedstawiamy jako szybszych metod i nie dodajemy ich do UI. Aktualna miękka krawędź pozostaje.

Wszystkie statystyki jakości powstały w odtwarzaniu zapisanych guides, bez NGX i prawdziwej sekwencji ruchu. Różnica wagi maski nie jest procentowym błędem finalnego obrazu. Zachowane 1–5 wymagają oceny smug i poprawnej stabilizacji w grze; nie ma potwierdzenia, że eksperymenty byłyby niewidoczne gołym okiem.
