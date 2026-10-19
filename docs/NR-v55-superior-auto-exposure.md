# NR v55 — automatyczna ekspozycja HDR poza interpassami

Zmiana dotyczy przygotowania wejścia Superior Scaling przy źródle white point
`Automatic HDR exposure` i natywnej teksturze `R16G16B16A16_FLOAT`.
Interpassy nie są zmieniane. Bazą pomiaru i zgodności jest v54,
commit `181aa3df16310341b40e5279c8695ec5d18b8df0`.

## Implementacja

- Mode 3 ma osobny PSO ze stałym numerem trybu. Zachowuje grupy 8×8,
  64×64 grup na obraz i to samo przypisanie pikseli do wątków.
  Odczytywane są wszystkie piksele źródła. Nie zmieniono luma,
  sanitizacji, średniej, granic kafli ani obsługi krawędzi/małych obrazów.
  Pozostaje dotychczasowe drzewo sumowania 64 sum częściowych.
- Mode 11 ma osobny PSO dla redukcji 64×64 do white point 1×1. Pozostają
  wagi powierzchni, highlight protection, PreExposure i wzór ekspozycji.
- Stałe tryby pozwalają kompilatorowi usunąć kod i LDS innych etapów.
  Każdy z dwóch nowych shaderów ma jedną tablicę LDS 256 float.
  W produkcji nie ma nowych instrukcji wave ani zmian geometrii grup.
  Nie zmierzono liczników sprzętowych; przypisanie zysku wyłącznie
  mniejszemu użyciu LDS/rejestrów byłoby hipotezą.

Dochodzi dwa PSO tworzone leniwie i pamiętane, bez nowych dispatchy,
tekstur, kopii, barier, timestampów ani alokacji w każdej klatce.
Nie dodano opcji ani suwaków. Gainy i ich skalowanie, modele/passy,
Area, encode, low, resolve, DLAA i harmonogram interpassów pozostają v54.
Wszystkie **19 wcześniej śledzonych binarek shaderów** jest identycznych
z bazą, łącznie z ogólnym shaderem, Area i wszystkimi interpassami.

## Selekcja i fallback

Flaga specjalizacji jest ustawiana tylko wewnątrz dotychczasowej gałęzi
auto HDR, gdy Superior jest włączony i format źródła to RGBA16F. Ekspozycja
gry/manualna, SDR i pozostałe formaty zachowują wcześniejsze PSO.
`ExposureStage` dodatkowo sprawdza dokładną flagę, tryb, rozmiary źródła
i celu oraz niezerowe rozmiary obrazu mierzonego. Tryb meter wymaga celu
64×64, white reduction celu 1×1 i źródła 64×64.

Błąd utworzenia dowolnego PSO przywraca jego tryb w ogólnym shaderze.
Nie przechodzi do resolve Mode 1. Mode 3 przekazuje 64×64 **grup** zarówno
dla specjalizacji, jak i fallbacku 8×8. Mode 11 i fallback również
zachowują grupę 8×8. Frame hold,
gotowość zasobów i dotychczasowe przejścia ich stanów pozostają bez zmian.

Próby innych układów grup zostały odrzucone. Poprawnie skompilowane
8×8 / 16×4 / 32×2 / 64×1 przy 4K P65 i ochronie 100% dały w krótkim
ABBA odpowiednio około 0,0141 / 0,0026 / 0,0095 / 0,0143 ms zysku
dla RGBA16F. Zachowano 8×8 ze względu na prostotę i tę samą arytmetykę.
RGBA32F nie zyskał na żadnym z tych układów: około 0,0029 / 0,0054 /
0,0084 / 0,0727 ms regresji. Nie włączono dla niego specjalizacji.
Inne formaty nie są niezmierzonymi zwycięzcami. Finalne serie obejmują
produkcyjne ograniczenie do RGBA16F; osobne dane porównania geometrii
mają oznaczenie `geometry-8/16/32/64` i nie opisują innych ustawień gry.

## Audyt różnic liczbowych

RTX 4090, sterownik NVIDIA 616.56. Porównanie obejmuje meter, white point,
natywne proxy, zachowaną kopię HDR i wynik Area; oddzielnie FP16/FP32
źródła i celów. To rzeczywiste wyniki shaderów D3D12 odczytane z GPU.

| Zestaw | Przypadki | Porównane słowa 32-bitowe |
|---|---:|---:|
| Małe i nieparzyste obrazy, stress | 2880 | 217294560 |
| Native 3840×2160, P50/P65 | 24 | 1395594336 |

Pierwszy zestaw używa 1×1, 3×5, 17×11, 65×37, 129×73, 257×143;
30 scenariuszy ochrony 0/50/100%, PreExposure 0,125/8, trim 1/5,
pięciu mapowań HDR i ośmiu anchorów, oraz czterech wzorów danych
(zwykłe, bardzo małe, duże i NaN/Inf/subnormal/max-float).
Jest zgodny liczbowo we wszystkich porównanych skończonych kanałach;
klasy niefinitywnych wartości również pozostają zgodne.

Zestaw 4K używa zwykłego sinusoidalnego źródła, trzech poziomów ochrony
i dwóch skal. Błąd znormalizowany to `abs(A-B)/max(1,abs(A))` dla RGB.
White point jest dodatkowo oceniany względnie do swojej wartości.
Alpha musi pozostać zgodna; NaN/Inf muszą zachować klasyfikację.

Wyniki końcowego zestawu 8×8 podano poniżej. Poprzednie małe różnice
zmienionej geometrii nie opisują wersji produkcyjnej.

Audyt sprawdza wejście modelu NR. Nie wykonano inferencji NGX ani
wizualnego porównania gry w ruchu. Zgodność w tych testach nie jest
formalnym dowodem dla wszystkich możliwych obrazów i urządzeń.

## Metoda pomiaru GPU

Syntetyczny test D3D12 na RTX 4090, native 3840×2160, P50/P59/P65,
źródła RGBA16F i RGBA32F, cele proxy/HDR/Area RGBA16F, meter/white RGBA32F.
Mapping HDR soft knee 0, PreExposure 1, trim 1, brak anchorów,
highlight protection 0% i 100%. Źródło jest deterministycznym wzorem
sinusoidalnym z fixture wejścia. Zasoby pozostają na GPU.

Timestamp obejmuje razem **meter → white → niezmienione encode →
niezmienione Area**, z ich barierami i przywróceniem stanów zasobów.
Nie obejmuje NR/NGX, DLAA, interpassów ani końcowego resolve. Nie wolno
przedstawiać go jako całkowitego zysku gry ani dodawać procentów do v54.
Nie zmienia liczby passów NR; benchmark ich nie uruchamia.

Każdy scenariusz ma osiem bloków ABBA/BAAB. Przed każdym blokiem jest
osobna rozgrzewka 16 kwartetów, potem 40 kwartetów mierzonych.
Jeden timestamp obejmuje cztery kompletne sekwencje i jest dzielony przez 4.
To 640 próbek na wariant/scenariusz. Wykonano dwie serie A/B i kontrolę
A/A; etykieta `v55` w A/A oznacza drugi zestaw tych samych bazowych PSO.
Zegary GPU są dynamiczne. Obciążenie podczas pomiaru pochodzi z fixture.

Raport podaje mediany A i B oraz sparowany zysk: dla każdego kwartetu
średnia dwóch A minus średnia dwóch B, następnie mediana i kwartyle.
Zmienność kwartetów i A/A jest ważniejsza niż ostatnia cyfra mediany.
Pełne dane: [NR-v55-superior-exposure-GPU.samples.csv](NR-v55-superior-exposure-GPU.samples.csv).

Wstępna próba sugerowała około 0,043 ms oszczędności. Wykryto w niej
błąd prototypu: indeksowanie/pętle 32×2 przy grupie nadal 8×8. Odrzucono
jej wyniki. Poprawny 32×2 przeszedł audyt, ale dał około 0,01 ms.
To finalne testy prawidłowego PSO 8×8 służą do deklarowania zysku.
Skrypt kompilacji sprawdza teraz również geometrię grup obu nowych PSO.
Rozważano również samodzielny Mode 10 guides;
różnica była zbyt mała, aby dołączać go do tej wersji.

## Weryfikacja i odtworzenie

Uruchomiono dziewięć `tests/test_nr_interpass_*.py`, testy integracji
Superior, zachowanych benchmarków i selektora C++. DXC kompiluje
19 wariantów, sprawdza LDS i generuje dwa nowe nagłówki/CSO.
Testy C++ zachowują PCH jako pierwszy include; nagłówki nie mają PCH.

```powershell
./tools/compile_nr_interpass.ps1
./superior_exposure_quality.exe baseline candidate
./superior_exposure_quality.exe baseline candidate --4k
./superior_exposure_timing.exe baseline candidate abba.csv normal
./superior_exposure_timing.exe baseline candidate repeat.csv normal
./superior_exposure_timing.exe baseline candidate aa.csv aa
```

`baseline` zawiera binarki v54, `candidate` aktualne shadery. Fixture
kompiluje workflow `build_nr_detail_quality_experiments.yml` (C++17,
`d3d12.lib dxgi.lib`). Lokalny MSVC v142 wystarcza do tych testów;
pełna DLL wymaga v143 i jest budowana na GitHub Actions. Zgodnie
z prośbą użytkownika agent nie oczekuje na zakończenie samego buildu.


## Finalne wyniki GPU (ms)

| Seria | Ochrona | Skala | v54 | v55 | Zysk median | Sparowany zysk | Q25 / Q75 zysku |
|---|---:|---:|---:|---:|---:|---:|---:|
| abba | 0% | P50 | 0.241152 | 0.224768 | 0.016384 | 0.017408 | 0.012800 / 0.021440 |
| abba | 0% | P59 | 0.246784 | 0.231936 | 0.014848 | 0.014528 | 0.012640 / 0.017056 |
| abba | 0% | P65 | 0.246528 | 0.232064 | 0.014464 | 0.014336 | 0.011904 / 0.016256 |
| abba | 100% | P50 | 0.241152 | 0.224768 | 0.016384 | 0.016640 | 0.014336 / 0.018432 |
| abba | 100% | P59 | 0.249728 | 0.234496 | 0.015232 | 0.015296 | 0.013056 / 0.017408 |
| abba | 100% | P65 | 0.250368 | 0.236288 | 0.014080 | 0.014272 | 0.012128 / 0.016416 |
| repeat | 0% | P50 | 0.237824 | 0.222208 | 0.015616 | 0.015872 | 0.012512 / 0.019712 |
| repeat | 0% | P59 | 0.246016 | 0.231808 | 0.014208 | 0.014016 | 0.011008 / 0.018048 |
| repeat | 0% | P65 | 0.247040 | 0.232960 | 0.014080 | 0.014144 | 0.011488 / 0.017056 |
| repeat | 100% | P50 | 0.241664 | 0.226048 | 0.015616 | 0.016000 | 0.012032 / 0.018976 |
| repeat | 100% | P59 | 0.250368 | 0.235008 | 0.015360 | 0.015488 | 0.011776 / 0.018048 |
| repeat | 100% | P65 | 0.249856 | 0.237312 | 0.012544 | 0.012800 | 0.010112 / 0.016000 |

aa, fp16: maks. bezwzględna różnica median 0.001792 ms; maks. bezwzględny sparowany wynik 0.002048 ms.

aa, fp32: maks. bezwzględna różnica median 0.000256 ms; maks. bezwzględny sparowany wynik 0.000384 ms.

abba, fp32: maks. bezwzględna różnica median 0.000256 ms; maks. bezwzględny sparowany wynik 0.000384 ms.

repeat, fp32: maks. bezwzględna różnica median 0.000256 ms; maks. bezwzględny sparowany wynik 0.000384 ms.

Finalny audyt 8x8: meter/white/proxy/HDR/Area mają zerowy błąd bezwzględny i RMS w obu zestawach, FP16 i FP32. Zgodne są również alpha i klasy niefinitywne.

SHA256 meter: `5d4bb7994751d7a710b1ef190fd6e1d4c7176e2ca10996305a59509131a813e4`.

SHA256 white: `81120ca7cd791f19b6b520eabc1a7353bf199f9332c19b1f5bd3f80676fb0123`.
