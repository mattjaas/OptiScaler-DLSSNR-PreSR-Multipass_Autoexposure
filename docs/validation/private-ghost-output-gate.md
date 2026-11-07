# Usuwanie maski na podstawie wyniku prywatnego DLSS

2026-11-07, baza `96bb2b5c`. Użytkownik oznaczył nową parę: `nr-edit` ma ghosting za postacią; w `nr-edit-001` od pasa w górę ghosting jest praktycznie niewidoczny. Nogi są osobną kontrolą, a nie przykładem całkowicie poprawnego obrazu. Zmiana white pointu była sposobem uzyskania różnych warunków, nie poszukiwanym predyktorem. Celem jest **usunięcie istniejącej maski w miejscach bez smugi**, z zachowaniem DLSS, a nie tylko odmienne skalowanie kontrastu wejścia.

## Wniosek z pary

Sam kontrast dwóch editów przed DLSS nie opisuje, ile starego obiektu zostało w wyniku DLSS. Również samo `abs(after - raw)` obejmuje poprawne zmiany szczegółów i stabilizację. Sprawdzamy kierunek zmiany wyniku względem editu przeszkody, która wcześniej zasłaniała tło.

Jednoklatkowy pakiet nie zawiera historycznego editu tej przeszkody. W analizie CPU używamy jawnego przybliżenia: koloru po wewnętrznej stronie bieżącej lewej sylwetki, niezależnie w każdym wierszu. Przesunięcia 4/8/16 px sprawdzają wrażliwość na ten wybór. Segmentacja: składowa głębi reversed Z >0.02 zawierająca (1250,800); góra od głowy do wiersza 876 /889, dół od pasa do 50 px przed końcem obrazu. Są to regiony dobrane do tej sceny, nie algorytm produkcyjnego rozpoznawania postaci.

Poniżej **średnia zachowana waga kryterium w pasie tła**, przy przybliżonej referencji 8 px. To nie rzeczywista maska gry ani precision/recall. Próg wejściowej jasności .0723, softness .01; kryterium wyniku .10/.05.

| Region tła | Próbka | Dotychczasowy warunek jasności | Nowy warunek wyniku |
|---|---|---:|---:|
|góra, 4–30 px od sylwetki|ghosting|94.2%|56.9%|
|góra, 4–30 px od sylwetki|praktycznie bez ghostingu|72.8%|10.4%|
|góra, 30–80 px|ghosting|93.4%|36.4%|
|góra, 30–80 px|praktycznie bez ghostingu|72.6%|2.0%|
|dół, 4–30 px|pierwsza próbka|100%|98.6%|
|dół, 4–30 px|druga próbka|96.7%|39.6%|

Przy referencji 4/8/16 px nowa waga przy górze, 4–30 px od sylwetki, wynosi 50.3–56.9% w złej próbce i 8.8–10.4% w dobrej. Dla 30–80 px: 30.2–36.4% oraz 1.9–5.1%. Nie utożsamiamy całego złego pasa z ghostingiem: są w nim również poprawne piksele. Wynik uzasadnia eksperyment, nie uniwersalny detektor. [Pełne statystyki i hashe](private-ghost-output-pair.json).

Reprodukcja CPU: `python tools/analyze_nr_output_gate.py <bad stem> <good stem> --waist 876 889 --output <report.json> --preview-dir <local previews>`. Wymaga NumPy/SciPy, podglądy Pillow. Zapisuje tylko statystyki/hashe do raportu; obrazy pozostają lokalne. Podgląd nakłada wskazania wyłącznie na opisany pas tła, bez symulowania prawdziwej historii maski. Ma wspólną skalę wizualizacji `0.5+4*edit`.

## Opcjonalna funkcja

**Remove mask without DLSS carry-over (experimental)**, domyślnie Off. Działa dla metod temporalnych 3–6 (indeksy 2–5), w tych samych ograniczeniach API co obecna maska. Jest niezależna od **Only mask low-contrast disocclusions**; można włączyć jedną albo obie. Wspólne referencje są potrzebne, gdy aktywna jest przynajmniej jedna z nich.

W grze referencją jest już istniejąca historia oryginalnego editu obiektu zasłaniającego, przenoszona wraz z licznikiem dysokluzji. Nie zastępujemy jej kolorem bieżącej sylwetki. Nie ma wyszukiwania postaci ani zależności od white pointu jako ustawienia.

Dla bieżącego editu `r`, zapamiętanego editu `o` i rzeczywistego wyniku DLSS `y`:

1. Przekształcamy osobno `r` i `o` aktualnym transportem do `R` i `O`. Uwzględniamy dodatni mnożnik lub współczynniki GPU transportu symetrycznego/przesuniętego oraz clipping wejścia. Ocena następuje po wzmocnieniu, przed jakąkolwiek maską/global fade.
2. `d=O-R`, `v=y-R`, `fraction=max(0,dot(v,d))/dot(d,d)`. To przybliżony udział starej przeszkody w zmianie wyniku. Nie wykonujemy ponownie dzielenia przez gain/dekodowania DLSS.
3. **Minimum DLSS occluder carry-over**: domyślnie .10. Poniżej progu maska znika; większy próg zachowuje więcej DLSS i może przepuścić delikatniejsze smugi. **DLSS carry-over gate softness**: domyślnie .05; pełna waga od .15, pomiędzy płynne smoothstep. Zero softness daje twardy próg.
4. Wymagamy dodatniego, zgodnego kolorystycznie kierunku zmiany: kwadrat cosinusa `dot(v,d)^2/(dot(v,v)*dot(d,d))`, smoothstep .64–.81. Nie używamy sqrt/normalize. Sama zmiana prostopadła lub przeciwna do koloru dawnej przeszkody nie wystarcza do utrzymania maski.
5. Zmiany w każdym kanale do `max(1/1024,abs(R)/1024)` są poniżej przyjętej tolerancji nośnika i usuwają maskę. Przy niemal identycznych referencjach, ale istotnej zmianie wyniku, pozostawiamy maskę jako przypadek niejednoznaczny. Niefinitywne dane także nie służą za dowód braku smugi.

Jedna wspólna waga RGB tylko **zmniejsza** istniejącą maskę. Nie inicjuje ani nie odnawia dysokluzji, nie wydłuża N, nie zmienia MV/depth, nie resetuje NR/NGX i nie steruje mnożnikiem. Zwykłe wygaszanie globalne nadal działa. Outward feather/rings następuje później: celowo może utworzyć miękki obrzeżny pas obok zachowanego rdzenia maski.

Pełna odpowiedź NR jest obsługiwana w istniejącym decode odpowiedzi: kierunek referencji jest rekonstruowany ze starego **editu** przy bieżącej bazie proxy. Nie przechowujemy historycznego proxy ani historycznej pełnej odpowiedzi, więc nie jest to dokładny model ich mieszanki. Główna dostarczona para dotyczy trybu Edit NR. W obu trybach to heurystyka: mieszane barwy, zmiana wyglądu przeszkody albo bardzo słaba smuga mogą zostać przepuszczone. Dobra stabilizacja zbieżna kolorystycznie z dawną przeszkodą może pozostać zamaskowana. Nie deklarujemy pewnego rozpoznawania braku ghostingu z jednej klatki.

Bez obu filtrów używane PSO i ich bajty pozostają bez zmian. Gdy referencje były już potrzebne do kontrastu, nowa opcja używa tych samych tekstur, historii i dispatchy. Jej samodzielne włączenie uruchamia opcjonalną historię referencji tak jak filtr kontrastu; nie dodajemy odczytu do CPU ani nowych zasobów względem tej ścieżki. Przełączenie potrzeby referencji inicjalizuje tylko lokalną historię maski. Zmiana suwaków lub przełączanie obu kryteriów przy nadal potrzebnej referencji nie resetuje tej historii.

INI/profil/reset: `SuperiorGhostOutputGate`, `SuperiorGhostOutputThreshold`, `SuperiorGhostOutputSoftness`, wszystkie w `kKeptKeys`. JSON zrzutu: `ghost_output_gate`, `ghost_output_threshold`, `ghost_output_softness`. `.raw.dds` i `.after.dds` nadal oznaczają edit przed transportem oraz dekodowany wynik przed odrzucaniem/fade. ABI 256 B i frozen v59 pozostają bez zmian; nazwane pola 180–188 przenoszą nową opcję, pole 176 nadal wybiera transport. Nie przywracamy odrzuconych ustawień nasycenia/kolorowania/rozjaśniania głębi.

## Weryfikacja

RTX4090 oraz WARP: 416 niezależnych przypadków znanych mieszanek i ich braku, zmiany prostopadłe/przeciwne, przejście editu przez zero przy gain .5/6, wszystkie cztery metody temporalne, pełna odpowiedź, transporty symetryczny/przesunięty, global fade, nieważona/ważona referencja, niejednoznaczność, tolerancja nośnika, shaderowe fallbacki NaN/Inf i połączenie z kontrastem. `--output-gate-quality[-warp]`, także w zestawie domyślnym/`--warp`. To test poprawności wybranego modelu mieszanki; nie etykietowana ocena jakości w grze.

Dotychczasowe 688 testów kontrastu RGB/jasności przechodzi na RTX/WARP. Kontrole profili z aktualnym `Config.cpp`, ekspozycji, metadanych/parsowania JSON, Superior port, frozen ABI oraz byte-for-byte 114 blobów również przechodzą. Nowe klucze mają deklaracje, defaulty, odczyt/zapis i pola profilu. Przebudowane są wyłącznie 12 opcjonalnych PSO kontrastu/decode. Nie zmieniamy polityki skalowania ani domyślnego toru bez filtrów.

## Koszt GPU

RTX4090,2496×1404,metoda6/coarse radius1,gain1,strength1,N19/fade9,depth.05,bez feather/global fade. Mierzymy cały tor clear→scatter→history→[reference history]→dilate→decode wraz z barierami. Bez NR, NGX i końcowego resolve. 16 par rozgrzewki,64 bloki ABBA,128 próbek/wariant. Oddzielne historie obu wariantów. Obrazy raw/after są z dostarczonej pary; `static` powtarza zapisane guides, `moving` używa kontrolowanej dwufazowej geometrii/MV istniejącego benchmarku. To **nie replay prawdziwej sekwencji gry**. Zmniejszone pokrycie zmienia też obciążenie decode, więc różnica nie jest izolowanym kosztem kilku mnożeń.

| Obraz / guides | Baza | Przed ms ±SD | Z kryterium wyniku ms ±SD | Różnica ms |
|---|---|---:|---:|---:|
|nr-edit / static|sama dysokluzja|.068960 ±.003214|.088480 ±.002109|+.019520|
|nr-edit / moving|sama dysokluzja|.069928 ±.001823|.088952 ±.001138|+.019024|
|nr-edit-001 / static|sama dysokluzja|.069008 ±.002718|.091600 ±.001817|+.022592|
|nr-edit-001 / moving|sama dysokluzja|.071656 ±.000661|.088328 ±.001963|+.016672|
|nr-edit / static|kontrast jasności|.092752 ±.002416|.092744 ±.000915|−.000008|
|nr-edit / moving|kontrast jasności|.093136 ±.001163|.093128 ±.000973|−.000008|
|nr-edit-001 / static|kontrast jasności|.092616 ±.000907|.092992 ±.000968|+.000376|
|nr-edit-001 / moving|kontrast jasności|.091464 ±.003333|.091352 ±.002540|−.000112|

Samodzielna opcja dodaje w tych pomiarach około .017–.023 ms, głównie opcjonalną historię referencji. Przy już włączonym kontraście przyrost jest mniejszy od zmienności; nie deklarujemy zerowego kosztu ani przyspieszenia. [Surowe czasy, statystyki i różnice pokrycia](private-ghost-output-timing.json). Odtworzenie: `--output-gate-timing <stem> static|moving [with-contrast]`. Przy bazie z kontrastem obie strony używają jasności .02/.005, B dodatkowo bramkuje wynik.

Ocena z gry po tej zmianie pozostaje do wykonania. Nie nazywamy lokalnego podglądu z przybliżoną referencją dokładnym wynikiem nowej maski gry.
