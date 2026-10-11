# NR v34 — inter-pass co dwa przebiegi przy 74–84%

> Wersja historyczna. [NR v35](NR-v35-cadence-74-84.md) zachowuje ten harmonogram,
> ale usuwa profil wysokich skal i ogranicza wszystkie benchmarki do 65%.

Na prośbę użytkownika zmieniamy wyłącznie progi harmonogramu z v33:

| Żądany WorkingScale | Inter-pass przy sześciu modelach |
| --- | --- |
| poniżej 74% | po modelach 1, 2, 3, 4, 5 |
| od 74% do 84% włącznie | po modelach 2 i 4 |
| powyżej 84% | wyłączony, zwykła ścieżka Off |

Dokładne 74% i 84% należą do zakresu korekcji co dwa przebiegi.
Progi odnoszą się do żądanej skali przed zaokrągleniem tekstur, również przy wartościach
ułamkowych. Już wartość większa niż 84% wyłącza korekcję.
Reguła jest wspólna dla referencji, Optimized i Fast. Off pozostaje wyłączony.
Ostatni model nie ma korekcji inter-pass. Przy dwóch modelach w zakresie 74–84% nie ma
korekcji; przy pięciu występuje po drugim i czwartym modelu.

Mnożenie gainów stylu/Structure zachowuje implementację [v33](NR-v33-per-pass-shaping.md).
Przy pięciu modelach bloki high/mid to g₁·g₂, g₃·g₄ i końcowe g₅; przy sześciu końcowe
g₅·g₆. Low nie jest mnożone. Bez inter-pass końcowy resolve obejmuje wszystkie modele.
Zmiana nie modyfikuje shaderów, ich binariów ani sposobu obliczania gainów.
Pominięte przejścia używają ClampProxy, a zmiana aktywnego harmonogramu resetuje historie
modeli zgodnie z istniejącą obsługą. Nie dodajemy nowych dispatchy ani zasobów GPU.

## Krótki benchmark

Przycisk **Compare Off / scheduled Fast at 74% / 84% / 85% (CSV)** sprawdza obie granice
zakresu co dwa przebiegi i pierwszą wyłączoną skalę całkowitą. Dodaje bieżący procent
74–84%, jeśli jest inny niż 74 i 84. Nie dopisuje dalszych wyłączonych skal powyżej 85%.

To 15 konfiguracji / 21 okien albo 20 / 28 z dodatkową bieżącą skalą. Każda skala ma
oba tryby referencyjne, Optimized oraz Off/Fast/Fast/Off w ABBA. Pozostają oddzielna
rozgrzewka, pomiar całego NR znacznikami GPU, statystyki zmienności, raw/paired CSV,
metadane stylów/Structure oraz opcjonalny Hold do oględzin bez próbek zatrzymania.
P74 i P84 przy sześciu modelach mają stride 2 i dwie udane korekcje; P85 w każdym
żądanym trybie raportuje faktyczne Off, stride 0 i zero korekcji.
Prefixy Fast to `NR-v34-cadence-high-*`, `NR-v34-fast-*` i `NR-v34-fast-low-*`.
Inne profile zachowują swoje zestawy konfiguracji i respektują nowe progi przy dodatkowej
bieżącej skali. Ustawienia wracają po zakończeniu lub anulowaniu.

## Weryfikacja

Test C++ sprawdza harmonogram w zakresie 25–200%, 0–32 modele, tryby 0–7,
kompresję przestrzenną i sąsiednie reprezentowalne wartości float wokół 74% i 84%.
Zachowuje kontrole iloczynów gainów, Structure, braku podwójnego rozliczania i błędów.
Testy Python sprawdzają integrację i nowy zestaw benchmarku; workflow buduje DLL i paczkę.
Nie ma nowego pomiaru GPU w grze ani oceny obrazu dla nowych progów. Liczba korekcji
nie jest zmierzonym przyspieszeniem. Benchmark służy do tej weryfikacji.
