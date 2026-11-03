# Rodziny prywatnego DLSS i cykliczny reset przy niskim MV

Aktualizacja 2026-10-31. W **DLSS to temporally stabilize NR** dostępne są CNN, Transformer oraz **Transformer 2-gen (L/M)**. Ta ostatnia ma **Private Transformer 2-gen preset override**: Automatic M, L, M. Numery L=12 i M=13 są kontrolowane względem SDK. Wskazówki presetów dotyczą prywatnego SR/DLAA; sukces tworzenia NGX nie potwierdza faktycznie użytego wewnętrznego modelu. Jawny override nie przechodzi po błędzie na Default. Automatic zachowuje istniejący fallback. Zmiana rodziny/override odtwarza prywatną instancję przez dotychczasowy mechanizm bezpiecznego zwalniania GPU. Wybór jest zachowany podczas DRS, a latch instancji czyszczony podczas shutdown NR/NGX.

## Osobny cel kontrastu

**Depth-boundary contrast target** i **Exact boundary contrast target** zapisują wartość tylko dla wybranej rodziny: CNN (A/C/D/E), Transformer (J/K) lub Transformer 2-gen (L/M). Przełączenie przywraca jej własny cel. Nie ma nowych kalibracji jakości dla L/M: początkowa wartość to dotychczasowe 0,1429128154. Stary niestandardowy `SuperiorGainPreviewBoundaryTarget` jest fallbackiem dla każdej jeszcze nieustawionej rodziny. Ustawienie jednej nie nadpisuje pozostałych. Metoda coverage zachowuje dotychczasową klasyfikację CNN/Transformer; L/M używają dotychczasowej kalibracji Transformer bez potwierdzenia jakości w grze.

## Ciągłość historii podczas Auto

Automatyczne zmiany dodatniego mnożnika nie ustawiają Reset prywatnego NGX ani nie zerują historii meteringu ekspozycji. Dotyczy to również rozgrzewki i automatycznego minimum 1×. Ręczna zmiana, powrót do ręcznego mnożnika po wyłączeniu Auto i inne zmiany transportu zachowują właściwe resety. Pozostałe przyczyny resetu (np. zmiana instancji lub reset gry) nadal obowiązują. Nie przeskalowujemy nieudokumentowanej historii NGX. Test CPU obejmuje 200 kolejnych automatycznych zmian i oddzielnie reset przy manual/zmianie transformacji; jakość temporalna wymaga testu gry.

## Powtarzany reset przy małym ruchu

Opcja **Reset private DLSS after sustained low motion** jest domyślnie wyłączona i niezależna od Auto/preview. **Low-motion reset MV threshold** ustala próg maksymalnej długości próbkowanego MV w pikselach roboczych na klatkę (domyślnie 0,25); **Low-motion reset delay** czas (domyślnie 2 s). Dokładne pola pozwalają na 0,001..64 px oraz 0,05..60 s.

Gdy wszystkie 576 punktów stałej siatki 32×18 są poprawne i poniżej progu przez zadany czas, resetowana jest prywatna historia DLSS oraz meteringu ekspozycji. Licznik czasu od razu zaczyna kolejny okres. Przy ciągle małym ruchu reset powtarza się co zadany okres, z dokładnością częstotliwości próbkowania i opóźnienia odczytu. Nie wymaga wcześniejszego wzrostu MV. Ruch powyżej progu zeruje odliczanie. Hold, zewnętrzny reset, zmiana kontekstu, niepoprawne dane lub brak świeżego wyniku przez 0,5 s również przerywają okres. Wielokrotne odczytanie tego samego wyniku nie przyspiesza zegara. W UI widać maksymalne MV, bieżący czas i liczbę żądań resetu. NR oraz DLSS gry nie dostają tego resetu.

Jest to próbkowany warunek spokoju: drobny ruch może ominąć punkty siatki. Nie jest detektorem ghostingu ani filmików. Nie wymaga zmiany MV między klatkami; poprawny stały ruch nadal może przekraczać próg.

## Koszt i czas życia GPU

Jeśli aktywny miernik coverage lub boundary z globalnym MV gate już dostarcza 576 wektorów, na CPU obliczamy ich maksimum; nie dodajemy kolejnego dispatchu/odczytu. W pozostałych przypadkach opcja uruchamia małą niezależną sondę MV najwyżej 8 razy/s: jeden dispatch 256 wątków, 576 odczytów MV i wynik 2×1 RGBA32_UINT (32 B danych, readback z wyrównaniem D3D12). Sonda korzysta z asynchronicznego runtime/puli oraz odroczonego zwalniania; bez oczekiwania CPU na GPU. Czyta rzeczywiste MV prywatnego DLSS, z uwzględnieniem subrect/origin i skali jednostek. Nie wykonuje dodatkowego przeskalowania pełnego obrazu. Po rozgrzaniu używa puli; wyłączona opcja nie tworzy PSO ani zasobów sondy.

Pomiar syntetyczny, NVIDIA GeForce RTX 4090, D3D12, syntetyczne MV, Off vs osobna sonda, 16 par rozgrzewki, następnie 16 ABBA / 32 próbki na wariant, GPU timestamps obejmujące dispatch, bariery i kopię wyniku:

| Rozdzielczość | Off średnia ± SD ms | Sonda średnia ± SD ms | Różnica ms |
|---|---:|---:|---:|
| 2240×1264 | 0,000416 ± 0,000503 | 0,006441 ± 0,000281 | +0,006025 |
| 2496×1404 | 0,000672 ± 0,000486 | 0,006379 ± 0,000286 | +0,005707 |

[Surowe próbki](validation/private-low-motion-watch-timing.csv). To koszt pojedynczego pomiaru w osobnym harnessie, bez NGX i pozostałych etapów gry; nie dowodzi kosztu całego toru. Shared probe nie dodaje pracy GPU do istniejącego miernika. Weryfikacja całego toru: ta sama scena, rozdzielczość i preset, rozgrzane ABBA Off/On oraz GPU timestamps NR+DLSS+composition. Nie uruchomiono testu NGX w grze dla tych zmian.

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

**No DLSS** zachowuje dopasowanie guides, korektę między przebiegami i końcową rekonstrukcję Superior, również na skompresowanej siatce. Dla rekonstrukcji kierowanej istniejący shader wylicza podpisany edit `odpowiedź NR − proxy` bezpośrednio na siatce roboczej z mnożnikiem 1×. Prywatny SR/DLAA oraz jego ustawienia transportu i pomiarów są wtedy pomijane. **Keep NR history when switching private DLSS** (`SuperiorKeepNrHistory`, domyślnie wyłączone) zachowuje historię modelu NR przy zmianie prywatnego presetu lub przejściu do/z No DLSS. Nowa instancja prywatnego DLSS zaczyna z własną świeżą historią. Zmiana rozdzielczości modelu NR i resety zgłaszane przez grę nadal obowiązują. Włączenie prywatnego DLSS nadal może pominąć klatkę NR w oczekiwaniu na zatwierdzenie utworzenia instancji.

Na RTX 4090 zmierzono izolowany dispatch ekstrakcji editu do FP16 wraz z barierami, względem pustego okna timestamp. Po 64 próbach rozgrzewki na wariant zebrano 128 próbek na wariant w kolejności ABBA. Przy 2496×1404 koszt wyniósł 0,0289 ± 0,0005 ms wobec 0,0001 ± 0,0003 ms, przy 3840×2160 0,1038 ± 0,0287 ms wobec 0,0002 ± 0,0004 ms (średnia ± SD). Test syntetyczny nie obejmuje zmiany całego toru, w szczególności korekty między przebiegami i resolve. Pomiar można powtórzyć przez `private_exposure_quality.exe --extract-timing`. W grze potrzebny jest osobny pomiar NR i rekonstrukcji przy tych samych ustawieniach, po rozgrzewce i z tym samym ruchem kamery.
