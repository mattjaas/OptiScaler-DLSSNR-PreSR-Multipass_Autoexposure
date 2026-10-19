# NR v57 — próby FP16 i większy cache końcowego low

**Produkcja zachowuje obliczenia i cache FP32.** Próby FP16 wykazały,
że poprawę daje przede wszystkim większa grupa współdzieląca próbki.
Wdrożono więc zmianę grupy końcowego low z 8×4 na 8×8, bez obniżania
precyzji i wymagań sprzętowych. Baza: v56, commit audytu
`2a97b7b22c675ba69bf161ea05d0c59de910a629`.

FP16 w samym cache przeszedł sprawdzony zestaw bez różnic, ale przy tej
samej geometrii nie wykazał przekonującej przewagi nad FP32.
Bardziej agresywne mnożenia FP16 zwiększały błędy liczbowe.
Eksperymenty są odtwarzalne offline, poza domyślną ścieżką DLL.

## Zmiana produkcyjna

Końcowy Mode 18 / Transfer 1 Superior Scaling używa istniejącego PSO low:

| Cecha | v56 | v57 |
|---|---:|---:|
| Grupa wyjściowa | 8×4 | 8×8 |
| Wspólny kafel próbek | 36×20 | 36×36 |
| Typ cache | float3 | float3 |
| LDS na grupę | 8640 B | 15552 B |
| Próbki na piksel pełnej grupy | 22,5 | 20,25 |

Liczba wspólnych próbek na piksel spada o 10%; nie jest to procent
przyspieszenia całego NR. Grupa potrzebuje więcej LDS, ale obsługuje
dwa razy więcej wyników i ponownie wykorzystuje więcej próbek w pionie.
Pozostają wszystkie 64 tapy na wynik, dodatnie wagi, ich kolejność,
sumowanie, normalizacja, sanitizacja i clamp. Wszystkie działania są FP32.
Wszystkie wątki uczestniczą w synchronizacji przed odrzuceniem wątków
poza ostatnią częściową grupą. Niezgodne wymiary wejść nadal dają zero.

Host używa `LowGroupHeight=8`, zgodnie z binarką. Generic fallback
pozostaje 8×8. Nie dochodzi PSO, tekstura, dispatch, kopia, bariera zasobu,
alokacja w klatce, opcja menu, diagnostyka ani zapytanie o obsługę FP16.
Interpassy, modele, gainy, ich skalowanie i średnia low pozostają zachowane.

Zmienia się tylko `dlssnr_superior_low_Shader.cso` i jego nagłówek;
pozostałe 20 istniejących CSO jest bajtowo identycznych z bazą.
Nowy SHA256: `ac6526a5e497e177fe303ac6d517cd9a78f23ac1fd309dd4d889190536a071f8`.
Historyczne fixture v55/v56 mają jawne grupy 8×4, żeby zmiana aktualnej
stałej produkcyjnej nie zafałszowała odtwarzania starych pomiarów.

## Co sprawdzono z FP16

- `half4`: cache `float16_t3`, dotychczasowa grupa 8×4 / kafel 36×20,
  4320 B LDS. W pilocie zysk był bliski szumowi pomiaru.
- `half8`: cache `float16_t3`, grupa 8×8 / kafel 36×36, 7776 B LDS.
  Współrzędne, sanitizacja, produkty i sumy pozostają FP32. Względem v56
  widać poprawę, lecz kontrola `float8` ujawnia udział zmiany geometrii.
- `float8`: ta sama grupa 8×8, cache FP32. To wybrany wariant produkcyjny.
- `product16`: cache FP16 i mnożenie próbki przez znormalizowaną wagę
  w FP16, akumulator FP32. Test wejścia FP16 dał max błąd znormalizowany
  0,00306214 i bezwzględny 1,30762 w stress HDR; przekroczył istniejący
  limit numeryczny. Nie był dalej mierzony ani wdrażany.
- `range16`: obliczanie kwadratów różnicy koloru w FP16, sumy/exp2 FP32.
  Audyt resolve wykazał max błąd znormalizowany 0,000388235 oraz RMS
  0,0000119873 dla wyniku FP32, ponad istniejący limit 0,00001.
  Nie był dalej mierzony ani wdrażany.

Próba zawężania dowolnego źródła FP32 do FP16 zmieniała też klasyfikację
niefinitywnych wartości w stress. Dlatego testy cache FP16 używają go
wyłącznie z teksturą residualu RGBA16F; dla RGBA32F zachowują bazowy PSO.
W produkcyjnym `float8` oba formaty korzystają z cache FP32.

Natywne prototypy używają SM6.2 i `-enable-16bit-types`, zgodnie z
[dokumentacją DXC](https://github.com/microsoft/DirectXShaderCompiler/wiki/16-Bit-Scalar-Types).
Samo `half` bez odpowiedniej kompilacji nie dowodzi obliczeń 16-bitowych.
Reprodukowany DXIL cache zawiera tablicę `half`, a arytmetyka pozostaje
`float`. Te binarki nie są wiązane w produkcyjnej DLL.

## Zgodność obrazu

RTX 4090, NVIDIA 616.56: produkcyjny `float8` przeszedł **264 porównania,
8 816 280 słów 32-bitowych, zero różnic bitowych** względem v56.
Cache `half8` również przeszedł ten zestaw przy opisanym ograniczeniu
formatu, z bazowym fallbackiem dla residualu FP32.

- 198 małych przypadków / 54 648 słów: dziewięć rozmiarów, w tym 1×1,
  1×19, 23×1, nieparzyste wymiary i częściowe grupy; niezależnie
  FP16/FP32 tekstura wejściowa i wynik.
- 66 przypadków większych / 8 761 632 słów: work 1920×1080,
  2266×1274, 2496×1404 odpowiadające native 4K przy P50/P59/P65.
- Wzorce signed, małe/duże wartości HDR, NaN/Inf, surowe losowe bity
  binary16, ±0, subnormal, wartości graniczne, sNaN/qNaN oraz signed szum.
  Dodatkowe surowe wzorce binary16 dotyczą wejść FP16.

Porównano surowe słowa, w tym alpha i payloady niefinitywnych wyników,
bez tolerancji. To pole low; downstream resolve i pozostałe shadery mają
niezmienione binaria. Nie testowano inferencji NGX ani obrazu gry w ruchu.
Zgodność dotyczy tych danych, GPU i sterownika, nie wszystkich możliwych
obrazów lub implementacji próbkowania tekstur.

## Pomiar GPU

RTX 4090, NVIDIA 616.56, native 3840×2160, P50/P59/P65.
Rezydentne syntetyczne sinusoidalne proxy/oryginał FP32, signed residual
z niezmienionego carriera FP16, low i wyjście FP16. Guided radius 1,
range sigma 0,015, spatial sigma 3, strength 1, high 0,8 / low 0,661,
white point 1, TransferStrength/ColourStrength 1. Profile SDR i HDR
mapping 0/2/4, bez inspektora/przywracania detalu. A/K i liczba modeli
nie uczestniczą w tym teście.

Timestamp obejmuje **końcowy low + niezmieniony resolve i bariery**,
cztery pełne sekwencje na próbkę, czas dzielony przez cztery. Poza pomiarem
są modele NR/DLAA, interpassy, encode/Area, ekspozycja, upload, alokacje,
pierwsze tworzenie PSO oraz CPU fence. To nie czas całej klatki w grze.

Każda pełna seria: 12 konfiguracji, 8 bloków, rozgrzewka 16 kwartetów
przed blokiem i 40 kwartetów pomiarowych, naprzemiennie ABBA/BAAB.
**640 próbek na stronę i konfigurację**; kwartet w jednym command list.
Pilot ma 2 bloki po 20 kwartetów (80 próbek na stronę/konfigurację).
Te same zasoby i fizyczny cel po obu stronach. Kontrola A/A używa
tego samego bazowego PSO 8×4 i tej samej geometrii po obu stronach.

Odtwarzanie wideo wstrzymano przed pomiarami. Nie było modelu TTS w VRAM.
W idle GPU około 7–13%, około 2,25 GB VRAM; podczas pełnej serii
wyrywkowo 98%, 2340 MHz rdzenia, 10501 MHz pamięci, P0, 352 W i 64°C.
Zegary były dynamiczne; to obserwacje punktowe, nie pełny log telemetryczny.
Nie należy sumować tych zysków z wynikami innych benchmarków.

W tabeli A/B to mediany pierwszej serii `float8`. Δ jest medianą sparowanych
różnic średnich A−B w kwartetach; w nawiasie IQR. Wszystko w ms,
**dodatnia Δ oznacza szybciej**. Dwie globalne mediany mogą dawać inną
różnicę niż parowanie wskutek dryfu.

| Profil / P | Mediana v56 / v57 | Δ seria 1 (IQR) | Δ powtórka (IQR) | Δ A/A (IQR) |
|---|---:|---:|---:|---:|
| sdr / 50 | 0.318720 / 0.318464 | +0.000768 (0.002432) | +0.001024 (0.002112) | +0.000000 (0.002592) |
| sdr / 59 | 0.338688 / 0.331520 | +0.006912 (0.003136) | +0.007040 (0.003136) | +0.000128 (0.002208) |
| sdr / 65 | 0.349696 / 0.341120 | +0.007040 (0.003776) | +0.007680 (0.004096) | +0.000000 (0.002464) |
| hdr0 / 50 | 0.282240 / 0.282624 | +0.000640 (0.001408) | +0.000960 (0.002592) | +0.000000 (0.002176) |
| hdr0 / 59 | 0.305280 / 0.297728 | +0.007040 (0.004256) | +0.007168 (0.003360) | +0.000000 (0.001792) |
| hdr0 / 65 | 0.317568 / 0.308864 | +0.006016 (0.001216) | +0.008320 (0.007296) | +0.000000 (0.002048) |
| hdr2 / 50 | 0.262400 / 0.261888 | +0.000256 (0.000512) | +0.000256 (0.000512) | +0.000000 (0.000640) |
| hdr2 / 59 | 0.284928 / 0.279040 | +0.005760 (0.001280) | +0.005760 (0.001152) | +0.000000 (0.000512) |
| hdr2 / 65 | 0.299264 / 0.292608 | +0.006528 (0.000896) | +0.006528 (0.000768) | +0.000000 (0.000640) |
| hdr4 / 50 | 0.260864 / 0.260608 | +0.000256 (0.000544) | +0.000256 (0.000512) | +0.000000 (0.000640) |
| hdr4 / 59 | 0.281088 / 0.278528 | +0.002688 (0.000768) | +0.005760 (0.001152) | +0.000000 (0.000640) |
| hdr4 / 65 | 0.298240 / 0.291328 | +0.006528 (0.000768) | +0.006528 (0.000640) | +0.000000 (0.000512) |

**Zysk jest mały:** około 0,003–0,008 ms przy P59/P65 w pełnych seriach.
P50 ma małe przesunięcia wobec rozrzutu i nie daje mocnego potwierdzenia
poprawy. Wynik gry może być inny. Wariant jest wspólną zmianą geometrii,
bez selektora procentowego/autotuningu; inne skale nie mają tu pomiarów GPU.

## Czy samo FP16 pomaga przy równej geometrii?

W pełnej serii `half8_vs_float8` A to FP32 8×8, B to FP16 8×8.
Oba mają 36×36 wspólnych próbek; nie porównujemy różnych grup.

| Profil / P | Δ FP32−FP16 ms | IQR ms |
|---|---:|---:|
| sdr / 50 | -0.000256 | 0.002464 |
| sdr / 59 | -0.000128 | 0.002976 |
| sdr / 65 | +0.000640 | 0.002336 |
| hdr0 / 50 | -0.000256 | 0.002048 |
| hdr0 / 59 | -0.000256 | 0.001664 |
| hdr0 / 65 | +0.001024 | 0.002816 |
| hdr2 / 50 | -0.000128 | 0.000512 |
| hdr2 / 59 | -0.000128 | 0.000512 |
| hdr2 / 65 | +0.000000 | 0.000768 |
| hdr4 / 50 | -0.000128 | 0.000544 |
| hdr4 / 59 | -0.000128 | 0.000512 |
| hdr4 / 65 | +0.000000 | 0.000640 |

W większości konfiguracji różnica jest bliska zeru lub na korzyść FP32.
Pojedynczy dodatni wynik HDR0/P65 około 0,001 ms przy IQR 0,0028 ms
nie uzasadnia dodatkowego natywnego PSO i wymagań sprzętowych dla całości.
Z tego powodu FP16 pozostaje prototypem offline.

## Weryfikacja i odtworzenie

DXC kompiluje wszystkie dotychczasowe warianty i aktualizuje low CSO/H.
Sprawdzono grupę 8×8 i LDS 3888 float, zgodność nagłówka z CSO,
niezmienione 20 pozostałych binarek, dziewięć testów interpassów,
integrację Superior oraz zachowane konfiguracje benchmarków.
Fixture kompilują się lokalnie MSVC v142; DLL buduje workflow v143.
Agent nie czeka na sam build, zgodnie z prośbą użytkownika.

W x64 Native Tools Command Prompt (komendy `cl` wymagają MSVC/Windows SDK):

```powershell
python tools/audits/fp16/prepare.py
cl /nologo /std:c++17 /EHsc /O2 tests/nr_gpu/superior_precision_quality.cpp /Fe:artifacts/nr-fp16-audit/quality.exe /Fo:artifacts/nr-fp16-audit/quality.obj /link d3d12.lib dxgi.lib
cl /nologo /std:c++17 /EHsc /O2 tests/nr_gpu/superior_precision_timing.cpp /Fe:artifacts/nr-fp16-audit/timing.exe /Fo:artifacts/nr-fp16-audit/timing.obj /link d3d12.lib dxgi.lib
./artifacts/nr-fp16-audit/quality.exe artifacts/nr-fp16-audit/baseline artifacts/nr-fp16-audit/float8 float8
./artifacts/nr-fp16-audit/quality.exe artifacts/nr-fp16-audit/baseline artifacts/nr-fp16-audit/float8 float8 --4k
./artifacts/nr-fp16-audit/timing.exe artifacts/nr-fp16-audit/baseline artifacts/nr-fp16-audit/float8 abba.csv normal full float8
# Powtórz z innym plikiem CSV, potem zamień normal na aa.
# Dla natywnych prototypów użyj folderu/argumentu half4 lub half8 (wymaga obsługi FP16).
# Równa geometria: baseline=float8, candidate=half8, ostatni argument half8-v-float8.
```

`prepare.py` przypina źródło i bazowe binarki v56, generuje także odrzucone
`product16` i `range16`. Nie edytuje produkcyjnych plików ani procesów.
Zregenerowane cache są bajtowo zgodne z mierzonymi prototypami,
a `float8` jest identyczny z wdrożonym CSO.

[Próbki GPU](NR-v57-superior-precision-GPU.samples.csv) i
[podsumowanie](NR-v57-superior-precision-GPU.summary.csv) zawierają
99 840 próbek / 120 grup konfiguracja-seria: piloty, dwie pełne serie
każdego głównego wariantu, A/A i kontrolę równej geometrii.
Nazwy serii rozróżniają prototypy i produkcję; `candidate` nie zawsze
oznacza wdrożony wariant. Domyślny benchmark gry pozostaje bez zmian.
