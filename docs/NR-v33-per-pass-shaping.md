# NR v33 — inter-pass zależny od skali

> Aktualna wersja [NR v35](NR-v35-cadence-74-84.md): inter-pass co dwa przebiegi przy 74–84% włącznie, powyżej 84% wyłączony; benchmarki wyłącznie do 65%. Reguła gainów z v33 pozostaje bez zmian.

Użytkownik zgłosił gorszy obraz z inter-pass powyżej 90% oraz wątpliwości w zakresie 80–90%.
Na jego prośbę harmonogram obowiązuje wszystkie tryby inter-pass, łącznie z referencjami,
Optimized i Fast. Zapisany wybór trybu pozostaje zachowany przy zmianach skali.

| WorkingScale | Korekcje między modelami przy sześciu przebiegach |
| --- | --- |
| poniżej 80% | po przebiegach 1, 2, 3, 4, 5 |
| od 80% do poniżej 90% | po przebiegach 2 i 4 |
| od 90% | brak — zwykła ścieżka Off |

Progi odnoszą się do żądanej skali przed zaokrągleniem wymiarów tekstur.
Nie ma korekcji po ostatnim modelu. Przy dwóch przebiegach i skali 80–<90% nie ma
żadnej korekcji; przy trzech pojawia się jedna, po drugim modelu.
Off, jeden przebieg i kompresja przestrzenna nie wykonują inter-pass.

W pominiętym przejściu następny model dostaje zwykły wynik ClampProxy.
Nie tworzymy ani nie zapisujemy wtedy pola low i obrazów korekcji P100/working;
pomijamy ich dispatch i bariery. Istniejące powierzchnie pozostają w puli do ponownego użycia.
Zmiana harmonogramu lub aktywnej ścieżki resetuje historie modeli bez odtwarzania funkcji NGX.
Planowe pominięcie korekcji nie zgłasza awarii ani nie wymusza resetu co klatkę.
Rzeczywista awaria zaplanowanej korekcji zachowuje dotychczasowy fallback i reset.

Residual nadal jest liczony względem niezmiennego wejścia pierwszego modelu.
Low36 i Cross5 pozostają częścią Fast w aktywnych przejściach, również przy P50.
Kod HLSL, nagłówki shaderów i ich binaria nie zmieniają się.
Wszystkie sześć modeli nadal jest wykonywanych; zmieniamy tylko korekcję między nimi.
Widok diagnostyczny 7 kopiuje faktyczne wejście modelu 2, również gdy jest nim ClampProxy.
Kopiowanie diagnostyczne wykonuje się tylko przy wybranym widoku 7.

Końcowy transfer pozostaje wybrany przez użytkownika. Dotychczasowe „potęgowanie” high/mid
jest teraz **iloczynem gainów rzeczywistych modeli w bieżącym bloku**. Każdy model korzysta
z gainu swojego skonfigurowanego stylu Standard/Natural/Cinematic, łącznie z override danego
przebiegu. Nie powtarzamy gainu ostatniego stylu dla wszystkich modeli.
Ta wspólna reguła działa również w zwykłym końcowym resolve bez inter-pass.

Najpierw stosujemy istniejącą geometryczną korekcję gainu dla WorkingScale, a potem:

`adjusted_high = 1 + (scale_corrected_high - 1) * clamp(configured Structure, 0, 1)`

| Gain po korekcji skali | Structure | Gain high/mid tego modelu |
| --- | --- | --- |
| 0,8 | 1,0 | 0,8 |
| 0,8 | 0,5 | 0,9 |
| 0,8 | 0,0 | 1,0 — neutralny |
| 0,8 | 1,5 | 0,8 — bez ekstrapolacji |
| 1,2 | 0,5 | 1,1 |

Structure oznacza skonfigurowane `LocalStructure` modelu, z jego override, przed niezależną
opcją eksperymentalnej kompensacji Structure dla skali. Wartość powyżej 1 zawsze używa
kalibracji dla 1. SkinStructure i Intensity nie zmieniają kalibracji gainu w tej zmianie.
Preset rozmiaru sieci jest raportowany, ale nie ma osobnego zestawu skalibrowanych gainów;
wybór gainów pozostaje związany ze stylem modelu.

| Liczba modeli przy 80–<90% | Po modelu 2 | Po modelu 4 | Końcowy resolve |
| --- | --- | --- | --- |
| 6 | g₁·g₂ | g₃·g₄ | g₅·g₆ |
| 5 | g₁·g₂ | g₃·g₄ | g₅ |

Opcja `GuidedResidualCompoundPasses` (domyślnie true) steruje mnożeniem w bloku.
Wyłączenie jej zostawia pojedynczy, Structure-adjusted gain ostatniego modelu bloku.
Low pozostaje pojedynczym gainem ostatniego modelu, bez korekcji Structure i mnożenia.
Inter-pass po każdym modelu stosuje jego własny gain; końcowy blok zawiera ostatni model.
Od P90, przy Off lub braku korekcji, wszystkie modele należą do końcowego bloku.
Awaria korekcji nie przesuwa licznika; następna udana korekcja/resolve uwzględnia cały
pozostały zakres, bez ponownego naliczania modeli już skonsumowanych.

Iloczyn jest liczony na CPU w double i przekazywany jako skończony float do istniejącego
shadera; overflow/NaN daje neutralny fallback 1. Nie tworzymy tablicy ani alokacji na klatkę.
Nie dodajemy osobnego dispatchu shaping. UI pokazuje końcowy blok i rozwijaną listę
konkretnych gainów high/mid, low, stylów i Structure poszczególnych modeli.
Rozliczenie gainów nie oznacza identyczności obrazu względem częstszej rekonstrukcji,
ponieważ NGX, guiding, low i clamp są nieliniowe.

## Krótka weryfikacja w grze

Przycisk **Compare Off / scheduled Fast at 80% / 85% / 90% (CSV)** zastępuje poprzedni
profil P91/P95/P99. Dodaje bieżący zaokrąglony procent 80–99%, jeśli jest inny.
Bazowy zestaw to **15 konfiguracji / 21 okien**, z dodatkową skalą **20 / 28**.

Każda skala: Classic reference, Fused reference, Optimized, następnie
**Off / Fast / Fast / Off** w ABBA. Wszystkie tryby respektują wspólny harmonogram.
P80 i P85 sprawdzają jakość korekcji co dwa przebiegi względem Off;
P90 sprawdza wyłączenie: każda konfiguracja powinna raportować `Off`, stride 0 i 0 korekcji.
Nazwy konfiguracji oznaczają żądany tryb; kolumna `selected_path` opisuje faktyczną ścieżkę.
Referencje i Optimized mają własne okno do kontroli obrazu i pomocniczego porównania czasu.
Pojedynczego okna nie należy traktować jako równoważnego pomiarowi ABBA.

Profil wymusza Temporal DLAA residual + P100-guided, sześć przebiegów, Area, radius 1,
guide strength 1. Pozostawia wybrane przez użytkownika shaping i shadow gate.
Domyślnie każde okno ma 90 próbek rozgrzewki i 160 pomiarów GPU (łącznie 3360 lub 4480).
Opcja Hold pozwala obejrzeć obraz i zrobić zrzut po każdym oknie; zatrzymane próbki
nie trafiają do pomiaru. Należy też osobno obejrzeć ruch.
Ustawienia wracają po zakończeniu lub anulowaniu.

Raport `NR-v33-cadence-high-*` zawiera summary CSV, raw samples CSV, pairs CSV i TXT.
`comparison=1` oznacza lokalne Off/Fast ABBA. Bazą `delta_vs_expected_path_ms` jest tu Off.
`symmetric_delta_ms = (B1+B2-A1-A2)/2`: wartość ujemna oznacza szybszy Fast.
Summary i raw zawierają `interpass_stride` oraz `interpass_corrections` — faktyczną liczbę
udanych korekcji na klatkę. Benchmark czeka na pełny łańcuch sześciu modeli i komplet
korekcji zamiast mierzyć niekompletny fallback. Mierzy cały przedział NR obejmujący NGX,
przejścia i nakładanie pracy, a nie sam shader ani pełną klatkę gry.

Pozostałe profile zachowują konfiguracje: regularny, Below50, Boundary, Fast P50/P59/P65
oraz Fast poniżej 50%. Profile Fast zapisują teraz prefix v31 i nowe kolumny harmonogramu.
Przy dodatkowej bieżącej skali >=80% również stosują nową regułę. TXT zapisuje stan high/mid compounding oraz preset, styl i skonfigurowane Structure wszystkich sześciu modeli.

## Kontrole i ograniczenia

Test C++ polityki obejmuje 25–200%, tryby 0–7, 0–32 przebiegi, kompresję przestrzenną,
liczbę korekcji, rozliczanie bloków 2+2+2 i 2+2+1, błędy korekcji, rzeczywistą wspólną funkcję iloczynu high/mid dla zmiennych gainów i Structure, NaN/infinity oraz sąsiednie reprezentowalne wartości
float po obu stronach 80% i 90%. Zachowuje wcześniejsze kontrole wyboru ścieżek i kafli.
`test_nr_interpass_cadence.py` sprawdza integrację harmonogramu i shapingu, reset, fallback,
niezmienną bazę residuala, debug i raportowanie. Istniejące testy arytmetyki i planu
benchmarku pozostają obowiązkowe w workflow buildu.

Zmiana gainów może zmienić wygląd również poniżej 80%, jeśli modele mają różne style albo Structure poniżej 1. Dotychczasowe wyniki nie są pomiarem tych nowych ustawień.
Nie ma jeszcze nowego pomiaru GPU całego NR w grze ani potwierdzenia poprawy jakości
w zakresie 80–<90%. Redukcja liczby korekcji 5→2 jest liczbą operacji, nie zmierzonym
przyspieszeniem. Wcześniejsze syntetyczne pomiary P91/P95/P99 nie mierzą tej zmiany.
Nie przypisujemy obserwowanego pogorszenia konkretnej przyczynie bez weryfikacji.

## Zweryfikowana paczka

- Źródło DLL: `4df53adde6ea315c68d9afbc768b11a1f70eeb72`.
- [GitHub Actions 38143314275](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/actions/runs/38143314275): success; testy Python, polityka i rzeczywiste funkcje iloczynu/Structure C++, kompilacja shaderów i fixture, build DLL, paczka i publikacja.
- [Release v33](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/releases/tag/nr-interpass-v33-scale-cadence-20261011).
- ZIP: `OptiScaler-NR-nr-interpass-v33-scale-cadence-20261011.zip`, 131119115 bajtów.
- SHA256: `a5bd83a0607def5c88eeb7274655b7fc4757665188100bab677c7a36d85dcc5d`.
- Lokalnie sprawdzono CRC ZIP, wszystkie 62 wpisy SHA256SUMS, źródłowy commit DLL, nowy benchmark i UI gainów/Structure oraz nowe opisy INI. Wszystkie sześć payloadów CSO odpowiada wcześniejszym shaderom v27; brak starego benchmarku P91/P95/P99 i wycofanych opcji.
- Paczki nie instalowano automatycznie w grze. Nowe wyniki obrazu i całego NR pozostają do oceny w benchmarku w grze; powyższe kontrole nie są pomiarem przyspieszenia.
