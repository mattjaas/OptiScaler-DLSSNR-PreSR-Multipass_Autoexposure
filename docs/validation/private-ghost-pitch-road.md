# Kontynuacja maski na jezdni podczas uniesienia kamery

2026-11-06. Baza `0301b61b`. Użytkownik potwierdził, że poprzeczna granica maski pojawia się także z wyłączonym feather, przy ruchu naprzód i lekkim uniesieniu kierunku patrzenia. Pokazał przybliżony kształt granicy na próbce013. Nie traktujemy rysunku jako dokładnej maski ani powtarzania jednej klatki depth/MV jako odtworzenia jej temporalnej historii.

## Reprodukcja i przyczyna

Sam zapis013 opisuje bieżącą głębię i MV. Przed i po DLSS nie zawierają gotowego historycznego licznika maski. Dodany podgląd `--rings-preview <stem> <evaluations> <0..3 layers> <motion gate>` zachowuje te same ograniczenia; używa jawnie N18/fade12/depth.05/coarse1 i unit gain w zwykłym Edit NR. Nie służy do potwierdzania prawdziwej historii gry.

Niezależna sekwencja testowa projektuje poziomą jezdnię kamerą otworkową. Kamera porusza się naprzód i zmienia nachylenie. Przy stałym kierunku patrzenia istniejąca kontynuacja drogi działała; pitch zmienia również nachylenie/przesunięcie płaszczyzny głębi NDC. Dotychczasowy warunek `abs(currentDepthAtOldScreenPosition-oldDepth)<=relativeTolerance` mógł wtedy odrzucić **tę samą** powierzchnię i wyzerować jej trwający licznik. Dopisywanie pierścieni nie naprawia tak utraconej historii; kończy się tam cały dopuszczony obszar.

Przypięte history PSO z0301b61b odtwarzają ten problem:145998 błędnych liczników w72 sekwencjach. Wariant po poprawce:0 błędów dla tego samego oczekiwania. Oczekiwanie pochodzi z jawnej projekcji i widoczności dwóch powierzchni, nie z innej wersji tego samego shadera. To potwierdza reprodukcję konkretnej klasy błędu; nie jest potwierdzeniem rozwiązania każdego odcięcia w grze.

## Zmiana

Zachowujemy dotychczasową ścieżkę kontynuacji, gdy porównanie ekranowej głębi przechodzi. Gdy zawiedzie, a bieżąca głębia i flow nadal tworzą lokalnie ciągłą płaszczyznę, sprawdzamy dodatkową korespondencję z poprzednią klatką. Końcowy lub środkowy punkt jest reprojektowany swoim MV, a jego poprzednia głębia musi zgadzać się ze wspólną lokalną zmianą względem śledzonego piksela. W pobliżu sylwetki oba punkty mogą wpadać w dawnego occludera; wtedy jeden punkt pół kroku **przeciwnie** do MV jest sprawdzany dopiero po potwierdzeniu kontynuacji bieżącej płaszczyzny depth/flow.

Pitch correction ograniczamy do zmian głębi do25% w jedną/d drugą stronę; dopasowanie korespondencji używa połowy skonfigurowanej tolerancji, maksimum.025, oraz istniejącego budżetu kwantyzacji depth. To konserwatywny lokalny test NDC bez macierzy projekcji, nie dowód tożsamości obiektu. Duży obrót, nieciągła nawierzchnia albo niejednoznaczne dane mogą nadal odrzucić kontynuację. Nie znosimy ogólnej ochrony foreground ani nie traktujemy dowolnej płaskiej powierzchni jako tej samej.

Kontrola działa tylko dla **już aktywnego licznika >1**, w istniejącym warunku awaryjnej kontynuacji. Nie inicjuje maski, nie odnawia N, nie wydłuża jej życia, nie resetuje NR/NGX i nie zmienia kryterium tworzenia dysokluzji. Wypływ do bliższej przeszkody jest nadal blokowany w testach. Invalid/NaN/Inf, wyjście poza obraz i niewiarygodny witness odrzucają correction. Krok reprojekcji to dokładnie2 lub4, także przy nieparzystych wymiarach. Głębia pozostaje FP32/compact16mantissa, MV compact pozostajeFP16. Foreground i jego własna historia nadal podlegają wcześniejszej logice.

Dotyczy temporalnych metod3–6, niezależnie od feather/kontrastu/trybu transportu. Nie ma nowego ustawienia, tekstury, dispatchu, readbacku, synchronizacji ani alokacji na klatkę. Używamy już dostępnej historii guides. Dodatkowe odczyty występują tylko na opisanej gałęzi: zwykle jeden poprzedni witness; alternatywny midpoint i reverse sprawdzane dopiero w razie potrzeby. Powiązane blobs history są przebudowane, pozostałe shader/API/ABI i domyślne timery pozostają bez zmian.

## Testy

72 sekwencje:4 ścieżki temporalne, obie konwencje Z, D32/D16/D24, pitch up.01/.025 rad i pitch down.015 rad na klatkę,10 klatek ruchu każda. Kamera320×240,focal180,near/height.015,dz/near8. Oczekiwana ścieżka widoczności uwzględnia rzeczywisty nearest-depth footprint i binary16 MV; wykluczamy bezpośrednią niepewność footprint przy sylwetce i sztucznie domknięty horyzont.454914 aktywnych próbek referencyjnych. Po ruchu osobny nowy bliższy obiekt z MV drogi nie może odziedziczyć licznika; po zatrzymaniu kamery liczniki muszą wygasnąć wN, bez odnawiania. Zliczamy błędy również poza aktywnymi próbkami. Pozostałe testy statycznych krawędzi, rozbieżnego MV, wnętrz008, stałego forward ruchu, nieba, kontrastu, transportu i feather pozostają uruchamiane.

Reprodukcja historyczna wymaga `tools/prepare_nr_compact_reference.py`, który wyciąga3 history blobs z konkretnego commita bez fallbacku do bieżących. `private_exposure_quality --pitch-road-audit` uruchamia niezależne oczekiwanie z przypiętą referencją; `--pitch-road-quality` używa aktualnych PSO i dodatkowych kontroli foreground/expiry. Domyślne testy RTX/WARP obejmują aktualną sekwencję. Nie wnioskujemy o całym zakresie ruchów kamery z tych72 sekwencji.

## Pomiary GPU

RTX4090,2496×1404,raster i edit013,coarse radius1,gain1,N19/fade9,MV gate2.5@1080p,depth.05,bez kontrastu/feather. Cały tor maski clear→scatter→history→dilate→decode, bariery w pomiarze; nie obejmuje NR, NGX, finalnego resolve gry.16par rozgrzewki,64bloki ABBA,128próbek/wariant, osobne historie i niemodyfikowane tablice descriptorów. Wariant A używa history PSO z0301b61b. B używa poprawki. `static` powtarza guides013, `moving` używa niezależnie projektowanej drogi z naprzemiennym pitch.15/.175 i ruchem w przód8*near, na tym samym rastrze i z editem013. Nie jest to replay prawdziwej wieloklatkowej sceny.

| Guides | Przed ms ±SD | Po ms ±SD | Różnica ms |
|---|---:|---:|---:|
|static|0.071344 ±0.001432|0.071048 ±0.002303|-0.000296|
|moving|0.071592 ±0.002726|0.073176 ±0.003309|+0.001584|

Różnica jest mniejsza od zmienności próbek; nie deklarujemy zerowego kosztu ani oszczędności. Większe zachowane pokrycie maski w moving zmienia również obciążenie decode. [CSV/statystyki/pokrycie](private-ghost-pitch-road.json). Odtworzenie: `--pitch-timing <stem> static|moving`. Pomiary innych GPU i jakości w grze nie zostały wykonane.
