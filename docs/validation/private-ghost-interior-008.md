# Odsłonięte wnętrza sylwetek — próbka 008

2026-11-05. Baza `b9924012a82022e4424931019d6ef6bccba54ace` (poprawione nieruchome krawędzie 007). Zmiana wspólnej polityki metod temporalnych 3–6; bez zmiany metod 1–2, formatu historii, ustawień i liczby dispatchy.

## Przyczyna

Zrzut użytkownika pokazuje powtarzające się puste sylwetki podczas obrotu kamery. Zapis 008: NR 2496×1404, Transformer K, gain 1, metoda 6, okno 19, powrót 9, radius 1, separacja .05, gate 2 px@1080p, dopasowane guides, reversed Z. MV tła ma w wielu miejscach 100–200 px NR na klatkę, podczas gdy kontrolny fragment postaci pozostaje bliski zera. Mediana składowej X na całym obrazie to -149.416 px, zakres -285.512..35.080.

Po reprojekcji punkt odsłoniętego tła trafia wtedy w środek szerokiej poprzedniej sylwetki. Poprawka 007 wymaga różnicy MV dwóch powierzchni z tej samej klatki, ale jej krótki lokalny test nie znajduje w tym środku żadnej próbki tła. W efekcie rozpoczyna okno tylko przy krawędziach. Zwykłe poszerzenie lub wypełnienie konturu mogłoby maskować powierzchnie, które nigdy nie były zasłonięte.

## Poprawka

Po istniejących weryfikacjach poprzedniej głębi sprawdzamy dodatkowy punkt historii:

`round(previousPos + (oldForegroundMV - currentBackgroundMV) / historyStep)`.

Jest to kandydat widocznego tła wskazany różnicą ruchu, zamiast stałego promienia od krawędzi. Wymaga poprawnego bounds/depth/MV, zgodności z bieżącą głębią tła i wystarczającej różnicy pomiędzy dwoma **poprzednimi** MV. Dla głębokiego odsłonięcia uruchamia okno dla całego potwierdzonego obszaru, również daleko od krawędzi. Nie zamienia różnicy MV pomiędzy klatkami w różnicę ruchu dwóch powierzchni.

Gdy obrót dopiero się zaczyna i oba poprzednie MV były jeszcze zerowe, warunkowo szukamy obecnego occludera w jego pozycji przewidzianej poprzednim MV: `(previousPos+.5)*historyStep-.5-oldForegroundMV`. Depth jest porównywany z poprzednim occluderem; jego bieżący MV porównujemy z bieżącym MV tła. Pozostaje potwierdzenie rekonstrukcją aktualnej głębi. Lokalna ścieżka 007 nadal służy za fallback.

Nowe próby dotyczą tylko zmiany głębi / warstwy, nie dodają pełnoekranowego przebiegu, tekstur, alokacji ani barier. Szybka ścieżka zwykłego podtrzymania licznika pozostaje. Maska śledzi odsłonięte tło przez ustawione N i fade; aktualna bliższa postać pozostaje chroniona. Sama różnica MV nadal nie uruchamia maski bez dowodu wcześniejszego zasłonięcia.

To heurystyka widoczności. Nietrafiona prognoza przy nagłym przyspieszeniu foreground, deformacje, przezroczystości, zbyt podobna głębia, niepoprawne MV lub brak widocznego świadka mogą nadal pominąć obszar. Jedna próbka nie zawiera rzeczywistych poprzednich depth/MV/liczników: nie deklarujemy pełnego odtworzenia historii z obrazu 008 ani usunięcia wszystkich smug w grze.

## Niezależna weryfikacja

192 kontrolowane sekwencje: metody 3/4/5/6, obie konwencje Z, D32/D16/D24, tło ±40/±80 px, nieruchomy szeroki foreground i rozpoczęcie ruchu od MV=0. CPU wyznacza dokładną widoczność i oczekiwany licznik każdego punktu maski przez pięć klatek, łącznie z wnętrzem sylwetki, ochroną foreground, transportem okna i wyjściem poza ekran. Nie porównujemy nowego shadera z nim samym.

Referencja b992: 371184 niepoprawne liczniki. Nowy shader: **0 niepoprawnych**, przy 474240 oczekiwanych aktywnych próbkach. Istniejące testy stóp, kierunków/prędkości, wspólnego przyspieszenia kamery, N/fade, resetu i NaN/Inf pozostają. Testy przeszły na RTX 4090; ten sam zestaw asercji jest weryfikowany na WARP w workflow Release.

Regresja 007: aktywne liczniki nadal `0,0,0,0` dla zapisanych i zerowych MV, na siatkach ceil(NR/2) oraz ceil(NR/4). Pełny wynik kompozycji powtarzanego 007 jest identyczny z b992. Zmieniają się tylko trzy shadery aktualizacji; pozostałe 78 blobów pozostaje identyczne.

## Koszt GPU

RTX 4090, D3D12, 2496×1404, maska 6, radius 1, N19/fade9, gate2.02, separacja .05, gain1, kolor FP16, bez globalnego fade/podglądu. Timestampy obejmują clear, scatter, aktualizację, dilation, decode i bariery; bez NGX/NR, alokacji i uploadów. 16 par rozgrzewki, 64 bloki ABBA, 128 próbek/wersję, osobne historie. Jedyna różnica PSO to aktualizacja b992 versus bieżąca.

| Wejście | Przed: średnia ± SD [ms] | Po: średnia ± SD [ms] | Różnica [ms] |
|---|---|---|---|
| 007, powtarzane guides | 0.064976 ± 0.001794 | 0.064840 ± 0.001610 | -0.000136 |
| 008, powtarzane guides | 0.072272 ± 0.001939 | 0.072560 ± 0.001388 | +0.000288 |
| Kontrolowany ruch tła ±128 px | 0.072216 ± 0.002879 | 0.070616 ± 0.002205 | -0.001600 |

Powtarzane guides 008 przy dużych MV służą do pomiaru kosztu decyzji; nie odtwarzają prawdziwej historii gry. W trzeciej próbie depth przedstawia nieruchomy prostokąt, MV foreground=0, tło ±128 px naprzemiennie; oba warianty dostają tę samą kolejność. Nowa maska ma inne pokrycie, więc pomiar obejmuje także wpływ decyzji na decode. Nie stwierdzono istotnego spowolnienia tych prób. Pełny tor NR+NGX w grze nie został zmierzony.

[Metadane, hashe i odnośniki do CSV](private-ghost-interior-008.json). Powtórzenie: `python tools/prepare_nr_compact_reference.py`, kompilacja `tests/nr_gpu/private_exposure_quality.cpp`, `--wide-visibility-baseline`, `--wide-visibility-quality`, `--interior-visibility-timing <capture stem> static` / `moving`. Domyślne testy i `--warp` obejmują nową regresję. Starsze raporty zachowują swoje przypięte bazy i wyniki historyczne.
