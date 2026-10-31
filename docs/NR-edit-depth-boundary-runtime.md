# Eksperymentalny podgląd i automat: kontrast granic głębi

D3D12 Superior Scaling, prywatne DLAA. Nowy tryb stosuje **jeden globalny mnożnik dodatniego editu**, oparty na [badaniu blisko/daleko](NR-edit-depth-boundary-target.md). Nie jest przestrzennym depth-weighted gain i nie rozjaśnia ujemnego editu. Mierzy `NR answer - current proxy` **przed** bieżącym gain; nie powstaje sprzężenie „ustaw 2×, a następnie miernik proponuje 1×”.

## UI i ustawienia

**Positive multiplier measurement** wybiera:

- **Positive R/G coverage** — dotychczasowy miernik i kalibracja CNN/Transformer, nadal domyślny. Jego shader pozostał identyczny bajt w bajt.
- **Depth-boundary contrast (experimental)** — nowy pomiar kontrastu na granicach głębi, wspólny dla **Preview suggested positive multiplier** i **Automatically apply suggested multiplier**. Sam wybór metody nie włącza pomiarów ani automatu.

**Depth-boundary contrast target** ma suwak 0,01..0,5. **Exact boundary contrast target** przyjmuje 0,000001..16; default **0,1429128154**, zapisany jako FP32 0,142912819982. To średnia absolutna różnica R/G z głowy i wody przy 6× w dostarczonej scenie Transformer K. Cel nie został zweryfikowany dla innych scen ani CNN; obie rodziny mogą wybrać ten sam algorytm i ręcznie dobrać cel/profil. Domyślnego celu nie nazywamy progiem braku ghostingu. W tamtej scenie 6× nadal miało resztkowe smużenie.

UI pokazuje **Depth-boundary pairs**, **smoothed contrast at 1x**, **target**, aktualną surową i wygładzoną propozycję oraz stan pauzy. Czasy EMA, catch-up i gate zmiany pola MV są wspólne z dotychczasowym miernikiem. Ręczny mnożnik pozostaje zachowany po wyłączeniu Auto.

```ini
[DlssNr]
; 0=Positive R/G coverage; 1=Depth-boundary contrast
SuperiorGainPreviewMethod=0
SuperiorGainPreviewBoundaryTarget=0.1429128154
```

Oba klucze mają deklarację/default, odczyt/zapis INI, zastosowanie/reset profilu i należą wyłącznie do `kKeptKeys`. Kontrolki są w istniejącej sekcji wyłączanej na native Vulkan / niedostępnej ścieżce Superior.

## Selekcja i wzór

Rzadki shader pracuje najwyżej 8 razy na sekundę: 256×144 punktów, 64 fazy 8×8. W każdym punkcie sprawdza dwa kierunki (pion/poziom):

1. Sąsiednie depth mają stosunek proximity ≥2. Proximity to `depth` przy reversed Z, `1-depth` przy zwykłym Z. To przestrzeń projekcyjna, nie fizyczna odległość. Wymagany skok > max(1e-7, 2 LSB źródłowego D16/D24); precision pochodzi z istniejącego rozpoznania formatu. Miernik czyta już dopasowaną R32 głębię, nie interpretuje bezpośrednio widoków D16/D24.
2. Końce editu są odsunięte o jeden piksel do wnętrza obu stron (odległe o 3 px). Oba końce zachowują depth swojej strony z tolerancją 25% i nadal mają ratio ≥2. Granice obrazu są sprawdzane przed unsigned odejmowaniem/odczytem.
3. Co najmniej jeden koniec ma MV >0,1 px, a różnica wektorów >2 px siatki prywatnego DLSS. Niefinitywne/skrajne MV i nieprawidłowa głębia odpadają.
4. Dopiero po selekcji odczytywany jest surowy edit R/G obu końców. Każdy kanał liczy osobną wartość bezwzględną; nie ma anulowania przeciwnych zmian R i G w luminancji.

Dla surowego signed edit `E`:

```
N = max(min(E,0),-0.5)
P = max(E,0)
A = mean_RG(abs(Na-Nb))
B = mean_RG(abs(Pa-Pb))
contrast(g) = mean_pairs(A) + g*mean_pairs(B)
gain = max(1, (target - mean_pairs(A)) / mean_pairs(B))
```

Dolne ograniczenie N modeluje clipping nośnika poniżej zera. Kontrast w każdym kanale rośnie monotonicznie z gain. Brak górnego clippingu jest warunkiem liniowego modelu; pary z dodatnim editem > `(65504-0.5)/1024` są nieważne. Wymagamy ≥64 poprawnych par i ≥95% poprawnych wśród kandydatów. Jeśli kontrast przy 1× wystarcza, wynik wynosi 1×. Brak sterowalnego B, niefinitywne sumy lub cel powyżej zakresu do 1024× wstrzymuje pomiar zamiast wymuszać maksimum. Identycznych editów obu powierzchni nie da się rozdzielić dodatnim mnożnikiem.

Obowiązuje dotychczasowy gate 576 punktów ruchu i zmiany między klatkami; nie wykonujemy odczytów depth/editu po odrzuceniu gate. To nie jest semantyczne wykrywanie menu/filmów. Hold i nieobsługiwane wejścia blokują również spóźnione wyniki. Wymagane są plain edit mode, saturation 1×, depth colour off, nieaktywne oba depth brightness i wyłączony eksperyment original guides. Dotychczasowe wyszarzenia oraz pauzy pozostają w mocy.

## EMA i automat

Wygładzane są najpierw **mean(A), mean(B)** przez czasową EMA, a potem log2 wyliczonego gain. Dwie kolejne propozycje odsunięte >0,5 EV od wyjścia w tym samym kierunku włączają szybsze nadganianie; przemienne skoki nie spełniają tego warunku. Catch-up kończy się przy różnicy ≤0,07 EV. To konserwatywniejsza bramka niż coverage, ponieważ pojedyncze fazy mają większy rozrzut. Zmiana nie przestawia reakcji starego trybu.

Automat używa istniejących zasad: minimum **1×**, cztery aktualne ruchome pomiary przed pierwszym sterowaniem, nie częściej niż co 0,5 s, histereza +2% / spadek o czynnik 1,05. W pauzie trzyma ostatni zastosowany gain; przed pierwszym użytecznym wynikiem pozostaje manual z minimum 1×. Nie zmienia brightness ani innych suwaków. Rzeczywista zmiana gain nadal resetuje prywatną historię DLSS/ekspozycji.

Metoda i cel uczestniczą w kontekście miernika/automatu. Zmiana czyści estymatę, baseline MV i unieważnia stare odczyty przed wyborem gain; nowy Auto ponownie przechodzi rozgrzewkę. Program, layout wyniku i deskryptory są niezmienne przez czas życia zapisu GPU, także podczas zmiany metody. Sloty zachowują własny program i referencję do depth; reset/shutdown nie zwalnia zasobów pod GPU. Zachowano limity puli i brak CPU/GPU wait.

## Zrzuty

Zapis DDS nadal zawiera edit po faktycznie zastosowanym gain. JSON schema 2 dodatkowo zapisuje `gain_preview_method_at_capture`, `gain_preview_boundary_target_at_capture` i stan pokazanej/odczytanej propozycji: `preview_request_*` / `preview_capture_method`, `boundary_pairs`, `boundary_base`, `boundary_slope`, `boundary_target`. Są to zakończone asynchroniczne pomiary z własnym wiekiem, nie deklaracja, że propozycja pochodzi dokładnie z przechwyconej klatki. `gain_preview_target_at_capture` opisuje cel aktualnej metody, nie zawsze cel coverage. `positive_gain` nadal odpowiada edicie w DDS. Zachowano oba przyciski zapisu i poprzednie ograniczenia guides.

## Testy

- CPU: rozwiązanie gain, brak sterowalności, zakres, reset kontekstu, EMA stabilnego sygnału i trwałego skoku, brak catch-up od przemiennych wahań, pauza przy <64 parach, kompaktowy wynik z padded rows i serial 64-bit; istniejące testy Auto.
- WARP i RTX 4090: **112 nowych przypadków** względem niezależnej skalarnej referencji w double (56 FP32 + 56 FP16): obie konwencje Z, cztery fazy, flat/layered/NaN/Inf depth, syntetyczna kwantyzacja UNORM16 na R32, identyczny MV, niedopuszczalny HDR edit, sam ujemny edit i frozen-MV gate. Testy nie zastępują walidacji źródłowych formatów D16/D24 w torze guides.
- Produkcyjny runtime obu metod: wynik przed Reset listy gry, replay bez powtórnego pomiaru, pula, recykling, pauza, zmiana metody/celu i odrzucenie starego kontekstu. Istniejące testy coverage pozostają uruchamiane.
- Rzeczywisty zrzut użytkownika na GPU, **64 fazy**: **6,18147154058×**, niezależne CPU 6,18147128662×. Próbki mają 145..228 par, raw gain 5,03866..7,96790×. To porównanie wspólnych momentów nieruchomego zrzutu, nie wynik EMA w poruszającej się grze. [Liczby](validation/nr-boundary-gain-capture-gpu.json). Nie uruchamiano NGX.
- Profile z aktualnym Config.cpp, JSON/DDS serialization, policy/ABI, rebuild blobów; workflow eksperymentu uruchamia nowe przypadki przez istniejący test gain-preview przed Release DLL. Nowy blob jest podłączony do rzeczywiście wybieranej opcji; stary shader coverage ma niezmieniony SHA256 `bbf0ea5b52300a197ce6683a64f615e6d6f22b1201e61d6656ca606583022374`.

## GPU: zmierzony koszt

RTX 4090, rezydentne proceduralne proxy/answer FP16, R32 depth, RG32 MV. Timestamp obejmuje przejścia COPY_SOURCE→UAV→COPY_SOURCE, dispatch oraz kopię wyniku do readback. Alokacje/upload i oczekiwanie fixture są poza oknem; produkcyjny koszt sygnału/kolejki oraz NR/NGX/final composition również poza pomiarem. Osobna rozgrzewka 16 prób każdego wariantu, 16 kwartetów ABBA, po 32 próbki A/B. A=Off (pusta para timestamp), B=próba gate lub cały miernik. [Surowe dane](validation/nr-boundary-gain-preview-timing.csv).

| Rozdzielczość | Etap B | Off średnia±SD ms | B średnia±SD ms | Różnica |
|---|---|---:|---:|---:|
| 2240×1264 | gate odrzucający ruch | 0,000448±0,000508 | 0,010287±0,000283 | +0,009839 |
| 2240×1264 | pełny nowy pomiar | 0,000544±0,000511 | 0,033343±0,000440 | +0,032799 |
| 2496×1404 | gate odrzucający ruch | 0,000512±0,000512 | 0,010535±0,000369 | +0,010023 |
| 2496×1404 | pełny nowy pomiar | 0,000576±0,000508 | 0,033330±0,000361 | +0,032754 |

W tej samej sesji osobne ABBA Off-vs-coverage dało odpowiednio 0,027361±0,000382 i 0,026860±0,000403 ms dla pełnego poprzedniego miernika ([dane referencyjne](validation/nr-boundary-gain-coverage-reference-timing.csv)). To różne przebiegi i pola MV testu, nie bezpośredni ABBA obu metod w grze; nie przedstawiamy różnicy ~0,006 ms jako dowodu kosztu całego toru. Nowy miernik czyta dodatkową głębię, a zmniejsza wynik i nie ma histogramu/atomików: kompaktowe 32×10 uint4 = **5120 B** kopiowanego wyniku vs stary readback około 76 KiB. Potrzebny jest jeden dispatch 8 grup, bez pełnoekranowej maski lub drugiego DLSS. Pomiar ≤8/s dotyczy nowych zapisów; replay gry może ponownie wykonać już nagrany dispatch, jak w poprzednim mierniku.

Domyślne Off preview/Auto nadal nie tworzy jego PSO, zasobów i fence ani nie wykonuje pracy GPU. Coverage pozostaje domyślne. Pomiar całego toru i jakości wymaga rozgrzanego ABBA w tej samej scenie gry, z porównaniem gain/stabilności/ghostingu i timestamp NR+prywatny DLSS+composition. Nie potwierdzono jeszcze jakości nowego automatu w grze, progu CNN ani odporności na zmianę odległości postać–tło.
