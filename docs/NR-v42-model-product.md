# NR v42 — iloczyn Structure i Intensity

Współczynniki Structure i ogólnej intensywności danego passu są mnożone
przy obliczaniu gainu high/medium. Zastępuje to sumowanie obniżeń z v41:

```
factor = clamp(configured_Structure, 0, 1) * clamp(configured_Intensity, 0, 1)
high = 1 + (scale_corrected_high - 1) * factor
```

Najpierw działa istniejąca korekta rozdzielczości, potem iloczyn obu
współczynników. Każdy model używa własnego stylu i ustawień obu suwaków,
z uwzględnieniem dziedziczenia i override. Wspólny helper nadal zasila
przewidywanie harmonogramu, interpass, końcowy resolve i podgląd menu.

Structure 50% i Intensity 50% zostawiają 25% odchylenia gainu od 1.
Dla high po korekcie skali równego 0,7 efektywny gain wynosi 0,925.
Structure 75% i Intensity 0% dają gain 1. Dowolny suwak na zero neutralizuje
shaping high/medium. Low nie jest modyfikowane przez żaden z tych suwaków;
średnie low i checkbox wymuszający inter-pass low=1 pozostają bez zmian.

Zachowano osobne ograniczenie obu współczynników do kalibracji 0–1,
obsługę nieprawidłowych wartości oraz dokładną zgodność z dotychczasowym
Structure przy Intensity=1. Faktyczne parametry modelu NVIDIA nadal
korzystają ze skonfigurowanych wartości, także powyżej 1. Ta reguła opisuje
gain shaping, nie pomiar odpowiedzi sieci neuronowej.

Menu, INI i opis benchmarku przedstawiają mnożenie. Nie dodano etapów,
PSO, zasobów, alokacji, kopii, barier ani odczytów GPU w normalnej ścieżce.
Zmiana dotyczy arytmetyki CPU i może zmienić harmonogram istniejących
korekcji. Reguła rosnącej różnicy ponad 0,25 pozostaje jak w v40.

## Weryfikacja

Produkcyjny test C++ przechodzi: przypadki 50%/50%, 75%/0%, wartości
neutralne i ponad 1, NaN, oba kierunki gainów, pełna macierz suwaków,
zmiana harmonogramu i niezmienione low. Przechodzi też dziesięć testów
Python NR/benchmarku. Są uruchamiane także w workflow v42, który buduje DLL i istniejące shadery.
HLSL i bytecode nie zostały zmodyfikowane.

## Kontrolny pomiar GPU — 15 października 2026

RTX 4090, NVIDIA 616.56; resident sinusoidy RGB, native 3840×2160,
wejścia FP32, pole low i wyjście FP16. Sześć modeli reprezentowanych
przez stałe wejścia fixture i pięć przejść Cached Low36 + Cross5 lub
ClampProxy. Area/radius 1/guide 1, residual 0,03, range sigma 0,015,
spatial 3, shadow gate wyłączony. Każdy model: P50 high=0,7, low=0,8,
Structure=0,8, Intensity=0,7, mnożenie high włączone, neutral low wyłączone.
A: sumowanie obniżeń v41; B: iloczyn v42. Low identyczne po obu stronach.

ABBA z osobną rozgrzewką 90 sekwencji i 160 próbkami w każdym oknie.
Zasoby i PSO rezydentne, stałe uploadowane przed pomiarem. Timestamp
obejmuje osiem powtórzeń całej sekwencji pięciu przejść i barier, podzielonych
przez osiem. Alokacje, upload, fence CPU, NGX, temporal DLAA i final resolve
są poza pomiarem. Fixture nie przepuszcza wyników przez modele NGX.
Nie jest to pomiar całego NR ani jakości obrazu. Zegary/temperatura
niekontrolowane.

| Skala / work | Korekcje A / B | A / B ms (średnia median okien) | Delta B−A ms | SD A / B ms | Mediany A1 / B1 / B2 / A2 ms |
| --- | --- | --- | --- | --- | --- |
| P50 / 1920×1080 | 1 / 1 | 0.405952 / 0.406016 | +0.000064 | 0.023231 / 0.022126 | 0.405888 / 0.406016 / 0.406016 / 0.406016 |
| P59 / 2266×1274 | 1 / 1 | 0.557344 / 0.557184 | −0.000160 | 0.024395 / 0.027862 | 0.556864 / 0.557184 / 0.557184 / 0.557824 |
| P65 / 2496×1404 | 1 / 1 | 0.720928 / 0.720928 | 0.000000 | 0.027364 / 0.029784 | 0.720768 / 0.720960 / 0.720896 / 0.721088 |

SD to odchylenie standardowe próbek obu okien danego wariantu. Koszt
sekwencji przy tej samej liczbie korekcji jest praktycznie jednakowy.
Przy P59 zmienia się miejsce pojedynczej korekcji: po modelu 4 w A,
po modelu 3 w B. Nie ma potwierdzonego zysku wydajności dla tego zestawu
gainów ani pomiaru całej gry. [1920 próbek](NR-v42-model-product-GPU.samples.csv).
Powtórzenie: `interpass_gain_schedule_timing.exe shader-dir output.csv model-product`.
Tryb `model-intensity` zachowuje historyczne porównanie v40/v41.

W grze porównać v41/v42 w ABBA ze stałą sceną, tymi samymi modelami
i ustawieniami, osobną rozgrzewką i timestampami całego NR. Sprawdzić
maski korekcji, ruch, detale, krawędzie i HDR, zwłaszcza przy mieszanych
Structure/Intensity oraz 50%/50%. Jakość w grze pozostaje do weryfikacji.
