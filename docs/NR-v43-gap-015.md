# NR v43 — próg interpassu 0,15

Próg przewidywanej bezwzględnej różnicy high/medium względem low spada
z 0,25 do 0,15. Interpass przed następnym modelem występuje wyłącznie,
gdy przewidywana różnica bez korekcji przekracza 0,15 i rośnie względem
bieżącej. Równa lub malejąca różnica nadal pomija korekcję, także gdy
przekracza próg. Nie wykonujemy korekcji po ostatnim modelu.

Przykład: wzrost różnicy z 0,1 do 0,2 uruchamia teraz interpass.
Spadek z 0,2 do 0,18 go pomija. Porównanie pozostaje ścisłe, bez epsilonu;
próg używa reprezentacji float, tak jak efektywne gainy. Test sprawdza
dokładną wartość progu oraz jej bezpośrednich sąsiadów float.

Przeliczenie rozdzielczości, iloczyn Structure × Intensity dla high/medium,
średnie low, checkbox low=1, rozliczanie nieudanych korekcji i końcowy resolve
zachowują dotychczasowe reguły. Dotychczasowe wyłączenia Off/spatial/>P90
pozostają. Menu, INI i opis benchmarku pokazują nowy próg.

Nie dodano etapów, zasobów, PSO, kopii, barier ani alokacji w zwykłej
ścieżce renderowania. Zmiana dotyczy progu na CPU. Korekcje mogą działać
częściej, co zwiększa liczbę istniejących dispatchy i ich koszt GPU.
Historyczne maski i pomiary v42 nie opisują harmonogramu v43.

Dziesięć testów Python NR/benchmarku przechodzi.
Test C++ przechodzi dla nowego progu i jego sąsiadów, zakresu 0,15–0,25,
różnic rosnących, malejących i równych oraz zaktualizowanych masek.
Zachowuje pokrycie rozdzielczości, Structure/Intensity, średnich low,
nieudanych korekcji i skrajnych liczebności modeli. Workflow v43 uruchamia
testy NR i buduje DLL oraz istniejące shadery. HLSL i bytecode niezmienione.

## Kontrolny pomiar GPU — 15 października 2026

RTX 4090, NVIDIA 616.56; rezydentne sinusoidy RGB, native 3840×2160,
wejścia FP32, pole low i wyjście FP16. Sześć modeli reprezentowanych przez
stałe wejścia fixture i pięć przejść Cached Low36 + Cross5 lub ClampProxy.
Area/radius 1/guide 1, residual 0,03, range sigma 0,015, spatial 3, shadow
gate wyłączony. Każdy model: bazowe P50 high=0,7, low=0,8, Structure=0,8,
Intensity=0,7; mnożenie high włączone, neutral low wyłączone. Gainy po
korekcie skali i obu suwaków identyczne po obu stronach. A: próg 0,25;
B: próg 0,15. Zmienia się wyłącznie harmonogram i wynikające z niego bloki.

ABBA: osobne 90 sekwencji rozgrzewki i 160 próbek w każdym oknie.
Zasoby i PSO rezydentne, stałe uploadowane przed pomiarem. Timestamp
obejmuje osiem powtórzeń pięciu przejść i barier, podzielonych przez osiem.
Alokacje, upload, fence CPU, NGX, temporal DLAA i final resolve są poza
pomiarem. Fixture nie przepuszcza wyników przez NGX pomiędzy przejściami.

Lokalną próbę przerwano: GPU wykazywało obciążenie 100%, a timestampy
pierwszego okna wynosiły około 21–24 ms, wobec około 0,406 ms dla tej
samej sekwencji w poprzedniej kontroli. Warunki nie były porównywalne;
nie ukończono pełnego ABBA i nie publikujemy delty czasu GPU dla v43.
Nie jest to dowód regresji shadera ani pomiar kosztu całego NR w grze.
Przerwano wyłącznie proces fixture uruchomiony w tym zadaniu.

Pomiar należy powtórzyć w porównywalnych warunkach:
`interpass_gain_schedule_timing.exe shader-dir output.csv gap-015`.
Pozostałe tryby fixture zachowują historyczny próg 0,25 do porównań.
W grze porównać v42/v43 w ABBA ze stałą sceną, tymi samymi modelami
i ustawieniami, po osobnej rozgrzewce, używając timestampów całego NR.
Sprawdzić maski korekcji, detale, ruch, krawędzie i HDR. Pełny czas GPU
i jakość w grze pozostają niezweryfikowane dla nowego progu.
