# NR v45 — Superior Scaling

`Enlargement → Superior Scaling` (`Transfer=11`) jest dodatkową opcją obok
dotychczasowych trybów. W jej ustawieniach pozostaje tylko preset DLSS:
A (większa wydajność, domyślny) lub K (wolniejszy, stabilniejszy NR w ruchu).
Rozdzielczość, liczba passów, modele/styl oraz ich własne Structure i Intensity
pozostają wybierane przez użytkownika. Wybór zapisuje nowy klucz
`[DlssNr] SuperiorScalingPreset=1/11` i nie nadpisuje ustawień starszych trybów.
Pozostają ogólne ustawienia umiejscowienia NR, kompozycji i opcjonalna inspekcja.

## Stała ścieżka

| Element | Superior Scaling |
| --- | --- |
| Downscale wejścia | Area |
| Prywatny DLSS | DLAA 1:1 w rozdzielczości roboczej; Auto Exposure i IsHDR włączone |
| Carrier | `clamp(0.5 + (NR - Proxy), -1, 2)`; K=1 |
| Dekodowanie | `DLAA(carrier) - 0.5` |
| Guided | radius 1, range sigma 0.015, spatial sigma 3.0, strength 1 |
| Interpass | Fast cached Low36 + Cross5 (mode 8), high/mid mnożone w bloku |
| Low interpass / resolve | średnia low danego bloku / ogona po ostatniej udanej korekcji |
| Shadow confidence | wyłączone i usunięte z wyspecjalizowanych shaderów |

Stałe gainy bazowe przy P50: Standard high/mid **0.8**, low **0.609**;
Natural **0.75 / 0.741**; Cinematic **0.95 / 0.78**.
Każdy model nadal osobno przelicza oba pasma względem rzeczywistej skali
rozdzielczości i iloczynu `clamp(Structure,0,1)*clamp(Intensity,0,1)`, jak w v44.
Low jest średnią arytmetyczną po tym przeliczeniu, nigdy iloczynem low modeli.
Keep inter-pass low gain at 1 jest w tym trybie zawsze wyłączone.
Interpass występuje tylko przy rosnącej przewidywanej różnicy high/mid–low
przekraczającej 0.15; zachowano ograniczenia skali i rozliczanie nieudanej korekcji.

Nie ma podglądu efektywnych gainów ani suwaków eksperymentów w panelu
Superior Scaling. Wyłączono dziedziczenie eksperymentalnego pre/postfiltra,
ghost guard, scale-aware Structure i peripheral compression. Zmiana trybu
resetuje odpowiednie historie; prywatny DLAA nie jest niszczony co klatkę.
Prywatna ścieżka wymaga DX12 po upscalerze gry (także obsługiwanych tras
pośrednich). Natywny Vulkan zgłasza brak tej ścieżki. Przy 100% i powyżej
nie ma enlargement/DLAA; pozostaje zwykłe przetwarzanie NR, jak w innych
trybach tej rodziny. Preset A/K dotyczy prywatnego DLSS, nie DLSS gry.

## Co usuwa koszt

Osobny entry point carriera ma tylko encode/decode i sanitację wartości.
Nie odczytuje K z tekstury, nie liczy midpointu/limitu FP16/headroomu,
safe/applied K ani statystyk Proxy. W Superior nie alokujemy tekstur
redukcji/diagnostyki/applied K ani buforów readback i nie dispatchujemy
Mode 20/21/24. Nie ma kopii telemetrii ani jej odczytywania na CPU.
Wyjście FP16 i clamp [-1,2] zachowano. `precise` utrzymuje kolejność
`(NR-Proxy)+0.5`, aby uniknąć różnicy jednego ULP FP32 przy reassociation.

Dla P50 przy native 3840×2160 odpada pięć dispatchy analizy/redukcji;
dla P59 i P65 siedem, oraz dwie kopie diagnostyczne na klatkę.
Pozostają dwa niezbędne przebiegi: encode przed DLAA i decode po DLAA.
Usunięte są zależne bariery; inference NR, DLAA i końcowy resolve nadal działają.
Nie dodano pomiarów ani timestampów do domyślnej ścieżki gry.
Trzy dedykowane PSO tworzą się leniwie i pozostają do ponownego użycia.

Starsze tryby zachowują kod K, telemetrię, opcje oraz dotychczasowe shadery
(ich wygenerowany bytecode pozostał identyczny). To umożliwia porównanie
ze starszą ścieżką; usuwanie tego kodu globalnie zmieniłoby zachowane tryby.
Stary benchmark Off/references/winners/ABBA pozostaje dostępny dla nich.
W Superior jest schowany i nie uruchamia wariantów zmieniających jego stałą
politykę. Inspekcja pre-DLAA carrier jest dostępna tylko w starszych trybach;
pozostawiona w INI wartość DebugView=5 jest w Superior ignorowana.

## Weryfikacja

Przechodzą testy Python interpass/benchmark i nowy kontrakt integracji,
produkcyjna polityka C++ oraz test stałych ustawień Superior/izolacji starszych
trybów. DXC kompiluje wszystkie dziesięć wariantów i generuje nagłówki/binarne
shadery; dedykowany carrier nie używa LDS. Workflow v45 dodaje nowe testy,
kompilację fixture GPU i pełną DLL/paczkę.

Na fizycznym RTX 4090 przechodzi **52/52 porównań bit w bit**:
legacy linear K=1 vs nowe encode/decode przy 1×1, 17×9 i 65×37,
FP16/FP32, signed HDR, małych residualach, NaN i krawędziach;
także resolve/Cross5 bez shadow confidence względem dotychczasowego shadera.
To nie jest ocena wizualna temporalnego DLSS w ruchu.

## Pomiar GPU

RTX 4090, sterownik NVIDIA 616.56, 2026-10-16. Przed próbą: 4% obciążenia,
1621 MiB VRAM. Syntetyczna nieruchoma scena sinusoidalna z residualem 0.03;
native 3840×2160; wejścia FP32, encoded/decoded FP16, statystyki FP32.
Resident fixture `superior_timing.cpp` porównuje stare linear Manual K=1,
range [-1,2], watermark mask off z nowymi Mode 37/38. Bazowa strona wykonuje
rzeczywiste Mode 20/21/24, encode/decode, bariery i dwie kopie telemetrii
(1×1 K oraz pełna alokacja końcowej tekstury diagnostycznej, jak runtime).
Pomiędzy encode i decode nie uruchamia NGX; DLAA jest poza zakresem obu stron.

ABBA przy P50/P59/P65, osobne 90 próbek rozgrzewki i 160 pomiarów w każdym
oknie; 320 próbek na wariant/skala. Jeden timestamp obejmuje cztery sekwencje
podzielone przez cztery. Alokacje, uploady, tworzenie PSO i fence CPU są poza nim.

| Skala | Stary carrier mediana ms | Superior mediana ms | Delta ms | P10–P90 stare / nowe ms |
| --- | ---: | ---: | ---: | --- |
| 50% (1920×1080) | 0.650368 | 0.095488 | -0.554880 | 0.647680–0.654592 / 0.095232–0.096512 |
| 59% (2266×1274) | 0.814848 | 0.141568 | -0.673280 | 0.809472–0.983296 / 0.141312–0.146432 |
| 65% (2496×1404) | 0.886016 | 0.169472 | -0.716544 | 0.880896–0.942080 / 0.169216–0.177664 |

Odchylenie standardowe stare/nowe: P50 0.018237/0.000528 ms,
P59 0.073768/0.024376 ms, P65 0.025917/0.003128 ms.
Mediany osobnych okien A: P50 0.650112/0.650496,
P59 0.868608/0.813440, P65 0.886144/0.885888 ms. P59 ma większą zmienność.
Pełne próbki: [NR-v45-superior-carrier-GPU.samples.csv](NR-v45-superior-carrier-GPU.samples.csv).

To rzeczywista oszczędność **izolowanego toru carriera**, nie pomiar całego
NR w grze ani obietnica takiej samej delty klatki. Brakuje pomiaru w grze
i porównania jakości w ruchu/A vs K. Do porównania całego NR ustawić starszy
Temporal DLAA residual na dokładnie powyższe parametry, Manual shaping z
tymi samymi trzema parami gainów, te same modele/Structure/Intensity,
compound ON, neutral low OFF, Fast cached Low36, AE/HDR ON. Wyłączyć
eksperymenty i kompresję; porównać z Superior w ABBA w identycznej scenie,
z osobną rozgrzewką i timestampami całego NR. Inne współczynniki starego
Auto/Manual zmieniłyby harmonogram i nie izolowały kosztu sprzątania.

Odtworzenie testów GPU po kompilacji fixture:

```text
superior_carrier.exe OptiScaler/shaders/dlssnr/precompile
superior_timing.exe OptiScaler/shaders/dlssnr/precompile output.csv
```
