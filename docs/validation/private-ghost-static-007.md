# Statyczne krawędzie i względny ruch — próbka 007

2026-11-05. Baza: `21b0ce92d3c54956b87b11e535f3244fe7213b21`.
Zmiana dotyczy metod temporalnych 3–6, bez zmiany legacy i metody przestrzennej 2.

## Przyczyna i poprawka

Użytkownik potwierdził nieruchomą kamerę i postać przez ponad sekundę. Zapis 007 przedstawia to samo miejsce co zgłoszony podgląd maski. Ustawienia: metoda 6, NR 2496×1404, okno 19, fade 9, radius 1, separacja .05, motion gate 2.02 px@1080p, reversed Z, gain 1, Transformer K.

Rozrzut najbliższej głębi do biliniowego footprintu nakładał foreground na sąsiednie tło również przy zerowym MV. Dotychczasowy test traktował ten overlap jako odsłonięcie i odnawiał okno co klatkę. W 007 mediana długości MV to 0.1192814 px NR, P99 0.2112646, maksimum 0.9503154. Błąd występuje także po zastąpieniu wszystkich MV zerami.

Nowe okno wymaga wcześniejszej bliższej powierzchni w rzeczywiście reprojektowanej próbce i różnicy MV foreground/background. Wykorzystujemy istniejące sąsiedztwo historii i `dot(mvA-mvB,mvA-mvB) >= gate*gate`. Oba MV pochodzą z tej samej poprzedniej klatki, więc wspólne przyspieszenie kamery nie jest różnicą ruchu powierzchni. Na głębokiej granicy wystarcza zgodne poprzednie tło i różny ruch occludera. Dla płytkich kontaktów zachowany jest dodatkowy test dopasowania warstwy.

Gdy foreground dopiero zaczyna ruch, poprzednie MV obu warstw mogą jeszcze wynosić zero. Tylko przy zmianie głębi bez poprzedniego dowodu sprawdzamy do ośmiu punktów bieżącej granicy: najpierw depth, potem MV pasującej bliższej powierzchni. Potwierdzenie rekonstrukcją bieżącej głębi pozostaje. Są to warunkowe odczyty w tym samym shaderze, bez nowych dispatchy, zasobów, barier, readbacków ani alokacji. Nie potrzeba kątów ani normalizacji wektorów.

Przeciwne kierunki, różne prędkości w tym samym kierunku i ruch na tle stojącej powierzchni obsługuje ta sama norma różnicy MV. Jednakowy ruch nie przekracza progu. **Ghost relative motion gate (1080p)** jest teraz obowiązkowym warunkiem startu/odnowienia każdego temporalnego okna. Zatrzymanie nie kasuje rozpoczętego okna: licznik nadal podąża za powierzchnią i wygasa. Zbyt wysoki próg, brak drugiej warstwy lub niepoprawne MV mogą ominąć prawdziwe odsłonięcie. To świadome ograniczenie czułości, nie idealny klasyfikator ghostingu.

## Odtworzenie i testy

Powtarzamy zapisane depth/MV cztery razy; to test nieruchomej sceny, a nie rekonstrukcja historii sprzed zrzutu.

| Siatka maski | MV | Aktywne liczniki przed (4 klatki) | Po |
|---|---|---|---|
| ceil(NR/2) | zapisane | 0, 5721, 5721, 5721 | 0, 0, 0, 0 |
| ceil(NR/2) | zera | 0, 5726, 5726, 5726 | 0, 0, 0, 0 |
| ceil(NR/4) | zapisane | 0, 2914, 2914, 2914 | 0, 0, 0, 0 |
| ceil(NR/4) | zera | 0, 2913, 2913, 2913 | 0, 0, 0, 0 |

Po poszerzeniu i decode maska metody 6 spada z 208869 aktywnych pikseli do zera. Nie oznacza to usunięcia każdego false positive w innych scenach.

Nowe niezależne testy GPU: 144 sekwencje statycznych krawędzi (cztery ścieżki, obie konwencje Z, D32/D16/D24, fazy krawędzi, zerowy i subpikselowy wspólny MV) i 112 kontroli kierunku, prędkości, minimum, wspólnego przyspieszenia kamery, rozpoczęcia ruchu z MV=0 i wygasania przy zatrzymanej krawędzi. Zachowano testy but–podłoże, ochrony occludera, przenoszenia licznika, dokładnego wygaśnięcia, resetu, niepoprawnych danych, transportu i kompozycji. WARP i RTX 4090 uruchamiają te same asercje. To nie obserwacja NGX w grze.

## Koszt GPU

Cały etap maski: clear, scatter, aktualizacja, dilation radius 1, decode i bariery; bez NGX/NR. RTX 4090, D3D12, 2496×1404, FP16 kolor, edit wokół .5, gain 1, bez globalnego fade i zielonego podglądu. Baza ma shader aktualizacji z przypiętego commita; wszystkie pozostałe PSO i ustawienia są identyczne. Osobne historie, 16 par rozgrzewki, 64 bloki ABBA, 128 próbek na wariant. Dane rezydentne; uploady i alokacje poza timestampami.

| Próba | Przed: średnia ± SD [ms] | Po: średnia ± SD [ms] | Po − przed [ms] |
|---|---|---|---|
| Nieruchomy 007 | 0.065872 ± 0.001880 | 0.065056 ± 0.001163 | -0.000816 |
| Ruchomy prostokąt | 0.069384 ± 0.001789 | 0.069000 ± 0.002252 | -0.000384 |

[Statyczne ABBA](private-ghost-static-007-static.csv), [ruch ABBA](private-ghost-static-007-moving.csv), [metadane](private-ghost-static-007.json).

Powtarzany 007 mierzy koszt nieruchomej sceny. Drugi przypadek zachowuje kolor 007, ale zastępuje guides kontrolowanym ruchem prostokąta ±8 px na nieruchomym tle; każdy wariant dostaje tę samą kolejność klatek. To syntetyczna próba ruchu. Nowa maska ma inne pokrycie, więc pomiar obejmuje również wpływ decyzji na decode.

Różnice są mniejsze od zmienności próbek. Nie gwarantujemy zerowego kosztu na każdym GPU i w każdej scenie. Pełny tor gry wymaga ABBA z tym samym ruchem kamery i timestampami NR+NGX+kompozycja.

Odtworzenie: `python tools/prepare_nr_compact_reference.py`, kompilacja `tests/nr_gpu/private_exposure_quality.cpp`, następnie `--static-visibility-quality`, `--static-visibility-capture <stem>`, `--static-visibility-timing <stem>`, `--moving-visibility-timing <stem>`. Stare CLI parity z d419/a13 są historycznym sprawdzeniem wcześniejszych optymalizacji; po zmianie kryterium nie oczekujemy identycznej historii. Referencja 21b nie jest zastępowana bieżącym shaderem.
