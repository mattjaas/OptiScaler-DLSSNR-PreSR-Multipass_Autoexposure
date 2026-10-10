# NR v18: porównanie inter-pass poniżej 50%

Hipoteza: RGB20/Mode28/isolated weights, który wygrywa przy 59%, może wygrywać
również poniżej 50%. W v17 blokował go warunek CPU native/work <2. Usunięto tę
blokadę dla wybranego RGB PSO, pozostawiając sprawdzanie wymiarów wejść oraz
jednolite zabezpieczenie całej grupy w shaderze. Sam shader nie zmienił się.

Automatyczna polityka wyboru pozostaje taka jak w v17: poniżej 50% Classic
jest ustawieniem tymczasowym, a nie potwierdzonym zwycięzcą w grze.
Rozszerzony RGB20 jest wymuszany tylko przez benchmark; zwykły rendering
nie zmienia swojego wyboru na podstawie syntetycznego testu.

## Test w grze

Nowy przycisk w panelu NR: **Compare inter-pass below 50% (CSV)**.

- Skale 33%, 40%, 45%, 49%, a także bieżący procent poniżej 50%, jeśli jest inny.
- Sześć passów, Area, radius 1, guide strength 1; shaping i cienie pozostają.
  Debug/spatial compression są wyłączone na czas testu.
- Na każdą skalę: Off, Classic reference i Fused reference, następnie
  Classic optimized / Fused optimized / RGB20 guarded / RGB20 guarded /
  Fused optimized / Classic optimized, na końcu bieżący automat.
- Każde okno ma osobną rozgrzewkę 90 świeżych znaczników GPU i 160 próbek
  domyślnie. Trzy kandydatury mają po 320 próbek po połączeniu dwóch okien.
  Kolejność A/B/C/C/B/A daje każdemu kandydatowi taką samą średnią pozycję
  w sekwencji i ogranicza wpływ liniowego dryfu.
- Zestaw ma 28 unikalnych konfiguracji / 40 okien; z dodatkową skalą 35 / 50.
  Nie dodano ponownie odrzuconych eksperymentów.
- Zakończenie i anulowanie przywracają ustawienia. Zmiana wymiarów native
  podczas sweepu lub working podczas okna anuluje test, żeby nie łączyć
  nieporównywalnych pomiarów.

RGB20 guarded oznacza faktyczny dedykowany PSO z kaflem 20×20 i isolated weights.
Grupa, której footprint nie mieści się w kaflu, wykonuje bezpośrednią
rekonstrukcję Mode28. Brak tilingu nie oznacza użycia zwykłego standardowego PSO:
wagi i izolacja Mode28 nadal pozostają. Raport selected_path identyfikuje PSO,
a nie liczbę grup, które rzeczywiście korzystały z LDS.

Pliki NR-v18-low-*.csv/.samples.csv/.txt powstają w OptiScaler-NR-Benchmarks
obok gry. CSV podaje rzeczywiste wymiary, GPU w TXT, ścieżkę, medianę/P95/SD,
różnicę względem Off/Fused reference i oczekiwanej ścieżki automatu.
Expected path control przy niższych skalach to Classic optimized. Zmieniono
nazwę z Known winner control, aby nie nazywać niezbadanego ustawienia zwycięzcą.
Kolumna delta_vs_expected_path_ms zastępuje delta_vs_known_winner_ms.
window_medians_ms i window_median_spread_ms pokazują dryf między powtórzeniami.
Historical gain ma nan przy niezmierzonych skalach.

Wygrana z Expected path control oznacza szybszy kandydat od obecnego Classic.
Zgodność automatu z kontrolą potwierdza tylko wybór obecnej polityki.
Oceń wszystkie trzy kandydatury, medianę, P95, SD i obie mediany okien.
Utrzymuj nieruchomą scenę, takie same HDR/ustawienia. Małe różnice wymagają
potwierdzenia; sam najniższy punktowy wynik nie gwarantuje stabilnej przewagi.

Dotychczasowy przycisk Verify automatic inter-pass performance pozostaje osobno,
z P50/P59/P65 i bieżącym procentem. Gdy bieżący procent <50, dodaje teraz to
samo porównanie trzech kandydatur, zamiast pomijać RGB20: 25 konfiguracji /
34 okna. Dla dodatkowej skali ≥50 pozostaje 24 / 32; bez dodatkowej 18 / 24.
Raport regularny ma prefiks NR-v18-auto-.

## Lokalny pomiar rzeczywistego GPU

RTX 4090, syntetyczna ciągła scena z sinusoidalnymi detalami i edycją NR,
native 3840×2160. Nie jest to The Last of Us ani cały łańcuch NGX.
Wejścia FP32 ze znakiem i wartościami >1, wyjścia oraz scratch Classic FP16.
Area, guided radius 1, confidence 1, sigma spatial 1,2/range 0,015,
bez frequency shaping i bramki cieni.

Runner tests/nr_gpu/interpass_low_timing.cpp utrzymuje tekstury/CBV/PSO przez
cały sweep. Uploady, alokacje, readback timestampów i oczekiwania na fence są
poza przedziałem. Jedna próbka obejmuje osiem przejść i jest dzielona przez osiem.
Classic obejmuje Mode27 + Mode32 oraz dwie zmiany stanu scratch; Fused i RGB20
jeden dispatch. Wszystkie warianty mają barierę UAV wyniku między powtórzeniami.
A/B/C/C/B/A, po 90 próbek rozgrzewki i 160 pomiarów w każdym oknie:
320 pomiarów na kandydaturę i skalę, 3840 próbek łącznie.

| Skala / working | Classic mediana ms | Fused mediana ms | RGB20 mediana ms | Najszybszy w tym teście |
|---|---:|---:|---:|---|
| 33% / 1267×713 | 0,313600 | 0,516800 | 0,544384 | Classic |
| 40% / 1536×864 | 0,331200 | 0,380544 | 0,274688 | RGB20, −0,056512 ms względem Classic |
| 45% / 1728×972 | 0,367488 | 0,654464 | 0,316032 | RGB20, −0,051456 ms względem Classic |
| 49% / 1882×1058 | 0,381312 | 0,643200 | 0,332736 | RGB20, −0,048576 ms względem Classic |

| Skala | Classic P95 / SD ms | Fused P95 / SD ms | RGB20 P95 / SD ms |
|---|---|---|---|
| 33% | 0,321024 / 0,004604 | 0,525696 / 0,015672 | 0,553984 / 0,008163 |
| 40% | 0,338688 / 0,004262 | 0,437888 / 0,022986 | 0,285440 / 0,010459 |
| 45% | 0,374912 / 0,005516 | 0,670848 / 0,009835 | 0,331520 / 0,009190 |
| 49% | 0,465664 / 0,027369 | 0,657152 / 0,009303 | 0,349312 / 0,008979 |

Mediany poszczególnych okien, Classic / Fused / RGB20, w ms:

- P33: 0,311808–0,319104 / 0,515136–0,518784 / 0,541824–0,545280.
- P40: 0,327680–0,334976 / 0,371584–0,380928 / 0,265216–0,279168.
- P45: 0,362752–0,372864 / 0,651264–0,658176 / 0,309504–0,320704.
- P49: 0,379648–0,386176 / 0,643008–0,643200 / 0,328704–0,339264.

W każdym powtórzeniu lokalny zwycięzca jest ten sam. Zwykły Fused przegrał
we wszystkich czterech przypadkach. RGB20 wygrywa poniżej 50% tam, gdzie
przydatny jest jego kafel; przy 33% wygrywa Classic.
To zmierzony wynik samego inter-pass, a nie dowód na taki sam ranking całego NR.
Powtarzanie identycznych rezydentnych danych sprzyja cache; NGX i inne passy
w grze mogą zmieniać cache/overlap. Nie mnożymy tych czasów przez liczbę
przejść i nie przedstawiamy wyniku jako pomiaru całego NR.
Taktowanie, temperatura i inne obciążenie GPU nie były kontrolowane.

Surowe dane: docs/NR-v18-low-GPU.samples.csv. Sprawdzono 3840 wpisów, 160
ciągłych indeksów na okno oraz 320 próbek kandydatury na skalę.
Powtórzenie: interpass_low_timing.exe shader-dir output.csv.

## Poprawność

Na RTX 4090 test faktycznych DXIL przeszedł 5760 porównań i 48 628 352 składowe
RGBA. Obejmuje skale 25/33/40/45/49/50/51/55/59/60/61/65/90/95/99%,
nieparzyste wymiary, niepełne grupy, FP32/FP16, HDR ze znakiem, alfa,
NaN, cienie i shaping. Poniżej 50% RGB20 porównano bitowo ze sprawdzonym
shaderem o tych samych isolated weights i Mode28, ale bez kafla. Zgodność
potwierdza zarówno tiled, jak i overflow fallback, z wyjątkiem payloadu NaN.
Nie wymagamy bitowej zgodności między różnymi specjalizacjami kompilatora
standardowego i isolated weights: ich końcowe zaokrąglenia FP32 mogą się różnić.
Matematyka filtra i precyzja nie zostały zmienione.

Test C++ sprawdza rzeczywisty plan benchmarku: trzy kandydatury po dwa razy,
symetrię kolejności i zachowanie produkcyjnego wyboru poniżej 50%.
Testy arytmetyki dynamic guided/Area i tiled rozszerzono do 25/33/40/45/49%,
z pokryciem fallbacku, krawędzi i alfa. Przeszły wraz z testem benchmarku.
Wspólny runner GPU wydzielono do interpass_fixture.h; workflow kompiluje oba
fixture i runner wydajności. Nie dodano nowych PSO ani przełączników renderingowych.

Do ostatecznego rozstrzygnięcia wyboru produkcyjnego potrzebny jest raport
NR-v18-low z gry. Po jego analizie progi automatu można ustawić na podstawie
zmierzonego czasu całego NR, a nie samego komponentu.

## Paczka i publikacja

Źródło paczki: bb08c2beade6088d1409431a5a4a9986bfc3fff3,
gałąź codex/nr-interpass-v13-bounded-area.
[Build 38076805006](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/actions/runs/38076805006)
zakończył się sukcesem: arytmetyka, test planu/polityki, kompilacja trzech
shaderów i wszystkich fixture GPU, pełna kompilacja DLL oraz publikacja.

[ZIP v18](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/releases/download/nr-interpass-v18-low-scale-20261010/OptiScaler-NR-nr-interpass-v18-low-scale-20261010.zip),
131 087 254 bajty.

- ZIP SHA256: e50adfb8b5e5d314ac8675840f4cc142a81743a38ea818c7af0a584f1fc0d7cd
- DLL SHA256: ae17e77053e995d89c18717b241fb52486b11b819992b39be33d099bd9b29218
- Pobrany ZIP: CRC i wszystkie 62 wpisy SHA256SUMS.txt poprawne.
  DLL Windows x64 zawiera nowy przycisk i kandydaturę RGB20 guarded oraz
  trzy bloby identyczne z lokalnie sprawdzonymi shaderami. Nie zawiera
  36 wcześniejszych blobów inter-pass.

Lokalnie usunięty przez Bitdefender package_release.ps1 pozostawiono poza
commitem. Nie przywracano go z kwarantanny; Actions używał wersji repozytorium.
