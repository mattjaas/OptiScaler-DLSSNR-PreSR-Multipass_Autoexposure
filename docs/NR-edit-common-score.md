# Wspólny wskaźnik dla CNN i transformera

**Po doprecyzowaniu etykiet: odrzucony jako regulator najniższego mnożnika.** Pliki `noghosting` są blisko samej granicy zaniku ghostingu. S bardzo różni się na tych granicach, dlatego poniższe progi klasyfikacji nie są poprawnymi celami automatu. [Audyt granic i błędu proponowanego mnożnika](NR-edit-boundary-audit.md). Historyczne wyniki 16/16 i pomiar GPU pozostają prawdziwe, ale nie rozwiązują właściwego zadania regulacji.

2026-10-31. Znaleziono **jedną wspólną funkcję**, która z ustalonymi progami 0,052 dla CNN i 0,084 dla transformera rozdziela wszystkie 16 dostarczonych pełnych editów. Te same współczynniki działają dla obu gier i obu rodzin modeli. Siatka 64×36 zachowuje etykiety we wszystkich 64 sprawdzonych fazach każdego pliku. Nie ma wyboru innego percentyla ani innej definicji funkcji dla CNN.

**To heurystyka dopasowana do tego zbioru, nie potwierdzony uniwersalny detektor.** Cechy, współczynniki i progi wybierano po obejrzeniu wszystkich etykiet oraz wyników próbkowania. Kalibracja progu na jednej grze i sprawdzenie drugiej daje 14/16, mimo 16/16 przy podanych ustalonych progach. Nie wykonano niezależnej walidacji nowych scen ani inferencji DLSS. W tej zmianie są narzędzia analizy i prototypy GPU; automat oraz DLL pozostają bez zmian.

## Definicja

Wejściem jest podpisany edit po dodatnim mnożniku, jak w przekazanych DDS, przed dodaniem 0,5, kolorami i clippingiem. Z każdego piksela bierzemy dodatnią część RGB:

```text
p = max(edit.rgb, 0)
lo = min(p.r, p.g, p.b)
mi = median(p.r, p.g, p.b)       # środkowy KANAŁ w jednym pikselu
hi = max(p.r, p.g, p.b)

L = mean(lo)
M = mean(mi)
A = mean(hi)
f = fraction(mi > 0)            # przynajmniej dwa dodatnie kanały

u = mean(p)                    # trzy średnie: R, G, B
u_lo, u_mi, u_hi = sort(u)

S = A/8
  + 2*(M-L)
  + (13/8)*(u_hi-u_lo)
  + (u_mi-u_lo)
  + (f > 0 ? M/f-M : 0)
```

Wszystkie średnie i `f` liczymy po całej siatce, z zerami i ujemnymi editami w mianowniku. Tylko `M/f` jest średnią środkowego dodatniego kanału w pikselach, gdzie jest on większy od zera. To nie P50 obrazu: środkowy kanał RGB jest wybierany lokalnie w każdym pikselu, bez sortowania wszystkich pikseli.

Pięć składników opisuje bazową siłę rozjaśniania, rozrzut dodatniego RGB w pikselu, rozrzut średnich kanałów oraz korektę pokrycia dwoma dodatnimi kanałami. Są nieujemne w arytmetyce rzeczywistej. Współczynniki są wspólne, stałe i dokładnie reprezentowalne binarnie. Samo obliczenie S nie przyjmuje parametru CNN/transformer; rodzina modelu wybiera wyłącznie próg werdyktu.

Składnik A/8 zachowuje dodatnią odpowiedź także dla szarego editu. Dla stałego szarego `p=(a,a,a)` wynik to `S=a/8`, dla jednego dodatniego kanału `S=1,75a`, a dla dwóch równych dodatnich kanałów `S=4,75a`. Pokazuje to także istotną cechę: wskaźnik mocno zależy od rozkładu RGB. Nie jest ekspozycją ani obiektywną jednostką widoczności ghostingu. Sceny szare, silnie monochromatyczne lub inne transformacje koloru wymagają nowych danych. Funkcja jest niezmiennicza względem permutacji R/G/B, w granicach arytmetyki FP32.

## Wyniki na dostarczonych plikach

Reguła jest jednakowa: `S < próg` oznacza przewidywany ghosting.

| Rodzina | S przy ghostingu, pełne obrazy | S bez ghostingu, pełne obrazy | Ustalony próg |
|---|---:|---:|---:|
| CNN | 0,028386–0,045995 | 0,054686–0,072918 | 0,052 |
| Transformer | 0,056661–0,079163 | 0,090640–0,286594 | 0,084 |

| Plik, bez `.dds` | Etykieta | S |
|---|---|---:|
| game1-cnn-ghosting | ghosting | 0,029378 |
| game1-cnn-ghosting2 | ghosting | 0,028678 |
| game1-cnn-noghosting | brak | 0,072918 |
| game1-cnn-noghosting2 | brak | 0,054686 |
| game2-cnn-ghosting1 | ghosting | 0,028386 |
| game2-cnn-ghosting2 | ghosting | 0,045995 |
| game2-cnn-noghosting1 | brak | 0,055973 |
| game2-cnn-noghosting2 | brak | 0,057610 |
| game1-transformer-ghosting | ghosting | 0,058312 |
| game1-transformer-ghosting2 | ghosting | 0,079163 |
| game1-transformer-noghosting | brak | 0,286594 |
| game1-transformer-noghosting2 | brak | 0,092342 |
| game2-transformer-ghosting1 | ghosting | 0,056661 |
| game2-transformer-ghosting2 | ghosting | 0,062271 |
| game2-transformer-noghosting1 | brak | 0,166489 |
| game2-transformer-noghosting2 | brak | 0,090640 |

Funkcja wykorzystuje RGB, którego wcześniejsze `P90(maxRGB)` i `P50(maxRGB)` nie opisywały. W tym zbiorze pozwala zachować jedną definicję pomiaru, podczas gdy same percentyle potrzebowały różnych definicji dla CNN i transformera.

## Próbkowanie i przenoszenie progu

Punktowe próbki w regularnych komórkach, 64 fazy jak we wcześniejszych raportach. Każda liczba poniżej dotyczy 8 plików × 64 faz, przy ustalonych progach 0,052/0,084:

| Siatka | Liczba odczytów | CNN | Transformer | Razem |
|---|---:|---:|---:|---:|
| 32×18 | 576 | 506/512 | 510/512 | 1016/1024 |
| 64×36 | 2304 | 512/512 | 512/512 | 1024/1024 |
| 128×72 | 9216 | 512/512 | 512/512 | 1024/1024 |

Siatka 64×36 jest najmniejszą z tych trzech zachowującą wszystkie etykiety. Przy niej największy wynik z ghostingiem/minimalny bez ghostingu to odpowiednio 0,050310/0,053331 dla CNN i 0,081178/0,087441 dla transformera. Dla CNN odstęp od ustalonego progu do najbliższej próbki wynosi tylko około 2,6% progu. To nie duży zapas na nowe sceny.

Osobny test procedury kalibracji: wyznacz próg jako środek między największym S przy ghostingu i najmniejszym bez ghostingu w jednej grze, potem sprawdź drugą. Współczynniki samej funkcji pozostają wspólne:

| Model | Gra użyta do kalibracji → sprawdzana | Próg | Trafienia pełnych obrazów |
|---|---|---:|---:|
| CNN | game2 → game1 | 0,050984 | 4/4 |
| CNN | game1 → game2 | 0,042032 | 3/4 |
| Transformer | game2 → game1 | 0,076455 | 3/4 |
| Transformer | game1 → game2 | 0,085753 | 4/4 |

Łącznie **14/16**: błędnie uznane za brak ghostingu są `game2-cnn-ghosting2` oraz `game1-transformer-ghosting2`. Nawet ten test nie jest niezależną walidacją wyboru cech: współczynniki korzystały ze wszystkich plików. Ustalone wspólne progi działają w obu obecnych grach, ale automatyczne wyznaczenie progu z jednej gry nie przenosi się bezbłędnie.

## Dobór funkcji i ryzyko dopasowania

Sprawdzono setki wariantów momentów, percentyli, warunkowych statystyk i kombinacji dodatnich kanałów. Odrzucono m.in. kolorowo asymetryczne rozwiązania, kruche kombinacje niskich percentyli oraz momenty czwartego rzędu zależne od nielicznych bardzo jasnych pikseli. Te ostatnie potrafiły rozdzielać gęsty podzbiór, ale zawodziły na pełnych danych i fazach próbkowania.

Końcowa funkcja powstała z nieujemnych składników rozkładu RGB, po dopasowaniu do wszystkich 16 obrazów i faz 64×36 oraz zaokrągleniu współczynników. Dodano bazową dodatnią amplitudę, aby szary edit nie był z definicji zerowy. Ustalono funkcję i progi przed końcowym sprawdzeniem 128×72 i GPU. Nie jest to jednak badanie na nowych scenach. Duża liczba sprawdzonych kandydatów zwiększa ryzyko przeuczenia; 16/16 nie oznacza 100% skuteczności przyszłego regulatora.

## Prototyp GPU i koszt

Wystarczy **pięć sum i jeden licznik**: dodatnie R/G/B, minimum, środkowy kanał i liczba dodatnich środkowych kanałów. A odtwarzamy ze średnich R+G+B−minimum−środkowy. Nie ma histogramu, logarytmów, sortowania obrazu, dodatkowych odczytów sąsiadów ani pełnej tekstury pośredniego editu. Na próbkę przypada jeden odczyt istniejącego podpisanego RGBA FP32 w tym stanowisku testowym.

Porównano pojedynczą grupę 64/256/512/1024 wątków oraz 36 grup po 64 wątki z osobną redukcją. Większe grupy były celowym sprawdzeniem hipotezy, że mniej szeregowych odczytów na wątek zrównoważy koszt redukcji. W tych pomiarach nie wygrały.

RTX 4090, sterownik 616.56, D3D12, residentne zapisy RGBA FP32, DXC cs_6_0 -O3. Osobne rozgrzewki 64 powtórzenia dla A/B, 4 bloki × 16 kwartetów ABBA, 128 okien na wariant, po 8 powtórzeń w oknie. GPU timestamps obejmują dispatche i bariery UAV. A=brak pomiaru, B=badany pomiar. Mediana A wynosi 0 ms, a P90 0,000128 ms przy użytym zakresie i rozdzielczości pomiaru; to nie czas całej bazowej klatki.

| Źródło | Wariant | A mediana ms | B mediana ms (P10–P90) | B−A ms |
|---|---|---:|---:|---:|
| game1-cnn-ghosting, 2240×1264 | 1×64 | 0 | 0,005830 (0,005632–0,006016) | +0,005830 |
| game1-cnn-ghosting, 2240×1264 | 1×256 | 0 | 0,003936 (0,003712–0,004224) | +0,003936 |
| game1-cnn-ghosting, 2240×1264 | 1×512 | 0 | 0,004118 (0,003968–0,004244) | +0,004118 |
| game1-cnn-ghosting, 2240×1264 | 1×1024 | 0 | 0,004390 (0,004224–0,004560) | +0,004390 |
| game1-cnn-ghosting, 2240×1264 | tiled64 + reduce | 0 | 0,003248 (0,003200–0,003456) | +0,003248 |
| game2-cnn-ghosting1, 2496×1404 | 1×64 | 0 | 0,006494 (0,006272–0,006784) | +0,006494 |
| game2-cnn-ghosting1, 2496×1404 | 1×256 | 0 | 0,004434 (0,004224–0,004608) | +0,004434 |
| game2-cnn-ghosting1, 2496×1404 | 1×512 | 0 | 0,004608 (0,004480–0,004736) | +0,004608 |
| game2-cnn-ghosting1, 2496×1404 | 1×1024 | 0 | 0,004900 (0,004736–0,005120) | +0,004900 |
| game2-cnn-ghosting1, 2496×1404 | tiled64 + reduce | 0 | 0,003610 (0,003456–0,003840) | +0,003610 |

Wygrywa wariant dwóch dispatchy. Używa około 1,5 KiB pamięci grupowej i zapisuje 36×32 B danych częściowych (24 B użytecznych sum w dwóch texelach RGBA FP32); wariant 1024 wątków używa 24 KiB. To pomiar osobnego etapu, nie całego toru gry. Wykluczono NR/NGX, odczyt dwóch żywych tekstur model/proxy, zastosowanie mnożnika, upload, readback, alokacje i oczekiwanie CPU. Przy integracji można czytać istniejące NR/proxy bez materializowania pełnego editu, ale narzut tej ścieżki wymaga osobnego pomiaru w grze po rozgrzewce i w ABBA.

Niezależna referencja CPU sumuje w double, sortuje kanały i używa algebraicznie rozwiniętej postaci wzoru; nie korzysta z funkcji shadera. Na RTX 4090 i WARP przeszło po **11680 przypadków liczbowych**: 16 rzeczywistych i 20 syntetycznych obrazów × 64 fazy × 5 wariantów, plus próby zmiany dodatniego mnożnika dla rzeczywistych wejść. Sprawdzane są skończoność i granice błędu `1e-6 + 2e-5*abs(reference)` dla S, pokrycia, średniej amplitudy i liczby próbek. Syntetyki obejmują zera, ujemne wartości, HDR, NaN/Inf, cienką siatkę wejściową oraz analityczne szare/jednokanałowe/dwukanałowe wejścia przy gain 0,01/0,5/1/16/1024. Liczbowy test nie uruchamia prywatnego DLSS.

Każdy z pięciu wariantów GPU zachowuje wszystkie etykiety na 64 fazach 16 plików: 5120/5120, czyli pięciokrotne powtórzenie tych samych 1024 pomiarów. To nie 5120 niezależnych scen. Po wydzieleniu wspólnego stanowiska testowego ponownie przeszły też wcześniejsze 13455 przypadków P50/P90 na obu adapterach. Każde stanowisko ma własną tablicę deskryptorów, aby równoczesne wejścia bazowe i wzmocnione nie nadpisywały swoich zasobów.

## Zastosowanie do mnożnika

Dla dodatniego m i niezmienionego bazowego editu dodatnie kanały, ich średnie, min/mid/max i wszystkie składniki S rosną liniowo z m. f się nie zmienia. W arytmetyce rzeczywistej:

```text
S(po mnożniku m) = m * S(przed mnożnikiem)
m_docelowe = m_bieżące * próg_modelu / S_bieżące
```

Na rzeczywistych plikach zweryfikowano dodatnie skalowanie 0,5× i 2×, z ujemnymi kanałami bez zmian. To pozwala bezpośrednio przewidzieć mnożnik potrzebny do osiągnięcia **poziomu wskaźnika**, a nie potwierdzonej granicy ghostingu. Zerowy/niepoprawny wynik, brak znaczącego editu i przekroczenie zakresu muszą zatrzymywać regulację, zamiast uruchamiać wzrost do 1024×. Użytkowy regulator wymaga histerezy i ograniczenia szybkości zmiany, a ten sam zastosowany mnożnik musi trafić do encode/decode w jednej ewaluacji.

Nie potwierdzono odporności na inne presety/wersje DLSS, ekspozycję, nasycenie/kolorowanie głębi, tryb pełnej odpowiedzi NR i wszystkie sceny monochromatyczne. W tych opcjach sam edit nie opisuje całego wejścia DLSS. Potrzebna jest sekwencja w ruchu ze zmianami mnożnika wokół przewidywanego progu, z automatycznym zapisem ustawień i oceną wyjścia po DLSS. Wspólna definicja pomiaru została znaleziona; dokładny uniwersalny detektor ani najniższy skuteczny mnożnik nie zostały wykazane.

## Odtwarzanie

NumPy wystarcza do obliczenia końcowej funkcji; SciPy używano tylko podczas eksploracji, nie jest wymagane przez pomiar ani prototyp GPU.

```powershell
python tools/analyze_nr_edit_common.py `
  'E:/Gry/Marvels SpiderMan 2/dlssnr-edit/game1-cnn-*.dds' `
  'F:/Gry/The Last of Us - Part I/dlssnr-edit/game2-cnn-*.dds' `
  'E:/Gry/Marvels SpiderMan 2/dlssnr-edit/game1-transformer-*.dds' `
  'F:/Gry/The Last of Us - Part I/dlssnr-edit/game2-transformer-*.dds' `
  --output artifacts/edit-analysis/common-study.json

$files = @(Get-ChildItem -LiteralPath `
  'E:/Gry/Marvels SpiderMan 2/dlssnr-edit', `
  'F:/Gry/The Last of Us - Part I/dlssnr-edit' -Filter 'game*-*.dds' |
  Where-Object { $_.Name -match '^game[12]-(cnn|transformer)-' } |
  Sort-Object FullName | ForEach-Object FullName)
./tools/test_nr_edit_common_probe.ps1 -DdsPaths $files `
  -TimingCsv artifacts/edit-analysis/common-gpu-timing.csv
./tools/test_nr_edit_common_probe.ps1 -Warp -DdsPaths $files
./tools/test_nr_edit_signal_probe.ps1 -DdsPaths $files
./artifacts/edit_signal_probe.exe --warp @files
```

[Wyniki i hashe](validation/nr-edit-common-study.json), [tabela wyników](validation/nr-edit-common-study.csv), [surowe okna GPU](validation/nr-edit-common-timing.csv). Raport JSON przechowuje zakresy faz; pełne fazy odtwarza skrypt. Wszystkie nowe shadery są testowe i kompilowane powyższym skryptem, bez zmian produkcyjnych blobów/ABI.
