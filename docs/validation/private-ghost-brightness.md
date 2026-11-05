# Maska dysokluzji: RGB lub sama jasność editu

2026-11-05. Baza `0ee40acfa0fa3d5c21593f7c87f8be030928b9af`.

**Disocclusion edit contrast metric** dodaje drugi sposób anulowania maski:

- **RGB difference (strongest channel)**, default: `max(abs(delta.R),abs(delta.G),abs(delta.B))`.
- **Brightness only (Rec.709)**: `abs(.2126*delta.R+.7152*delta.G+.0722*delta.B)`.

`delta` to podpisana różnica bieżącego surowego editu NR i zapamiętanego oryginalnego occludera. W trybie jasności najpierw liczymy jedną ważoną sumę, a dopiero potem moduł. Zatem różnice kanałów wynikające z samej zmiany barwy mogą się znosić. Nie używamy sumy ważonych modułów, maksimum kanałów ani długości RGB. To jasność w domenie NR proxy, nie fizyczna luminancja w nitach.

Dotychczasowe **Only mask low-contrast disocclusions**, próg i miękkość są wspólne. Wartości poniżej progu zachowują maskę; większy kontrast stopniowo ją usuwa. Próg większy oznacza więcej maskowania. Obie metryki są przed dodatnim mnożnikiem transportu i działają w edicie / pełnej odpowiedzi NR oraz metodach 3–6, bez zależności od rodziny/presetu prywatnego DLSS. Zielony podgląd pokazuje wynik wybranej metryki.

Przełączenie metryki nie resetuje historii maski, NR ani NGX. Referencje nadal przechowują RGB, więc nowy wybór działa od bieżącego decode bez ponownej rozgrzewki. Bez dodatkowego przebiegu, tekstur, odczytów tekstur, synchronizacji czy alokacji; zmienia się uniform branch i kilka operacji skalarnych w opcjonalnym decode. Ten opis nie zastępuje pomiaru.

INI/profil: `[DlssNr] SuperiorGhostContrastMetric`: 0 RGB, 1 jasność, nieznane wartości →0. Deklaracja/default, ReadDlssNr, zapis, NR_PROFILE_FIELD i kKeptKeys mają ten sam klucz. Zrzut JSON dodaje `ghost_edit_contrast_metric`. Odczyt starych ustawień pozostawia dotychczasowy RGB. Selektor znajduje się w ExposureAnchors[15] / byte236; ABI 256B, 50 slotów PSO i liczba 94 blobów pozostają bez zmian. NaN lub nieznany selektor po stronie shadera również wybiera RGB.

## Poprawność

688 niezależnych przypadków GPU. Obie metryki przechodzą testy każdego kanału, progów/softness, gain .5/6, globalnego fade i pełnej odpowiedzi, na wszystkich czterech metodach. Nowa para o zmianie RGB (-.5,+.148681640625,0) ma różnicę jasności mniejszą niż .001: RGB usuwa maskę, jasność ją zachowuje. Przeciwny znak daje to samo oczekiwane zachowanie. Zmiana neutralnoszara zachowuje duży kontrast jasności i usuwa maskę w obu trybach. Są asercje dla nieznanego selektora i NaN bezpośrednio w shaderze, niezależnie od sanitacji CPU.

Zachowane testy oryginalnego occludera, resetu, nieparzystych siatek, depth/MV, ochrony foreground, ciągłej drogi, dokładnego N/fade, przeciwnego ruchu i przeciwnych wzorców RGB nie są zastępowane porównaniem shadera z nim samym. Nr_profiles_smoke używa aktualnego Config.cpp; testy metadata/JSON sprawdzają zapis metryki. Pełne testy są wykonywane na RTX 4090 i WARP.

## Pomiar etapowy

RTX 4090, D3D12, NR2496×1404, metoda6/radius1, N18/fade12, MV gate2.02, depth .05, gain1, threshold .02/softness .005, bez globalnego fade/overlay. **Kontrast jest włączony w obu wariantach** i korzysta z tych samych aktualnych shaderów oraz zasobów; zmienia się tylko selector 0→1. Pomiary nie izolują ewentualnej zmiany kosztu nowego selektora względem historycznego RGB PSO sprzed tej funkcji.

Timestampy obejmują clear, scatter, widoczność, referencje RGB, dilation, decode i bariery. Bez NR/NGX, uploadów/alokacji. Osobne historie, 16 par rozgrzewki, 64 bloki ABBA, po 128 próbek na wariant, dane rezydentne.

| Próba | RGB: średnia ± SD [ms] | Jasność: średnia ± SD [ms] | Jasność − RGB [ms] |
|---|---|---|---|
| 007, brak aktywnej maski | 0.089336 ± 0.000915 | 0.089568 ± 0.000935 | +0.000232 |
| 009, powtarzane guides | 0.098696 ± 0.001885 | 0.098712 ± 0.002045 | +0.000016 |

Różnice są małe i mniejsze niż SD pojedynczych próbek. Metryka jasności może zachować więcej maski; próba 009 sprawdza, że nie usunęła maski zachowanej przez maksimum RGB (tolerancja alpha .002). Zmienione pokrycie wpływa na koszt decode. Powtarzane guides nie odtwarzają rzeczywistej poprzedniej historii gry, a pokrycie maski nie jest precision/recall ghostingu. Nie zmierzono pełnego toru gry ani innych GPU.

Zmienia się tylko 8 opcjonalnych blobów kontrastu; pozostałe 86 jest identyczne z bazą. Off nadal wybiera stare shadery. [Dane, SD, sparowane przedziały różnic i CSV](private-ghost-brightness.json). Powtórzenie: `private_exposure_quality.exe --brightness-contrast-timing <stem> static`; `--contrast-quality` / `--contrast-quality-warp` oraz pełne domyślne testy sprawdzają obie metryki.
