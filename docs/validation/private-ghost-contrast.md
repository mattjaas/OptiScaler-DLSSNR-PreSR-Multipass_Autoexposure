# Kontrast odsłoniętego editu i poprzedniego occludera

2026-11-05. Baza d18852b3 (pełne wnętrza dysokluzji 008). Funkcja domyślnie wyłączona. Nie zmienia istniejących PSO widoczności ani warunków depth/MV.

## Znaczenie

`raw = NR answer - NR input proxy`, w podpisanych jednostkach editu z neutralnym zerem, przed dodatnim gain i transportem do DLSS. Po wykryciu nowego odsłonięcia zapisujemy poprzedni raw edit powierzchni wskazanej reprojekcją. Przez pozostałe klatki N przenosimy ten oryginalny wzorzec MV wraz z licznikiem. Bieżący raw edit jest przechowywany osobno: jeżeli powierzchnia mająca już maskę później sama zasłania dalszy obiekt, nowy wzorzec pochodzi z jej rzeczywistego editu, nie z wcześniejszego occludera.

Przy decode, już po przestrzennej rekonstrukcji/poszerzeniu maski, porównujemy bieżący pełnorozdzielczy raw edit z wybranym wzorcem:

`distance = max(abs(raw.r-reference.r), abs(raw.g-reference.g), abs(raw.b-reference.b))`.

`mask *= 1-smoothstep(threshold,threshold+softness,distance)`; przy softness=0 maska jest dozwolona dla distance<=threshold. Domyślne wartości to .02 i .005. Każdy kanał może wyłączyć maskę; podobna luminancja nie ukrywa różnicy barwy. Próg jest absolutny w domenie NR, nie w nitach ani procentach kontrastu sceny. AutoExposure NR może zmienić wartości tego editu; mnożnik transportu nie bierze udziału w pomiarze.

Kontrolki: **Only mask low-contrast disocclusions**, **Disocclusion edit contrast threshold**, **Disocclusion edit contrast softness**. Dostępne dla metod temporalnych 3–6 (indeksy 2–5), w ramach istniejących ograniczeń DX12/Superior/prywatnego DLSS. Próg i miękkość mają efekt od bieżącego decode. Włączenie/wyłączenie inicjalizuje tylko lokalną historię maski; nie resetuje NR ani NGX. Brak poprawnego wzorca oznacza brak odrzucania DLSS. Początek historii wymaga rozgrzewki.

## Koszt i cykl życia

Tylko po włączeniu dodajemy jeden dispatch na siatce historii i cztery tekstury RGBA16F (ping-pong bieżącego raw i wzorca occludera). Kolor wybieramy z tego samego najbliższego depth 2×2/4×4 co MV, również na nieparzystych brzegach. FP16 dotyczy składowania RGB; obliczenia różnicy i współrzędne pozostają FP32. Nieskończony lub niemieszczący się w FP16 raw unieważnia próbkę. Nie odczytujemy niezainicjalizowanej historii.

Metoda 6 używa dodatkowo jednej tekstury RGBA16F rozszerzonego wzorca. Istniejący pass dilation zapisuje wzorzec tego samego sąsiada, który dostarcza maksymalny wiek; nie potrzeba osobnego przebiegu rozszerzania koloru. Decode wybiera najbliższy ważny wzorzec w swoim footprint, zamiast uśredniać przeciwne barwy w sztucznie podobny kolor. Obecny soft fringe i globalne wygaszanie pozostają.

Dodatkowe nominalne VRAM przy 2496×1404: około 26.74 MiB w metodach 3–5 i 8.35 MiB w metodzie 6, plus wyrównania. Stan należy do GhostHistory/Generation, więc uczestniczy w tym samym odroczonym zwalnianiu, odrzucaniu niezrealizowanej listy, zmianach rozmiaru i obsłudze widoków. Wyłączenie zwalnia zasoby bez oczekiwania CPU/GPU. Nie ma readbacków ani alokacji co klatkę. Off nie dodaje dispatchy ani tekstur; wszystkie 81 dotychczasowych blobów pozostaje identyczne.

Używamy tych samych sześciu SRV/dwóch UAV i ABI 256 B. Opcjonalny decode ma wzorzec na t1, a proxy pełnej odpowiedzi czyta z już obecnego t2. Nowe parametry mieszczą się w ExposureAnchors[12..14] (224..232 B), z nazwanymi setterami i asercjami ABI. Dodano 13 opcjonalnych blobów, razem 94; liczba slotów PSO ma wspólną stałą 50. Przy błędzie przygotowania pomijana jest maska zamiast użycia nieprawidłowej referencji.

INI: `[DlssNr] SuperiorGhostContrast`, `SuperiorGhostContrastThreshold`, `SuperiorGhostContrastSoftness`. Pełny obieg odczytu/zapisu/resetu/profili i kKeptKeys; zrzuty mają dodatkowe `ghost_edit_contrast_enabled`, `ghost_edit_contrast_threshold`, `ghost_edit_contrast_softness`. `.raw.dds` i `.after.dds` zachowują dotychczasowe znaczenie.

## Weryfikacja

352 niezależne przypadki GPU obejmują R/G/B, próg i miękkość, gain .5/6, edit/pełną odpowiedź NR, wszystkie cztery metody, globalne fade, przeciwne referencje RGB bez znoszenia koloru, brak historii, dokładne wzorce dla N/N-1/0, reset, obie konwencje Z, wybór depth zamiast środka komórki, 1×1/nieparzyste siatki i oba promienie dilation. Asercje liczbowe sprawdzają kolor i siłę maski względem niezależnych oczekiwań CPU. Ten sam zestaw działa na RTX 4090 i WARP. Pozostałe testy widoczności/transportu pozostają. Test profili używa aktualnego Config.cpp; JSON ma asercje nowych wartości.

Przy kontrastowym A/B wymagamy też, by maska po włączeniu nie przekroczyła wyłączonej o więcej niż .002 (tolerancja składowania). 007 pozostaje bez maski; 008 dostaje mniejszą maskę dla tych ustawień. To nie precision/recall ghostingu: pojedynczy zapis nie zawiera prawdziwej poprzedniej historii. Silny kontrast nie gwarantuje, że DLSS nigdy nie zrobi smugi; funkcja jest opcjonalnym kryterium wyboru.

## Pomiar GPU

RTX 4090, D3D12, NR 2496×1404, metoda 6/radius1, N19/fade9, separacja .05, MV gate2.02, gain1, FP16 kolor, threshold .02/softness .005, bez globalnego fade/podglądu. Timestampy całego etapu maski: clear, scatter, historia, opcjonalne wzorce, dilation, decode i bariery; bez NR/NGX, uploadów i alokacji. Te same bieżące PSO widoczności w obu wersjach. Osobne historie, 16 par rozgrzewki, 64 bloki ABBA, po 128 próbek/wariant.

| Próba | Wyłączone: średnia ± SD [ms] | Włączone: średnia ± SD [ms] | Narzut [ms] |
|---|---|---|---|
| 007, powtarzane guides | 0.067320 ± 0.003093 | 0.090408 ± 0.004301 | +0.023088 |
| 008, powtarzane guides | 0.074848 ± 0.003767 | 0.098328 ± 0.001731 | +0.023480 |
| Kontrolowane tło ±128 px | 0.072496 ± 0.000580 | 0.090832 ± 0.002959 | +0.018336 |

Powtarzane guides służą do kosztu, nie do odtworzenia prawdziwej sekwencji gry. Próba ruchu używa nieruchomego prostokąta i naprzemiennych MV tła ±128 px. Zmiana pokrycia maski wpływa również na decode. Jest mierzalny narzut po włączeniu opcji; nie przedstawiamy go jako darmowej operacji. Pełnego toru gry i innych GPU nie zmierzono.

[Metadane i CSV](private-ghost-contrast.json). Odtworzenie: kompilacja `tests/nr_gpu/private_exposure_quality.cpp`, `--contrast-quality`, `--contrast-quality-warp`, `--contrast-timing <capture stem> static` / `moving`. Domyślne testy i `--warp` również zawierają nową grupę.
