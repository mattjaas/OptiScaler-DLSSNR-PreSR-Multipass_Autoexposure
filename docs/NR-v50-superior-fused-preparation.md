# NR v50 — wspólna głębia i połączone przygotowanie Superior Scaling

Superior Scaling używa gotowej głębi R32 przygotowanej wcześniej dla modeli NR.
Skalowanie motion vectors dla prywatnego DLAA wykonuje razem z kodowaniem
carriera. Usuwa to jeden dispatch, ponowny odczyt/zapis głębi i dwie jej bariery.
Przy tworzeniu tego toru nie alokuje drugiej tekstury głębi. Nie zmienia passów,
gainów, filtrów, próbek resolve, parametrów NGX, historii ani UI.

## Warunki i zachowanie awaryjne

Optymalizacja wymaga Superior Scaling, udanego MatchGuides w bieżącej klatce,
tego samego źródła głębi, formatu R32_FLOAT, identycznych rozmiarów roboczych
i identycznego obszaru głębi (origin oraz width/height). Region motion musi być
ważny. Wspólny helper produkcyjny `SuperiorScaling::ReuseDepth` sprawdza
źródło, format, rozmiary i region; wywołujący sprawdza tryb i dostępność tekstur.

Przy niespełnieniu warunków zachowany jest Mode 37 + osobny Mode 10.
Jeżeli MatchGuides zostanie wyłączone w działającym DLAA, brakująca prywatna
głębia powstaje leniwie. Nie zmienia to wartości pikseli ani historii DLAA.
Tryby Enlargement 0–10 nie korzystają ze współdzielenia głębi.

Nowy Mode 39 jest częścią istniejącego PSO carriera. Oblicza carrier tymi
samymi operacjami `precise` co Mode 37, a dodatkowo zapisuje motion do
istniejącego g.motion. Czyta **oryginalne** motion, nie dane uprzednio
sanityzowane dla NR. Zachowuje punktowe współrzędne z Mode 10, kolejność
`frameScale * workSize / referenceSize`, podrect i wspólne `all(isfinite)`
dla pary XY. Hold nadal ustawia oba mnożniki na zero. Jest to istotne m.in.
dla NaN/Inf i znaku zera. Nie dodaje PSO, tekstury, deskryptora ani LDS.

Głębia NR pozostaje SRV podczas modeli i DLAA. Jej właściciel State rejestruje
bieżącą command listę i zapewnia czas życia do zakończenia GPU; Enlarger
nie przechowuje tego wskaźnika. Końcowe przywrócenie do UAV w Run pozostaje
po wszystkich odbiorcach. Prywatne motion zachowuje swoje dotychczasowe
przejścia UAV/SRV. Bazowy shader i pozostałe dziewięć PSO są binarnie bez zmian.

## Zgodność i kontrole

RTX 4090: **240/240 przypadków zgodnych bitowo**, 10957920 słów 32-bitowych
porównanych osobno dla carriera, głębi i motion. Sześć geometrii od 1×1 do
257×131 (nieparzyste rozmiary i krawędzie), wycinki głębi i motion o różnych
rozmiarach/origin, pięć par skal (także ujemne, zero/Hold i -0), cztery zestawy
danych: zwykłe, małe, bardzo duże i NaN/Inf/-0/subnormal/max-float.
Carrier ma wyjścia FP16/FP32, głębia R32, motion RG32.

Dotychczasowy test carriera przechodzi **52/52** przypadki encode/decode/shaping.
Test C++ polityki wykonuje ten sam helper co produkcja: akceptacja poprawnego
R32, odrzucenie innych źródeł/formatów/rozmiarów, zmiany każdego pola regionu
i zerowych wymiarów. Test integracji kontroluje binding oryginalnego motion,
warunki fallbacku, brak dodatkowych barier na głębi NR i wywołanie z Run.
Wszystkie testy `test_nr_interpass_*.py`, benchmark i Superior przechodzą.
DXC kompiluje wszystkie 11 wariantów i weryfikuje LDS. Zaktualizowano cso
i nagłówek carriera: SHA256
`816F60AC9EDA1AA69F920BFE80D373FFC9B54B758047AB6684F28FC6E9F95438`.

Pełna DLL wymaga toolchainu v143 używanego przez workflow; lokalnie dostępny
v142 wystarczył do fixture C++17. Workflow kompiluje DLL oraz nowe fixture.
Nie wykonano testu gry ani prywatnej inferencji NGX/DLAA. Zgodność bitowa
potwierdza opisane dane wejściowe na tej karcie; nie jest deklaracją pełnej
walidacji temporalnej we wszystkich grach i sterownikach.

## GPU: v49 Superior → v50 Superior

RTX 4090, sterownik 616.56, 2026-10-18. Syntetyczna nieruchoma scena sinusoidalna,
residual 0.03, native 3840×2160. Work: P50=1920×1080, P59=2266×1274,
P65=2496×1404. Głębia 2560×1440; motion low=2560×1440 lub high=3840×2160,
origin 0. Punktowy resize, skale motion work/motion; carrier FP16, dane
proxy/model FP32, głębia R32, motion RG32. Gainy i preset DLSS nie uczestniczą
w tym fragmencie; są jednakowe po obu stronach i nie zmieniono ich w kodzie.

Timestamp obejmuje **wspólny resize guides NR + carrier i prywatne guides**,
z przejściami do SRV oraz przywróceniem do UAV: 3 dispatchy w v49 i 2 w v50.
Jest to pomiar przygotowania z danymi rezydentnymi. Nie obejmuje modeli NR,
interpassów, NGX/DLAA, decode ani finalnego low/resolve. Brak modeli między
wspólnym resize a przygotowaniem DLAA oznacza inną sytuację cache niż w grze.
Alokacje, upload, tworzenie PSO i synchronizacja CPU są poza timestampami.

Dwie pełne serie ABBA. Każde okno ma osobną rozgrzewkę 90 próbek, potem
160 próbek; 320 próbek na wariant/skala/typ motion w każdej serii. Jedna
próbka zawiera osiem sekwencji, wynik dzielony przez osiem. GPU używa
dynamicznych zegarów; odczyt przed powtórzeniem: 37%, core 585 MHz,
VRAM 810 MHz, 38.94 W (po testach zgodności). Przed pracą GPU było wolne.
Rozrzut jest znaczny, dlatego zapisano oba przebiegi i nie przeliczono tego
wyniku na FPS ani oszczędność całego toru gry.

Poniżej mediany i p10–p90 **drugiej, potwierdzającej serii**, ms:

| Skala / motion | v49 mediana | v50 mediana | Zysk ms | Zysk % | v49 p10–p90 | v50 p10–p90 |
|---|---:|---:|---:|---:|---:|---:|
| P50 low | .196608 | .193664 | .002944 | 1.5 | .195072–.255616 | .192384–.250112 |
| P59 low | .275968 | .251648 | .024320 | 8.8 | .273280–.356224 | .249344–.323584 |
| P65 low | .359552 | .292736 | .066816 | 18.6 | .328832–.412416 | .291200–.375040 |
| P50 high | .303488 | .278528 | .024960 | 8.2 | .301568–.401152 | .276608–.359040 |
| P59 high | .380992 | .326144 | .054848 | 14.4 | .351616–.452864 | .323584–.418560 |
| P65 high | .437632 | .405696 | .031936 | 7.3 | .390656–.492928 | .359424–.459008 |

Odchylenia standardowe v49/v50 (ms), w tej samej kolejności:
.032724/.031274, .037985/.032633, .037731/.036017,
.039107/.037703, .039896/.035013, .040940/.038023.
Pierwsza seria też wskazała zysk we wszystkich sześciu grupach:
low .002816/.024320/.063040 ms, high .024640/.056896/.046848 ms.
Zysk P50 low jest mały względem rozrzutu pojedynczych próbek, choć mediany
obu okien A oraz obu B w powtórzeniu zachowały tę samą kolejność.

Surowe próbki: [pierwsza seria](NR-v50-superior-prepare-GPU.samples.csv),
[powtórzenie](NR-v50-superior-prepare-GPU-repeat.samples.csv).
Reguła nie zależy od procentu skali ani od wyniku autotuningu; usuwa dokładnie
powtórną pracę pod warunkami zgodności danych. Czas całego NR wymaga osobnego
pomiaru w grze z identyczną sceną, presetami, passami i ABBA.

## Odtworzenie

1. Z commita v49 `eabe78a5d1a4c7e80355f59756bf0a844b103bea` zachować
   `DlssNr_Shader.cso` i `dlssnr_superior_carrier_Shader.cso` w katalogu baseline.
2. Uruchomić `tools/compile_nr_interpass.ps1` dla bieżącego kodu.
3. W środowisku MSVC x64 skompilować `tests/nr_gpu/superior_prepare_quality.cpp`
   oraz `superior_prepare_timing.cpp` z `/std:c++17 /EHsc /O2` i linkowaniem
   `d3d12.lib dxgi.lib` (bez PCH projektu, lokalne `tests/nr_gpu/pch.h`).
4. Uruchomić `superior_prepare_quality.exe baseline OptiScaler/shaders/dlssnr/precompile`.
5. Przy wolnym GPU uruchomić `superior_prepare_timing.exe baseline OptiScaler/shaders/dlssnr/precompile wynik.csv`.
6. Powtórzyć pełny ABBA i porównać mediany, rozrzut i osobne okna.
