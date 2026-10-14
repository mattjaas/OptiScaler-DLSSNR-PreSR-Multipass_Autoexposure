# NR v39 — średnia gainów low

Low jest średnią arytmetyczną efektywnych gainów modeli. Najpierw każdy gain
pochodzi ze stylu danego passu i wybranych ustawień Auto/Manual oraz przechodzi
dotychczasową korekcję skali. Dopiero te wartości są uśredniane. Nie jest to
średnia obrazów ani mnożenie gainów.

| Keep inter-pass low gain at 1 | Low w interpass | Low w final resolve |
| --- | --- | --- |
| wyłączony | średnia modeli od ostatniej udanej korekcji do bieżącej | średnia modeli po ostatniej udanej korekcji |
| włączony | dokładnie 1 | średnia wszystkich efektywnie wykonanych modeli |

Przykład: sześć modeli, korekcja co dwa modele. Bez checkboxa są średnie
gainów 1–2, potem 3–4, na końcu 5–6. Z checkboxem obie korekcje mają low=1,
a final resolve średnią gainów 1–6. Przy odstępie 3 i sześciu modelach są
średnie 1–3 oraz 4–6, albo interpass=1 i końcowa średnia 1–6.

Nieudana korekcja nie przesuwa początku bloku. Gdy nie ma interpassów,
final low obejmuje wszystkie modele niezależnie od checkboxa. Pojedynczy
model zachowuje swój gain; identyczne gainy modeli zachowują wspólną wartość.
Shaping Off i neutralny Auto dla nieskalibrowanych transferów pozostają neutralne.

High/mid zachowuje dotychczasowy zakres i regułę mnożenia. Z checkboxem
zmienia się tylko zakres final low: high/mid nadal obejmuje modele po ostatniej
korekcji, więc gainy high nie zostają policzone ponownie dla całej serii.
Wyłączenie mnożenia high/mid nie wyłącza uśredniania low. Średnia low jest
stosowana raz w danej korekcji/resolve. Shadow confidence zachowuje swoją regułę.

Menu pokazuje final gain tym samym helperem, którego używa renderowanie,
oraz liczbę modeli uwzględnianych przez low. Checkbox i INI pozostają zgodne
z v38. Historia resetuje się przy zmianie aktywnego stanu checkboxa.

Obliczenia średniej odbywają się na CPU, bez alokacji w każdej klatce.
HLSL, bytecode, PSO, tekstury i harmonogram nie zmieniają się. Nowy gain
może zmienić potrzebę istniejącego filtra low, jeśli high/low staną się równe
lub przestaną być równe. To celowa zmiana shaping dla różnych gainów modeli,
bez gwarancji identycznego obrazu. Jakość i czas całego NR trzeba ocenić w grze.

## Weryfikacja

Test C++ sprawdza konkretne mieszane gainy, średnie par/trójek/całej serii,
puste i pojedyncze zakresy, identyczne gainy, zero, NaN i duże wartości.
Rozliczenie bloków obejmuje 1–32 modele, stride 0/1/2/3 i nieudane korekcje.
Test integracji sprawdza osobne zakresy low/high oraz zgodność final preview
z renderowaniem. Jest uruchamiany w istniejącym workflow razem z testami NR.

## Kontrolny pomiar GPU — 14 października 2026

RTX 4090, NVIDIA 616.56, resident synthetic RGB sinusoidy, native 3840×2160,
residual 0,03, wejścia FP32, pole low i wyjście FP16. P50 1920×1080,
P59 2266×1274, P65 2496×1404. Cached Low36 + Cross5, Area/radius 1/guide 1,
range sigma 0,015, spatial 3, efektywny high 1,13, shadow gate wyłączony.
A używa low ostatniego modelu Cinematic, B średniej trzech gainów Standard,
Natural i Cinematic: bazowe low 0,609 / 0,741 / 0,780, skorygowane dla każdej
skali przed uśrednieniem. Low field jest potrzebne po obu stronach.

Każde okno ABBA: osobne 90 rozgrzewek i 160 timestampów GPU. Timestamp
obejmuje osiem powtórzeń low+korekcji i barier, podzielonych przez osiem.
Alokacje, upload i fence CPU są poza timestampem. Zegary/temperatura
niekontrolowane. Pomiar nie obejmuje NGX, temporal DLAA ani final resolve,
nie jest zatem pomiarem całego NR w grze ani kosztem obliczania średniej na CPU.

| Skala | Low A / B | A / B ms (średnia median okien) | Delta B−A ms | SD A / B ms | Mediany A1 / B1 / B2 / A2 ms |
| --- | --- | --- | --- | --- | --- |
| P50 | 0.780000 / 0.710000 | 0.281856 / 0.281888 | +0.000032 | 0.020993 / 0.029089 | 0.281728 / 0.281600 / 0.282176 / 0.281984 |
| P59 | 0.827677 / 0.769741 | 0.367840 / 0.368000 | +0.000160 | 0.022561 / 0.022524 | 0.367552 / 0.367872 / 0.368128 / 0.368128 |
| P65 | 0.856916 / 0.807233 | 0.425120 / 0.425088 | -0.000032 | 0.024486 / 0.024313 | 0.425152 / 0.425088 / 0.425088 / 0.425088 |

W tym przypadku koszt shaderów jest praktycznie taki sam; nie ma spójnego
zysku. Nie ekstrapolujemy tego wyniku na sytuacje aktywujące/pomijające low
ani na grę. [1920 próbek](NR-v39-low-mean-GPU.samples.csv), fixture
`tests/nr_gpu/interpass_low_mean_timing.cpp`. Weryfikacja w grze wymaga ABBA
ze stałą sceną i tym samym zestawem mieszanych modeli, z osobną rozgrzewką
oraz oględzinami detali, ruchu, krawędzi i HDR. Wyników cached v37 nie traktujemy
jako pomiaru nowej reguły low.
