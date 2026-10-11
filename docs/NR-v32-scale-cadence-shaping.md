# NR v32 — inter-pass zależny od skali

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

Końcowy transfer pozostaje wybrany przez użytkownika. Potęgowanie high/mid uwzględnia
liczbę przebiegów od ostatniej **udanej** korekcji inter-pass. Jest sterowane dotychczasową
opcją `GuidedResidualCompoundPasses` (domyślnie true), dostępną również przy korekcji co dwa przebiegi.
Najpierw wyliczamy gain dla skali, następnie podnosimy go do potęgi długości bloku:

| Liczba modeli przy 80–<90% | Po modelu 2 | Po modelu 4 | Końcowy resolve |
| --- | --- | --- | --- |
| 6 | high/mid gain² | high/mid gain² | gain² za modele 5–6 |
| 5 | high/mid gain² | high/mid gain² | gain¹ za model 5 |

Przy gainie pojedynczego przebiegu 0,90 para korzysta z 0,81. Blok używa stylu modelu,
który go kończy, zgodnie z regułą zwykłego resolve używającego ostatniego stylu.
Low nie jest potęgowane i zachowuje pojedynczy gain w każdym correction/resolve.
Wyłączenie opcji potęgowania pozostawia również high/mid pojedyncze dla każdego bloku.
Nie naliczamy ponownie przebiegów objętych wcześniejszą korekcją; awaria nie przesuwa
licznika. Przy inter-pass po każdym modelu końcowy blok nadal liczy jeden model.
Od P90 wszystkie modele należą do końcowego bloku, jak przy Off.
Potęgowanie stałego gainu poprawnie rozdziela liczbę przebiegów; nie oznacza identyczności
obrazu względem częstszej rekonstrukcji, ponieważ NGX, guiding, low i clamp są nieliniowe.

Nie dodajemy osobnych dispatchy shaping. CPU przekazuje odpowiedni gain do istniejących
shaderów. UI pokazuje przewidywane końcowe gains i liczbę modeli końcowego bloku.

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

Raport `NR-v32-cadence-high-*` zawiera summary CSV, raw samples CSV, pairs CSV i TXT.
`comparison=1` oznacza lokalne Off/Fast ABBA. Bazą `delta_vs_expected_path_ms` jest tu Off.
`symmetric_delta_ms = (B1+B2-A1-A2)/2`: wartość ujemna oznacza szybszy Fast.
Summary i raw zawierają `interpass_stride` oraz `interpass_corrections` — faktyczną liczbę
udanych korekcji na klatkę. Benchmark czeka na pełny łańcuch sześciu modeli i komplet
korekcji zamiast mierzyć niekompletny fallback. Mierzy cały przedział NR obejmujący NGX,
przejścia i nakładanie pracy, a nie sam shader ani pełną klatkę gry.

Pozostałe profile zachowują konfiguracje: regularny, Below50, Boundary, Fast P50/P59/P65
oraz Fast poniżej 50%. Profile Fast zapisują teraz prefix v31 i nowe kolumny harmonogramu.
Przy dodatkowej bieżącej skali >=80% również stosują nową regułę. TXT zapisuje także stan high/mid compounding.

## Kontrole i ograniczenia

Test C++ polityki obejmuje 25–200%, tryby 0–7, 0–32 przebiegi, kompresję przestrzenną,
liczbę korekcji, rozliczanie bloków 2+2+2 i 2+2+1, błędy korekcji, rzeczywistą wspólną funkcję potęgowania high/mid, NaN/infinity oraz sąsiednie reprezentowalne wartości
float po obu stronach 80% i 90%. Zachowuje wcześniejsze kontrole wyboru ścieżek i kafli.
`test_nr_interpass_cadence.py` sprawdza integrację harmonogramu i shapingu, reset, fallback,
niezmienną bazę residuala, debug i raportowanie. Istniejące testy arytmetyki i planu
benchmarku pozostają obowiązkowe w workflow buildu.

Nie ma jeszcze nowego pomiaru GPU całego NR w grze ani potwierdzenia poprawy jakości
w zakresie 80–<90%. Redukcja liczby korekcji 5→2 jest liczbą operacji, nie zmierzonym
przyspieszeniem. Wcześniejsze syntetyczne pomiary P91/P95/P99 nie mierzą tej zmiany.
Nie przypisujemy obserwowanego pogorszenia konkretnej przyczynie bez weryfikacji.
