# NR v44 — Structure i Intensity również dla low

Oba pasma używają teraz tego samego iloczynu Structure × Intensity danego
modelu. Najpierw wykonywana jest istniejąca korekta rozdzielczości, potem:

```
factor = clamp(configured_Structure, 0, 1) * clamp(configured_Intensity, 0, 1)
high = 1 + (scale_corrected_high - 1) * factor
low  = 1 + (scale_corrected_low  - 1) * factor
```

Współczynnik jest liczony raz dla obu pasm. High/medium zachowuje poprzedni
wynik; low przechodzi teraz to samo skalowanie w stronę neutralnego 1.
Structure 50% i Intensity 50% dają współczynnik 25%: low 0,8 po korekcie
rozdzielczości staje się 0,95. Przy dowolnym suwaku na zero oba gainy
wynoszą 1. Dla gainów ponad 1 efekt również maleje w stronę 1.
Zachowano osobne ograniczenie obu współczynników do kalibracji 0–1,
obsługę wartości nieprawidłowych i rzeczywiste parametry modeli NVIDIA.

Każdy pass używa własnego stylu, skonfigurowanego Structure i Intensity,
z zachowaniem dziedziczenia/override. Dopiero po indywidualnej korekcie
gainów obliczana jest średnia arytmetyczna low. Low nie jest mnożone
między modelami; high zachowuje opcjonalne mnożenie w obrębie bloku.
Pass z intensywnością zero wnosi do średniej low gain 1; nie jest pomijany.

| Keep inter-pass low gain at 1 | Low w interpass | Końcowy low |
| --- | --- | --- |
| wyłączony | średnia przeliczonych gainów bieżącego bloku | średnia przeliczonych gainów po ostatniej udanej korekcji |
| włączony | dokładnie 1 | średnia przeliczonych gainów wszystkich modeli |

Checkbox nadal nadpisuje faktyczny gain interpassu po przeliczeniu bloku.
Przewidywanie harmonogramu używa przeliczonych gainów low i właściwego
zakresu średniej, jak wcześniej. Wspólny helper per-pass zasila zarówno
przewidywanie, faktyczne korekcje, końcowy resolve, jak i podgląd menu.
Shaping Off i nieskalibrowany Auto pozostają neutralne.

Próg 0,15 i warunek wzrostu różnicy pozostają z v43. Zmiana gainów low
może zmienić maskę korekcji, zakresy bloków i potrzebę filtra low. Dla sześciu
modeli przy P50, bazowych gainach high=0,7/low=0,8, Structure=0,8,
Intensity=0,7, włączonym mnożeniu high i wyłączonym neutral low, harmonogram
zmienia się z maski 10 (dwie korekcje) na 31 (pięć korekcji). To rozliczenie
polityki CPU, nie pomiar czasu GPU. Zmiana aktywnej maski nadal resetuje historię.

## Weryfikacja i koszt

Dziesięć testów Python NR/benchmarku przechodzi.
Test C++ przechodzi dla przeliczania low w stronę 1, intensywności zero,
gainów ponad 1, mieszanych ustawień i średnich par/całej serii/ogona,
zaktualizowanych masek oraz pozostałej polityki interpassu. Test integracji
pilnuje kolejności scale → oba suwaki → średnia i osobnych zakresów low/high.
Zachowano testy checkboxa, końcowego resolve i podglądu. Workflow v44 buduje
DLL, istniejące shadery i fixture pomiarowy.

Nie dodano dispatchy, zasobów, PSO, kopii, barier, timestampów ani alokacji
w domyślnej ścieżce. Współczynnik obu suwaków jest obliczany raz dla obu pasm
w helperze CPU. HLSL i bytecode nie zmieniają się.
Liczba istniejących korekcji i ich koszt mogą się jednak zmienić.

Lokalnie nie wykonano nowego wiarygodnego pomiaru GPU: RTX 4090,
NVIDIA 616.56, aktualne obciążenie 100%, zajęte 16560 MiB. Poprzednia próba
v43 w takich warunkach była silnie zakłócona. Nie przypisujemy v44 oszczędności
ani konkretnej delty czasu na podstawie poprzednich wersji lub arytmetyki.

Przygotowany i skompilowany fixture porównuje v43/v44:

```
interpass_gain_schedule_timing.exe shader-dir output.csv low-tuning
```

Obie strony mają próg 0,15, te same high, tekstury i PSO. A zachowuje
low po samej korekcie skali, B koryguje je także przez Structure/Intensity.
Sześć modeli reprezentowanych przez stałe wejścia, pięć przejść Cached
Low36 + Cross5 lub ClampProxy, native 3840×2160, P50/P59/P65, wejścia FP32,
low/wyjście FP16. Bazowe high=0,7 i low=0,8, Structure=0,8, Intensity=0,7,
Area/radius 1/guide 1, residual 0,03, range sigma 0,015, spatial 3, shadow
gate i neutral low wyłączone, mnożenie high włączone. ABBA, osobne 90
rozgrzewek i 160 próbek na okno. Timestamp obejmuje osiem sekwencji przejść
i barier, podzielonych przez osiem; alokacje/upload/fence CPU są poza nim.
NGX, temporal DLAA i końcowy resolve są poza pomiarem.

Po uzyskaniu porównywalnych warunków powtórzyć pomiar syntetyczny i ABBA
w grze ze stałą sceną i timestampami całego NR. Sprawdzić maski korekcji,
detale, ruch, krawędzie i HDR, w tym mieszane intensywności i oba stany
checkboxa. Jakość oraz pełny czas GPU w grze pozostają niezweryfikowane.
