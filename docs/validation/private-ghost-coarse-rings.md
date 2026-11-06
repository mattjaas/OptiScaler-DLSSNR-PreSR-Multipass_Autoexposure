# Dodatkowy wariant: warstwy punktów coarse na granicy maski

2026-11-06. Rozszerzenie `9f7a537c`; zachowuje **Fast soft reconstruction** oraz poprawki sky/kontrastu po dodatnim gain. Nie zastępuje metody wykrywania dysokluzji. Domyślny wariant pozostaje0; cały feather domyślnie Off.

## Obsługa

Dla **Ghost rejection method 6** włącz **Outward-only mask feather**, wybierz **Outward mask feather method → Coarse boundary rings (method 6)** i ustaw **Added coarse boundary layers** na1–3. Ogólny promień w px jest wtedy wyszarzony; ten wariant liczy punkty istniejącej siatki coarse (jeden na4×4 piksele NR). Poza metodą6 wybór rings jest niedostępny, a runtime używa Fast soft reconstruction. Powrót do6 przywraca zapisaną preferencję rings. Nie zmienia to preferencji presetu ani historii NR/NGX.

| Dodatkowe warstwy | Krycie kolejnych warstw względem punktu granicznego |
|---|---|
|1|50%|
|2|66.67%, 33.33%|
|3|75%, 50%, 25%|

Brzeg definiuje dodatni punkt coarse mający co najmniej jednego zerowego sąsiada w ośmiu kierunkach. Dodatkowe warstwy są kwadratowymi pierścieniami (odległość Chebysheva), a krycie=alpha źródła*(N+1-d)/(N+1). Bierzemy maksimum, nie sumę nakładających się pierścieni. Krycie pochodzi wyłącznie z rzeczywistych punktów granicznych; mocniejsze wnętrze maski nie może podnieść nowego krycia ponad wzór. Obejmuje zewnętrzną granicę oraz dziury po kontrastowym wycięciu.

Wejściem jest alpha już po anulowaniu kontrastem, z uwzględnieniem bieżącego dodatniego mnożnika obu editów. Redukcja4×4 bierze maksimum tej finalnej alpha i oryginalną głębię tego punktu. Wybór jest blokowy: nie rekonstruuje wcześniejszego konturu subpixel wewnątrz jednej komórki. Zachowujemy oryginalną pełnorozdzielczą maskę przez max. Nowe warstwy są schodkowe, ze zredukowanym kryciem; nie jest to ciągłe rozmycie Gaussa. Wybranie Fast soft reconstruction wraca do poprzedniego miękkiego brzegu.

Głębia źródła pozostaje razem z polem. Końcowy point lookup komórki4×4 dodaje warstwę tylko na tej samej/dalszej powierzchni w ramach istniejącej tolerancji depth i kwantyzacji; wyraźnie bliższy occluder blokuje nową ekspansję. Nie usuwamy istniejącego rdzenia. Ochrona jest przybliżeniem na NDC; brak macierzy projekcji uniemożliwia dokładną segmentację obiektów. Dominujący wzorzec w mieszanej komórce może zachowawczo odrzucić fringe dla innej głębi. Niepoprawne depth/MV nadal nie stanowią dowodu dysokluzji; niepoprawne depth nie dodaje nowych warstw.

## GPU i stan

Dwa przebiegi: redukcja i obliczenie pierścieni naquarter grid, potem domieszanie tylko zmienianych pikseli na pełnym rastrze. Redukcja/pierścienie używają wspólnego kafla i halo N+1; jedna dodatkowa komórka halo służy identyfikacji rzeczywistej granicy. RingTile256×float2 +RingBoundary256×uint=3072B LDS na grupę8×8. Wątki poza obrazem uczestniczą w obu barierach przed wyjściem; pola/krawędzie obrazu sprawdzamy przed odczytem. Nie ma odczytu sąsiednich UAV output. Zasady typed-load/fallback, wspólnego globalfade, domieszki raw i odroczonego życia zasobów jak w [szybszym feather](private-ghost-feather-fast.md).

Osobne opcjonalne PSO 57/58/59: rings field / osobny output / in-place. Domyślne soft PSO zachowują swoją matematykę, bez warunku runtime rings. 56 wspólnych slotów PSO, 100 blobów, ABI256B. Jednorazowe sprawdzenie typed UAV load na urządzeniu; dla rings tylko pole624×351 R32G32F przy2496×1404 (około1.67MiB) na głównej ścieżce. Fallback dodatkowo potrzebuje RGBA16F output. Opcja Off nadal nie dodaje shaderów ani zasobów feather.

Nowe klucze `[DlssNr] SuperiorGhostOutwardMethod` (0soft,1rings) i `SuperiorGhostOutwardLayers` (1–3,default1). Deklaracje/default, odczyt/zapis, profile/reset i kKeptKeys sprawdzone. JSON dodaje `ghost_outward_feather_method` oraz `ghost_outward_feather_layers`. Nieznany wariant wraca do0, liczba warstw jest ograniczana1–3, nieobsługiwana metoda dysokluzji używa0. Nazwany setter i asercje ABI obejmują licznik warstw w Passthrough/byte32. Nie zmieniamy frozen v59 ani resetów NR/NGX.

## Walidacja

252 niezależne przypadki na GPU/WARP: 1/2/3 warstwy, in-place i osobny output, obie konwencje Z, trzy globalfade, pojedynczy punkt, obszar z dziurą po kontraście, słaby brzeg przy mocnym wnętrzu, foreground z obu stron, nieprawidłowa głębia oraz niepełne ostatnie bloki/halo grup. Oczekiwanie CPU to brute-force odległości Chebysheva do wszystkich oryginalnych punktów granicznych, niezależne od kafla LDS. Asercje obejmują każdy piksel RGB/alpha i brak erozji rdzenia. Zachowane288 przypadków soft feather,24 sky,688 kontrastu i cały dotychczasowy zestaw maski/Superior. Rebuild100 blobów, v59/ABI/embedded, świeże profile smoke z aktualnym Config.cpp i JSON/metadata.

## Pomiary

RTX4090,2496×1404, ustawienia jak w JSON. Depth/MV to syntetyczny nieruchomy prostokąt z tłem poruszającym się ±128px na rastrze011; edit pochodzi z próbki011. Kontrast RGB jest włączony po obu stronach porównania. Cały tor maski od clear/scatter do decode/feather, z barierami, DX12 timestamp. 16par rozgrzewki +64bloki ABBA,128próbek na wariant; osobne historie i descriptor tables, PSO przed pomiarem. Nie obejmuje całego NR/NGX ani kompozycji gry.

| Warstwy | Feather Off ms ±SD | Rings On ms ±SD | Narzut ms |
|---|---:|---:|---:|
|1|0.092152 ±0.002490|0.158032 ±0.004142|+0.065880|
|2|0.090096 ±0.003735|0.169728 ±0.004116|+0.079632|
|3|0.091928 ±0.001030|0.184584 ±0.003299|+0.092656|

Wariant daje bezpośrednią kontrolę liczby punktów i spadku krycia. Nie deklarujemy, że będzie szybszy od Fast soft reconstruction: tutaj1warstwa ma podobny narzut, a2–3kosztują więcej. Poprzednie pomiary soft około.06ms są innym przebiegiem ABBA, nie jednoczesnym rankingiem obu algorytmów. Zmieniony kształt maski to świadomy kompromis. Samo pokrycie maski nie jest precision/recall ghostingu ani potwierdzeniem działania w grze. [Statystyki i pokrycie](private-ghost-coarse-rings.json); CSV obok.

Odtworzenie: pełny `private_exposure_quality` / `--warp`, grupa `--sky-feather-quality[-warp]`; pomiar `--rings-timing <capture stem> moving <1|2|3>`.

Kontynuacja licznika przy jednoczesnym ruchu kamery w górę: [poprawka niezależna od feather, reprodukcja i pomiary](private-ghost-pitch-road.md).
