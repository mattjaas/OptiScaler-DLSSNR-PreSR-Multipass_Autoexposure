# NR v41 — sumowane obniżenia Structure i Intensity

Ogólna intensywność danego modelu wpływa teraz na efektywny gain high/medium,
tak jak dotychczas Structure. Zgodnie z ustaleniem low pozostaje bez zmian.
Obniżenia obu suwaków względem 100% sumują się:

```
S = clamp(configured_Structure, 0, 1)
I = clamp(configured_Intensity, 0, 1)
factor = max(0, 1 - ((1-S) + (1-I)))
high = 1 + (scale_corrected_high - 1) * factor
```

Structure 80% i Intensity 70% dają obniżenie 20%+30%=50%, więc pozostaje
50% odchylenia gainu od neutralnego 1. Przy high po korekcie skali równym
0,7 wynikiem jest 0,85. Structure 50% i Intensity 50% dają neutralny gain 1.
Przy sumie obniżeń ponad 100% nadal pozostaje gain 1; nie przechodzimy
przez neutralny punkt w przeciwną stronę. Dla gainów powyżej 1 ta sama
reguła zmniejsza wzmocnienie w stronę 1.

Każdy suwak jest ograniczony osobno do zakresu kalibracji 0–1. Wartość
powyżej 1 nie kompensuje obniżenia drugiego suwaka ani nie ekstrapoluje
kalibracji. Faktyczne ustawienia przekazywane do modelu NVIDIA nadal mogą
mieć wartości do 2. Ta zmiana dotyczy gainów shaping, nie parametrów modelu.
Intensity=1 zachowuje dokładnie poprzedni wynik Structure, także numerycznie.

Najpierw wykonywana jest istniejąca korekta gainu według rozdzielczości
`p50_gain ^ log2(1 / clamp(scale, 0.25, 1))`, potem sumowanie wpływu suwaków.
Każdy pass używa własnego stylu, intensywności i Structure, z zachowaniem
dziedziczenia i indywidualnych override. Structure nadal pochodzi z ustawienia
użytkownika przed osobną eksperymentalną kompensacją skali w modelu.

Wspólny helper per-pass zasila przewidywanie harmonogramu, faktyczne gainy
interpassu, końcowy resolve i podgląd menu. High jest mnożone dopiero po
przeliczeniu każdego modelu, jeśli mnożenie jest włączone. Reguła interpassu
pozostaje z v40: przewidywana różnica bez korekcji musi przekraczać 0,25
i rosnąć. Średnie low oraz checkbox wymuszający low=1 działają jak wcześniej.
Intensity i Structure nie zmieniają low ani jego zakresu uśredniania.

Menu pokazuje intensywność obok Structure w podglądzie gainów modeli;
opisy i INI wyjaśniają sumowanie. Benchmark zapisuje `_configured_intensity`
każdego modelu obok stylu i Structure. Nie dodano domyślnych dispatchy,
kopiowania, barier, zasobów, PSO, readbacku ani timestampów GPU. Dodatkowa
arytmetyka jest na CPU, bez alokacji. Zmiana gainów może zmienić liczbę
istniejących korekcji oraz potrzebę istniejącego pola low.

## Weryfikacja

Przechodzi dziesięć testów Python NR/benchmarku i produkcyjny test C++.
C++ sprawdza sumowanie obniżeń, nakładanie się obu suwaków, zero, neutralne
100%, wartości powyżej 1 i nieprawidłowe, gainy poniżej i powyżej 1,
zgodność z poprzednim wynikiem przy Intensity=1 oraz zmianę harmonogramu.
Test integracji pilnuje odczytu indywidualnych intensywności, wspólnego
helpera i niezmienionej ścieżki low. Workflow v41 buduje DLL i istniejące
shadery. HLSL i bytecode nie zostały zmodyfikowane przez tę zmianę.

## Syntetyczny pomiar GPU — 15 października 2026

RTX 4090, NVIDIA 616.56. Resident sinusoidy RGB, native 3840×2160, wejścia
FP32, low i wyjście FP16, sześć modeli reprezentowanych przez niezmienne
wejścia fixture, pięć przejść. Cached Low36 + Cross5 dla korekcji albo
ClampProxy dla pominiętego interpassu. Area/radius 1/guide 1, residual 0,03,
range sigma 0,015, spatial 3, shadow gate wyłączony. Każdy model: bazowe
P50 high=0,7, low=0,8, Structure=0,8, Intensity=0,7, mnożenie high włączone,
neutral low wyłączone. Gainy są przeliczane według skali przed użyciem.

A: v40, gain high uwzględnia tylko Structure. B: v41, sumuje obniżenia obu
suwaków. Low identyczne po obu stronach. Te same rezydentne zasoby i PSO
przez całe ABBA, stałe uploadowane przed pomiarem. Każde okno ma osobne
90 sekwencji rozgrzewki i 160 timestampów GPU. Timestamp obejmuje osiem
powtórzeń pięciu przejść i barier, podzielonych przez osiem.
Alokacje, upload, fence CPU, modele NGX, temporal DLAA i final resolve są
poza pomiarem. Fixture nie przepuszcza wyników przez NGX pomiędzy przejściami.
To koszt sekwencji dispatchy, nie czas całego NR ani pomiar jakości obrazu.
Zegary i temperatura niekontrolowane.

| Skala / work | Korekcje A / B | A / B ms (średnia median okien) | Delta B−A ms | SD A / B ms | Mediany A1 / B1 / B2 / A2 ms |
| --- | --- | --- | --- | --- | --- |
| P50 / 1920×1080 | 2 / 1 | 0.627648 / 0.405600 | −0.222048 | 0.028558 / 0.028634 | 0.627328 / 0.405376 / 0.405824 / 0.627968 |
| P59 / 2266×1274 | 2 / 1 | 0.843840 / 0.548864 | −0.294976 | 0.032781 / 0.027847 | 0.843904 / 0.548864 / 0.548864 / 0.843776 |
| P65 / 2496×1404 | 2 / 1 | 1.011840 / 0.706336 | −0.305504 | 0.034527 / 0.031477 | 1.011456 / 0.706368 / 0.706304 / 1.012224 |

SD to odchylenie standardowe próbek obu okien danego wariantu.
Mniejsza liczba korekcji przyspieszyła tę sekwencję dla tego zestawu gainów.
Nie przenosimy wyniku na inne gainy ani na cały tor renderowania w grze.
[1920 próbek](NR-v41-model-intensity-GPU.samples.csv). Powtórzenie pomiaru:

```
interpass_gain_schedule_timing.exe shader-dir output.csv model-intensity
```

Bez ostatniego argumentu fixture zachowuje porównanie v39/v40.
Weryfikacja w grze powinna porównać v40/v41 w ABBA ze stałą sceną,
identycznym zestawem modeli i ustawień, osobną rozgrzewką oraz timestampami
całego NR. Sprawdzić maski korekcji, detale, ruch, krawędzie i HDR,
także mieszane intensywności między passami. Zgodnie z ustaleniem wpływ
na low odkładamy. Jakość w grze dla tej reguły nie została zweryfikowana.
