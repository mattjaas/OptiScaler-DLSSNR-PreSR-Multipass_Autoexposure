# NR v22: wynik porównania shaderów obecnych i v14 przy P65

Raport `NR-v22-p65-revision-20261011-005612` z gry The Last of Us Part I
nie wykazał powtarzalnej przewagi shaderów v14 wyjaśniającej historyczne
około 0,1–0,13 ms różnicy narzutu. Obecny Optimized uzyskał 37,245440 ms,
a rzeczywiste standard+RGB16 v14 w tym samym aktualnym torze 37,233152 ms.
Różnica to 0,012288 ms (0,033% czasu NR). Nie zmieniamy produkcyjnej selekcji
ani shaderów na podstawie tego pomiaru.

## Warunki i poprawność danych

Źródła użytkownika: `.csv`, `.samples.csv`, `.txt` o powyższym prefiksie,
`F:/Gry/The Last of Us - Part I/OptiScaler-NR-Benchmarks/`.
GPU NVIDIA RTX 4090, sterownik DXGI 32.0.16.1656, DX12,
3840×2160 → 2496×1404 (65%), sześć passów, Area, radius 1, guide 1.
Scena zapisana jako `unspecified`; zegarów i temperatur nie kontrolowano.
User transfer=9, range sigma=0,015, spatial sigma=3, shaping=2,
compound=1, HDR transfer=0, shadow gate=0. Wszystkie sześć passów ma
shaping=1, efektywne inter-pass high=0,853497565 / low=0,829333186.
Szczegóły profilu: [NR-v22-P65-revision-benchmark.md](NR-v22-P65-revision-benchmark.md).

- 7 okien po 160 próbek; 90 próbek niezależnej rozgrzewki na okno.
- Wszystkie 1120 surowych czasów są skończone i dodatnie; indeksy 0..159
  każdego okna są kompletne, bez duplikatów.
- Kolejność Off / Classic reference / Fused reference / current / v14 / v14 / current.
- Faktyczne ścieżki są zgodne z profilami, bez mieszania ani fallbacku:
  `Fused RGB16` dla obecnego i `Fused RGB16 (v14 shaders)` dla archiwalnego.
- Geometria i liczba passów są zgodne w pięciu konfiguracjach.
- Mediany, nearest-rank P95, średnie, min/max i populacyjne SD odtworzono
  z raw; największy błąd względem CSV to 0,0000044 ms (zaokrąglenie raportu).
- Nie usuwano outlierów ani innych próbek.

## Cały przedział GPU NR

Pomiary obejmują NGX, przejścia zasobów i zależności; nie są izolowanym
czasem inter-pass ani czasem całej klatki gry.

| Wariant | Próbki | Mediana ms | Narzut wobec Off ms | P95 ms | SD ms |
|---|---:|---:|---:|---:|---:|
| Off | 160 | 35,765760 | 0 | 35,969024 | 0,131781 |
| Classic reference | 160 | 38,775296 | 3,009536 | 38,989824 | 0,137440 |
| Fused reference | 160 | 41,764352 | 5,998592 | 42,033152 | 0,145893 |
| Obecny Optimized | 320 | 37,245440 | 1,479680 | 37,533696 | 0,182659 |
| Optimized z shaderami v14 | 320 | 37,233152 | 1,467392 | 37,527552 | 0,160017 |

Obecny Optimized oszczędza 1,529856 ms względem Classic reference i
4,518912 ms względem Fused reference. Wybór RGB16 przy P65 działa poprawnie.
Są to porównania z referencjami bez optymalizacji; ten profil nie mierzy
wszystkich zachowanych zoptymalizowanych torów ani innych skal.

## Okna ABBA

| Okno | Wariant | Mediana ms | Średnia ms |
|---|---|---:|---:|
| 3 / A1 | obecny | 37,236736 | 37,254259 |
| 4 / B1 | v14 | 37,238272 | 37,235514 |
| 5 / B2 | v14 | 37,232128 | 37,259898 |
| 6 / A2 | obecny | 37,256192 | 37,255642 |

Pierwsza sąsiednia para daje v14 zysk −0,001536 ms (obecny nieco szybszy),
druga +0,024064 ms. Średnia median okien daje +0,011264 ms dla v14,
połączona mediana próbek +0,012288 ms, a połączona średnia +0,0072448 ms.
P95 poprawia się o 0,006144 ms. Mediany dwóch okien obecnego różnią się
o 0,019456 ms, v14 o 0,006144 ms.

Zmiana znaku między parami i tylko dwa okna na rewizję nie uzasadniają
twierdzenia o powtarzalnym zwycięstwie v14. SD pojedynczych próbek nie jest
testem istotności ani granicą błędu median; nie traktujemy 320 kolejnych
klatek jako 320 niezależnych powtórzeń doświadczenia. Nie dowiedziono
ścisłej równoważności ani braku różnicy dla każdej sceny i ustawień.

## Co to mówi o historycznych 37,02 ms

Historyczny v14 miał Off=35,649536 ms i RGB16=37,020160 ms, czyli narzut
1,370624 ms. W tej sesji rzeczywiste shadery v14 dają narzut 1,467392 ms:
nadal +0,096768 ms wobec historycznej sesji po odjęciu Off. Obecny narzut
1,479680 ms jest wyższy od historycznego o 0,109056 ms.
Nowy Off=35,765760 ms jest niemal identyczny z v21=35,763712 ms.
Wynik obecnego Optimized jest też o 0,017920 ms niższy od v21=37,263360 ms;
nie przypisujemy tego poprawce, ponieważ produkcyjne shadery nie zmieniły się.

Archiwalne shadery w tej sesji nie odtwarzają dawnego wyniku. Ich mała
obserwowana różnica nie wyjaśnia historycznego narzutu. Nadal nie ustalono,
czy źródłem pozostałej różnicy są niezapisane historyczne ustawienia/scena,
prywatna biblioteka modelu, stan temporalny, warunki GPU czy inne zachowanie
starego wykonania. Podobny czas Off nie dowodzi identycznego kosztu kolejnych
NGX passów po inter-pass. B zmienia tylko standard+RGB16, a nie całe DLL v14.

Nie ma podstaw do cofania shaderów, kolejnego testu samego RGB16 ani
przełączania P65 na inny tor. Gdy dalsze dochodzenie jest potrzebne, nowa
hipoteza powinna dotyczyć całego v14 DLL kontra obecne DLL przy jawnie
uzgodnionym configu, modelu i scenie, z zapisanym metadata i powtórzeniami
ABBA. Nie byłby to automatycznie replay historycznej sesji, której pełny
config nie został zapisany.

## Identyfikacja źródeł

SHA256 niezmienionych plików użytkownika:

- `.csv`: `0af9b63fe14a71dc8b6d7974f268ad3a1e546c5f072c6b45bd4470b455e2c788`.
- `.samples.csv`: `3f2e3df0f74f0731250761123d73c1e9939e23871bb6e6d8075239101a958ac9`.
- `.txt`: `3e3f6e442551c3ac6f42bd39efc6d83cb9a01fda996c75be2eec72be5a4c4a23`.

Lokalny niezależny reader: `artifacts/nr-v22/analyze_game.py`, wyniki
`artifacts/nr-v22/game_analysis.json`. Pozostają poza publikowaną paczką.
