# Ekspozycja editu jako wskaźnik ghostingu

2026-10-31. Zgodnie z prośbą policzono ekspozycję 16 zapisanych editów wszystkimi algorytmami obrazu dostępnymi w naszych kontrolkach NR i prywatnego DLSS. **Żadna pojedyncza ekspozycja nie rozdziela wszystkich dobrych/złych zapisów przy jednym progu na CNN i jednym na transformer.** W sprawdzonych prostych połączeniach tych pomiarów również nie znaleziono wspólnego wskaźnika.

## Zakres i odtworzenie wejścia

Surowy DDS przechowuje signed edit po dodatnim mnożniku; neutralny edit w nim wynosi zero. Wejście odtworzono jako `FP16_RNE(clamp(0.5 + E, 0, 65504))`, czyli obraz faktycznie przekazywany prywatnemu DLSS przy potwierdzonych ustawieniach: edit zamiast pełnej odpowiedzi NR, nasycenie 1, kolor głębi wyłączony. Bez gamma/sRGB/tonemappingu. PreExposure prywatnego nośnika = 1. Wszystkie SHA-256 źródeł ponownie zgodne z wcześniejszymi 16 plikami.

Obliczono:

- **Legacy CNN, Legacy K:** logarytmiczny pomiar 0,25R+0,5G+0,25B, endpoint nearest, pełny mianownik, epsilon 1e-6, wynik half RTZ. W prywatnym DLAA `render=output`, więc obie metody mają identyczną siatkę `floor(W/2)×floor(H/2)` i identyczne wyniki.
- **Transformer 2:** średnia arytmetyczna tych samych wag i siatki, clamp luminancji do [0,10000], `0.2354708761/max(mean,0.005)`, half RTZ.
- **Automatic Average:** aktualny miernik NR, wagi 0,2126/0,7152/0,0722, dokładne średnie kafli 64×64, ważenie liczbą pikseli, odrzucanie czarnych kafli. Sprawdzono Ignore bright highlights = 0/50/100% (100% jest domyślne dla Average).
- **Automatic Percentile (log):** aktualna implementacja NR, ten sam obraz i kafle, histogram 64 przedziałów −24..24 EV, sumy fixed-point 1/1024 EV i udział binów według liczebności. Domyślne okno 10–90%, dodatkowo 0–100% i 25–75%.

To dziewięć kolumn ustawień/metod, w tym równoważne CNN/K. Dodatkowo obliczono te same metody na R/G/B osobno, przez powielenie danego kanału do szarego RGB. To wariant badawczy; standardowe metody produkują jedną ekspozycję z RGB.

**Unit** = 1 dla każdego pliku, więc nie odróżnia etykiet. **Manual/Paper white** są nastawami, a **Game exposure/scan** odczytem innego zasobu, nie algorytmem do odzyskania z tych DDS. **Native NGX** nie daje tutaj odczytu własnej tekstury; policzone jawne metody są implementacjami dostępnymi w OptiScalerze, nie pomiarem zamkniętej instancji NGX każdego presetu. Eye adaptation, poprzednia/utrzymana ekspozycja oraz cadence co drugą klatkę Legacy wymagają historii; raport przedstawia świeży pomiar bieżącego wejścia przed adaptacją. Automatic zastosowano diagnostycznie do nośnika prywatnego DLSS, a nie do nieobecnego w DDS oryginalnego obrazu gry.

## Wyniki rzeczywistych PSO GPU

RTX 4090, sterownik 616.56. Wszystkie wartości niżej pochodzą z istniejących produkcyjnych shaderów, bez przebudowy/zmiany HLSL. Skrócono nazwy o `.dds`; CNN/K mają wspólną kolumnę. Dwa ostatnie mierniki to aktualny Automatic NR zastosowany do odtworzonego nośnika.

| Plik | Ghosting | Legacy CNN/K | Transformer 2 | Average, 100% | Percentile 10–90% |
|---|---|---:|---:|---:|---:|
| game1-cnn-ghosting | tak | 0.450684 | 0.479980 | 0.448784 | 0.446005 |
| game1-cnn-ghosting2 | tak | 0.435547 | 0.465332 | 0.434167 | 0.435609 |
| game1-cnn-noghosting | nie | 0.434814 | 0.460693 | 0.431528 | 0.438950 |
| game1-cnn-noghosting2 | nie | 0.431885 | 0.458984 | 0.428574 | 0.428504 |
| game1-transformer-ghosting | tak | 0.439697 | 0.467041 | 0.437301 | 0.437416 |
| game1-transformer-ghosting2 | tak | 0.428223 | 0.449463 | 0.420335 | 0.422901 |
| game1-transformer-noghosting | nie | 0.378662 | 0.389160 | 0.367423 | 0.384214 |
| game1-transformer-noghosting2 | nie | 0.440430 | 0.468994 | 0.439096 | 0.454465 |
| game2-cnn-ghosting1 | tak | 0.467041 | 0.492188 | 0.458529 | 0.447463 |
| game2-cnn-ghosting2 | tak | 0.448486 | 0.473633 | 0.441317 | 0.442956 |
| game2-cnn-noghosting1 | nie | 0.456055 | 0.479004 | 0.445864 | 0.439761 |
| game2-cnn-noghosting2 | nie | 0.453613 | 0.475830 | 0.443047 | 0.445022 |
| game2-transformer-ghosting1 | tak | 0.455078 | 0.478516 | 0.445350 | 0.439653 |
| game2-transformer-ghosting2 | tak | 0.453369 | 0.474854 | 0.441775 | 0.446270 |
| game2-transformer-noghosting1 | nie | 0.417725 | 0.430664 | 0.400272 | 0.403585 |
| game2-transformer-noghosting2 | nie | 0.448242 | 0.468018 | 0.433987 | 0.438077 |

Pełne dziewięć metod/ustawień na RGB: [CSV z GPU, 16 wierszy](validation/nr-edit-exposure-rgb.csv). Wszystkie widoki RGB/R/G/B: [GPU, 64 wiersze](validation/nr-edit-exposure-gpu.csv), [referencja CPU](validation/nr-edit-exposure-study.csv), [pełna analiza JSON](validation/nr-edit-exposure-study.json).

Przykład sprzeczności z pojedynczym kierunkowym progiem już w jednej grze/rodzinie: game1-transformer-ghosting ma Legacy 0,439697, a odpowiadający dobry 0,378662. W drugiej parze ghosting2 ma 0,428223, a dobry noghosting2 **0,440430**. Dobre wyniki występują po obu stronach złych. Nie przypisujemy numerom par procedury zmiany jasności/mnożnika; użytkownik nie podał tego przypisania. Osobne R/G/B również nie dają uniwersalnego progu dla żadnej z dziewięciu kolumn.

## Czy ekspozycja może mimo tego celować w mały zapas mnożnika?

Dodatkowo odtworzono nośnik dla `min(E,0)+rho*max(E,0)`, dodano 0,5, clamp i ponownie zapisano FP16 przy każdej próbie. Zmienia się **tylko dodatni mnożnik**, nie ekspozycja/NR brightness ani ujemne składowe. Nie użyto prostego ilorazu ekspozycji jako propozycji mnożnika: stałe 0,5 i ujemna część uniemożliwiają taką proporcję.

Dla każdej rodziny wybrano ekspozycję docelową nie wyższą niż minimum pierwszych dobrych i ściśle poniżej minimum złych. To jednostronna próba uhonorowania wszystkich pierwszych dobrych granic i odrzucenia złych, z zapasem tylko w górę. Wyniki względne (dokładne suwaki dawnych zapisów nie są potrzebne):

| Miernik | Największa propozycja względem pierwszego dobrego, CNN | Transformer |
|---|---:|---:|
| Legacy CNN/K | 2,2612× | 5,0404× |
| Transformer 2 | 1,8003× | 4,1479× |
| Average 0% | 1,7350× | 4,0240× |
| Average 100% | 1,7350× | 4,2453× |

Przykładowo 4,1479× dotychczasowego mnożnika oznaczałoby około **6× → 24,9×** albo **10× → 41,5×**. Są to przykłady skali błędu, nie odczyt suwaków ani sprawdzone nastawy w grze. To znacznie większy zapas niż w badanym wcześniej odsetku kanałowym (+33–39%). Nie ma potwierdzenia jakości po zastosowaniu żadnego z tych celów.

Szukano pierwszego trafionego przedziału przez podwajanie rho do 128, następnie 18 bisekcji. Zapisano też monotoniczność wszystkich odwiedzonych punktów. Dla Legacy, T2 i Average była zachowana na badanych krzywych. Percentile potrafił wykazać odwrócenia; jego eksperymentalnych wyników w JSON **nie traktujemy jako dowiedzionego minimalnego rho**. To dodatkowa trudność przy regulacji na podstawie tej statystyki, nie zmiana zasady mnożnika: każdy dodatni kanał wejścia nadal rośnie, ujemny pozostaje bez zmian.

## Proste połączenia ekspozycji

Sprawdzono min/medianę/max/średnią ekspozycji R/G/B i rozpiętość EV kanałów. Dodatkowo: iloraz i różnicę średniej arytmetycznej/geometr. wyprowadzonych z T2/Legacy, iloraz/różnicę Average i Percentile oraz agregaty kanałowe tych różnic. To jawne wzory bez uczonych wag; selekcja nadal korzysta z tych samych etykiet, bez niezależnego testu.

Nie znaleziono wspólnego separatora dla obu rodzin w tym zestawie. W samym transformerze iloraz `Percentile10–90_G / Average0_G` rozdziela 8 etykiet: dobre 1,011763–1,045416, złe 0,989431–1,010236. Margines jest wąski, CNN tego nie spełnia; to obserwacja z tych danych, nie rozwiązanie zadania wspólnego automatu.

Average 0/50/100% w większości zapisów daje praktycznie te same wartości: na takim nośniku rzadko uruchamia się knee świateł. Okna Percentile 10–90 i 25–75 również często wychodzą tak samo lub bardzo podobnie. Histogram 64 binów obejmuje 48 EV, czyli jeden bin ma szerokość 0,75 EV; bliskie neutralnego średnie kafli łatwo trafiają do tego samego binu. Nie jest to pomiar liczby pikseli upakowanych w wąskim paśmie wokół 0,5. Nawet dokładniejsze jawne Legacy/T2 nie rozdzieliły jednak etykiet, więc wynik nie sprowadza się do rozdzielczości histogramu.

## Weryfikacja i odtwarzanie

`tests/nr_gpu/edit_exposure_study.cpp` używa aktualnych, istniejących PSO `private_exposure_Shader.cso` i żywego NR `DlssNr_Shader.cso`, z ich właściwymi typami stałych. Odczytuje DDS, rekonstruuje rzeczywisty FP16 input, wykonuje wskazane metody i zapisuje CSV. Siatki/formaty/przedziały odpowiadają hostowi. Wyniki są porównane z niezależną referencją NumPy (double, natural log/exp, całki kafli, histogram integer), a nie shaderem z nim samym.

- RTX 4090: **576 porównań PASS** (16 plików × 4 widoki × 9 ustawień). Wszystkie 192 wyniki metod prywatnego DLSS są identyczne jako FP32 z wartościami half referencji; maksymalny względny błąd pozostałych = **2,68e-6** przy granicy asercji 1e-5. Prywatne metody dopuszczają w ogólnym checkerze jeden przedział half (0,0011 względnie), jak wcześniejszy test private exposure.
- WARP: **192 prywatne wyniki PASS**, identyczne z RTX i referencją; [CSV](validation/nr-edit-exposure-warp.csv). Miernik żywego NR nie jest uruchamiany na WARP ze względu na znany problem tego shadera z gałęzią Percentile, opisany już w jego źródle. Nie ukryto tego przez kompilację innego wariantu.
- SHA-256 16 źródeł oraz użytych shaderów zachowane w JSON. Original DDS nie są publikowane. HLSL, bloby, INI, UI i DLL pozostają bez zmian; nie ma nowej pracy GPU w grze. To wykonanie numeryczne offline, **nie benchmark GPU, pomiar ghostingu w grze ani walidacja temporalna**.

```powershell
$files = @(Get-ChildItem 'E:/Gry/Marvels SpiderMan 2/dlssnr-edit/game*-*.dds') +
         @(Get-ChildItem 'F:/Gry/The Last of Us - Part I/dlssnr-edit/game*-*.dds')
$paths = @($files | Where-Object { $_.Name -notmatch '\.(depth|motion)\.dds$' } |
           Sort-Object FullName | Select-Object -ExpandProperty FullName)
./tools/test_nr_edit_exposure.ps1 -DdsPaths $paths -Csv artifacts/edit-analysis/exposure-gpu.csv
./tools/test_nr_edit_exposure.ps1 -Warp -DdsPaths $paths -Csv artifacts/edit-analysis/exposure-warp.csv
python tools/analyze_nr_edit_exposure.py @paths `
  --gpu-csv artifacts/edit-analysis/exposure-gpu.csv `
  --warp-csv artifacts/edit-analysis/exposure-warp.csv `
  --gpu-label 'NVIDIA RTX 4090, driver 616.56' --gain-audit `
  --output artifacts/edit-analysis/exposure-study.json
```

Źródła algorytmów: `superior/PrivateExposure.hlsl`, `DlssNr_PrivateExposure.h`, żywy `precompile/dlssnr.hlsl` (mode 3/13), CPU `DlssNr_ExposureMeter.h`. Stara kopia v59 Automatic w `superior/precompile/dlssnr.hlsl` nie jest miernikiem używanym przez bieżący host NR i nie zastąpiła aktualnych PSO w badaniu.
