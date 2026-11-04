# Rodziny prywatnego DLSS i cykliczny reset przy niskim MV

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


## Eksperymentalne lokalne odrzucanie ghostingu prywatnego DLSS

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
