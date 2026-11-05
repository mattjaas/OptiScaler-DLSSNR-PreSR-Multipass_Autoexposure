# Niebo, kontrast po dodatnim gain i jednostronny miękki brzeg

2026-11-06. Baza `c6a88c1d8f272b9a40e11b3e351488cf54883a47`. Wersja próbna, bez PR.

## Niebo w 011

32,3914% pikseli próbki ma prawidłową głębię 0 (reversed Z). Wszystkie mają skończone MV. Poprzednie scatter/history/decode traktowały far-plane jako brak danych, więc maska nie mogła obejmować nieba za rusztowaniem. Wspólny `GhostNearDepth` rozróżnia poprawną daleką płaszczyznę (dodatni token 1e-20) i nieprawidłową głębię (-1; stan historii 0). Token zachowuje wykładnik w compact FP32; nie jest konwertowany do FP16. Wartości poza [0,1], NaN/Inf i nieprawidłowe MV nadal nie dowodzą dysokluzji. Zachowane są bramka względnego MV i ochrona foreground. Dotyczy metod 3–6, obu konwencji Z, także wzorca kontrastu i kontynuacji drogi.

Powtórzenie jednego depth/MV przez 19 klatek to **przybliżenie, nie odtworzenie historii gry**. W 011: aktywne piksele maski na niebie 0 → 11 766; po miękkim brzegu 4 px → 24 299. Silna maska >.9: 0 → 3925 → 3925; żaden piksel rdzenia nie maleje. Część smug w takim podglądzie nadal pozostaje. Nie deklarujemy usunięcia wszystkich smug ani potwierdzenia w grze na podstawie pojedynczej klatki.

## Kontrast po gain

`boost(e,g)=min(e,0)+max(e,0)*g`; różnica to `boost(raw,g)-boost(originalOccluder,g)`. g jest tym samym efektywnym mnożnikiem manual/auto, który trafia do encode/decode w tej klatce. Historia nadal zapisuje surowy wzorzec: zmiana g działa od razu, bez resetu NR, NGX lub maski. Nie mnożymy różnicy obu editów — błędnie wzmocniłoby to ujemne części. Neutralne 0 i wartości ujemne pozostają bez wzmocnienia. RGB używa max modułów kanałów; Brightness only używa modułu sumy Rec.709 **po** indywidualnym boost obu RGB. Wspólne .5 nie wpływa na różnicę. Nie wprowadzamy dodatkowego clippingu przy pomiarze kontrastu.

## Outward-only mask feather

Opcja domyślnie Off; metody temporalne 3–6, także Coarse mask expansion=1. Suwak **Outward mask feather radius (1080p)**: .25–32, default4, skaluje się wysokością rastra roboczego. Kolejność: widoczność/history → istniejąca ekspansja coarse → anulowanie kontrastem po gain → nowy miękki brzeg → końcowy resolve. Przełączenie lub zmiana promienia nie resetuje historii.

Dwa separowalne ważone maksima. Dla przesunięcia d w obrębie ceil(r): w=max(0,1-|d|/(r+1)). Najpierw max poziomo, potem pionowo; wynik >= oryginalna maska. To miękkie poszerzenie, nie uśredniające rozmycie erodujące środek. Pole R16F zaokrąglane w górę; alpha zachowuje rdzeń. Kontrola głębi w obu osiach odrzuca nowy fringe na wyraźnie bliższej powierzchni: nearTarget<=nearSource*(1+historyDepth)+2*precision (minimum1e-6). Używa ustawienia Disocclusion depth separation oraz rzeczywistej precyzji depth. Bez macierzy projekcji to przybliżona granica, nie segmentacja obiektów. Nie usuwa istniejącej maski na foreground. Nieprawidłowa głębia nie dostaje nowej ekspansji. Wzdłuż silnie nachylonych powierzchni kontrola separowalna może być bardziej zachowawcza niż pełny filtr 2D.

Miękki fringe może celowo wejść w sąsiedni piksel, którego bazową maskę anulował kontrast, ale depth blokuje bliższy occluder. Ponowny test kontrastu po feather odtworzyłby twarde odcięcie. Stosowana jest jedna waga RGB. Uwzględniamy globalne wygaszanie DLSS: alpha b już po fade, nowe A>=b, dodatkowa domieszka raw=(A-b)/max(1e-6,1-globalRaw-b). Nie dekodujemy ponownie transportu i nie wyłączamy innych funkcji Superior.

Off: brak dodatkowych dispatchy/alokacji. On: dwa przebiegi po rastrze roboczym, R16F scratch i RGBA16F output, około10 bajtów/piksel dodatkowej pamięci. Zasoby są ponownie używane, tworzone leniwie i odraczane do końca odpowiednich list GPU po wyłączeniu/zmianie siatki. Cały stan należy do Generation/widoku. 52 wspólne sloty PSO, 96 blobów; ABI nadal256B. Nazwany setter i asercje offsetów zabezpieczają reinterpretację stałych. Brak zmian zamrożonego v59.

INI/profil: `SuperiorGhostOutwardFeather` i `SuperiorGhostOutwardRadius`, deklaracje/default, Read/Save, NR_PROFILE_FIELD i kKeptKeys. Zrzuty JSON: `ghost_outward_feather_enabled`, `ghost_outward_feather_radius_1080p`, `ghost_edit_contrast_after_positive_gain=true`; surowe DDS pozostają surowe.

## Weryfikacja

24 niezależne sekwencje odsłaniania nieba: wszystkie cztery temporalne ścieżki, normal/reversed Z, D32/D16/D24, statyczna krawędź, ruch, foreground, NaN. 144 przypadki pełnego feather względem niezależnego skalara CPU: maska po kontrastowym wycięciu, dwie osie/oba kierunki głębi, sky, NaN, brzeg, nieparzysty raster, HDR/ujemny edit, FP16 storage i globalfade. Asercje na każdy piksel RGB/alpha i brak erozji. 688 przypadków kontrastu zaktualizowanych o niezależny boost każdego editu, w tym gain .5/6, zmiana znaku kanału, obie metryki i pełna odpowiedź NR. Zachowane testy 007/static, wnętrz008, drogi009, dokładnych timerów oraz pozostałych funkcji. Testy urządzenia i WARP, świeże profiles smoke z Config.cpp, JSON, polityki Superior i tożsamość v59.

## Pomiary GPU

RTX4090, DX12 timestamp, 2496×1404, dane011, coarse radius1, gain1, N19/fade9, gate2.02px@1080p, depth.05. Feather4px@1080p. 16 par rozgrzewki, 64 bloki ABBA, 128 próbek/wariant; oddzielne historie, niezmienne PSO i tablice descriptorów per dispatch. Kontrast RGB .02/.005 włączony w obu wariantach feather. Pomiar obejmuje cały obszar pracy maski z barierami; bez NR/NGX i końcowej kompozycji gry. „Moving” to syntetyczny nieruchomy prostokąt z MV tła ±128px na rastrze próbki; „static” powtarza guides011 (nie musi oznaczać zerowego MV).

| Porównanie | Przed ms ±SD | Po ms ±SD | Różnica ms |
|---|---:|---:|---:|
| sky.static | 0.070272 ±0.004955 | 0.071040 ±0.004104 | +0.000768 |
| feather.static | 0.095112 ±0.005263 | 0.220144 ±0.008234 | +0.125032 |
| feather.moving | 0.093248 ±0.003668 | 0.209592 ±0.006904 | +0.116344 |

Koszt feather jest mierzalny (około .12ms w tych warunkach), dlatego pozostaje opcjonalny. Różnica samej poprawki sky jest mniejsza od zmienności pomiaru. Te wyniki nie są pomiarem całej gry ani obietnicą dla innych GPU/rozdzielczości/promieni. Nie mierzą oddzielnie nowej arytmetyki kontrastu po gain. [Dane i statystyki](private-ghost-sky-feather.json).

Odtworzenie: `tools/prepare_nr_compact_reference.py`; build `tests/nr_gpu/private_exposure_quality.cpp`; `--sky-feather-quality` / `--sky-feather-quality-warp`, `--sky-timing <stem> static`, `--feather-timing <stem> static` / `moving`, `--sky-baseline-preview <stem> 19`, `--frozen-visibility-preview <stem> 19`, `--feather-preview <stem> 19`. Nie dołączamy prywatnych capture/screenshotów do repo.
