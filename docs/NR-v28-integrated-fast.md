# NR v28: Cross5 w podstawowym Fast, z wyjątkiem P50

**Inter-pass fast (INI 4) zawiera teraz Low36 i Cross5. Przy P50 zachowuje
Low36 i pełne dziewięć sąsiadów, bez Cross5.** Osobna opcja Fast + Cross5
znika z menu. Zapisane wartości 5/6/7 migrują do 4; sama polityka też
traktuje je jako aliasy Fast, więc wcześniejszy tryb 7 zachowuje swój tor.
Off i oba tryby referencyjne pozostają dostępne.

To integracja istniejących shaderów i zatwierdzonych wyników, bez zmiany
HLSL ani bytecode. Aktualne working/native musi wynosić 25–90% na obu
osiach, native musi być większe od working, filtr Area, radius 1 i guide >0.
Pozostałe ustawienia zachowują dokładny fallback Optimized v20.
Nie przybywa dispatchy, tekstur, barier ani alokacji na klatkę. Lazy PSO
Cross5 i Low36 pozostają bez LDS; Low36 jest pomijany, gdy shaping nie
potrzebuje low field. Cała zmiana dotyczy inter-pass. Zwykły/końcowy
filtr Mode18 8×8, final resolve i temporal DLAA zachowują swój tor.

## Dlaczego Cross5 wyłączamy przy P50

Dokładny Optimized ma specjalizację idealnego native/working = 2 na obu
osiach: `Path::FusedOptimized`, Mode28, z czterema próbkami native na
working i współdzieleniem ich stencilów. Ta specjalizacja redukuje koszt
dokładnej integracji guidingu z Area. Fast ją jednak omija: Mode33 lub
Mode36 liczy korektę raz na piksel working, korzystając z próbki native
w centrum. Fast nie przechodzi przez P50 gałąź Mode28.

Nie można więc stwierdzić, że wcześniejsza dokładna specjalizacja P50
jest bezpośrednią przyczyną remisu Cross5 przeciw Fast. Cross5 usuwa
cztery rogi 3×3 i ich odczyty/wagi; pozostawia resztę guidingu, shaping,
Low36, model NR i końcowe przetwarzanie. Przy P50 udział usuwanej pracy
w całym NR może być niewielki. Cache, rozkład kosztów i przepustowość są
możliwymi czynnikami, ale nie wykonaliśmy profilowania potwierdzającego
konkretną przyczynę. Nie jest to twierdzenie, że Cross5 nigdy nie może
wygrać przy P50 w innej scenie/GPU.

[Pomiar v26](NR-v26-20261011-ingame-analysis.md) dał P50 zmianę
Cross5 względem Fast +0,002304 ms, z sąsiednimi −0,006144 / +0,010752 ms:
praktyczny remis. Zachowujemy zatwierdzony wyjątek przy P50. Rozpoznanie
opiera się na rzeczywistej geometrii obu osi, również przy zaokrągleniu
nieparzystego native w górę o pół texela working, np. 65×37 → 33×19.
Nie wyłączamy Cross5 we wszystkich skalach poniżej P50.

## Podstawa decyzji: pomiary z gry

RTX 4090, NVIDIA 616.56, native 3840×2160, The Last of Us Part I według
folderu źródłowego; scena `unspecified`. Transfer 9, Temporal DLAA residual
+ P100-guided, sześć passów, Area/radius 1/guide 1, range 0,015, spatial 3,
shaping 2, shadow gate 0. Każde okno: 90 klatek rozgrzewki i 160 timestampów
GPU. Zmiana ABBA to średnia dwóch median wariantu minus średnia dwóch
median kontroli. **Minus oznacza szybszy cały NR, nie sam shader ani całą
klatkę gry.** Zegarów/temperatury nie kontrolowano.

| Skala | Cross5 przeciw Fast bez Cross5: zmiana ABBA ms | Pierwsza / ostatnia zmiana ms | Przebieg |
|---|---:|---:|---|
| P33 | −0,006912 | −0,003584 / −0,010240 | v27 |
| P40 | −0,014848 | −0,010752 / −0,018944 | v27 |
| P41 | −0,001792 | −0,000512 / −0,003072 | v27 |
| P42 | −0,011776 | −0,011264 / −0,012288 | v27 |
| P49 | −0,022528 | −0,004096 / −0,040960 | v27 |
| P50 | +0,002304 | −0,006144 / +0,010752 | v26 |
| P59 | −0,049152 | −0,069120 / −0,029184 | v26 |
| P65 | −0,111360 | −0,107008 / −0,115712 | v26 |

Pełne czasy przed/po, SD, P95, mediany okien i surowe próbki:
[v26](NR-v26-20261011-ingame-analysis.md),
[v27](NR-v27-20261011-ingame-analysis.md). Oba przebiegi mają po jednym
bloku ABBA na dane porównanie: kolejne klatki nie są niezależnymi
powtórzeniami eksperymentu. P41 to praktycznie remis; P49 ma zmienną
wielkość zysku, choć oba porównania wskazują przyspieszenie. Użytkownik
nie zauważył pogorszenia jakości v26/v27 po pytaniu obejmującym ruch.
Nie otrzymano klatek do niezależnej oceny. Nie uznajemy niezmierzonych
skal i innych scen za potwierdzonych zwycięzców.

Integracja nie daje nowego zysku ponad dawny Fast + Cross5: wybranie
podstawowego Fast uruchamia teraz ten sam zatwierdzony tor. Nie mnożymy
podanych czasów przez sześć passów i nie sumujemy oddzielnych bloków
jako bezpośredniego pomiaru Cross5 przeciw Optimized.

## Benchmark i ocena obrazu

Podstawowy benchmark **Compare reference / Optimized / Fast at 50% / 59% / 65% (CSV)**
porównuje nowy Fast z dokładnym Optimized w ABBA, z Off i oboma trybami
referencyjnymi: 15 konfiguracji / 21 okien.

Profil **Compare integrated Fast / Fast without Cross5 at 50% / 59% / 65% (CSV)**
zachowuje dodatkową kontrolę poprzedniego Fast bez Cross5 w ABBA przy
P59/P65: 17 konfiguracji / 29 okien. P50 nie powtarza identycznej pary.

Profil **Compare Optimized / integrated Fast / Fast without Cross5 below 50% (CSV)**
zachowuje P33/P40/P41/P42/P49 i bieżącą niższą skalę, gdy jest inna,
Off, oba referencyjne, alternatywny Classic/RGB20 i wymuszonego dokładnego
zwycięzcę przeciw automatowi w ABBA. Następnie porównuje Optimized / Fast
oraz Fast bez Cross5 / nowy Fast. To 40 konfiguracji / 80 okien lub 48 / 96
z dodatkową skalą, z osobną rozgrzewką. P45 i odrzucony Generic Fused
optimized nie wracają do tego profilu.

Kontrola **Fast without Cross5 control** używa INI 4 i wymuszonego
`Path::FastGuided` (7) wyłącznie podczas benchmarku. Wymuszenie może
wyłączyć Cross5 tylko wtedy, gdy automat już wybrał kwalifikujący się
Fast; nie omija ograniczeń geometrii/ustawień ani dokładnego fallbacku.
Nie jest osobną opcją produkcyjną. `forced_path` odróżnia ją od automatu.
Kolumny `gain/delta_vs_fast_median_ms` odnoszą się do nowego automatycznego
Fast, a `.pairs.csv` do sąsiedniej kontroli z konkretnego bloku.

Nowe prefiksy to `NR-v28-fast-`, `NR-v28-fast-baseline-` i `NR-v28-fast-low-`.
Opcjonalne **Hold each ... benchmark window for visual inspection** pozwala
obejrzeć rozgrzany wynik i przejść ręcznie dalej. Klatki oglądania są
wyłączone z pomiaru. Ruch sprawdzamy osobno od statycznych pomiarów.
Ustawienia i override wracają po zakończeniu/anulowaniu. Regularny,
Below50 i Boundary dla dokładnych torów zachowują swoje zestawy.

## Weryfikacja

Lokalnie przeszły wszystkie sześć istniejących testów arytmetyki inter-pass
i test benchmarku. Nowo skompilowany test produkcyjnej polityki sprawdza
referencje, dokładne zwycięskie tory, wszystkie nominalne skale P25–P100,
filtry/radii/guide, aliasy dawnych trybów, wyjątek P50 z nieparzystymi
wymiarami i 37 622 osie kafli (21 z chronionym overflow).

Na RTX 4090 przeszło 648 kontroli istniejących shaderów: Low36 + Fast,
Cross5 w izolowanym PSO i fallbacku, signed DC, krawędzie, FP16/FP32,
HDR/nonfinite, alfa i niezmieniony zwykły Mode18. Porównanie używa
zatwierdzonej bazy v25 z `artifacts/nr-v26/baseline`, nie wcześniejszej
bazy v21 znajdującej się w `artifacts/nr-v25/baseline`.
To kontrola kontraktu, nie nowa ocena percepcyjna ani pomiar wydajności.

[Build 38135544209](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/actions/runs/38135544209)
zakończył się sukcesem: testy Python/C++, kompilacja sześciu shaderów,
fixture GPU, pełna DLL, ZIP i publikacja release. Źródłowy commit:
`18ca84113fd9dbdfb4b3d12412108e2a6cd6e543`.

[Release v28](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/releases/tag/nr-interpass-v28-integrated-fast-20261011)
i [ZIP](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/releases/download/nr-interpass-v28-integrated-fast-20261011/OptiScaler-NR-nr-interpass-v28-integrated-fast-20261011.zip).
SHA256: `33ad822f5cb21279994d52f125ee2aa25bc173d31dcc7f541ae56f808e7c5a13`.
Sprawdzono CRC, wszystkie 62 wpisy SHA256SUMS, commit zaszyty w DLL,
menu benchmarków, brak osobnej opcji Cross5, migrację INI i opis wyjątku
P50. Wszystkie sześć blobów CSO z v27 występuje bajtowo w nowej DLL;
odrzuconego PSO Weights nie ma. Dokument jest dostępny w repozytorium;
istniejący skrypt pakowania nie dołącza go do ZIP. Paczka nie została
automatycznie podmieniona w grze.
