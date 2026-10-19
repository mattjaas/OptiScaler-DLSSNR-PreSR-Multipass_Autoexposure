# NR v56 — współdzielenie próbek końcowego low

Przeniesiono pomysł współdzielenia próbek z interpassowego Low36/cache
do **końcowego** filtra low Superior Scaling (Mode 18, Transfer 1).
Interpassy nie są zmieniane. Bazą jest v55
`ea336e3133d1364b87d639585ed4b6984c7fba4f`, z zachowaną optymalizacją
automatycznej ekspozycji. Nie należy dodawać wyników różnych benchmarków
v55/v56 ani ich procentów do całkowitego czasu NR.

## Co przeniesiono

Grupa 8×4 wyjściowych pikseli low współdzieli 36×20 biliniowych próbek
zdekodowanego signed residualu. Sąsiednie filtry nakładają się na siatce
o kroku 0,25 footprint; przesunięcie wyjściowego piksela odpowiada czterem
krokom siatki. Zamiast 32×64=2048 próbek na pełną grupę jest 720 próbek
oraz odczyty LDS. To redukcja liczby instrukcji próbkowania, nie procent
przyspieszenia całego NR.

Pozostają **wszystkie 64 próbki na wynik**, wagi
`7,11,15,17,17,15,11,7`, normalizacja 10000, kolejność sumowania,
sanitizacja, clamp na krawędziach i zapis FP16/FP32. Nie przeniesiono
redukcji filtra do 36 próbek. Przy współdzieleniu współrzędne FP32 mogą
zaokrąglać się inaczej; zgodność niżej dotyczy zmierzonych danych.

Koszt: 8640 bajtów LDS na grupę oraz jedna synchronizacja wątków grupy.
Każdy wątek uczestniczy w synchronizacji przed odrzuceniem wątków poza
ostatnią częściową grupą. Nie ma nowego dispatchu, tekstury, kopii,
bariery zasobu D3D12, PSO, alokacji w klatce, pomiaru ani opcji menu.

Pozostają ten sam slot PSO, grupy 8×4 i produkcyjny selektor
`SuperiorScaling::LowResidual`. Inne zastosowania Mode 18 i błąd tworzenia
specjalizacji nadal używają ogólnego shadera z grupami 8×8. Niezgodne
rozmiary source/model nadal dają zerowy residual. Cache jest kompilowany
wyłącznie dla dotychczasowego dedykowanego PSO low.

Nie zmieniają się gainy, ich skalowanie, średnia low po passach, modele,
liczba passów, harmonogram interpassów, HDR, encode, Area ani final resolve.
Z 21 śledzonych binarek shaderów zmienia się **tylko** `superior_low`;
pozostałe 20, w tym wszystkie interpassy i resolve, jest identycznych z v55.
Nowy low SHA256:
`3312eedf5142ee6a6060bd3c20b2dbcd8d52d88cdb760c1d008132d4e5c26808`.

## Weryfikacja obrazu

RTX 4090, NVIDIA 616.56: **192 porównania**, 6411840 słów 32-bitowych:

- 144 przypadki małych/nieparzystych rozmiarów, jednej osi, 1×1 i częściowych
  grup; niezależnie FP16/FP32 źródło i cel, zwykłe, bardzo małe/duże,
  signed oraz NaN/Inf/subnormal dane.
- 48 przypadków work 1920×1080, 2266×1274 i 2496×1404 przy native 4K,
  z tymi samymi kombinacjami formatów i czterema wzorami danych.

Maksymalna różnica bezwzględna, znormalizowana i RMS: **0** w obu
zestawach, FP16 i FP32. Zgodne są alpha i klasy niefinitywnych wartości.
Test mierzy pole low; downstream resolve ma niezmienione binaria.
Nie wykonano oceny gry w ruchu ani inferencji NGX. Wynik nie jest
formalną gwarancją wszystkich możliwych danych, GPU i sterowników.

Skrypt DXC kompiluje 19 wariantów, generuje nagłówek/CSO i sprawdza
jedną tablicę LDS 2160 float oraz grupę 8×4 dla nowego low. Uruchomiono
dziewięć testów interpassów, integrację Superior i zachowanych benchmarków.
Fixture C++17 kompilują się lokalnie w MSVC v142; pełną DLL buduje v143
na GitHub Actions. Agent nie czeka na sam build, zgodnie z prośbą użytkownika.

## Pomiar GPU

RTX 4090, NVIDIA 616.56, native 3840×2160, P50/P59/P65. Syntetyczne
sinusoidalne proxy i oryginał FP32, signed residual po niezmienionym
carrierze, low i wynik FP16. Zasoby rezydentne i te same dla A/B.
Radius 1, range sigma 0,015, spatial sigma 3, guided strength 1,
high 0,8 / low 0,661, WhitePoint 1, TransferStrength/ColourStrength 1,
MaxRatio 2, MaxDarkening 100, skin/environment 1, inspector i przywracanie
natywnego detalu wyłączone. Gainy są jednakowe po obu stronach przy każdej
skali; to nie pomiar ich przeliczania. Profile: SDR i HDR mapping 0/2/4.

Timestamp obejmuje **low + niezmieniony końcowy resolve z barierami**,
cztery kompletne sekwencje na próbkę, wynik dzielony przez 4.
Nie obejmuje encode/Area, ekspozycji, modeli NR, interpassów, DLAA,
uploadu, alokacji, pierwszego tworzenia PSO ani fence CPU.
A/K i liczba passów nie uczestniczą w tym benchmarku. To fragment toru,
a nie zysk całej klatki czy całego NR w grze.

Osiem bloków naprzemiennych ABBA/BAAB, przed każdym 16 kwartetów
rozgrzewki, następnie 40 kwartetów pomiarowych. Kwartet jest nagrywany
w jednym command list. To 640 timestampów na wariant/profil/skalę.
Dwie pełne serie i A/A z bazowym low 8×4 po obu stronach; zegary dynamiczne.

Sparowany zysk to średnia dwóch A minus średnia dwóch B w kwartetach,
następnie mediana 320 kwartetów. Dodatnie bloki oznaczają dodatnią medianę
sparowanych różnic danego bloku. Dodatni wynik oznacza szybciej.
Wyniki i zmienność są w tabelach niżej.

**Zysk jest bardzo mały.** P65 powtarza około 0,003 ms, ale P50 SDR nie
wykazuje mocnego potwierdzenia poprawy. Rozrzut pojedynczych kwartetów A/B
jest duży, a sama różnica niesparowanych median bywa myląca. Nie należy
przedstawiać wysokich pojedynczych wyników pierwszej serii jako typowego
zysku ani twierdzić, że poprawa jest potwierdzona dla wszystkich skal.
Potrzebny pozostaje pomiar całego NR w tej samej scenie gry, po rozgrzewce,
z przeplatanymi wersjami i tymi samymi ustawieniami.

## Inne próby zapożyczenia z interpassów

Nie wdrożono następujących prototypów:

- Jeden `exp2(a+b)` zamiast `exp2(a)*exp2(b)` w końcowym guided resolve.
  Krótki pomiar HDR dał około -0,0003 do +0,0008 ms; to za mało, aby
  deklarować zysk. Początkowy SDR P50 był niestabilny. Nie zmieniono wzoru wag.
- Kafel proxy i signed residualu dla końcowego resolve, analogiczny do
  interpassowego współdzielenia stencil. Dwa pola 10×10 i metadane zajmują
  2412 B LDS. Dynamiczna granica ogranicza pobieranie do potrzebnych próbek.
  Audyt 7776 małych przypadków i 12 wyjść 4K nie wykrył różnic liczbowych.
  Dwie pełne serie ujawniły jednak regresję HDR soft knee około
  0,029–0,074 ms. Zysk w innych profilach nie uzasadnia zastąpienia całego
  resolve. Wariant ładowania 2D poprawiał P59/P65, ale nadal zwalniał P50.
- Usunięcie ponownej sanitizacji po odczycie cache low zmieniło arytmetykę
  wygenerowaną przez DXC. W stress 4K FP32 błąd znormalizowany 1,40015e-5
  przekroczył przyjęty budżet 1e-5. Zachowano sprawdzoną wersję z zerową
  różnicą w audycie; odrzuconego shadera nie mierzono dalej.

Nie włączono kolejnych ścieżek ani polityk wybieranych z trzech punktów
pomiarowych. Dane odrzuconych prób są oddzielone od produkcyjnego cache low.

[Surowe próbki produkcyjnego low i odrzuconych prototypów](NR-v56-superior-shared-GPU.samples.csv).

## Odtworzenie

`baseline` zawiera shadery v55, `candidate` bieżące shadery v56.
Workflow kompiluje oba nowe fixture z `/std:c++17 /EHsc /O2` i
`d3d12.lib dxgi.lib`.

```powershell
./tools/compile_nr_interpass.ps1
./superior_cached_low_quality.exe baseline candidate
./superior_cached_low_quality.exe baseline candidate --4k
./superior_cached_low_timing.exe baseline candidate abba.csv normal
./superior_cached_low_timing.exe baseline candidate repeat.csv normal
./superior_cached_low_timing.exe baseline candidate aa.csv aa
```

## Wyniki obu serii i kontroli A/A

### abba

| Profil | Skala | v55 ms | v56 ms | Sparowany zysk ms | Q25 / Q75 ms | Dodatnie bloki |
|---|---:|---:|---:|---:|---:|---:|
| sdr | P50 | 0.337280 | 0.329984 | 0.005056 | -0.032512 / 0.063328 | 8/8 |
| sdr | P59 | 0.355072 | 0.353792 | 0.001152 | -0.054816 / 0.059296 | 7/8 |
| sdr | P65 | 0.362624 | 0.360960 | 0.002816 | -0.054240 / 0.058528 | 7/8 |
| hdr0 | P50 | 0.291840 | 0.293504 | 0.002048 | -0.050944 / 0.031424 | 8/8 |
| hdr0 | P59 | 0.310272 | 0.302720 | 0.003392 | -0.010368 / 0.060480 | 8/8 |
| hdr0 | P65 | 0.318720 | 0.315392 | 0.002944 | -0.053280 / 0.055648 | 7/8 |
| hdr2 | P50 | 0.266240 | 0.264448 | 0.001792 | -0.033056 / 0.052384 | 8/8 |
| hdr2 | P59 | 0.293632 | 0.283392 | 0.010240 | -0.016576 / 0.054976 | 8/8 |
| hdr2 | P65 | 0.313088 | 0.300544 | 0.012416 | -0.023296 / 0.063808 | 8/8 |
| hdr4 | P50 | 0.264960 | 0.263168 | 0.002176 | -0.042688 / 0.055200 | 8/8 |
| hdr4 | P59 | 0.286720 | 0.284672 | 0.001920 | -0.049056 / 0.040736 | 8/8 |
| hdr4 | P65 | 0.304384 | 0.300800 | 0.003392 | -0.021248 / 0.057216 | 8/8 |

### repeat

| Profil | Skala | v55 ms | v56 ms | Sparowany zysk ms | Q25 / Q75 ms | Dodatnie bloki |
|---|---:|---:|---:|---:|---:|---:|
| sdr | P50 | 0.334208 | 0.334080 | 0.000704 | -0.060832 / 0.050496 | 5/8 |
| sdr | P59 | 0.353920 | 0.353280 | 0.001280 | -0.052608 / 0.057280 | 6/8 |
| sdr | P65 | 0.363776 | 0.361216 | 0.003328 | -0.052096 / 0.059072 | 7/8 |
| hdr0 | P50 | 0.304384 | 0.300160 | 0.002176 | -0.034400 / 0.057344 | 7/8 |
| hdr0 | P59 | 0.307200 | 0.305920 | 0.001280 | -0.052832 / 0.034016 | 6/8 |
| hdr0 | P65 | 0.316928 | 0.313600 | 0.003264 | -0.033920 / 0.043744 | 8/8 |
| hdr2 | P50 | 0.266496 | 0.264448 | 0.001664 | -0.045504 / 0.033376 | 8/8 |
| hdr2 | P59 | 0.288768 | 0.286464 | 0.002496 | -0.027456 / 0.056608 | 8/8 |
| hdr2 | P65 | 0.305664 | 0.302336 | 0.003200 | -0.027168 / 0.053664 | 8/8 |
| hdr4 | P50 | 0.265216 | 0.263424 | 0.001536 | -0.049120 / 0.028352 | 8/8 |
| hdr4 | P59 | 0.286976 | 0.284928 | 0.002304 | -0.039488 / 0.053120 | 8/8 |
| hdr4 | P65 | 0.303872 | 0.300800 | 0.003456 | -0.037472 / 0.048096 | 7/8 |

### aa

| Profil | Skala | v55 ms | v56 ms | Sparowany zysk ms | Q25 / Q75 ms | Dodatnie bloki |
|---|---:|---:|---:|---:|---:|---:|
| sdr | P50 | 0.348544 | 0.345216 | -0.000128 | -0.002176 / 0.002336 | 3/8 |
| sdr | P59 | 0.358144 | 0.358656 | 0.000128 | -0.001408 / 0.001792 | 4/8 |
| sdr | P65 | 0.370816 | 0.372480 | -0.000256 | -0.002048 / 0.001536 | 2/8 |
| hdr0 | P50 | 0.301568 | 0.300800 | 0.000128 | -0.001152 / 0.001440 | 4/8 |
| hdr0 | P59 | 0.315392 | 0.313856 | -0.000128 | -0.001024 / 0.001024 | 3/8 |
| hdr0 | P65 | 0.328064 | 0.328192 | 0.000000 | -0.001664 / 0.001280 | 4/8 |
| hdr2 | P50 | 0.265216 | 0.265472 | 0.000128 | -0.001280 / 0.001152 | 3/8 |
| hdr2 | P59 | 0.288256 | 0.288256 | 0.000000 | -0.001280 / 0.001440 | 4/8 |
| hdr2 | P65 | 0.303104 | 0.303104 | 0.000000 | -0.001824 / 0.001920 | 5/8 |
| hdr4 | P50 | 0.264192 | 0.264192 | -0.000128 | -0.001408 / 0.001024 | 4/8 |
| hdr4 | P59 | 0.286848 | 0.286976 | -0.000256 | -0.001664 / 0.001152 | 2/8 |
| hdr4 | P65 | 0.302080 | 0.302080 | 0.000000 | -0.001664 / 0.001920 | 3/8 |
