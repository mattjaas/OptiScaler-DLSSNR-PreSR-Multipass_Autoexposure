# Znikająca maska ziemi podczas biegu — próbka 009

2026-11-05. Baza `95dbc4810f9a716bc642ec48b1698bb974834089`. Dotyczy aktualizacji historii metod 3–6, z kontrastem i bez. Bez zmian ustawień, N/fade, NR ani NGX.

## Dane i odtworzenie

Użytkownik zgłosił dziurę za postacią podczas szybkiego biegu: postać pozostawała w podobnym miejscu ekranu, a ziemia szybko przesuwała się pod nią. Zdjęcie i DDS nie przedstawiają tej samej klatki. Przed DLSS zapisany edit ma pojedynczą sylwetkę; po DLSS ma kilka przesuniętych kopii. `.after.dds` jest przed odrzucaniem ghostingu i nie zawiera faktycznej maski. Nie odtwarzamy dokładnej historii gry z jednego zapisu.

Próbka 009: Transformer K, NR 2496×1404, edit wokół .5/gain 1, metoda 6, N18/fade12, depth .05, MV gate 2 px@1080p, radius 1, reversed Z, **kontrast wyłączony**. Do porównania jakości zastosowano produkcyjne shadery maski do 19 powtórzeń zapisanej pary depth/MV; obraz raw i after pozostaje ten sam. Użytkownik potwierdził, że ta przybliżona symulacja bardzo dobrze odtwarza brak pokrycia z gry. To potwierdzenie problemu, nie walidacja poprawki w grze.

Pole drogi ma prawie liniową głębię NDC w osi Y. Dopasowanie kontrolnego pasa na prawo od postaci daje slope 0.00003302467218 na piksel NR i intercept -0.01457056963. W analizowanej części drogi porównanie tej samej powtarzanej siatki po reprojekcji daje medianę current/previous near-depth 1.06269, P90 1.08040 i P99 1.08563. To przekracza zwykłe 5% rozdzielenia głębi, mimo braku nowego obiektu. W 11472 z 15608 próbek tej diagnostycznej strefy spełniony jest test „current nearer”; w 9191 różnica MV między klatkami przekracza dotychczasowy szybki próg. Te liczby diagnozują niejednoznaczność, nie oznaczają zapisanej historii widoczności.

## Przyczyna i poprawka

Dotychczasowy licznik mógł zostać skasowany, gdy current NDC depth był bliższy od previous NDC depth albo gdy po reprojekcji zmieniała się prędkość ruchu tła. Przy kamerze jadącej do przodu obydwa zjawiska występują na tej samej płaszczyźnie drogi. Wcześniejsze testy szerokiego odsłonięcia miały stałą głębię i stałe MV tła, więc nie wykrywały tego problemu.

Dodano warunkową kontynuację **już aktywnego** licznika. Działa tylko przy dotychczasowym konflikcie głębi/ruchu i wieku >1. W bieżącej klatce sprawdza punkt końcowy wektora MV oraz jego środek:

- oba punkty muszą mieścić się w obrazie i mieć poprawne depth/MV;
- głębia punktu końcowego musi pasować do poprzedniej powierzchni w tej pozycji ekranu, z dotychczasowym marginesem kwantyzacji;
- depth w środku musi pasować do interpolacji depth obu końców (głębia NDC płaszczyzny jest afiniczna); margines to min(depthThreshold/4, 0.0125) plus precision;
- MV w środku musi pasować do średniej MV obu końców z progiem 2× istniejącego motion gate, sprawdzanym przez iloczyn skalarny, bez pierwiastka.

Gdy test przechodzi, wiek wynosi min(oldAge,N)-1. **Nie uruchamia ani nie odnawia okna**. Nowe odsłonięcia nadal wymagają wcześniejszego zasłonięcia i sprzecznych warstw MV, jak po poprawkach 007/008. Nowy bliższy obiekt nie powinien przejmować licznika tła: głębia na końcu lub w środku albo nieciągły ruch odrzucą taką kontynuację. Kontrast korzysta z przeniesionego oryginalnego occludera jak wcześniej.

To konserwatywna heurystyka ciągłej, lokalnie płaskiej powierzchni, bez identyfikatorów obiektów ani macierzy kamery. Krzywizna, ostre uskoki depth, niepoprawne MV, obrót zmieniający głębię całego widoku lub niedokładność w pobliżu sylwetki mogą odrzucić kontynuację. Zbieżne głębie/ruchy różnych powierzchni pozostają niejednoznaczne. Nie gwarantujemy idealnej widoczności ani zamaskowania smug obecnych poza długością N.

## Weryfikacja poprawności

Niezależna sekwencja z kamery otworkowej jadącej do przodu nad płaszczyzną: near/Z=.00025×(y+.5−16), dzCamera/near=6. Reprojekcja skaluje odległość od principal point przez 1/(1+6×nearDepth). Foreground porusza się razem z kamerą. CPU wyznacza widoczność i każdy oczekiwany licznik z geometrii, bez użycia wyniku shadera. Uwzględniono rzeczywistą kwantyzację binary16 MV (truncation), aby referencja nie używała innej siatki reprojekcji.

24 sekwencje: wszystkie cztery metody, obie konwencje Z i D32/D16/D24. Przez 12 klatek sprawdzamy 496272 aktywne próbki referencji i próbki, które mają pozostać bez maski. Z kontroli ilościowej wyłączone są sky oraz pas jednego texela historii przy czterech granicach testowego foreground. Baza ma 375318 niepoprawnych liczników; poprawka ma **0**. Osobne asercje chronią nowy bliższy obiekt z przypisanym MV drogi i sprawdzają wygaśnięcie po zatrzymaniu, bez odnawiania ponad N.

Pozostają istniejące testy nieruchomych krawędzi, przeciwnego/jednostajnego ruchu, przyspieszenia kamery, pełnych wnętrz odsłonięcia, stóp, N/fade, resetów, kontrastu, transportu i kompozycji. Powtarzana próbka 007 ma nadal zerowe liczniki przy zapisanych oraz zerowych MV, na obu siatkach historii. Testy nie zastępują sprawdzenia nowej DLL podczas biegu w grze.

W symulowanym wycinku 009 x=360..1099/y=650..1403 liczba pikseli maski >0.5 wzrasta z 99921 do 216560. 113684 piksele przechodzą z <0.1 do >0.5. Maska nie maleje w tym porównaniu. To odzyskany zakres maski, nie miara skuteczności rozpoznawania ghostingu. [Metadane i hashe](private-ghost-road-009.json).

## Zakres kosztu i pomiary

Zgodnie z poleceniem użytkownika **nie wykonano testów wydajności**. Uruchomienia GPU służą wyłącznie asercjom poprawności i wizualizacji, bez odczytów timestampów. Planowany ABBA na zapisach 007/008/009 i kontrolowanej drodze został pominięty. Nie podajemy zysku ani narzutu w ms.

Z przeglądu kodu: bez nowego dispatchu, tekstur, kopii, alokacji i barier; najwyżej dwa dodatkowe odczyty depth i dwa MV dla konfliktowej aktywnej próbki, z wcześniejszym wyjściem przy błędzie. Koszt instrukcji i odczytów oraz większy shader aktualizacji może wpłynąć na czas GPU. To opis implementacji, nie wynik pomiaru. Zmieniają się tylko trzy bloby aktualizacji; pozostałe 91 pozostaje identyczne. ABI i formaty stanu pozostają bez zmian.

Odtworzenie: `python tools/prepare_nr_compact_reference.py`, kompilacja `tests/nr_gpu/private_exposure_quality.cpp`, `--road-baseline`, `--road-quality`. Domyślny zestaw i `--warp` również obejmują test. `--frozen-visibility-baseline <stem> 19` i `--frozen-visibility-preview <stem> 19` zapisują przybliżenie RGBA32F: signed edit po masce i maskę w alpha. Te dwa polecenia są diagnostyką ustawień 009 (N18/fade12, metoda6/radius1, depth .05, motion gate2, reversed Z, gain1, bez kontrastu), nie ogólnym odtwarzaczem JSON. Podgląd PNG: neutralny szary .5, signed edit ×4; nie jest to kolor finalnego obrazu gry.
