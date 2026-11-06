# Szybszy miękki brzeg po kontrastowym wycięciu

2026-11-06; zastępuje pełnorozdzielczą filtrację z `ca52c0e3`. Pozostałe poprawki sky i kontrastu po dodatnim gain bez zmian. Ta optymalizacja świadomie zmienia przybliżenie miękkiego brzegu; nie jest obietnicą identycznego wyniku bit po bicie.

## Algorytm i bezpieczeństwo

1. Maksimum finalnej alpha po kontraście z bloków S×S, `S=clamp(ceil(radius1080*workHeight/1080),1,128)`. Każdy piksel maski jest czytany raz w tym przebiegu; nie ma już pełnorozdzielczego poziomego/pionowego przeszukiwania sąsiadów. Zachowujemy dokładną głębię wybranego oryginalnego piksela w polu R32G32_FLOAT. Jeden odczyt głębi na blok, po wybraniu maksimum. Nieprawidłowe dane nie dodają fringe.
2. Cztery próbki tego pola są sprawdzane na docelowej pełnej głębi przed interpolacją biliniową. Nowa alpha=max(oryginalna,interpolowana), zaokrąglana w górę do FP16; rdzeń pozostaje zachowany. Warunek głębi nadal ogranicza poszerzenie na bliższy occluder. Oryginalna głębia źródła pozostaje razem z maską, więc rozszerzenie nie dziedziczy fałszywej głębi z pośredniego tła.
3. Na urządzeniu z typed UAV load RGBA16F działamy **w miejscu** na carrier. Każdy wątek odczytuje/zapisuje tylko własny piksel; sąsiedzi pochodzą wyłącznie z gotowego osobnego pola. Nie ma wyścigu SRV/UAV, t0 ma proxy. Obszary zerowego pola wcześnie kończą pracę bez odczytu RGB/depth/zapisu, pozostałe bez powiększenia maski nic nie zapisują. Żadnego kopiowania całej tekstury i żadnej dodatkowej tekstury RGBA16F na tym torze. Globalfade i domieszka surowego editu zachowują dotychczasowy wzór.

Obsługę typed UAV load sprawdzamy raz na generację urządzenia po włączeniu opcji. Fallback urządzeń bez tej możliwości używa tego samego algorytmu przestrzennego i osobnego RGBA16F output. To fallback dostępu do zasobu, a nie rezygnacja z FP16. Testy obejmują oba warianty. 53 sloty PSO / 97 blobów / bez zmiany ABI256B. Off nie uruchamia shaderów feather ani kontroli formatu, nie alokuje pól. Po wyłączeniu/dezaktywacji zasoby nadal zwalniane dopiero po zakończeniu list GPU. Brak zmian/resetów NR i NGX.

UI/INI pozostają te same: **Outward-only mask feather**, **Outward mask feather radius (1080p)**. Promień jest teraz przybliżoną skalą bloków; brzeg zmienia kształt względem dawnego ważonego maksimum. S zmienia się skokowo o piksel roboczy, a S=1 pozostawia maskę bez nowego fringe. Wybrany dominujący wzorzec w bloku może zachowawczo blokować ekspansję na innej głębi. Nie jest to segmentacja obiektów. Orzeczenie kontrastu nadal jest wykonane przed polem feather i uwzględnia bieżący gain. Żaden piksel bazowej maski nie jest zmniejszany.

Przy 2496×1404 i radius4 S=6: pole416×234, 778752 bajty (około.743MiB); poprzedni tor wymagał pola R16 pełnej wielkości oraz dodatkowego RGBA16F output (łącznieokoło33.42MiB). Dotyczy aktywnej ścieżki typed load, nie fallbacku.

## Testy i kompromis

288 pełnoobrazowych niezależnych przypadków CPU↔GPU: in-place i osobny output, normal/reversed Z, nieparzyste wymiary i końcowe niepełne bloki, rozmaite promienie, brak/ujemne dane, sky, foreground w obu kierunkach/osiach, NaN, HDR, globalfade. Asercje RGB/alpha i brak erozji rdzenia. Zachowane wszystkie testy sky, statycznych krawędzi, wnętrz, drogi, kontrastu i reszty Superior; WARP/RTX4090. Rebuild blobów, profile/metadata/JSON i v59 pozostają obowiązkowe.

Porównanie pokrycia starego i nowego miękkiego brzegu na danych011: część fringe rośnie, część maleje. W obu pomiarach nie ma utraty silnej maski (> .9 → < .1). To nie walidacja pełnej historii gry ani dowód braku każdego ghostingu; oryginalną maskę zachowujemy niezależnie od przybliżenia nowego brzegu.

## Pomiary

RTX4090, 2496×1404, radius4@1080p, gain1, coarse radius1, kontrast RGB .02/.005, N19/fade9, gate2.02, depth.05. DX12 timestampy obejmują **cały tor maski**, a nie sam shader. 16 par rozgrzewki +64 bloki ABBA, 128 próbek/wariant; osobne historie, niezmienne PSO, niepokrywające się tablice descriptorów. Statyczny przypadek powtarza guides011, ruch to syntetyczny prostokąt i MV tła ±128px. Nie mierzymy tutaj całego NR/NGX ani kompozycji gry.

| Porównanie | Przed ms ±SD | Po ms ±SD | Różnica ms |
|---|---:|---:|---:|
| feather-fast.static | 0.220632 ±0.003469 | 0.155088 ±0.003491 | -0.065544 |
| feather-fast.moving | 0.212312 ±0.002883 | 0.152096 ±0.001498 | -0.060216 |
| feather.static | 0.093256 ±0.002583 | 0.152912 ±0.001748 | +0.059656 |
| feather.moving | 0.092408 ±0.001802 | 0.150584 ±0.001796 | +0.058176 |

`feather-fast`: poprzedni pełny feather vs uproszczony in-place. `feather`: feather Off vs nowy On. Narzut w tych warunkach spada z około .12ms do około .06ms. Sam cały tor maski skraca się o około .06ms (około30%). Nie przenosimy procentu na pełną klatkę ani inne GPU/promienie. [Statystyki i pokrycie](private-ghost-feather-fast.json); CSV obok.

Odtworzenie: `tools/prepare_nr_compact_reference.py` pobiera **przypięte** shadery poprzedniej wersji, bez zastępowania ich bieżącymi. `private_exposure_quality --feather-fast-timing <stem> static|moving` oraz `--feather-timing <stem> static|moving`; pełne testy domyślne i `--warp` obejmują obie ścieżki jakości. Pojedyncza próbka nie przechowuje poprzednich klatek gry.

Dodatkowy wariant dla metody6: [Coarse boundary rings,1–3warstwy o równomiernie malejącym kryciu](private-ghost-coarse-rings.md). Wariant Fast soft reconstruction pozostaje dostępny.
