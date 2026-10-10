# NR v23: obecny RGB16 kontra Guide16 ze screena v14

Nowy przycisk w statusie NR: **Compare Optimized / v14 Guide16 at 65% (CSV)**.
Rozstrzyga konkretną wskazówkę ze screenów: compact16, isolated weights i
exact guide-one włączone, RGB corrected tile wyłączone. Przy dokładnym guide=1
to osobny historyczny Guide16 RGBA, nie RGB16 porównywany w v22.
Tło i pomiary v22: [NR-v22-P65-game-results.md](NR-v22-P65-game-results.md).

## Zakres i uruchomienie

DX12, 4K 3840×2160 → 2496×1404, sześć passów, Area/radius1/guide1.
Pięć konfiguracji / siedem okien:

1. Off.
2. Classic reference.
3. Fused reference.
4. Obecny Optimized RGB16 — A.
5. Archiwalny Guide16 RGBA — B.
6. Ponownie B.
7. Ponownie A.

Każde okno ma osobną rozgrzewkę (domyślnie 90) i 160 pomiarów; A i B po 320
próbek w podsumowaniu. Zmiana RGB16/Guide16 resetuje historię w ten sam sposób.
Test automatycznie ustawia P65/sześć passów/Area/radius1/guide1. Zachowuje
bieżący Enlargement/Transfer, sigmy, shaping, shadow, HDR i ustawienia passów.
Uruchom w tej samej nieruchomej scenie i nie zmieniaj ustawień/kamery w trakcie.
Zakończenie i anulowanie przywracają zmienione ustawienia i usuwają override.

**W obu wariantach standardowy shader oraz cały bieżący CPU/NGX/resolve są
takie same. Zmieniany jest tylko inter-pass PSO.** Guide16 pochodzi z dokładnego
DXIL paczki v14 (`c88b3c56`), nie jest ponowną kompilacją obecnego HLSL.
Ma kafel RGBA 16×16, LDS 4096 B, izolowane wagi i compile-time Mode28.
Obecny RGB16 ma LDS 3072 B. Mniej LDS samo w sobie nie dowodzi szybszego toru.
Specjalizacja guide-one zachowuje runtime lerp; agresywny wariant zmieniający
zaokrąglenia został odrzucony już w v14.

Archiwalny PSO tworzy się leniwie tylko w oknach B. Nie dodajemy go do
produkcyjnego Optimized ani rutynowego benchmarku. Nie dodano dispatchów,
barier, tekstur roboczych ani timerów poza istniejącym benchmarkiem.
Nieodpowiednia geometria/guide, brak PSO lub faktyczny fallback przerywa test.
Starszy benchmark v22 jest osobnym przyciskiem i zachowuje dotychczasowy zakres.

## Raporty i interpretacja

W `OptiScaler-NR-Benchmarks` obok gry powstaną:
`NR-v23-p65-guide16-*.csv`, `.samples.csv`, `.txt`.
Raport zawiera rzeczywisty cały przedział NR na GPU z NGX/przejściami,
P95, SD, mediany okien, geometry/pass count oraz pełne metadata jak v22.
`selected_path` B musi wynosić `Fused Guide16 RGBA (v14)`,
`shader_revision` B to `v14-guide16`; A to `Fused RGB16` / `current`.
Ta etykieta dotyczy inter-pass: standard pozostaje bieżący również w B.
`gain_vs_optimized_median_ms` dodatnie oznacza zysk Guide16.
`path_matches_control=0` dla B jest oczekiwaną różnicą PSO, nie błędem jakości.
Nie porównuj średniej z nakładki ze screenów bezpośrednio z medianą CSV.

Pomiar wydajności całego NR w grze pozostaje do wykonania przez użytkownika.
Nie ogłoszono przewagi Guide16 ani zmiany produkcyjnej polityki.

## Weryfikacja

Dokładny hash Guide16: `f55dadbcf9b48e11d7e623843e28606f6bd0d37ae136d6ceb87084eed8f48c3a`,
47244 bajty. Sprawdzono pojedyncze wystąpienie tych bajtów w oryginalnym DLL v14.
Nagłówek osadza dokładnie ten blob; test Python chroni tożsamość, ograniczenie
profilu, ABBA, osobny override, guards i przywracanie ustawień. Fixture GPU
`tests/nr_gpu/interpass_guide16_parity.cpp` porównuje rzeczywisty Guide16
z obecnym RGB16, FP16/FP32, brzegi i odd dimensions, HDR/non-finite, underflow,
shadow, equal/unequal gains i pełne 4K→P65. Porównanie jest bitowe z wyjątkiem
payloadu NaN.

- Lokalny test GPU RTX 4090: **PASS, 102 porównania / 84 688 704 składowe RGBA**.
- Python Guide16/revision/benchmark oraz zachowane shared_bilinear,
  dynamic_geometry, classic_downsample_clamp, classic_guided_stencil,
  tiled_fused_area: PASS.
- HLSL i cztery produkcyjne bloby pozostają bez zmian. Fixture korzystało
  z identycznych shaderów bieżących wygenerowanych i zweryfikowanych przy v22.
- Pełna kompilacja/paczka zostanie sprawdzona w istniejącym workflow;
  jego wynik i link do release zostaną dopisane po zakończeniu.
