# NR v36 — inter-pass co trzy przebiegi przy 85–90%

Wszystkie włączone tryby inter-pass używają tego samego harmonogramu:

| Żądany WorkingScale | Odstęp | Korekcje przy sześciu modelach |
| --- | --- | --- |
| poniżej 74% | 1 | po modelach 1, 2, 3, 4, 5 |
| 74–84% włącznie | 2 | po modelach 2 i 4 |
| powyżej 84%, poniżej 85% | wyłączony | brak |
| 85–90% włącznie | 3 | po modelu 3 |
| powyżej 90% | wyłączony | brak |

Progi dotyczą skali żądanej przed zaokrągleniem tekstur. Ułamkowy odstęp 84–85%
zachowuje dotychczasowe wyłączenie. Off i kompresja przestrzenna nadal wyłączają
inter-pass. Ostatni model nigdy nie ma korekcji: w nowym zakresie potrzeba co
najmniej czterech modeli. Przy siedmiu modelach korekcja następuje po 3 i 6.

High/mid uwzględnia gain własnego stylu i Structure każdego modelu od ostatniej
udanej korekcji. Przy sześciu modelach w nowym zakresie są to bloki 1–3 i 4–6
(3+3), przy pięciu 1–3 i 4–5 (3+2). Low pozostaje jednym gainem ostatniego modelu
bloku. Nieudana korekcja nie zużywa bloku. Zmiana odstępu resetuje historie NR
przez istniejący mechanizm. Menu, komentarze INI i raport opisują nowy odstęp.

Zmiana używa istniejących shaderów, dispatchy i zasobów; nie zmienia HLSL ani
bytecode. W porównaniu z dotychczasowym Off przy 85–90% wykonuje dodatkową
korekcję i związane operacje GPU po każdym trzecim modelu, gdy jest kolejny model.
To świadomy kompromis jakości i kosztu, bez deklaracji zmierzonego przyspieszenia.

Nie wykonano pomiaru GPU ani oceny obrazu w grze. Weryfikacja wymaga ręcznego
porównania poprzedniego Off i nowego harmonogramu w tej samej scenie przy
85/90%, z taką samą liczbą modeli i ustawieniami: osobna rozgrzewka, ABBA,
znaczniki czasu całego NR, mediana i zmienność w ms oraz oględziny HDR,
krawędzi, detali i ruchu. Należy zapisać GPU i rozdzielczość. Rutynowe benchmarki
zachowują limit 65% z v35; zmiana nie przywraca profilu wysokich skal.

Test C++ polityki obejmuje wszystkie tryby, 0–32 modeli, wyłączenia, sąsiednie
wartości float przy 74/84/85/90%, brak korekcji po ostatnim modelu oraz
rozliczenie gainów bloków co trzy modele, również po nieudanej korekcji.
Istniejący workflow buduje i pakuje v36, uruchamiając testy Python/C++, kompilację
shaderów i fixture GPU. Sama kompilacja fixture nie jest pomiarem GPU w grze.
