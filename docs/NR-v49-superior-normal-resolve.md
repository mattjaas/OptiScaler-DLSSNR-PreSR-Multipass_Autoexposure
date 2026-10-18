# NR v49 — dokładna specjalizacja zwykłego resolve Superior Scaling

Dodano automatycznie wybierany shader zwykłej ścieżki gry. Używa tych samych
próbek, wag, precyzji, HDR, gainów i akumulacji co v48. Nie zmienia modeli,
liczby passów, harmonogramu ani filtrów low; nie dodaje opcji w menu.

## Zakres i wybór ścieżki

Nowy PSO ma stałe Mode=1, signed Transfer=9, DebugView=0, CompareMode=0,
ApplyModel=1 i flagi dokładnie Superior. Kompilator może usunąć nieużywane
odczyty i warunki dla starszych transferów, debugowania i porównania obrazu.
Dziewięć tapów radius-one nadal wykonuje się w pętlach w kolejności v48;
nie scala się exp2 i nie zmienia blendu, sum ani mapowań HDR. Bytecode
specjalizacji ma 14360 bajtów, wobec 173348 bajtów ogólnego resolve v48.

Wspólny helper produkcyjny `SuperiorScaling::NormalResolve` wymaga Mode 1,
radius 1, signed Transfer 9, włączonego Apply, wyłączonych DebugView/Compare/
ShowSkinMask, dokładnych flag oraz rzeczywistych rozmiarów tekstury source
nie większych niż wyjście i różnych od wyjścia. Sama skonfigurowana skala
nie wystarcza: np. native 1×1 zaokrąglone do work 1×1 pozostaje na starej
ścieżce. Redukcja tylko jednej osi jest poprawnie obsługiwana.

Inspekcja, porównanie obrazu, skin-mask, Apply wyłączone i przypadki native
zachowują binarnie niezmieniony ogólny shader v48. Carrier i Cross5 też mają
niezmienione binaria. W przypadku błędu tworzenia nowego PSO pozostaje
dotychczasowy fallback. Dochodzi jeden PSO tworzony leniwie i przechowywany
do ponownego użycia; nie ma nowych dispatchy, tekstur, kopii, barier,
alokacji w klatce ani timestampów w normalnym torze gry.

## Zgodność obrazu

Na RTX 4090 nowa ścieżka przechodzi **1008/1008 porównań bit w bit** względem
resolve v48: 7151328 słów wynikowych. Test obejmuje siedem zredukowanych
geometrii (także jedną oś, source 1×1, nieparzyste wymiary i krawędzie),
wyjścia FP16/FP32, wszystkie pięć HDR mapping, passthrough, trzy pary gainów,
low włączone/wyłączone, skin/environment, darkening i signed/non-finite dane.
Wejścia FP32 zawierają skorelowane native/proxy ze zmiennym signed residualem
oraz losowy HDR o dużej amplitudzie i NaN. Nowy profil wymusza Compare=0,
Debug=0 i Apply=1 zgodnie z zakresem nowej specjalizacji.

Test C++ uruchamia ten sam helper, który wybiera PSO w produkcji. Sprawdza
redukcję obu/jednej osi, native i rounded 1×1, nadwymiarowe/nieważne rozmiary,
inne promienie, transfery, etapy, dodatkowe flagi oraz każde wyłączenie
specjalizacji przez kontrolki inspekcji/Apply. Test integracji sprawdza
zgodność warunków wyboru z definicjami kompilacji. Kompilacja DXC obejmuje
11 wariantów i potwierdza brak LDS nowej specjalizacji. Workflow buduje DLL
i zaktualizowane fixture GPU. Wizualnej oceny w grze tu nie wykonano;
potwierdzona zgodność bitowa dotyczy opisanych przypadków na tej karcie.

## Pomiar GPU: v48 Superior → v49 Superior

RTX 4090, NVIDIA 616.56, 2026-10-18. Native 3840×2160; syntetyczna
nieruchoma scena sinusoidalna, residual 0.03. Proxy/native FP32, przygotowany
signed residual, low i wynik FP16. Passthrough=1, guide radius 1,
range sigma .015, spatial sigma 3, strength 1, high=.8/low=.661 na obu
stronach i przy wszystkich skalach. Filtr low pozostaje oryginalny Mode 18.
Nie mierzono przeliczania gainów/harmonogramu CPU ani modeli.

Timestamp obejmuje **filtr low + końcowy resolve i ich bariery**. Inference
NR, interpassy, carrier i NGX/DLAA są poza pomiarem. Resident resources/PSO;
alokacje/upload/fence CPU poza timestampami. ABBA, 90 rozgrzewek na każde
okno, 160 próbek na okno, 320 próbek na wariant/skala. Cztery sekwencje na
timestamp, wynik dzielony przez cztery. Przed osobnym eksperymentem low
karta miała 0% obciążenia; bezpośrednio po jakościowych testach GPU, przed
tym ABBA odczyt wynosił 34%, 1926 MiB, 28.56 W. Zmienność podano poniżej.

| Skala | v48 mediana ms | v49 mediana ms | Delta ms | Zmiana etapu |
| --- | ---: | ---: | ---: | ---: |
| 50% (1920×1080) | 0.488448 | 0.362752 | -0.125696 | -25.73% |
| 59% (2266×1274) | 0.527360 | 0.388864 | -0.138496 | -26.26% |
| 65% (2496×1404) | 0.539136 | 0.416384 | -0.122752 | -22.77% |

| Skala | P10–P90 v48 / v49 ms | Odchylenie standardowe v48 / v49 ms |
| --- | --- | --- |
| 50% | 0.482560–0.570368 / 0.320768–0.446464 | 0.040375 / 0.059041 |
| 59% | 0.521472–0.609792 / 0.340736–0.427264 | 0.039740 / 0.049597 |
| 65% | 0.534784–0.642816 / 0.361728–0.496128 | 0.050016 / 0.058942 |

Mediany A0/B1/B2/A3: P50 .488448/.326784/.380160/.488704 ms;
P59 .524800/.365952/.395520/.530688 ms;
P65 .538368/.395520/.421632/.540928 ms.
Surowe dane: [NR-v49-superior-normal-GPU.samples.csv](NR-v49-superior-normal-GPU.samples.csv).

Zysk dotyczy tego etapu na jednej karcie. Nie jest pomiarem całego NR w grze
ani gwarancją jednakowej delty klatki. Nie sumować go z wcześniejszymi
pomiarami v45/v48 przeprowadzonymi w innych warunkach. Pełny czas NR można
sprawdzić przez ABBA DLL v48/v49 z tym samym presetem A/K, sceną, HDR,
rozdzielczością i passami, z osobną rozgrzewką i timestampami całego NR.

## Odrzucona hipoteza Low64

Przetestowano osobny Mode 18 z Transfer=1, zachowujący wszystkie 64 tapy.
192 przypadki low były zgodne bitowo; izolowany audyt DXIL pokazywał spadek
statycznej liczby wywołań SampleLevel z 128 do 64. Mimo tego przy P59
filtr zwolnił .027200→.031680 ms, a przy P65 .033600→.038592 ms.
P50 miało niestabilną bazę pomiędzy oknami .035008/.018880 ms i nie stanowi
podstawy zmiany polityki. Low+resolve nie wykazało zysku na żadnej skali.
Dlatego tego PSO ani wyboru zależnego od skali nie włączono do produktu.
Filtr low pozostał nietknięty.

Próbki odrzuconego wariantu:
[sam low](NR-v49-rejected-low64-GPU.samples.csv),
[low + resolve](NR-v49-rejected-low64-resolve-GPU.samples.csv).

## Odtworzenie

Kompilacja shaderów: `tools/compile_nr_interpass.ps1`; fixture C++17,
`d3d12.lib dxgi.lib`. Referencję resolve odtworzyć binarnie przez Python:

```python
from pathlib import Path
import subprocess
p = Path("artifacts/nr-v49/baseline")
p.mkdir(parents=True, exist_ok=True)
ref = "5f2c0e59f2025463a27e68d2fc3b492357ca4dfc"
name = "dlssnr_superior_resolve_Shader.cso"
p.joinpath(name).write_bytes(subprocess.check_output([
    "git", "show", f"{ref}:OptiScaler/shaders/dlssnr/precompile/{name}"
]))
```

```text
superior_resolve_quality.exe artifacts/nr-v49/baseline OptiScaler/shaders/dlssnr/precompile normal
superior_resolve_timing.exe artifacts/nr-v49/baseline OptiScaler/shaders/dlssnr/precompile output.csv normal
```
