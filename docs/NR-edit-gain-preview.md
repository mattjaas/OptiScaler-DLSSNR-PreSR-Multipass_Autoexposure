# Podgląd sugerowanego dodatniego mnożnika

Eksperyment z 2026-10-31. W Superior Scaling włącz **Preview suggested positive multiplier**. **Suggested positive multiplier** pokazuje wygładzoną propozycję; **Latest sample** pokazuje ostatni surowy pomiar. Podgląd nigdy nie zmienia `SuperiorPositiveEditGain`, obrazu, flag NGX ani historii prywatnego DLSS. Domyślnie jest wyłączony. Osobne **Automatically apply suggested multiplier** włącza automat opisany poniżej. W Vulkanie i przy niedostępnym Superior obowiązuje istniejące wyszarzenie. Wymagane są edit wokół 0,5, saturation 1x, wyłączone kolorowanie głębi (albo strength 0), nieaktywne oba tryby depth brightness oraz wyłączony eksperyment original guides. Dla nieobsługiwanego wejścia można wyłączyć już aktywny podgląd; włączenie jest wyszarzone.

To propozycja do prób użytkownika, **nie detekcja ghostingu ani dowód osiągnięcia pierwszego dobrego mnożnika**. Kalibracja pochodzi z 16 dostarczonych statycznych zapisów z dwóch gier. Prywatna ekspozycja i inne sceny mogą zmieniać zachowanie DLSS. Nie ma potwierdzenia jakości tego podglądu w grze. Surowe fazy próbkowania w poprzednim [badaniu R/G](NR-edit-rg-mean-study.md) potrafiły zaniżać granicę; EMA ogranicza wahania, ale nie daje gwarancji jakości.

## Pomiar R/G

Pomiar używa `NRanswer - currentProxy` na rzeczywistej aktywnej siatce wejścia prywatnego DLSS, przed ręcznym mnożnikiem i offsetem nośnika. Dzięki temu pokazuje **bezwzględny docelowy mnożnik**, bez sprzężenia z aktualnym suwakiem. Model input brightness wpływa na sam edit i jest uwzględnione przez aktualne dane. Nie rozjaśniamy żadnego ujemnego editu.

Dla dodatniego editu E, `fC(m) = count(m*E_C > epsilon) / count(E_C > 0)`. Cel dotyczy `(fR+fG)/2`, nie luminancji ani średniej samych mnożników:

- epsilon = 0,008791791791791792;
- CNN A/E: 0,9205390681940847;
- transformer K: 0,9723433127521812.

Rodzinę wybiera rzeczywisty preset prywatnego DLSS, niezależnie od presetu NR i DLSS gry. Brak co najmniej 64 dodatnich próbek w każdym z R/G, ponad 5% niepoprawnych editów lub oszacowanie poza mierzalnym 0,01..1024x wstrzymują wynik zamiast pokazywać przypadkową wartość.

Maksymalnie 8 prób/s: 256×144 punktów, rotacja 64 faz 8×8, osobne histogramy dodatniego R i G. Osiem grup po 256 wątków tworzy niezależne części histogramu w **jednym dispatchu**. CPU sumuje małe histogramy, bez drugiego przebiegu GPU. 1024 koszyki mają po 32 przedziały mantysy na wykładnik; dolna krawędź koszyka daje konserwatywną stronę przybliżenia (do 3,125% szerokości koszyka), dodatkowo zaokrągloną w górę do float. Ten margines dotyczy histogramu, nie niepewności ghostingu lub próbkowania.

## Wygładzanie i ruch

**Multiplier preview response** zawiera **Preview EMA time** (domyślnie 1,5 s), **Preview catch-up time** (0,25 s), **Preview minimum motion** (0,1 piksela siatki prywatnego DLSS) i **Require changing motion field** (domyślnie włączone). **Reset multiplier preview** odrzuca poprzednią średnią i bazę MV.

Najpierw normalizujemy R i G oddzielnie, a następnie stosujemy czasową EMA histogramów: alpha = 1-exp(-dt/tau). Kwantyl tej średniej daje propozycję, wygładzaną także EMA w log2 mnożnika. Dwie kolejne istotne zmiany rozkładu i mnożnika uruchamiają krótszą stałą czasu; nadganianie trwa do zbliżenia się do nowego celu. Pojedyncza zmiana nie włącza szybkiego trybu. Przerwy nie sumują czasu do średniej, a ostatnia wartość pozostaje widoczna. Stan po ponad sekundzie bez aktualnego pomiaru jest oznaczany jako wstrzymany.

Kontrola używa 576 stałych punktów (32×18) z dokładnie tego bufora MV, który trafia do prywatnego DLSS, już w jednostkach jego pikseli. Co najmniej 548 próbek musi być poprawnych i co najmniej 6 musi przekroczyć próg ruchu. Przy **Require changing motion field** co najmniej 6 próbek musi też zmienić się o więcej niż 0,0625 piksela względem poprzedniej zakończonej próby. Pierwsza próba tylko ustanawia bazę. Nie żądamy zmiany wszystkich pikseli. Bufor zamrożony na niezerowych MV wstrzymuje pomiar, a niewielki szum nie wystarcza. Bez tego przełącznika wystarczy niezerowy ruch.

Stały poprawny ruch całego płaskiego obrazu również może wstrzymać pomiar. Zmieniające się MV od sceny pod filmem/menu nadal mogą go przepuścić. To praktyczny filtr aktywności, nie rozpoznawanie filmów. Bramka ruchu kończy shader przed odczytami editu i inicjalizacją histogramu, kiedy pomiar jest wstrzymany.

## Cykl życia i koszt

`DlssNr_GainPreview_Dx12.h` ma osobny tracker `GpuLifetime`. Deskryptory, stałe, wejścia i mały obraz wyniku pozostają niezmienne aż zapis gry nie może już zostać odtworzony. Dopiero po rzeczywistym ExecuteCommandLists prywatna lista na tej samej kolejce kopiuje wynik do własnego readbacku; jego fence określa gotowość odczytu, niezależnie od Reset gry. Replay listy gry nie zapisuje readbacku i nie liczy drugi raz tej samej próby. Nie ma oczekiwania CPU na GPU.

Pula ogranicza się do 8 slotów programu i 16 łącznie przy zmianie urządzenia/programu; przy zajętej puli pomiar czeka. Po rozgrzaniu korzysta z istniejących zasobów. Wynik ma 257×18 texeli RGBA32_UINT, readback około 76 KiB ze stride 4352. Części histogramu łączymy na CPU. Utrata urządzenia, wyłączenie obu trybów pomiaru, zmiana rodziny/siatki/kontekstu i shutdown NR/NGX unieważniają stare wyniki; ustawienia pozostają preferencją konfiguracji. Zawieszenie pełnego NR answer/kolorów/hold blokuje też spóźniony wynik asynchroniczny. Tylko widok główny zasila średnią; sloty i fence są stanem sesji, nie kopiowanym stanem per-view. Zmiana ręcznego mnożnika nie resetuje podglądu.

Domyślne Off obu opcji nie tworzy PSO, tekstur, uploadu/readbacku, list ani fence podglądu i nie wysyła jego pracy na GPU. Podgląd i automat współdzielą jeden miernik. Nagrywa on najwyżej osiem nowych prób na sekundę; replay listy gry może ponownie wykonać zapisany dispatch, ale nie duplikuje odczytu ani pomiaru w EMA. Własny fence śledzący zapis i fence odczytu są dodatkowym kosztem kolejki; nie należy utożsamiać poniższych czasów shadera z całym czasem klatki.

Nowe klucze `[DlssNr]`: `SuperiorGainPreview=false`, `SuperiorAutoPositiveEditGain=false`, `SuperiorGainPreviewChangingMotion=true`, `SuperiorGainPreviewSlowSeconds=1.5`, `SuperiorGainPreviewFastSeconds=0.25`, `SuperiorGainPreviewMotionThreshold=0.1`. Deklaracja/default, load/save INI, pola profilu i dokładnie jedna lista kluczy profilu są zsynchronizowane. Wartości niefinitywne mają fallback; czas i ruch są ograniczone, a fast nie przekracza slow. Shader ma własne bezpieczne progi i strażniki wymiarów.

## Weryfikacja

`tools/test_nr_gain_preview.ps1` kompiluje test CPU i test GPU/runtime z rzeczywistym produkcyjnym `DlssNr_GpuLifetime.cpp`. Jedynym stubem jest identyfikacja wrapperów gry: test korzysta z rzeczywistych obiektów D3D12, bez wrapperów. Testy przechodzą na RTX 4090 i WARP. Obejmują:

- kwantyl względem niezależnie sortowanego ważonego rozkładu, nierówne liczby dodatnich R/G, granice pomiaru, padded rows i sumowanie części;
- stabilny sygnał, ograniczoną reakcję na pierwszą zmianę, nadganianie po drugiej, pauzę i reset EMA;
- wszystkie koszyki względem niezależnego CPU na FP32 i FP16, signed edit, cztery fazy, payload MV/serial;
- bazę, stałe/zmienne/zerowe/NaN MV, dokładną granicę 5/6 próbek i tolerancję zmiany, NaN edit, fallbacki i błędne wymiary;
- odczyt przed Reset listy gry, replay bez duplikowania próby, ograniczenie/recykling puli, unieważnianie spóźnionego wyniku.

Osobno przeszły `nr_profiles_smoke` z aktualnym `Config.cpp`, istniejące policy/ABI/shader tests oraz rebuild blobów. Workflow eksperymentu uruchamia nowe testy na WARP przed kompilacją Release DLL. Testy nie uruchamiają modeli DLSS ani nie potwierdzają jakości w grze.

## Pomiar GPU

RTX 4090, rezydentne proceduralne tekstury FP16 proxy/answer i FP32 MV, saturation 1/depth colour off. To test kosztu nowego etapu, bez NR/NGX, final composition, uploadów wejść i alokacji. Timestamp obejmuje przejścia wyniku COPY_SOURCE→UAV→COPY_SOURCE, dispatch i CopyTextureRegion do readbacku; osobne sygnały i opóźnienie kolejkowania gry są poza oknem. Wersja ruchowa używa pełnych 36864 próbek. Kontrola ruchu używa niezmienionych wejść i progu 10 px, wymuszając szybkie wyjście.

Każde porównanie: osobna rozgrzewka 16 okien na wariant, potem 16 kwartetów ABBA, czyli 32 próbki na wariant. Średnia ± odchylenie standardowe, w ms:

| Siatka | A | B | A ms | B ms | B−A ms |
|---|---|---|---:|---:|---:|
| 2240×1264 | Off | Tylko MV | 0,000608 ± 0,000503 | 0,013037 ± 0,000344 | +0,012429 |
| 2240×1264 | Off | Pełny pomiar | 0,000480 ± 0,000511 | 0,027517 ± 0,000315 | +0,027037 |
| 2240×1264 | Pierwotna 1 grupa | Produkcyjne 8 grup | 0,117490 ± 0,002062 | 0,027581 ± 0,000382 | −0,089909 |
| 2496×1404 | Off | Tylko MV | 0,000608 ± 0,000503 | 0,012976 ± 0,000376 | +0,012368 |
| 2496×1404 | Off | Pełny pomiar | 0,000576 ± 0,000508 | 0,027283 ± 0,000338 | +0,026707 |
| 2496×1404 | Pierwotna 1 grupa | Produkcyjne 8 grup | 0,115267 ± 0,001868 | 0,027266 ± 0,000464 | −0,088001 |

[Surowe okna](validation/nr-gain-preview-timing.csv). `tools/test_nr_gain_preview.ps1 -Timing` odtwarza procedurę i kompiluje wariant jednej grupy z tego samego HLSL tylko do artefaktów testowych; nie jest to runtime opcja renderowania. Przy 8 próbach/s i 60 FPS arytmetyczne rozłożenie 0,0275 ms na wszystkie klatki daje około 0,0037 ms/klatkę, **nie jest to pomiar gry**. W grze należy porównać podgląd Off/On w ABBA po rozgrzewce, przy tej samej scenie, siatce, ruchu i ustawieniach, mierząc cały tor oraz okna z próbą i bez próby. Nie wykonano takiego pomiaru ani obserwacji ghostingu w tej sesji.


## Dalsza kalibracja przez zapisy użytkownika

**Save pre-DLSS edit** zapisuje teraz DDS i JSON schema 2. **Save edit + object guides** dodaje jeszcze depth/MV. `positive_gain` to faktyczny mnożnik zapisanego editu; `preview_request_suggested_gain` to propozycja widoczna przy kliknięciu, a `preview_capture_suggested_gain` to ostatnia zakończona propozycja w chwili nagrania następnej klatki NR. Każda sugestia ma wiek, ważność, stan pauzy, surowy wynik i liczbę pomiarów; JSON zawiera też opóźnienie od kliknięcia, preset oraz konfigurację podglądu. Brak pomiaru oznacza null.

Przykład: `positive_gain=2.5`, `preview_request_suggested_gain=3` plus opis użytkownika „2,5x już bez ghostingu” pozwala sprawdzić nadwyżkę propozycji. Pozytywny edit przed mnożnikiem odtwarzamy przez podział dodatnich kanałów DDS przez 2,5; ujemnych nie dzielimy. Potrzebne są DDS i odpowiadający JSON oraz informacja, czy był ghosting. Przy próbach tej samej sceny warto zachować podobny ruch i poczekać na ustabilizowanie historii DLSS po zmianie mnożnika. Sam zapis wejścia nie rozstrzyga o ghostingu, a wiek pomiaru pozwala odróżnić starszą sugestię od konkretnej klatki. Dopisanie JSON do zwykłego przycisku nie dodaje pracy GPU.


## Automatyczne stosowanie mnożnika

**Automatically apply suggested multiplier** (`SuperiorAutoPositiveEditGain=false` domyślnie) stosuje wygładzoną sugestię do głównego widoku, niezależnie od checkboxa samego podglądu. **Applied positive multiplier** pokazuje rzeczywiście wybraną wartość, obok niezmienionej sugestii. Automat używa zakresu **1..1024x**: również rozgrzewka, brak pomiaru, nieobsługiwane wejście i dodatkowe widoki nigdy nie schodzą poniżej 1x przy włączonej opcji. Ręczne 0,01..1024x pozostaje dostępne po jej wyłączeniu.

Pierwsze przejęcie następuje po co najmniej czterech prawidłowych pomiarach w ruchu. Później nowy pomiar zmienia wartość, gdy sugestia wzrosła co najmniej o 2% lub spadła o co najmniej współczynnik 1,05; między rzeczywistymi zmianami automatu mija co najmniej 0,5 s. Brak ruchu, niezmienne MV, hold, brak wystarczającego sygnału, wynik niefinitywny lub starszy niż 1 s zatrzymują regulację na ostatniej wartości. Ta martwa strefa oznacza, że Applied może trochę różnić się od Suggested.

Przed przejęciem używana jest zapisana wartość ręczna, ograniczona od dołu do 1x. Pełna odpowiedź NR, saturation różne od 1x lub aktywne kolorowanie głębi wyłączają sterowanie i wracają do `max(1, manual)`. W takim wejściu włączenie Auto jest wyszarzone, ale można wyłączyć już zaznaczoną opcję. Zwykły suwak jest wyszarzony, gdy Auto jest włączone dla obsługiwanego wejścia. Wyłączenie Auto przywraca zapisaną ręczną wartość bez jej modyfikowania, również gdy była mniejsza niż 1x. Automat nie zapisuje każdego wyniku do konfiguracji ani profili.

Sterownik jest stanem sesji dla głównego widoku. Dodatkowe widoki nie używają cudzej sugestii: zostają przy ręcznym mnożniku z minimum 1x, gdy Auto jest włączone. Zmiana urządzenia lub kontekstu (siatka koloru, preset, ustawienia prywatnej ekspozycji/HDR, packed/pre-SR) odrzuca poprzedni stan sterowania. Przed wyborem sprawdzamy, czy sugestia należy do tego samego kontekstu; odczyt ze starej siatki nie może zostać użyty tylko dlatego, że nowy pomiar nie został jeszcze nagrany. Shutdown NR i NGX jawnie zerują sterownik. Reset samego miernika utrzymuje ostatnio zastosowaną wartość do zebrania nowych czterech pomiarów.

Jedna lokalna wartość `gain` trafia do obu ścieżek przygotowania nośnika, dekodowania po DLSS i zapisu editu. Dlatego odwrotność używa dokładnie mnożnika tej klatki. Pomiar nadal czyta surowe `answer - proxy`, przed automatycznym i ręcznym mnożeniem. Nie ma sprzężenia „sugerował 2x → ustawione 2x → teraz sugeruje 1x”.

Każda faktycznie zastosowana zmiana nadal resetuje historię prywatnego DLSS i historii meteringu jego ekspozycji, tak jak przy ręcznym suwaku. Nie przeskalowujemy nieudokumentowanej wewnętrznej historii NGX i nie mieszamy historii z różnych nośników. EMA, histereza i limit zmian ograniczają te resety; sam odczyt/przesunięcie sugestii w martwej strefie ich nie powoduje. To eksperyment jakościowy, nie potwierdzenie braku ghostingu ani stabilności w grze.

JSON nadal zapisuje `positive_gain` rzeczywiście użyte do editu. Dodano `auto_positive_gain_enabled_at_request`, `auto_positive_gain_enabled_at_capture`, `auto_positive_gain_controlled_capture`, `manual_positive_gain`, `effective_positive_gain_at_request` i `auto_positive_gain_changes_primary`. Dotychczasowe `positive_gain_at_request` nadal opisuje zapisaną wartość ręczną; effective rozróżnia ją od automatu. Dzięki temu DDS z 2,5x i sugestią 3x można opisać także przy aktywnej regulacji.

Dodatek nie zmienia HLSL ani blobów, nie dodaje dispatchu, kopii, synchronizacji czy alokacji GPU ponad dotychczasowy miernik. Sterowanie jest na CPU. Dotychczasowy pomiar GPU powyżej dotyczy wspólnego miernika; wpływu częstszych resetów NGX na cały tor i jakość w grze nie zmierzono. W tej iteracji lokalnie uruchomiono wyłącznie kontrole CPU: sterownik (limit 1x, rozgrzewka, histereza, cooldown, pauza, stary/NaN wynik, konteksty i powrót do manual), JSON i profile. Workflow powtarza je oraz regresję runtime na WARP serwera CI przed buildem Release. Test z 60 wywołaniami/s i aktualizacją pomiaru co osiem wywołań potwierdza brak zmian przy małym szumie oraz odstęp >=0,5 s przy dużych naprzemiennych sugestiach. Nie jest to pomiar GPU ani test sceny gry.


## Niezależny przypadek: niedoszacowanie w nowej scenie

[Audyt nowego zapisu Spider-Man 2 z transformerem](NR-edit-transformer-plateau-audit.md): UI, pełny obraz i wszystkie 64 fazy zgodnie sugerują 2,250699x, ale użytkownik obserwuje poprawę do około 6x, z pozostającym ghostingiem i bez dalszej poprawy powyżej. Tego punktu nie etykietujemy jako ghost-free. To kontrprzykład dla traktowania obecnego S_RG jako wystarczającego do doboru mnożnika: nie naprawi go dłuższa EMA ani gęstsze próbkowanie. Audyt pokazuje również małą różnicę na granicy FP16 oraz silnie różne wyniki masek depth/MV. Podniesienie globalnego celu pod tę klatkę dawałoby 2,25..20,73 razy pierwsze dobre mnożniki na starych zapisach transformera. Progi DLL pozostały niezmienione; obecny Auto jest w tej scenie niedoszacowany względem obserwowanych korzyści.

Nowe [tryby jasności według głębi](Private-DLSS-depth-brightness.md) wstrzymują podgląd/Auto, ponieważ kalibracja dotyczy jednolitego mnożnika. Przełącznik original guides także wstrzymuje pomiar: bramka ruchu nie może twierdzić, że bada rzeczywiste wejście MV NGX podczas użycia innej siatki. Ręczny gain pozostaje dostępny; zapis edit-only uwzględnia przestrzenny gain i dołącza matched depth do odtworzenia wagi.
