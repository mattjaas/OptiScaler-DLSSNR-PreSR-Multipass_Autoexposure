# NR v27: wynik poniżej 50%, 11 października 2026, raport 130323

**Fast z Low36 skraca czas całego NR w każdym z pięciu zmierzonych punktów:**
o 0,465920–1,223424 ms względem dokładnego Optimized. Obie strony ABBA
potwierdzają przyspieszenie. Największe zyski P33/P41 występują tam,
gdzie dokładny tor używa Classic optimized; P40/P42/P49 używają RGB20.
Cross5 dodaje 0,001792–0,022528 ms oszczędności względem Fast.
Wszystkie pięć średnich i wszystkie dziesięć sąsiednich różnic wskazują
przyspieszenie Cross5, choć jego zysk jest znacznie mniejszy od zysku Fast.

Użytkownik na pytanie o Fast i Fast + Cross5 względem Optimized poniżej 50%,
także w ruchu, odpowiedział **„Nie zauważyłem pogorszenia”**. Dla sprawdzonej
sceny oba warianty mają więc akceptowaną jakość. Nie otrzymano klatek do
niezależnej analizy; ocena nie gwarantuje identyczności ani jakości innych scen.

W tej konfiguracji można używać Fast + Cross5 również poniżej P50.
P41 daje praktycznie remis; P49 ma zgodny kierunek, lecz dużą różnicę
wielkości zysku między stronami ABBA. To nie jest powód do odrzucenia
wariantu, ale ogranicza pewność oszacowania jego dodatkowego zysku.
Analiza zachowuje obie opcje, istniejący wyjątek P50 i dokładną politykę
Optimized. Nie zmienia kodu ani domyślnego Off. P25/P45 i ułamkowe skale
nie były mierzone w tym przebiegu; nie wskazujemy ich jako potwierdzonych zwycięzców.

## Warunki

- The Last of Us Part I według folderu źródłowego; scena/kamera `unspecified`.
- RTX 4090, DXGI 32.0.16.1656 (NVIDIA 616.56), native 3840×2160.
- Working P33 1267×713, P40 1536×864, P41 1574×886, P42 1613×907, P49 1882×1058.
- Temporal DLAA residual + P100-guided, Transfer 9, sześć efektywnych passów.
- Area, radius 1, guide 1, range sigma 0,015, spatial sigma 3,0; shaping 2, shadow gate 0.
- Raport nie określa stanu HDR ani dokładnych gainów i stylów wszystkich passów.
- 40 konfiguracji / 80 okien / 12 800 próbek / 15 bloków ABBA.
- Każde okno: osobne 90 klatek rozgrzewki i 160 próbek; manual hold 0.
- Mierzony cały przedział NR na GPU obejmuje NGX i zależności; to nie czas całej klatki.
- Zegary i temperatura niekontrolowane. Każda para ma dwa okna kontroli i dwa wariantu;
  320 kolejnych klatek nie oznacza 320 niezależnych powtórzeń eksperymentu.

## Lokalny wynik ABBA

Zmiana = `(mediana B1 + mediana B2 − mediana A1 − mediana A2)/2`.
**Minus oznacza przyspieszenie.** Procent odnosi się do średniej median
dwóch okien kontroli. Kolumny `gain` w CSV mają przeciwny znak.
Poniższe czasy A/B to średnie median okien tego samego lokalnego bloku.

| Skala | A / B | Czas A / B ms | Zmiana ms | Zmiana % | Pierwsza / ostatnia zmiana ms |
|---|---|---:|---:|---:|---:|
| P33 | Optimized / Fast | 19,045376 / 17,903360 | -1,142016 | -5,9963% | -1,143296 / -1,140736 |
| P33 | Fast / Fast + Cross5 | 17,903616 / 17,896704 | -0,006912 | -0,0386% | -0,003584 / -0,010240 |
| P40 | Optimized / Fast | 20,553984 / 20,088064 | -0,465920 | -2,2668% | -0,471552 / -0,460288 |
| P40 | Fast / Fast + Cross5 | 20,081920 / 20,067072 | -0,014848 | -0,0739% | -0,010752 / -0,018944 |
| P41 | Optimized / Fast | 21,998080 / 20,774656 | -1,223424 | -5,5615% | -1,235968 / -1,210880 |
| P41 | Fast / Fast + Cross5 | 20,773120 / 20,771328 | -0,001792 | -0,0086% | -0,000512 / -0,003072 |
| P42 | Optimized / Fast | 22,168064 / 21,470976 | -0,697088 | -3,1446% | -0,696832 / -0,697344 |
| P42 | Fast / Fast + Cross5 | 21,444352 / 21,432576 | -0,011776 | -0,0549% | -0,011264 / -0,012288 |
| P49 | Optimized / Fast | 26,968832 / 26,261504 | -0,707328 | -2,6228% | -0,713728 / -0,700928 |
| P49 | Fast / Fast + Cross5 | 26,256128 / 26,233600 | -0,022528 | -0,0858% | -0,004096 / -0,040960 |

Cross5 P33/P40/P42 ma dodatni zysk po obu stronach; P42 jest szczególnie
zbliżony: 0,011264 / 0,012288 ms. P41 daje zaledwie 0,000512 / 0,003072 ms.
P49 daje 0,004096 / 0,040960 ms, przy zmianie median kontroli o 0,030208 ms
i wariantu o 0,067072 ms. Średni zysk 0,022528 ms jest poprawnym wynikiem
tego ABBA, ale nie świadczy o równie dużym zysku w każdej chwili.
P95 Cross5 jest minimalnie wyższy przy P41 (+0,004096 ms) i P49 (+0,009216 ms);
przy pozostałych trzech skalach jest niższy. Nie przypisujemy Cross5 poprawy
każdego wskaźnika ani statystycznej istotności na podstawie jednego bloku.

Nie mnożymy zysków przez sześć passów: podane czasy już obejmują cały NR.
Nie sumujemy dwóch oddzielnych bloków jako nowego bezpośredniego pomiaru
Cross5 względem Optimized. Wynik komponentowego fixture v27 dotyczył
innych danych i zakresu pracy; nie zastępuje tego pomiaru z gry.

## Kontrola dokładnej ścieżki

Automat wybiera tę samą ścieżkę co niezależna wymuszona kontrola w każdej
skali, bez fallbacków. Różnice ABBA −0,003584…+0,007168 ms są niewielkie
w porównaniu z zyskiem Fast. Alternatywny Classic/RGB20 pozostaje kontrolą
z pojedynczego wcześniejszego okna; jego mediany nie są symetrycznym ABBA.
W tym przebiegu również są wyższe od median wybranych wymuszonych kontroli.
Raportowanie RGB20 opisuje PSO; nie dowodzi użycia LDS w każdej grupie.

| Skala | Wymuszona ścieżka | Wymuszona / automat: mediana puli ms | Zmiana ABBA ms | Alternatywa / mediana ms |
|---|---|---:|---:|---|
| P33 | Classic optimized | 19,042304 / 19,045888 | +0,004352 | Fused RGB20 / 19,431420 |
| P40 | Fused RGB20 | 20,543488 / 20,547584 | +0,007168 | Classic optimized / 21,198850 |
| P41 | Classic optimized | 21,995008 / 21,996544 | -0,000000 | Fused RGB20 / 22,411260 |
| P42 | Fused RGB20 | 22,167040 / 22,157824 | -0,001280 | Classic optimized / 22,679550 |
| P49 | Fused RGB20 | 26,959360 / 26,956288 | -0,003584 | Classic optimized / 27,504130 |

## Rozrzut i ogony rozkładu

Pula obejmuje wyłącznie 320 próbek A i 320 B z danego bloku. SD jest
populacyjne, P95 nearest rank. Mediana puli może różnić się od średniej
median dwóch okien. Globalne summary pulujące cztery okna Fast/Optimized
nie zastępuje kontroli lokalnego bloku. Nie wyznaczamy przedziałów ufności
z założeniem niezależności kolejnych klatek.

| Skala / porównanie | Mediana A / B ms | SD A / B ms | P95 A / B ms | A1 / B1 / B2 / A2 ms |
|---|---:|---:|---:|---|
| P33 forced / auto | 19,042304 / 19,045888 | 0,044386 / 0,045436 | 19,132416 / 19,128320 | 19,035136 / 19,040256 / 19,052544 / 19,048960 |
| P33 Optimized / Fast | 19,044864 / 17,903616 | 0,044774 / 0,023090 | 19,133440 / 17,947648 | 19,047936 / 17,904640 / 17,902080 / 19,042816 |
| P33 Fast / Cross5 | 17,902592 / 17,896448 | 0,020517 / 0,020710 | 17,935360 / 17,934336 | 17,901568 / 17,897984 / 17,895424 / 17,905664 |
| P40 forced / auto | 20,543488 / 20,547584 | 0,085560 / 0,082666 | 20,745216 / 20,714496 | 20,540928 / 20,546048 / 20,554752 / 20,545536 |
| P40 Optimized / Fast | 20,554240 / 20,088320 | 0,084246 / 0,072644 | 20,732928 / 20,198400 | 20,553216 / 20,081664 / 20,094464 / 20,554752 |
| P40 Fast / Cross5 | 20,082688 / 20,066816 | 0,078117 / 0,076950 | 20,222976 / 20,206592 | 20,079104 / 20,068352 / 20,065792 / 20,084736 |
| P41 forced / auto | 21,995008 / 21,996544 | 0,109877 / 0,108101 | 22,237184 / 22,216704 | 21,998592 / 21,996544 / 21,994496 / 21,992448 |
| P41 Optimized / Fast | 21,998592 / 20,777472 | 0,116594 / 0,089306 | 22,232064 / 20,974592 | 22,005760 / 20,769792 / 20,779520 / 21,990400 |
| P41 Fast / Cross5 | 20,774400 / 20,770816 | 0,090428 / 0,096292 | 20,976640 / 20,980736 | 20,769792 / 20,769280 / 20,773376 / 20,776448 |
| P42 forced / auto | 22,167040 / 22,157824 | 0,148605 / 0,139839 | 22,568960 / 22,462464 | 22,148608 / 22,150144 / 22,169600 / 22,173696 |
| P42 Optimized / Fast | 22,168064 / 21,471744 | 0,142985 / 0,112276 | 22,499328 / 21,703680 | 22,163456 / 21,466624 / 21,475328 / 22,172672 |
| P42 Fast / Cross5 | 21,448192 / 21,431296 | 0,111030 / 0,117736 | 21,684224 / 21,653504 | 21,449216 / 21,437952 / 21,427200 / 21,439488 |
| P49 forced / auto | 26,959360 / 26,956288 | 0,203340 / 0,198557 | 27,301888 / 27,288576 | 26,960384 / 26,969088 / 26,938368 / 26,954240 |
| P49 Optimized / Fast | 26,968576 / 26,258944 | 0,205351 / 0,186889 | 27,305984 / 26,566656 | 26,970112 / 26,256384 / 26,266624 / 26,967552 |
| P49 Fast / Cross5 | 26,256384 / 26,236928 | 0,188091 / 0,179536 | 26,544128 / 26,553344 | 26,271232 / 26,267136 / 26,200064 / 26,241024 |

## Uzgodnienie i archiwum

Odtworzono wszystkie 40 wierszy summary i 15 par z 12 800 próbek:
kolejność 80 okien, indeksy próbek, liczebności, geometrie, tryby, wymuszenia
i ścieżki, mean/median/P95/min/max/SD, mediany okien, rozrzuty, wszystkie
delty/gains i wartości historyczne. Maksymalny błąd summary 0,0000049 ms
mieści się w zaokrągleniu do pięciu miejsc; pairs poniżej 0,000000000000006 ms.
Wartości historyczne są metadanymi wcześniejszych przebiegów, nie kontrolą
bieżącego porównania. Nazwa ścieżki nie dowodzi użycia Low36 w każdym passie.

Cztery kopie w `docs/` są identyczne bajtowo ze źródłami:

| Plik | SHA256 |
|---|---|
| [NR-v27-fast-low-20261011-130323.csv](NR-v27-fast-low-20261011-130323.csv) | `1363cec8a9a49d7b4b6991b4af7689885c0b4517a46a3a151141960bd40cac75` |
| [NR-v27-fast-low-20261011-130323.pairs.csv](NR-v27-fast-low-20261011-130323.pairs.csv) | `7074eb8628a37c0fc602435e0b62aee5cb547a0d3021aa051f763a4ce8ec3080` |
| [NR-v27-fast-low-20261011-130323.samples.csv](NR-v27-fast-low-20261011-130323.samples.csv) | `336e94325438dbaf15efcb316750a4f216e7de83614b74418a4559e8c689e267` |
| [NR-v27-fast-low-20261011-130323.txt](NR-v27-fast-low-20261011-130323.txt) | `30d67c76942a58dce1183c8c88da5d3f694ed9a0ef30929742d8a652fea30106` |
