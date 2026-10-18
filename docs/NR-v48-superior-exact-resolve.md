# NR v48 — dokładna optymalizacja końcowego resolve Superior Scaling

Optymalizacja włącza się automatycznie w Superior Scaling. Nie zmienia
gainów v46, rozdzielczości, liczby modeli/passów ani harmonogramu interpassów.
Nie dodaje opcji w menu. Zachowuje przywrócone w v47 wspólne ustawienia HDR.

## Zmiana

Końcowy shader ma osobną kompilację dla Mode 1 i promienia 1. Wcześniej
przechodził przez ogólne pętle od -3 do 3, pomijając pozycje poza promieniem
1. Teraz wykonuje pętle od -1 do 1. Te same dziewięć pozycji jest przetwarzane
w tej samej kolejności, z identycznymi współrzędnymi, clampem na krawędziach,
wagami, dwoma wywołaniami exp2, akumulacją, fallbackiem i blendem.
Nie stosujemy przybliżenia exp2 sumy, nowych filtrów ani redukcji próbek.

Pozostawiono `[loop]`: wariant `[unroll]` został odrzucony, ponieważ kompilator
zmieniał zaokrąglenia akumulacji. Kompilacja ze stałym Mode 1 usuwa też kod
niepowiązanych etapów i ich LDS; bytecode maleje z 322052 do 173348 bajtów.
Ten rozmiar nie jest pomiarem zajętości rejestrów ani pamięci GPU.

Wybór PSO wymaga flagi Superior, Mode 1 oraz promienia dokładnie 1.
Inne etapy/promienie zachowują ogólny fallback. Carrier oraz Cross5 nadal
wybierają swoje odrębne PSO. Wszystkie starsze tryby, filtry low i pozostałe
binaria shaderów są niezmienione. Nie dodano dispatchy, tekstur, kopii, barier,
alokacji w klatce, diagnostyki ani timestampów do ścieżki gry.

## Zgodność obrazu

Na fizycznym RTX 4090 **864/864 przypadków GPU daje identyczny wynik bit w bit**
względem shadera Superior Scaling v47, łącznie 6667056 porównanych słów.
Fixture obejmuje wyniki FP16/FP32, sześć geometrii (w tym 1×1, krawędzie,
nieparzyste wymiary i różne skale), wszystkie pięć mapowań HDR, passthrough,
shaping włączony/wyłączony, trzy pary gainów, ograniczenie darkening,
skin/environment, porównanie obrazu, debug i wyłączone Apply model.
Wejścia są FP32: skorelowane native/proxy z przestrzennie zmiennym signed E50
oraz losowe signed HDR o dużej amplitudzie i NaN. W pole low podajemy wartości
niezerowe, aby sprawdzać także mieszanie pasm. To test zgodności shadera,
nie nowy test inference NR lub temporalnego DLAA.

Dotychczasowy test encode/decode/shaping Superior również przechodzi 52/52
przypadków. DXC kompiluje dziesięć wariantów; testy Python interpass/benchmark
i integracji sprawdzają dotychczasową politykę oraz ograniczenie nowego PSO
do właściwego etapu/promienia. Workflow buduje DLL i oba nowe fixture.

## Pomiar GPU: v47 Superior → v48 Superior

RTX 4090, NVIDIA 616.56, 2026-10-18. Po odciążeniu karty: 2% obciążenia,
1638 MiB VRAM. Native 3840×2160, syntetyczna nieruchoma scena sinusoidalna,
residual 0.03. Proxy/native FP32; carrier/dekodowany residual, low i wynik
FP16. Carrier przygotowano przed timestampami z tą samą kwantyzacją FP16.
Passthrough=1, guide radius 1, range sigma .015, spatial sigma 3, strength 1;
high=.8 i low=.661 w każdej skali, aby izolować ten sam etap obu wariantów.
Nie uruchamiamy w fixture modeli ani przeliczania gainów/harmonogramu CPU.

Timestamp obejmuje **oryginalny filtr low Mode 18 + końcowy resolve** i ich
bariery. Filtr low jest ten sam po obu stronach. Inference NR, interpassy,
encode/decode carriera, NGX/DLAA oraz pozostałe etapy gry są poza pomiarem.
Tekstury i PSO pozostają resident; alokacje/uploady/fence CPU są poza nim.
ABBA, osobna rozgrzewka 90 próbek na każde okno, następnie 160 pomiarów;
320 próbek na wariant/skala. Cztery sekwencje na timestamp, wynik dzielony
przez cztery.

| Skala | v47 mediana ms | v48 mediana ms | Delta ms | Zmiana |
| --- | ---: | ---: | ---: | ---: |
| 50% (1920×1080) | 0.672000 | 0.503040 | -0.168960 | -25.14% |
| 59% (2266×1274) | 0.704256 | 0.552960 | -0.151296 | -21.48% |
| 65% (2496×1404) | 0.713984 | 0.564864 | -0.149120 | -20.89% |

| Skala | P10–P90 v47 / v48 ms | Odchylenie standardowe v47 / v48 ms |
| --- | --- | --- |
| 50% | 0.656128–0.763904 / 0.485888–0.519680 | 0.046088 / 0.037764 |
| 59% | 0.694272–0.810752 / 0.530432–0.591616 | 0.047953 / 0.042508 |
| 65% | 0.709376–0.796416 / 0.548096–0.632832 | 0.045384 / 0.047303 |

Mediany osobnych okien A0/B1/B2/A3:
P50 .659200/.508416/.493568/.680064 ms;
P59 .697984/.555264/.536704/.709888 ms;
P65 .710400/.567936/.551168/.732288 ms.
Surowe próbki: [NR-v48-superior-resolve-GPU.samples.csv](NR-v48-superior-resolve-GPU.samples.csv).

To pomiar wskazanego etapu na jednej karcie, nie całego NR ani obietnica
identycznej oszczędności czasu klatki. Pełny czas NR w grze pozostaje do
weryfikacji: porównać DLL v47/v48 z tym samym presetem A lub K, sceną,
rozdzielczością, passami i HDR w ABBA z osobną rozgrzewką. Nie zmieniać
gainów ani liczby interpassów pomiędzy wariantami. Jakości algorytmicznie
nie zmieniono; zgodność bitowa jest potwierdzona dla opisanych przypadków
na RTX 4090, nie dla wszystkich możliwych obrazów i sterowników.

## Odtworzenie

Kompilacja: `tools/compile_nr_interpass.ps1`; fixture C++17 z
`d3d12.lib dxgi.lib`. Bazowy plik `dlssnr_superior_resolve_Shader.cso` można
odtworzyć bez konwersji tekstowej PowerShell:

```python
from pathlib import Path
import subprocess
p = Path("artifacts/nr-v48/baseline")
p.mkdir(parents=True, exist_ok=True)
name = "dlssnr_superior_resolve_Shader.cso"
ref = "ffb523b187404721571cd908287231fc935c3751"
p.joinpath(name).write_bytes(subprocess.check_output([
    "git", "show", f"{ref}:OptiScaler/shaders/dlssnr/precompile/{name}"
]))
```

```text
superior_resolve_quality.exe artifacts/nr-v48/baseline OptiScaler/shaders/dlssnr/precompile
superior_resolve_timing.exe artifacts/nr-v48/baseline OptiScaler/shaders/dlssnr/precompile output.csv
```
