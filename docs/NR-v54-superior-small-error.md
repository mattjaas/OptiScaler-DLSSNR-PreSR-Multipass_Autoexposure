# NR v54 — szybszy Superior Scaling z małymi różnicami liczbowymi

Ta wersja celowo dopuszcza zmianę zaokrągleń FP względem v53, zgodnie
z prośbą użytkownika. Nie zmniejsza liczby próbek ani promienia filtrów.
Zmiany są dwie:

- Normalny końcowy resolve HDR rozwija obie pętle 3×3. Pozostają wszystkie
  dziewięć próbek, wzór wag z dwoma exp2, sigma, sanitizacja, fallback
  biliniowy i cała kompozycja koloru. Kompilator może inaczej grupować
  arytmetykę. Specjalizacja SDR zachowuje oryginalną binarkę: wyniki jej
  kandydatów były mieszane, więc nie włączono ich na podstawie samej hipotezy.
- Pomniejszanie natywnego proxy Area ma osobny PSO ze stałym Mode 2 i
  Transfer 0. Współrzędne, pole powierzchni i akumulator używają `precise`,
  aby ograniczyć różnice względem wcześniejszego kandydata. Nadal powstaje
  natywne materializowane proxy, potem Area z tych samych wag i oryginalnym
  doborem alpha. Nie połączono encode z Area i nie usunięto zapisu FP16.

Pozostają gainy, ich przeliczanie od rozdzielczości/Structure/Intensity,
modele i liczba passów, średnia low, harmonogram interpassów, Low36/cache,
carrier, DLAA, ekspozycja i ustawienia HDR. Nie dodano suwaków ani opcji.
Dochodzi jeden PSO tworzony leniwie. Nie ma nowych dispatchy, tekstur,
kopii, barier, pomiarów ani alokacji w każdej klatce.

## Zakres selekcji i fallback

Tylko pomniejszenie wejścia przy Superior dostaje flagę 65536.
`SuperiorScaling::AreaDownsample` sprawdza Mode 2, Transfer 0, dokładną flagę,
dodatnie wymiary, brak powiększania obu osi i rzeczywistą redukcję co
najmniej jednej osi. Inne Enlargement, Mode 32 z clamp i interpassy zachowują
wcześniejsze shadery. Błąd utworzenia PSO Area wraca do głównego Mode 2,
nigdy do resolve Mode 1. Area i jego fallback używają grup 8×8; wyłącznie
niezmieniony low z v53 zachowuje swoje grupy 8×4.

Normalne PSO HDR są wybierane przez niezmieniony `NormalResolve` i
`NormalColorProfile`. Ogólny shader/resolve inspektora oraz legacy zachowują
dotychczasowe pętle. SDR ze specjalizacją pozostaje binarnie niezmieniony;
rzadki fallback SDR z aktywnym przywracaniem detalu nadal może trafić do
ogólnego normal PSO, objętego testami numerycznymi.

## Różnice obrazu — dane, nie gwarancja percepcyjna

RTX 4090, NVIDIA 616.56. Wykonano **11640 przypadków**:

| Etap / zestaw | Przypadki | Porównane słowa 32-bitowe |
|---|---:|---:|
| Końcowy resolve, wszystkie profile i style | 7776 | 43830720 |
| Area, SDR/HDR, ekspozycja i stress | 3840 | 11247360 |
| Area, native 3840×2160, P50/P59/P65 | 24 | 203156832 |

Resolve obejmuje małe/nieparzyste wymiary, krawędzie, redukcję jednej osi,
FP16/FP32 wyjście, signed i bardzo małe/duże wartości oraz NaN.
Osiem przesunięć syntetycznej krawędzi i tekstury sprawdza zmieniający się
dobór próbek. CPU generuje proxy i oryginał poniżej knee oraz signed edit;
nie symuluje historii ani inferencji NGX. Wszystkie style, low włączone/
wyłączone, skin/environment, limit darkening oraz przywracanie detalu
włączone/wyłączone są częścią macierzy. Test stosuje produkcyjny selektor
profilu i jego fallback.

Area sprawdza niezależnie źródło FP16/FP32 i cel FP16/FP32, SDR i pięć
HDR mapping, cztery tryby ekspozycji/anchorów/trim, zwykłe, małe, duże
i niefinitywne dane oraz krawędzie. Zestaw 4K używa zwykłego sinusoidalnego
wejścia, SDR/HDR soft knee, trzech skal i czterech kombinacji formatów.

Błąd znormalizowany to `abs(A-B)/max(1,abs(A))`, osobno dla kanałów RGB.
Nie jest to SSIM ani model widoczności różnicy. Alpha pozostaje zgodna;
klasy niefinitywnych wartości muszą się zgadzać.

| Etap | Format celu | Maks. błąd bezwzględny | Maks. znormalizowany |
|---|---|---:|---:|
| Resolve, cały stress i ruch | FP32 | .000122070 | .000002146 |
| Resolve, cały stress i ruch | FP16 | .000976562 | .000795545 |
| Area 4K, zwykłe wejście | FP32 | .000187397 | .000187397 |
| Area 4K, zwykłe wejście | FP16 | .000488281 | .000488281 |

Dla Area 4K różnica proxy FP16 odpowiada najwyżej około 0.125 jednego
stopnia 8-bitowego. W resolve FP16 zmieniło się 127 spośród 21896406
skończonych kanałów RGB w całym zestawie. To przemawia za bardzo małą
widocznością różnicy w tych danych, ale nie zastępuje oceny gry w ruchu.

W ekstremalnym Area stress surowa różnica bezwzględna jest duża, bo wejście
zawiera max-float i wartości rzędu 10000: FP32 max 6.38896e32, FP16 max 16.
Odpowiadające maksima znormalizowane to 4.87053e-6 i .000973710.
Tych bezwzględnych wyników nie należy przedstawiać jako zwykłej sceny.

Ważne ograniczenie: Area zmienia wejście modelu NR. Test porównuje proxy,
nie późniejszą odpowiedź sieci ani jej historię. Nie wykonano pełnego NGX
ani wizualnej oceny gry/ghostingu. Mały błąd wejścia nie jest formalnym
ograniczeniem błędu wyjścia sieci. Ocena praktyczna wymaga porównania v53/v54
w tej samej grze i scenie; nie deklarujemy 100% zachowania jakości.

## Weryfikacja kodu i shaderów

Test polityki C++ sprawdza selekcję Area, flagi, tryby, zerowe wymiary,
native i powiększanie. Python sprawdza integrację, geometrię fallbacku,
pełną liczbę próbek/wzór wag, zachowane benchmarki i interpassy.
DXC kompiluje 17 wariantów. Z 18 wcześniej śledzonych cso zmieniają się tylko
normal, hdr2 i hdr4; pozostałe 15, w tym główny shader, SDR, input encode,
low oraz interpassy, są binarnie identyczne. Dochodzi Area.

MSVC v142 lokalnie kompiluje testy/fixture C++17; pełna DLL wymaga v143
z workflow. GitHub Actions kompiluje DLL i fixture oraz publikuje ZIP
i osobny `OptiScaler.dll`. Samo zlecenie buildu nie oznacza jego powodzenia.

## Pomiar GPU v53 → v54

RTX 4090, NVIDIA 616.56, 2026-10-19, native 3840×2160.
P50=1920×1080, P59=2266×1274, P65=2496×1404. Syntetyczne nieruchome sceny
sinusoidalne; źródło kodowania, natywne proxy, HDR copy i wynik Area FP16.
Input: WhitePoint=2, ekspozycja auto z rezydentnego scalar .5, PreExposure=1,
trim=1, brak anchorów; kodowanie ekspozycji/HDR jest identyczne dla A/B.
Guides finalnego resolve/original FP32, decoded residual/low/wynik FP16.
Guided radius 1, range .015, spatial 3, strength 1, high=.8/low=.661,
WhitePoint=1, TransferStrength/ColourStrength=1, MaxRatio=2, darkening=100,
skin/environment strength=1, debug/compare/przywracanie detalu wyłączone.
Gainy są takie same w obu wariantach przy wszystkich skalach.

Mierzone razem są **input encode + Area + rezydentny low + końcowy resolve
z barierami**. To wspólny pomiar obu zmienionych fragmentów, nie suma median
z osobnych testów. Syntetyczny residual dla resolve jest przygotowany przed
timestampami; nie pochodzi z inferencji NR nad właśnie zmienionym Area.
Poza pomiarem: wyliczanie ekspozycji, guides, modele NR, interpassy, carrier,
DLAA, upload, alokacje, tworzenie PSO i fence CPU. A/K i liczba passów nie
uczestniczą w tym pomiarze. To nie czas całego NR/FPS.

Osiem bloków naprzemiennie ABBA/BAAB. Każda czwórka jest nagrywana razem.
Każdy blok ma 16 czwórek rozgrzewki i 40 pomiarowych: 640 próbek na wariant,
profil i skalę. Jedna próbka to cztery sekwencje, dzielona przez cztery.
A/B używają tych samych obiektów/obszarów wyjścia, bez różnic umiejscowienia
pamięci. Powtórzono cały zestaw, potem kontrolę A/A z tymi samymi starymi
shaderami po obu stronach. Brak równoległych testów GPU w tych trzech seriach.
Zegary dynamiczne; przed pracą GPU miało 5% użycia, 210 MHz i 28.69 W.

Mediany drugiej pełnej serii (ms):

| Profil | Skala | v53 | v54 | Oszczędność | Sparowana delta bloków | Dodatnie bloki |
|---|---:|---:|---:|---:|---:|---:|
| sdr | 50 | 0.489472 | 0.456960 | 0.032512 | 0.031776 | 8/8 |
| sdr | 59 | 0.550656 | 0.483328 | 0.067328 | 0.068448 | 8/8 |
| sdr | 65 | 0.583936 | 0.496768 | 0.087168 | 0.088128 | 8/8 |
| hdr0 | 50 | 0.514304 | 0.444160 | 0.070144 | 0.071200 | 8/8 |
| hdr0 | 59 | 0.582144 | 0.475904 | 0.106240 | 0.106432 | 8/8 |
| hdr0 | 65 | 0.621568 | 0.486656 | 0.134912 | 0.135072 | 8/8 |
| hdr2 | 50 | 0.482048 | 0.434432 | 0.047616 | 0.048224 | 8/8 |
| hdr2 | 59 | 0.538624 | 0.461696 | 0.076928 | 0.078208 | 8/8 |
| hdr2 | 65 | 0.572928 | 0.476672 | 0.096256 | 0.097056 | 8/8 |
| hdr4 | 50 | 0.479232 | 0.431104 | 0.048128 | 0.048032 | 8/8 |
| hdr4 | 59 | 0.537856 | 0.459776 | 0.078080 | 0.077408 | 8/8 |
| hdr4 | 65 | 0.572672 | 0.474880 | 0.097792 | 0.097824 | 8/8 |

Sparowana delta to mediana ośmiu różnic średnich median okien A/B w każdym
bloku. W obu pełnych seriach wszystkie konfiguracje miały zysk w 8/8 bloków.
W SDR zysk pochodzi z Area; końcowy specjalizowany resolve SDR ma ten sam
shader po obu stronach. Pierwsza pełna seria miała dla HDR0 P50/P59/P65
oszczędności .083072/.109568/.135168 ms, a SDR .032256/.068096/.090112 ms.

Rozrzut próbek przy P65 w powtórzeniu, ms:

| Profil | p10–p90 v53 | p10–p90 v54 | Odch. std. v53/v54 |
|---|---:|---:|---:|
| sdr | 0.574720–0.672256 | 0.490496–0.582374 | 0.049650/0.045032 |
| hdr0 | 0.612122–0.710272 | 0.481024–0.550323 | 0.050671/0.043287 |
| hdr2 | 0.564224–0.662758 | 0.471552–0.566758 | 0.049488/0.042198 |
| hdr4 | 0.564736–0.669389 | 0.470810–0.562611 | 0.047911/0.043822 |

Kontrola A/A: maksymalna bezwzględna różnica median 0.000768 ms,
maksymalna bezwzględna sparowana delta bloków 0.000928 ms.
Zysk jest większy od tej kontroli, mimo dużego rozrzutu pojedynczych próbek.
Nie należy dodawać wyników v50/v51/v52/v53 z poprzednich sesji.

[Surowe próbki obu pełnych serii i kontroli A/A](NR-v54-superior-GPU.samples.csv).

## Odrzucone warianty i odtworzenie

Jeden exp2 z sumy wykładników zamiast dwóch exp2 i mnożenia nie dał
przekonującego dodatkowego zysku względem samego rozwinięcia pętli.
W produkcji pozostają dwa exp2. Rozwijanie pętli SDR dało mieszane wyniki
między skalami; zachowano stary SDR dla wszystkich skal, bez nowej polityki
procentowej. Zmniejszanie liczby próbek/reorganizacja filtra low nie były
potrzebne do uzyskania opisanego zysku i nie weszły do tej wersji.

Area bez `precise` miało większy błąd FP32 na stress; wybrano wariant z
precyzyjnymi współrzędnymi i akumulatorem. Wczesne krótkie pomiary służyły
przesiewowi. Jedna sesja kontrolna nakładała się z innym pilotem GPU;
żadnych jej danych nie użyto w tabeli ani w pliku z finalnymi trzema seriami.
Wspólne końcowe pomiary używają dokładnie opublikowanych cso.

1. Zachować cso v53 (`b0f2a20d16038046ce50911eb6d417924a1507c5`) w baseline.
2. Uruchomić `tools/compile_nr_interpass.ps1`.
3. Skompilować MSVC x64 `/std:c++17 /EHsc /O2` nowe fixture
   `superior_numeric_quality`, `superior_area_quality`, `superior_numeric_timing`
   i opcjonalny `superior_area_timing`; linkują `d3d12.lib dxgi.lib`.
4. `superior_numeric_quality.exe baseline candidate color` sprawdza wszystkie
   normalne profile i fallback. `superior_area_quality.exe baseline candidate`
   i ta sama komenda z `--4k` sprawdzają wejście.
5. Przy wolnym GPU dwukrotnie wykonać
   `superior_numeric_timing.exe baseline candidate wynik.csv normal combined`,
   następnie `superior_numeric_timing.exe baseline candidate kontrola.csv aa combined`.
   Opcjonalny tryb `resolve` mierzy tylko low+resolve, a `superior_area_timing`
   służy do osobnego pomiaru Area/input; nie sumować ich median jako pomiaru łącznego.
6. Pełną DLL porównać w grze v53/v54: ta sama scena, rozdzielczość, passy,
   HDR i A/K, rozgrzewka i ABBA; osobno obejrzeć ruch, cienkie krawędzie,
   ciemne gradienty oraz jasne HDR. Testy syntetyczne nie zastępują tego kroku.

Sumy SHA256 nowych/zmienionych cso:

- area (3032 B): `f15b3556fbc80385d9f6ba7cf4e68dbe94bc0c6cc5bc9b098f510841eceb5ba9`
- normal (15712 B): `78914feee9a2e710586d59696dcbf8b482b76382233061d52420ea57918cb3c2`
- hdr2 (10768 B): `984d4fedbe37a320dd2dc97648f88b40791d3471ddc5dea39e84c86f89d8dad0`
- hdr4 (10876 B): `1fa1acf7c27c486231801b75e95104bfae9cba6586b067994e77729e5f77cd26`
