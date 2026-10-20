# NR v58 — specjalizacja końcowego resolve HDR soft knee

Dodano jeden PSO dla normalnego renderowania Superior Scaling z ustawieniem
**HDR mapping: Off (soft knee)**. Bazą jest v57,
`0d246bea8777c0674a26b2da65754a2cb4e9159a`.
Zysk w syntetycznym teście końcowego low + resolve jest mały:
około **0,003–0,008 ms** na RTX 4090. Nie zmierzono całego NR w grze.

## Zmiana

Dotychczasowy shader normal resolve wybierał mapping HDR dynamicznie.
Nowy PSO kompiluje ten sam HLSL z istniejącym `DLSSNR_SUPERIOR_HDR=0`;
kompilator zna więc `gPassthrough=0` i `gReversibleMode=0`.
Nie zmieniono źródła HLSL, precyzji, próbek, wag, sanitizacji ani blendowania.
Nie wdrożono przybliżeń prowadzących do różnic numerycznych.

Selekcja wymaga całego istniejącego kontraktu `NormalResolve`: Mode 1,
Transfer 9, ważny residual, rzeczywiste zmniejszenie rozdzielczości,
flaga Superior, włączony model, wyłączone Compare/debug/maska skóry.
Dodatkowo wymaga HDR i mappingu 0. Inne profile zachowują dotychczasową
selekcję. Nie ma wyboru shadera według procentu skali.

PSO jest tworzony leniwie tylko raz na właściciela. Nieudane utworzenie
przechodzi do dotychczasowego normal PSO, a następnie istniejącego
głównego shadera. Destruktor zwalnia również nowy slot tablicy.
Dochodzi jeden PSO i 13 916 B DXIL; pierwszy koszt kompilacji sterownika
nie jest objęty pomiarami po rozgrzewce.
Nie dochodzi dispatch, tekstura, kopia, bariera, alokacja w klatce,
diagnostyka ani opcja w menu. Wymagania pozostają SM6.0/FP32.

Wszystkie **21 dotychczasowych śledzonych CSO są bajtowo identyczne z v57**.
Nowy `dlssnr_superior_hdr0_Shader.cso` ma SHA256:
`9630836ff757ac2a80cfc7c8131a32ccb446af9fd31f09db03cccac5e2e7b614`.

## Obraz

RTX 4090, sterownik NVIDIA 616.56, D3D12:
**7776 porównań / 43 830 720 słów 32-bitowych, zero różnic bitowych**.
Porównanie jest ścisłe, bez tolerancji, obejmuje również alpha i wyniki NaN.

- Dziewięć geometrii ze zmniejszeniem, od native 3×3/work 1×1 do
  129×73/84×47; skala tylko w jednej osi, wiersz, nieparzyste wymiary,
  krawędzie i częściowe grupy. Native 1×1 bez zmniejszenia odrzuca selektor.
- Wyniki RGBA16F i RGBA32F, źródła RGBA32F; trzy style i ich gainy,
  guide strength 0/0,5/1, niski/domyślny limit darkening, ochrona skóry,
  dostępny/niedostępny guide oraz native detail 0/0,5.
- Dwanaście wzorców: skorelowane guide/residual, signed HDR i małe wartości,
  NaN oraz przesuwane krawędzie i tekstura.
- Test CPU sprawdza selekcję HDR/SDR i mappingów 0..11. Istniejące testy
  odrzucenia Compare/debug, braku modelu, innych flag i native fallbacku
  nadal przechodzą. Test integracyjny sprawdza kolejność fallbacków.

To zgodność na sprawdzonym GPU i zestawie danych, nie dowód dla każdego
sterownika i dowolnej sekwencji gry. Nie wykonano nowego porównania
pełnego NGX/NR ani oceny ruchu w grze.

## Czas GPU

RTX 4090 / NVIDIA 616.56, native 3840×2160. Work:
P50 1920×1080, P59 2266×1274, P65 2496×1404.
Rezydentny syntetyczny obraz sinusoidalny i residual, Standard
high/low 0,8/0,661, guided radius 1, range 0,015, spatial 3, strength 1,
HDR mapping 0, FP16 residual/low/wynik, FP32 proxy/original.

Znaczniki D3D12 obejmują **końcowy low + resolve i ich bariery**.
Każdy pomiar zawiera cztery powtórzenia, czas jest dzielony przez cztery.
Osiem bloków na konfigurację, naprzemienne ABBA/BAAB; osobna rozgrzewka
16 kwartetów w każdym bloku i 40 kwartetów pomiarowych:
320 sparowanych kwartetów / 640 próbek na wariant i konfigurację.
PSO, zasoby, upload, carrier encode/decode, readback i oczekiwanie CPU
są poza timestampami. Nie ma inferencji modeli ani wszystkich etapów NR.

| Seria | Skala | Baza ms | v58 ms | Zysk parowany ms | IQR zysku ms |
|---|---:|---:|---:|---:|---:|
| 1 | 50% | 0,292800 | 0,284928 | 0,004416 | 0,010368 |
| 1 | 59% | 0,316992 | 0,304384 | 0,007552 | 0,005248 |
| 1 | 65% | 0,326336 | 0,321280 | 0,004480 | 0,005536 |
| 2 | 50% | 0,297216 | 0,294400 | 0,003136 | 0,002592 |
| 2 | 59% | 0,316928 | 0,308096 | 0,004864 | 0,004896 |
| 2 | 65% | 0,331200 | 0,321216 | 0,004736 | 0,005504 |

Baza i v58 to mediany średnich dwóch próbek danego wariantu w kwartetach.
Zysk to mediana różnic wewnątrz kwartetu, dlatego nie musi równać się
różnicy dwóch median. IQR używa kwartylów inclusive.
Zmienność pozostaje istotna względem małego zysku; nie należy traktować
tych liczb jako gwarantowanego skrócenia czasu gry.

Kontrola A/A z identycznym PSO dała mediany zysku -0,000064/-0,000128/0 ms.
P50 miało wysoki IQR 0,070592 ms, więc kontrolę powtórzono:
**0/0/0 ms**, IQR 0,001792/0,002048/0,002304 ms.
Obie kontrole zachowano w danych. Wcześniejsze pilotaże zakłócone przez
systemowy wygaszacz Bubbles odrzucono; wygaszacz został zamknięty przed
końcowymi seriami. Nie zmieniano ustawień zegarów ani wygaszacza.

Próbowano też innych adresowań cache low, usunięcia blendowania guide
przy strength 1, odroczenia bilinear fallbacku i stałych sigma.
Nie wdrożono ich: część była wolniejsza, część dawała mały lub niestabilny
zysk, a dodatkowe uproszczenia zmieniały zaokrąglenia. Produkcyjny wariant
zachowuje pełne obliczenia.

Surowe serie są w [measurements/nr-v58](measurements/nr-v58).

## Odtworzenie

Z katalogu repo, w Developer PowerShell z kompilatorem x64:

```powershell
python tools/audits/softknee/prepare.py
./tools/compile_nr_interpass.ps1
cl /nologo /std:c++17 /EHsc /O2 tests/nr_gpu/superior_softknee_quality.cpp /Fe:artifacts/nr-softknee/quality.exe /Fo:artifacts/nr-softknee/quality.obj /link d3d12.lib dxgi.lib
cl /nologo /std:c++17 /EHsc /O2 tests/nr_gpu/superior_softknee_timing.cpp /Fe:artifacts/nr-softknee/timing.exe /Fo:artifacts/nr-softknee/timing.obj /link d3d12.lib dxgi.lib
./artifacts/nr-softknee/quality.exe artifacts/nr-softknee/baseline OptiScaler/shaders/dlssnr/precompile
./artifacts/nr-softknee/timing.exe artifacts/nr-softknee/baseline OptiScaler/shaders/dlssnr/precompile artifacts/nr-softknee/abba.csv normal full
./artifacts/nr-softknee/timing.exe artifacts/nr-softknee/baseline OptiScaler/shaders/dlssnr/precompile artifacts/nr-softknee/aa.csv aa full
python tools/audits/softknee/summarize.py artifacts/nr-softknee/abba.csv artifacts/nr-softknee/aa.csv
```

Kompilacja wszystkich shaderów i kontrole arytmetyki/interpassów/integracji
przeszły lokalnie. Fixture jakości, pomiaru i selektora skompilowano MSVC
v142; pełna DLL jest budowana istniejącym workflow z v143 jako v58.
