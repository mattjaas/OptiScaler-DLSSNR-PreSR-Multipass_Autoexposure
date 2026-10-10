# Inter-pass NR v16: szeroki kafel, pary odczytów, osobny Mode18

Trzy niezależne opcje DX12, domyślnie wyłączone:

- `InterPassV16WideTile`: grupa 16×8 i prostokątny kafel RGB 28×16 lub 34×20. Ogranicza powtórne rekonstrukcje P100 na granicach grup. Większy LDS może zmniejszyć zajętość GPU.
- `InterPassV16PairLoads`: dwa sąsiednie P100 na wątek, wspólne odczyty source/model z bufora lokalnego jednej czteroelementowej linii. Osobne prowadzenie, wagi i kolejność sumowania dziewięciu tapów. Bez cache source/model w LDS. Nieparzysta ostatnia kolumna używa rekonstrukcji pojedynczej. Nietypowa geometria przechodzi na dwa odczyty referencyjne.
- `InterPassV16LowShader`: osobny Mode18 z tym samym filtrem 64 `SampleLevel`. Brak LDS i innych trybów w skompilowanym DXIL; żadnego usuwania dispatchu, zmiany filtra ani mniejszej precyzji.

Pierwsze dwie opcje wymagają Fused/Area/radius 1, tiled, RGB i isolated weights: Compact16 lub isolated Linear20 v14. Cache/strided/v11/v12/Area/guide-one wykluczają nową kombinację. Sześć osobnych PSO obejmuje wide/pair/both w obu rozmiarach. Mode18 działa przy nierównych aktywnych wzmocnieniach niskiej/wysokiej częstotliwości. PSO tworzą się raz, dopiero po wybraniu opcji. Błąd tworzenia PSO zachowuje wcześniejszy wariant. Szerokość dispatchu wynika z rzeczywiście wybranego PSO, więc fallback nie gubi połowy obrazu.

## Krótki benchmark w grze

P50 ma 3 przypadki: wyłączony inter-pass, Fused reference i Fused optimized. P59/P65 mają po 7: wyłączony inter-pass, Fused reference, zwycięski RGB v14, wide, pair, wide+pair i Mode18. Mode18 jest pomijany, gdy żadne przejście między sześcioma passami nie potrzebuje niskiego pasma. Łącznie 15–17 przypadków. Nie wracają stare przegrane cache/strided/v11/v12 ani redundantne kontrole weights/guide-one. Ustawienia użytkownika są zapisywane i odtwarzane, także wszystkie trzy flagi v16. CSV zawiera nowe flagi. Bazę dla pojedynczej zmiany stanowi RGB v14 przy tej samej skali; `No inter-pass` nadal pokazuje koszt całego mechanizmu.

Pomiar pozostaje sześciopassowy, 90 próbek rozgrzewki i 160 świeżych znaczników czasu GPU na przypadek domyślnie. Wynik obejmuje cały przedział NR, w tym NGX i przejścia. Zachowaj tę samą nieruchomą scenę i ustawienia HDR; oceniaj medianę, P95 i rozrzut, nie sam FPS. Rutynowy sweep jest pojedynczy; wynik blisko szumu trzeba potwierdzić celowanym A/B lub ABBA.

## Poprawność i pomiar lokalny

RTX 4090, 2026-10-10: runner `tests/nr_gpu/v16_parity.cpp` porównuje faktyczne DXIL. PASS: 2336 porównań, 14 474 368 składowych RGBA. Wyjścia FP32 i FP16 zgodne bitowo z RGB v14/standardowym Mode18, z wyjątkiem nieistotnego payloadu NaN. Rozmiary 65×37, 127×73, 17×11; skale 25/55/59/60/61/65/75/90%, prowadzenie 0,35 i 1, HDR ze znakiem, ciemne wartości, skrajne zakresy, NaN, alfa, kształtowanie częstotliwości i bramka cieni, niepełne grupy oraz wymuszony fallback. Mode18 dodatkowo 1×1. WARP nie został zweryfikowany. Testy arytmetyczne/geometryczne v10–v16 i manifestu przeszły. Hash standardowego oraz starszych shaderów v10–v14 po kompilacji pozostaje zgodny z dotychczasowym workflow.

Poniżej tylko diagnostyczny pomiar shadera, **nie rezultat gry ani całego NR**. RTX 4090, syntetyczne dane HDR, natywne 1920×1080; P59 ma 1132×637, P65 1248×702. Area, radius 1, confidence 0,75, sigma range 0,015/spatial 1,2, wyjście FP16. Mode18: wejście 1248×702, wyjście 156×88. Timestampy wokół ośmiu dispatchy z barierą UAV, wynik podzielony przez osiem; przesyłanie danych i odczyt wyniku poza przedziałem. Rozgrzewka obu PSO, dwa bloki ABBA, cztery próbki wariantu i cztery jego własnej bazy. Zmienne obciążenie komputera/taktowanie nie było kontrolowane. Rozrzut jest duży i nie pozwala ogłosić wygranej. Koszt lokalnego bufora par odczytów może przewyższać oszczędność; wszystkie opcje zostają OFF do pomiaru w grze. Nie przedstawiamy liczby unikniętych odczytów jako zysku w ms.

| Skala | Wariant | Baza mediana ms | Wariant mediana ms | Różnica ms | Min–max baza / wariant ms |
|---|---|---:|---:|---:|---|
| 59% | dlssnr_tiled_v16_wide20 | 0.081408 | 0.080256 | -0.001152 | 0.081364–0.081596 / 0.080128–0.080256 |
| 59% | dlssnr_tiled_v16_pair20 | 0.186270 | 0.298176 | +0.111906 | 0.166528–0.379648 / 0.295552–0.398768 |
| 59% | dlssnr_tiled_v16_both20 | 0.238104 | 0.270254 | +0.032150 | 0.212224–0.294340 / 0.267008–0.365792 |
| 65% | dlssnr_tiled_v16_wide16 | 0.277372 | 0.305708 | +0.028336 | 0.259172–0.353624 / 0.244156–0.479488 |
| 65% | dlssnr_tiled_v16_pair16 | 0.415232 | 0.774888 | +0.359656 | 0.391484–0.459008 / 0.765544–1.031680 |
| 65% | dlssnr_tiled_v16_both16 | 0.416896 | 1.224125 | +0.807229 | 0.406768–0.591616 / 1.167600–1.258750 |
| 65% | dlssnr_v16_low | 0.249212 | 0.193920 | -0.055292 | 0.193280–0.619392 / 0.185728–0.348160 |

Surowe próbki: `docs/NR-v16-GPU-smoke.samples.csv`. Powtórzenie: skompiluj runner, uruchom `v16_parity.exe <shader-dir> --hardware` (poprawność) lub dodaj `--timing` (diagnostyczne ABBA). Workflow kompiluje runner i wszystkie siedem nowych PSO; lokalna kontrola GPU jest oddzielna od kompilacji CI.

LDS: pair16 3072 B, wide/both16 5376 B; pair20 4800 B, wide/both20 8160 B; Mode18 0 B. Prefabrykowane nagłówki/binarne eksperymenty generuje workflow oraz `tools/compile_nr_v16.ps1`, zgodnie z dotychczasową strukturą repozytorium.

## Gotowa paczka i publikacja

Źródło buildu: `7d1a639d63aab42bdb8bbc1cd5d4dc9d4bfb3898`, gałąź `codex/nr-interpass-v13-bounded-area`. [Build 38051955534](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/actions/runs/38051955534) zakończył się sukcesem: testy, kompilacja siedmiu nowych shaderów i runnera, pełny build DLL i publikacja release.

[ZIP v16](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/releases/download/nr-interpass-v16-wide-pair-low-20261010/OptiScaler-NR-nr-interpass-v16-wide-pair-low-20261010.zip), 135 742 676 bajtów.

- ZIP SHA256: `b37d25b081390127ae22f620b6fca1cd2a757bdeacfb459d70595f6c0e1aa10d`
- DLL SHA256: `4355e757836d7d6e7cf3ac9f461bcdb4ed83ff864f43aa3f2dd217197e301d25`
- Po pobraniu potwierdzono CRC całego ZIP, wszystkie 62 wpisy `SHA256SUMS.txt`, format Windows x64 DLL, tekst nowego benchmarku oraz 19 referencyjnych/nowych blobów DXIL identycznych z lokalnie sprawdzonymi shaderami, w tym wszystkie siedem PSO v16.

Lokalne usunięcie `package_release.ps1` przez Bitdefender pozostało poza commitem. Nie przywracano pliku z kwarantanny. GitHub Actions korzystał z istniejącego skryptu w repozytorium.
