# NR v51 — dokładna specjalizacja koloru w Superior Scaling

Dodano trzy automatycznie wybierane PSO końcowego resolve: SDR, HDR Neutwo +
replace i HDR Hybrid + replace. Kompilator usuwa nieużywane ścieżki koloru
oraz obliczenia zastępowane przez wynik replace. Używane operacje, próbki,
wagi, kolejność pętli, gainy, precyzja i ustawienia użytkownika pozostają te
same. Nie dochodzi opcja w UI, dispatch, tekstura, kopia, bariera ani pomiar
w normalnym renderowaniu. Carrier, przygotowanie guides v50, low i interpass
pozostają bez zmian.

## Wybór i zachowanie dokładnej referencji

Najpierw obowiązuje dotychczasowy `NormalResolve`: Mode 1, radius 1, signed
Transfer 9, Apply włączone, inspekcja/compare/skin-mask wyłączone, dokładne
flagi Superior oraz rzeczywista redukcja obu lub jednej osi, bez nadwymiaru.
Następnie wspólny produkcyjny `NormalColorProfile` wybiera:

- Passthrough=1, mapowanie 0–4: SDR, bez utrwalania selektora mapowania.
- Passthrough=0, mapowanie 2: Neutwo + replace.
- Passthrough=0, mapowanie 4: Hybrid + replace.
- Pozostałe przypadki: dokładnie dotychczasowy normal PSO v50.

Opcjonalne przywracanie natywnego detalu w trybie replace zachowuje v50:
mapowanie 2/4, ModelWorkScale >0 i <.999 oraz ReplaceDetailStrength >0.
Przy rozszerzeniu testu wykryto w tej gałęzi różnicę 1 ULP dla SDR FP32;
nie zaakceptowano jej jako regresji jakości. Ten warunek obejmuje HDR i SDR.
Domyślne ReplaceDetailStrength=0 korzysta z optymalizacji.

Composed HDR 0/1/3 pozostaje przy v50: zysk kandydatów był zbyt mały albo
zbyt niestabilny względem kontroli dryfu. Dotyczy to także domyślnego
HDR Off/soft knee. Nie przedstawiamy tego trybu jako przyspieszonego.
Nieprawidłowy Passthrough/mapowanie również zachowuje referencję.

PSO są tworzone leniwie i zapamiętywane; przełączenie koloru wybiera właściwy
shader bez zmiany parametrów NGX lub historii. Przy błędzie utworzenia nowego
PSO następuje próba użycia normal PSO v50, a potem istniejący fallback ogólny.
Dotychczasowe 11 cso ma identyczne sumy SHA256. Nowe cso mają odpowiednio
9924, 9496 i 9600 bajtów wobec 14360 bajtów normal v50. Nie używają LDS.
Koszt pierwszego tworzenia PSO nie uczestniczy w pomiarze ścieżki rozgrzanej.

## Weryfikacja jakości

RTX 4090: **2592/2592 zgodnych bitowo porównań**, 14610240 słów 32-bitowych.
Test używa tego samego `NormalColorProfile`, który wybiera PSO produkcyjne.
Obejmuje dziewięć geometrii z redukcją (także jedna oś, source o szerokości 1,
output o wysokości 1, nieparzyste rozmiary i krawędzie), FP16/FP32, pięć HDR
mapping, SDR, trzy pary gainów, low włączone/wyłączone, skin/environment,
darkening, signed HDR i NaN. Każdy scenariusz wykonuje się osobno z
wyłączonym i włączonym przywracaniem detalu (.0/.5). Zestaw zawiera skorelowane guide/residual oraz
losowe dane o małej i dużej amplitudzie. Sprawdza też aktywne przywracanie
detalu w replace i jego zachowanie na niezmienionym fallbacku.

Wcześniejszy osobny audyt wszystkich sześciu kandydatów koloru bez gałęzi
przywracania detalu także przeszedł 1296 porównań. W produkcji pozostawiono
tylko trzy warianty z powtarzalnym wynikiem czasowym.

Test C++ polityki sprawdza SDR/HDR 0–4, inne wartości, Passthrough 2, granice
ModelWorkScale 0/.5/.999/1 oraz wyłączenie fallbacku przez zerowe przywracanie.
Python kontroluje binding, selekcję, fallback, interpassy i benchmark.
DXC kompiluje 14 wariantów i weryfikuje brak LDS nowych PSO; generuje cso i h.
Workflow kompiluje pełną DLL i fixture. Lokalnie dostępny MSVC v142 kompiluje
fixture C++17; pełna DLL wymaga v143 z workflow. Nie wykonano testu w grze
ani walidacji wszystkich sterowników/GPU. Potwierdzona zgodność bitowa ma
zakres opisanej macierzy na RTX 4090.

## Pomiar GPU i kontrola dryfu

RTX 4090, NVIDIA 616.56, 2026-10-18/19. Native 3840×2160;
work P50=1920×1080, P59=2266×1274, P65=2496×1404. Syntetyczna nieruchoma
scena sinusoidalna, residual amplitudy .03. Dane native/proxy FP32;
zdekodowany residual, low i wynik FP16. Low to niezmieniony Mode 18,
guided radius 1, range sigma .015, spatial sigma 3, guide strength 1,
high=.8/low=.661 przy każdej skali po obu stronach, WhitePoint=1,
TransferStrength/ColourStrength=1, MaxRatio=2, MaxDarkening=100,
skin/environment strength=1, inspekcja/compare wyłączone. Przywracanie
detalu jest wyłączone (domyślne); wartości gainów są celowo jednakowe,
nie stanowią pomiaru przeliczania gainów od rozdzielczości.

Mierzono **low + final resolve z barierami** na zasobach rezydentnych.
Carrier encode/decode wykonano przed timestampami; NGX/DLAA, modele NR,
interpassy, upload, alokacje, tworzenie PSO i fence CPU są poza pomiarem.
Nie jest to czas całego NR ani test FPS w grze. Preset A/K i liczba passów
nie są częścią tego fragmentu; ich kod i ustawienia nie uległy zmianie.

Pierwsze długie ABBA miało istotny dryf: kontrola **tego samego shadera A/A**
wykazała różnice median 0.0201/0.0365/0.0384 ms dla P50/P59/P65. Tych okien
nie użyto do oceny kandydatów. Zmieniono fixture na osiem krótkich bloków,
naprzemiennie ABBA i BAAB. Każde z 32 okien ma osobną rozgrzewkę 32 próbki,
potem 20 próbek; 320 próbek na wariant/skala/profil/seria. Jedna próbka
obejmuje cztery sekwencje low+resolve, wynik dzielony przez cztery.

Krótka kontrola A/A dała mediany A/B (ms):
P50 .362496/.362496, P59 .385792/.379776, P65 .377472/.380800.
Pozostały dryf i rozrzut, więc sprawdzono też różnice sparowane w każdym
bloku i powtórzono całą serię. Przed pracą odczyt GPU wynosił 10%, 1317 MiB,
29.05 W; w przerwie między testami 6%, core 420 MHz, VRAM 5001 MHz,
46.63 W. Zegary były dynamiczne. Nie należy sumować delty z v49/v50,
zmierzonymi w innych warunkach i na innych etapach.

Mediany **drugiej pełnej serii**, ms:

| Profil | Skala | v50 | v51 | Oszczędność | Mediana sparowanej delty | Bloki z zyskiem |
|---|---:|---:|---:|---:|---:|---:|
| SDR | 50 | .362240 | .341504 | .020736 | .018752 | 8/8 |
| SDR | 59 | .378752 | .358016 | .020736 | .020064 | 8/8 |
| SDR | 65 | .407680 | .380160 | .027520 | .018240 | 8/8 |
| Neutwo + replace | 50 | .389888 | .327552 | .062336 | .057248 | 8/8 |
| Neutwo + replace | 59 | .399616 | .334208 | .065408 | .061856 | 8/8 |
| Neutwo + replace | 65 | .426112 | .351872 | .074240 | .052384 | 8/8 |
| Hybrid + replace | 50 | .404096 | .329984 | .074112 | .059456 | 8/8 |
| Hybrid + replace | 59 | .412416 | .340224 | .072192 | .063392 | 8/8 |
| Hybrid + replace | 65 | .424320 | .349440 | .074880 | .059136 | 8/8 |

Sparowana delta to średnia dwóch median okien A minus średnia dwóch median
okien B w danym bloku, następnie mediana ośmiu bloków. Ogranicza wpływ
dryfu i pojedynczych dużych skoków. Rozrzut pojedynczych próbek jest duży:

| Profil / skala | p10–p90 v50 | p10–p90 v51 | Odch. std. v50/v51 |
|---|---:|---:|---:|
| SDR 50 | .318208–.452352 | .296704–.428032 | .060317/.057483 |
| SDR 59 | .338688–.489984 | .322304–.439296 | .062375/.057501 |
| SDR 65 | .351488–.493056 | .341504–.471040 | .062779/.059755 |
| Neutwo replace 50 | .351744–.555264 | .311808–.497664 | .101465/.090015 |
| Neutwo replace 59 | .364288–.503040 | .321024–.427264 | .060988/.054168 |
| Neutwo replace 65 | .374784–.555264 | .340224–.447488 | .070780/.053698 |
| Hybrid replace 50 | .354304–.582912 | .308480–.460800 | .094863/.087014 |
| Hybrid replace 59 | .369664–.511744 | .321792–.428544 | .064795/.054352 |
| Hybrid replace 65 | .379904–.521728 | .337152–.442112 | .064214/.056591 |

W pierwszej krótkiej serii delty median P50/P59/P65 wyniosły (ms):
SDR .020992/.025600/.029824, HDR2 .068864/.073728/.070656,
HDR4 .080000/.085632/.071168. Kontrolne kandydaty HDR0/1/3 zawarte są
w surowych danych jako `not_selected`. Np. HDR0 P50 w powtórzeniu miał
pozorny zysk .014848 ms w medianie zbiorczej, lecz medianę sparowaną
-.001696 ms i zysk tylko w 4/8 bloków. To za mało na włączenie tego toru.

Surowe dane obu krótkich serii:
[próbki kandydatów](NR-v51-superior-colour-GPU.samples.csv),
[obie kontrole A/A](NR-v51-resolve-AA-control.samples.csv).

## Pozostałe hipotezy i odtworzenie

Wspólny kafel proxy/residualu zachował 1008 porównań bitowo, lecz nie wykazał
przekonującego zysku. Przesiew alternatywnych grup 8×4, 16×4, 16×8, 8×16,
16×16 i 32×2 również nie dał podstaw do zmiany; był wykonany przed poprawą
kontroli dryfu i nie jest dowodem uniwersalnej regresji tych układów.
Usuwanie redundantnych warunków promienia i utrwalenie sigma w kompilatorze
zmieniało zaokrąglenia, więc nie weszło do produktu. Pętle i arithmetic
pozostawiono identyczne z v50.

1. Zachować `dlssnr_superior_normal_Shader.cso` z v50
   `e3babe377ca4bad489f8dfbdbba3b2c3be6a81db` w katalogu baseline.
2. Uruchomić `tools/compile_nr_interpass.ps1`.
3. Skompilować fixture `superior_policy.cpp`, `superior_resolve_quality.cpp`
   i `superior_resolve_timing.cpp` w MSVC x64 `/std:c++17 /EHsc /O2`.
   Dwa fixture GPU linkują `d3d12.lib dxgi.lib`.
4. `superior_resolve_quality.exe baseline OptiScaler/shaders/dlssnr/precompile color`
   wykonuje pełną macierz przez produkcyjny selektor i fallback.
5. `superior_resolve_timing.exe baseline OptiScaler/shaders/dlssnr/precompile wynik.csv sdr`
   uruchamia SDR. Dla replace użyć `hdr2` lub `hdr4`.
6. Powtórzyć wszystkie profile przy wolnym GPU. Kontrolę A/A wykonać przez
   `normal-pair`, z katalogiem baseline po obu stronach (wymaga też
   niezmienionych `DlssNr_Shader.cso` i carriera w katalogu fixture).
7. Zysk całego NR sprawdzić osobno w grze: te same modele/passy/preset A/K,
   rozdzielczość, HDR i scena; rozgrzewka, ABBA i timestampy całego toru.
