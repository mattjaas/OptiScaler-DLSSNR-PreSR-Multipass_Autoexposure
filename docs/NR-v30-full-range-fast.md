# NR v30: cały zakres pomniejszenia Fast i krótsze benchmarki

**Fast z Low36 + Cross5 działa przy każdym rzeczywistym pomniejszeniu,
również P50 i P91–P99.** Usuwamy wcześniejsze procentowe progi P25/P90
wyboru Fast zgodnie z poleceniem użytkownika. Menu NR dopuszcza pomniejszenie
25–99%; zaokrąglenie P25 poniżej 25% na jednej osi nie blokuje już Fast.
Jedna oś może zachować native przy zaokrągleniu P99, jeśli druga jest
pomniejszona. Obie osie muszą mieć dodatnie wymiary i nie przekraczać native.
Przy 100–200% nie działa inter-pass dla pomniejszonego obrazu; nie jest to
wyjątek procentowy Fast. Warunki Area/radius 1/guide >0 pozostają potrzebne
dla tego algorytmu. Inne filtry/radii/guide wybierają dokładny Optimized.

**Dostępność nie gwarantuje przyspieszenia:** lokalny syntetyczny test
etapów GPU P91/P95/P99 dał Fast wolniejszy od dokładnej kontroli o
0,218624 / 0,358880 / 0,508000 ms. To nie wynik całego NR w grze.
Pozostawiamy możliwość wyboru Fast w całym zakresie zgodnie z poleceniem;
nie przywracamy automatycznego progu ani nie zmieniamy dokładnej polityki
na podstawie tego syntetycznego pomiaru. Nowy krótki benchmark wysokich
skal służy do sprawdzenia rzeczywistego zysku/kosztu i jakości w grze.

## Zachowane optymalizacje i zakres zmiany

Jedyna produkcyjna opcja Fast (INI 4; dawne 5/6/7 migrują do 4) zawsze
wybiera Mode36 Cross5 w kwalifikującej się geometrii. Low36 jest pomijany,
gdy shaping nie potrzebuje pola low. Wagi `exp2`, shaping, shadow gate,
clamp/sanitize, ograniczenia krawędzi i alfa zachowują zatwierdzoną matematykę.
Nie zmieniamy HLSL ani bytecode; nie przybywa dispatchy, tekstur, barier
czy alokacji na klatkę. Zwykły/końcowy Mode18 8×8, DLAA i final resolve
pozostają w dotychczasowym torze.

Fast daje gotowy wynik working jednym przebiegiem korekty i ponownie
używa zasobów oraz lazy PSO. Nie liczy dokładnej integracji korekty native
do working. Właśnie tę pominiętą pracę optymalizują dokładne P50 Mode28,
RGB16/RGB20 LDS i cache stencilów; Fast ich nie uruchamia dodatkowo.
Wybranie ich przywróciłoby dokładną pracę zamiast przybliżenia Fast.
Poza warunkami algorytmu dokładny Optimized nadal wybiera swoje specjalizacje.
Szersza dostępność Fast nie oznacza potwierdzonego zwycięstwa w całym zakresie.

## Krótsze benchmarki

Każda skala Fast ma teraz tylko Off, Classic reference, Fused reference
oraz Optimized / Fast / Fast / Optimized w ABBA: **pięć konfiguracji,
siedem niezależnie rozgrzewanych okien i jeden blok ABBA**. Usuwamy
rutynowe ponowne porównania z Fast bez Cross5 oraz osobny przycisk tego
porównania. Kontrola bez Cross5 pozostaje tylko w fixture diagnostycznym
shadera, uruchamianym jawnie; produkcyjny Fast nie odczytuje jej override.

| Profil Fast | Skale | Konfiguracje / okna | Z dodatkową bieżącą skalą |
|---|---|---:|---:|
| podstawowy | 50/59/65% | 15 / 21 | bez dopisywania |
| poniżej 50% | 33/40/41/42/49% | 25 / 35 | 30 / 42 |
| wysokie skale | 91/95/99% | 15 / 21 | 20 / 28 |

Nowy przycisk: **Compare reference / Optimized / Fast at 91% / 95% / 99% (CSV)**.
P91 bada pierwszy pełny procent poza dawnym limitem, P95 środek nowego
przedziału, P99 prawie native. Dodatkowa bieżąca skala jest dopisywana
tylko, gdy leży w P91–P99 i różni się od tych trzech. To celowana hipoteza,
nie skanowanie wielu bliskich skal. Wszystkie trzy profile ustawiają Transfer 9,
sześć passów, Area/radius 1/guide 1; zachowują ustawienia shaping/passów.

Default warmup 90 klatek / 160 próbek na okno: podstawowy i wysoki mają
3360 próbek, niższy 5600. Z dodatkową skalą wysoki ma 4480, niższy 6720.
CSV summary/raw, `.pairs.csv` i TXT zawierają cały przedział NR na GPU,
aktualną geometrię/ścieżkę, SD/P95, mediany okien, rozrzut i sąsiednie delty.
Minus oznacza przyspieszenie. Wszystkie profile Fast teraz zapisują pairs,
także podstawowy FastQuality. Nie mnożymy czasów przez liczbę passów.

Manual hold pozwala obejrzeć rozgrzany wynik bez doliczania klatek oglądania
do czasu. Ruch sprawdzamy oddzielnie od statycznego benchmarku. Ustawienia
są przywracane po zakończeniu/anulowaniu. Prefiksy: `NR-v30-fast-`,
`NR-v30-fast-low-`, `NR-v30-fast-high-`.

Dokładne profile Regular, Below50 i Boundary zachowują niezależne wymuszone
kontrole, alternatywny Classic/RGB20, Off i oba referencyjne. Ich liczby
nie zostały zmienione. Porównania Fast nie dublują już tych osobnych testów
polityki dokładnej, dlatego niższy benchmark spada z 80 do 35 okien.

## Weryfikacja

Lokalnie przeszły sześć istniejących testów arytmetyki inter-pass i test
benchmarku. Nowo skompilowany test polityki sprawdza cały zakres menu
P25–P200, filtry/radii/guide i aliasy trybów, równość jednej osi, no-reduction,
supersampling na jednej osi, zerowe i duże wymiary, zaokrąglenie P25/P50/P99,
oraz 37 622 osie kafli (21 z chronionym overflow). Dokładne zwycięskie
ścieżki pozostają takie same.

Nowo skompilowany test na RTX 4090 przeszedł 936 kontroli istniejących
shaderów, po rozszerzeniu o P90/P91/P95/P99. Obejmuje rzeczywisty Low36
+ guiding, Mode18, signed DC, krawędzie, FP16/FP32, HDR/nonfinite, alfę
oraz izolowane/fallback PSO Cross5. To kontrola kontraktu, nie dowód
percepcyjnej jakości nowych wysokich skal. Użytkownik nie zauważył
pogorszenia wcześniejszych v26/v27; oceny P91–P99 jeszcze nie otrzymano.

[Build 38137651375](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/actions/runs/38137651375)
zakończył się sukcesem: testy Python/C++, kompilacja sześciu shaderów,
fixture GPU, pełna DLL, ZIP i release. Krok publikacji pozostawał przez
kilka minut przy wersji roboczej po uploadzie; po weryfikacji ZIP
dokończono publikację przez `gh release edit --draft=false`, a workflow
następnie również zamknął swój krok z sukcesem.
Źródłowy commit: `6e42aa735b5e9af7d57c32efe70c758b186ba954`.

[Release v30](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/releases/tag/nr-interpass-v30-full-range-fast-20261011)
i [ZIP](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/releases/download/nr-interpass-v30-full-range-fast-20261011/OptiScaler-NR-nr-interpass-v30-full-range-fast-20261011.zip).
SHA256: `a56c19cee761b8d3aee49277a221a4431ed0b599b487ffdcf4d5ec27eb33eb50`.
Sprawdzono CRC, wszystkie 62 wpisy SHA256SUMS, commit zaszyty w DLL,
krótkie benchmarki, pełny zakres w INI i brak osobnej opcji/kontroli
Cross5. Wszystkie sześć blobów shaderów z v27/v28 występuje bajtowo
w nowej DLL; odrzuconego PSO Weights nie ma. Dokument i surowe próbki
są dostępne w repozytorium; istniejący skrypt pakowania nie dołącza ich
do ZIP. Paczka nie została automatycznie podmieniona w grze.

## Syntetyczny pomiar GPU wysokich skal

RTX 4090, NVIDIA 616.56, native 3840×2160, rezydujące sinusoidy RGB,
residual 0,03. FP32 wejścia, FP16 output i low. Area/radius 1/guide 1,
range 0,015, spatial 3, nierówne gainy high 1,13 / low 0,71, shadow gate off.
90 klatek rozgrzewki i 160 timestampów na każde okno ABBA; osobne zasoby
rezydują w obrębie danej skali. Kontrola to produkcyjny Select(3), wariant
Select(4). Przy P91/P95/P99 są to Classic optimized i Fast z Low36 + Cross5.

Timestamp obejmuje osiem powtórzeń low + korekty (+ redukcji dla Classic)
i ich barier; czas dzielony przez osiem. Alokacje, uploady i fence CPU
są poza pomiarem. Zegarów i temperatury nie kontrolowano. **To czas etapów
inter-pass bez NGX/NR, temporal DLAA i final resolve, nie wynik całego NR
w grze.** Nie mnożymy tych wyników przez liczbę passów. Benchmark w grze
służy do oceny całego toru i jakości. Nie zmieniamy dokładnej polityki
Optimized na podstawie tych danych.

| Skala / working | Mediana A / B ms | Zmiana ABBA ms | Zmiana % | SD A / B ms | P95 A / B ms | A1 / B1 / B2 / A2 ms |
|---|---:|---:|---:|---:|---:|---|
| P91 / 3494×1966 | 1.239488 / 1.454784 | +0.218624 | +17.634% | 0.040109 / 0.036160 | 1.313664 / 1.525760 | 1.243264 / 1.453248 / 1.463552 / 1.236288 |
| P95 / 3648×2052 | 1.260544 / 1.619328 | +0.358880 | +28.470% | 0.035904 / 0.039561 | 1.327616 / 1.686656 | 1.260864 / 1.619264 / 1.619584 / 1.260224 |
| P99 / 3802×2138 | 1.351616 / 1.859008 | +0.508000 | +37.597% | 0.040011 / 0.041431 | 1.427712 / 1.921024 | 1.348736 / 1.858816 / 1.859520 / 1.353600 |

Minus oznacza przyspieszenie. SD jest populacyjne, P95 nearest rank,
mediany puli A/B obejmują po 320 klatek. ABBA używa czterech median okien,
więc jego delta różni się od różnicy median puli. Kolejne klatki nie są
320 niezależnymi powtórzeniami eksperymentu. Wynik komponentowy nie
potwierdza percepcyjnej jakości ani uniwersalnego zysku w grze.

Źródło: [1920 próbek GPU](NR-v30-high-GPU.samples.csv).
SHA256 `f138dd13015defd7156537f6cb70dde0883e0abb78b828b0c386510860be3654`.
