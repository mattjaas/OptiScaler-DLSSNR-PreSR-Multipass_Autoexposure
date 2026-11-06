# Limit automatu i trzy sposoby transportu editu NR

2026-11-06. Baza `a1fd2b1b`. Domyślnie pozostaje poprzedni **Positive edit only**. Nowe tryby dotyczą wejścia Edit NR prywatnego DLSS. Pełna odpowiedź NR używa dotychczasowego transportu dodatniego; nowe pozycje są wtedy wyszarzone. Zmiany nie dotyczą głównego DLSS gry ani samego modelu NR.

## Limit dotychczasowego automatu

**Maximum automatic positive multiplier** i **Exact automatic multiplier maximum**, default1024, zakres1–1024. Ograniczają automatyczne stosowanie pozytywnego mnożnika, także warmup/hold, brak nowych pomiarów, unsupported fallback i secondary views. Obniżenie limitu jest wymuszane od razu, niezależnie od cooldown/histerezy. Dolna granica tego automatu nadal1x. Ręczna preferencja nie jest nadpisywana ani ograniczana tym suwakiem; po wyłączeniu automatu wraca bez zmian. Limit nie modyfikuje wartości sugestii, tylko faktycznie stosowany mnożnik. Nie dodaje pracy GPU.

## NR edit transport mode

Niech e=answer-proxy będzie podpisanym editem z neutralnym0, a m=min(e) po **wszystkich pikselach i wszystkich kanałach RGB**, bez alpha. Zwykły carrier ma neutralne0.5. Nie liczymy osobnych minimów/mnożników dla R/G/B.

| Tryb | Wejście DLSS | Powrót do podpisanego editu |
|---|---|---|
|Positive edit only|0.5+min(e,0)+max(e,0)*g|ujemne bez zmian, dodatnie dzielone przez g|
|Symmetric auto to zero|0.5+g*e|(output-0.5)/g|
|Shift minimum to zero, then multiply|(e-m)*g|output/g+m|

Symmetric: gdy m<0, g=0.5/(-m), jeden wspólny skalar dla całego obrazu/RGB. Najciemniejszy kanał osiąga0, neutralne e=0 nadal wchodzi jako0.5. Ujemne i dodatnie zmiany rosną symetrycznie. Jeśli trzeba, g może być mniejsze od1, albo większe od1024; to osobny automat, nie dawny automat dodatniej części. Ręczny mnożnik i jego limit automatu nie sterują tym trybem. Gdy nie ma ujemnego editu, nie da się osiągnąć0 przez dodatnie mnożenie wokół0.5: fallback1x. Górny zakres formatu może dodatkowo ograniczyć g do(65504-0.5)/max(e), aby nie przepełnić carrier; wtedy minimum może pozostać powyżej0.

Shift: najpierw wspólne przesunięcie do zera, potem **cały** zakres mnożony ręcznym suwakiem **Shifted NR edit transport multiplier** / **Exact transport multiplier**, .01–1024. Nie mnożymy względem0.5. Neutralne piksele mogą mieć inny poziom przed DLSS; inverse przywraca ich domenę. To odpowiada `C'=(C-min(C))*g` dla dotychczasowego C=0.5+e. Wynik wejściowy jest ograniczony tylko do nieujemnego zakresu FP16 0–65504; po clippingu/kwantyzacji/filteringu nie da się obiecać dokładnej rekonstrukcji.

Minimum jest dokładne na bieżącym rastrze trafiającym do prywatnego DLSS, bez percentyla/subsamplingu ani opóźnionego odczytu CPU. Oba nowe tryby używają tej samej granicy i jednego g dla wszystkich kanałów. Dostępne niezależnie od rodziny/presetu prywatnego DLSS oraz wybranej ekspozycji. Eksperyment Original guides pozostaje dostępny: zmienia guides, nie raster pomiaru koloru. Przy packed guides o innej siatce nowe tryby korzystają z istniejącego MatchGuides zamiast fused positive-carrier preparation; oznacza to dodatkowy przebieg guides w tym szczególnym przypadku. Zwykła matched ścieżka nadal łączy encode i MV.

## Kontrast, maska i historia

Oryginalny wzorzec occludera pozostaje surowy. Dla obu nowych transformacji wspólny offset znosi się, więc kontrast po transporcie to g*(raw-reference), także dla ujemnych kanałów. RGB i Brightness only, wszystkie metody maski, globalfade oraz oba warianty feather działają już na prawidłowo odwróconym edicie. Nie są używane osobne gainy kanałów. Dotychczasowy positive-only nadal porównuje osobno wzmocnione dodatnie części.

Zmiana trybu/ręcznego mnożnika może resetować prywatny DLSS; NR nie jest resetowany przez ten parametr. Oznaczamy również uśpioną instancję CNN E do resetu przy jej następnym evaluate, aby nie wróciła z historią zakodowaną innym ręcznym przekształceniem. Dynamiczne zmiany minimum i symetrycznego g na GPU nie resetują historii. Automatyczne zmiany dodatniego g, w tym obniżenie przez maksimum, również zachowują prywatną historię przy działającym automacie. Niezależne opcje automatycznego resetu/transition nadal działają normalnie.

## Implementacja i koszt

Dwa opcjonalne dispatches: dokładne min/max w blokach32×32 (64lane, każdy do16 pikseli), następnie redukcja do256B default-heap bufora. Tylko pierwszy float4 jest używany: gain,bias,raw_min,raw_max. Bufor po przejściu UAV→CONSTANT_BUFFER jest czytany przez encode, inverse i kontrast jako b1. Brak mapowania, readbacku, fence wait ani synchronizacji CPU w zwykłej klatce. Dodatkowa pamięć przy2496×1404 to3432 pary float32 (27456B) plus256B przed narzutem alokacji.

Osobny opcjonalny root zachowuje istniejącą tablicę6SRV/2UAV/1CBV i dodaje root CBV b1. Domyślny root, tablice deskryptorów i wszystkie100 wcześniejszych blobów pozostają bez zmian. Wyłącznie nowe PSO czytają GPU-parametry; nie dokładamy kolejnych pełnoobrazowych transformacji. 14 dodatkowych blobów: dwa bounds i12 rzeczywiście wybieranych carrier/mask PSO. Łącznie114,70 wspólnych slotów PSO; ABI256B bez zmian. Flaga wysokiego bitu DirectResolveFlags oraz nazwany setter zabezpieczają wybór rodziny: pozostawione stare sloty kolorów/nasycenia nie mogą jej uruchomić. Każdy widok/generacja posiada własne zasoby. Wyłączenie/resize/shutdown odracza zwolnienie do końca używających list GPU. Nowe scratch zaczynają/kończą w stanie odczytu, także w pierwszym nagraniu, aby powtórzenie listy było poprawne.

## Zapis próbek

Do każdego zapisu w nowym trybie dochodzi `.transport.dds`,1×1 RGBA32F z dokładnymi współczynnikami tej samej klatki `(gain,bias,raw_min,raw_max)`. Symmetric bias=0.5; Shift bias=-raw_min*gain jako informacja o przesunięciu afinicznym. Inverse Shift używa raw_min bez dodatkowego zaokrąglenia bias. Dane kopiowane wyłącznie po wciśnięciu przycisku; snapshot ma niezależne życie i wspólny budżet. Nie czytamy ich na CPU w zwykłym renderowaniu.

`.dds`: w symmetric podpisany edit*g z neutralnym0, przed offsetem0.5; w Shift rzeczywisty nieujemny shifted carrier. `.raw.dds` nadal surowy edit, `.after.dds` nadal po odwrotnym transporcie, przed maską/fade. JSON dodaje mode, maximum, units, info o `.transport.dds`. `positive_gain` dla nowych trybów jest null, ponieważ CPU nie zna faktycznego g z tej klatki; nie przedstawiamy preferencji jako pomiaru GPU. Ręczna preferencja/request pozostają osobnymi polami. Readback batch ma maksymalnie6 elementów zamiast5, z jednym wspólnym typem/limitem dla tablic. DDS używa wymiarów każdej footprint, więc1×1 parametrów nie jest zapisywane jako pełen obraz.

INI/profile: `SuperiorAutoPositiveEditGainMax`, `SuperiorEditTransportMode`: deklaracja/default, Read/Save, reset/profile map i dokładnie jedna kKeptKeys; świeży profiles smoke z aktualnym Config.cpp.

## Weryfikacja

70 przypadków bounds/roundtrip: RGB wspólnym g, minimum w ostatnim niebieskim kanale, negative/positive/zero-only, nieparzyste rozmiary, FP16, HDR i NaN/Inf; również symetryczny gain większy od1024 oraz copy bufora do1×1 snapshot. CPU wyznacza minimum niezależnie, a inverse sprawdzamy względem rzeczywiście zapisanego FP16 z granicą błędu arytmetyki float32. Kwantyzacja dużego przesunięcia może być znacznie większa od błędu samego dzielenia, więc nie zastępujemy jej obietnicą idealnego roundtrip.

160 przypadków integracji maski: wszystkie12 nowych carrier PSO, raw blend0/.25, wszystkie metody, temporal history i RGB/brightness contrast, także kontrast różniący się wyłącznie ujemnym G. Osobne asercje limitu automatu podczas fresh/hold/unsupported i zachowania ręcznego gain, sanitacji, full-answer fallback oraz metadata bez fałszywego CPU gain. Readback completion sprawdza batch6 na DIRECT/COMPUTE/WARP, replay, DDS payloads i niezmienne readbacki. Zachowane pełne testy Superior/retired controls, shader ABI/blob rebuild i profile/JSON. Wyników headless nie przedstawiamy jako obserwacji jakości w grze.

## Pomiary GPU

RTX4090,2496×1404, dane raw011, proxy0.5, positive-only1x, Shift6x, symmetric według GPU-min. Zachowany oryginalny shader jest przypięty do a1fd2b1b; nie zastępujemy referencji bieżącym wariantem. 16 par rozgrzewki,64 bloki ABBA,128 próbek/wariant; osobne scratch i niezmienne descriptor tables, PSO poza pomiarem. DX12 timestamps obejmują bounds/reduce gdy aktywne, encode+matched MV i inverse z carrier identity oraz bariery. **Bez NR, NGX/DLSS evaluate, ghost mask i końcowego resolve gry**; szczególny packed-guide fallback nie jest tutaj mierzony.

| Tryb | Positive-only ms ±SD | Nowy tryb ms ±SD | Różnica ms |
|---|---:|---:|---:|
|1|0.082808 ±0.000912|0.183152 ±0.002228|+0.100344|
|2|0.082792 ±0.000844|0.183040 ±0.002166|+0.100248|

W tych warunkach nowe tryby dodają około0.10ms; koszt wynika także z dokładnego minimum całego obrazu. Nie jest to wzrost czasu pełnej gry. Limit automatu i domyślny positive-only nie dodają tych dispatches. [CSV i statystyki](private-edit-transport-timing.json).

Odtworzenie: `tools/prepare_nr_compact_reference.py`; build `tests/nr_gpu/private_exposure_quality.cpp`; `--transport-quality[-warp]`, `--transport-timing <capture stem>`, pełne domyślne testy i `--warp`. Release workflow jak dotychczas, bez PR.
