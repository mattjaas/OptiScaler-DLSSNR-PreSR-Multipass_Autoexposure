# NR v14 — RGB, Linear20 i specjalizacja guide-one

Wszystkie trzy nowe opcje są domyślnie wyłączone. Nie zmieniono bazowego NR
ani v10/v11/v12/v13. Nowe PSO zawsze kompilują tylko Mode28, a cache
source/model pozostaje wyłączony. Opcje nie dodają dispatchy, barier,
tekstur roboczych ani alokacji per klatka; PSO są tworzone raz i zwalniane
z właścicielem. Nie ma jeszcze pomiaru przyspieszenia v14 w grze.

## Opcje i kwalifikacja

| Opcja INI `[DlssNr]` | Zmiana | Warunki |
|---|---|---|
| `InterPassV14RgbTile` | FP32 RGB zamiast RGBA w kaflu i akumulatorze Area; alfa nadal pobierana z oryginału | Linear + Weights, Compact16 lub jawnie włączone v14 Linear20 |
| `InterPassV14Linear20` | Izolowany shader Weights + Mode28 o pitch 20, bez cache i flag v9 podczas wykonania | Compact16 OFF lub niekwalifikujący się; Linear + Weights |
| `InterPassV14GuideOne` | Stała kwalifikacja guide strength = 1 w warunkach wyboru ścieżki | Dokładnie `ResidualConfidenceSensitivity == 1.0f`; przy innych wartościach opcja jest ignorowana |

Wszystkie wymagają zwykłej kwalifikacji tiled Area radius 1, skali
`>0.505` i `<=0.90` oraz zgodnych rzeczywistych wymiarów. Strided, cache,
v11, v12 i v13 Area wykluczają v14. v13 Mode28 może pozostać włączone,
ponieważ v14 już zawiera tę specjalizację. Awaria nowego PSO loguje ostrzeżenie
i pozostawia starszą ścieżkę. Udane utworzenie loguje indeks, pitch, RGB
i guide-one. Włączenie opcji samo w sobie nie dowodzi użycia PSO.

| Indeks PSO | Kafel | RGB | Guide-one | LDS potwierdzone w DXIL |
|---:|---:|---|---|---:|
| 0 | 16×16 | tak | nie | 3072 B |
| 1 | 16×16 | nie | tak | 4096 B |
| 2 | 16×16 | tak | tak | 3072 B |
| 3 | 20×20 | nie | nie | 6400 B |
| 4 | 20×20 | tak | nie | 4800 B |
| 5 | 20×20 | nie | tak | 6400 B |
| 6 | 20×20 | tak | tak | 4800 B |

## Zgodność arytmetyki

RGB zachowuje adresy, wagi, kolejność mnożenia/sumowania, sanitizację,
clamp, FP32 i końcowy odczyt alfy. Zapis docelowy zachowuje oryginalny format.
Linear20 nie rozszerza granic bezpieczeństwa Compact16.

Agresywna specjalizacja guide-one została **odrzucona**: nawet zachowanie
`precise (guided-bilinear)+bilinear` zamiast runtime lerp dało różnicę jednego
ULP na RTX 4090. Dostarczony wariant zachowuje pełny runtime lerp z odczytem
współczynnika i jego saturate. Definicja stałej jest umieszczona po helperze,
więc upraszcza tylko zewnętrzne warunki, w tym fallback. Bilinear i fallback
przy małej sumie wag pozostają. Nie deklarujemy zysku dla tego wariantu;
na zwykłej ścieżce tiled może być neutralny. Nie usuwamy mnożenia przez 1
kosztem innego zaokrąglenia.

`tests/nr_gpu/v14_parity.cpp` wykonuje rzeczywiste skompilowane shadery DX12
na syntetycznych teksturach: skale 55/59/60/61/65/75/90%, częściowe kafle,
małe i nieparzyste wymiary, signed HDR, ciemne wartości, NaN, arbitralna alfa,
underflow wag, shaping equal/unequal gains i shadow confidence. P25 jest
wyłącznie testem wymuszonego fallbacku, poza kwalifikacją CPU. Porównuje
16×16 z v13 Mode28; 20×20 z v9 Linear20 przy kwalifikujących się skalach,
a fallback P25 z odpowiadającą referencją v13/v10. NaN traktuje jako równoważne
niezależnie od payloadu, pozostałe wartości muszą być identyczne bitowo.
Sprawdzane są wyjścia RGBA32_FLOAT i RGBA16_FLOAT. To nie test historii NGX,
ruchu w grze ani pomiar wydajności.

Wynik lokalny na RTX 4090: **1920 porównań, 12 056 320 składowych RGBA — PASS**.
RGB i Linear20 sprawdzono przy guide strength 0,35 i 1; warianty guide-one
tylko przy dokładnie 1, zgodnie z ich kwalifikacją CPU. Wszystkie istniejące
`tests/test_nr_interpass_*.py` oraz nowy test v14 zaliczone. Wszystkie stare
i nowe shadery skompilowane lokalnie. Standardowy DXIL zachował SHA-256
`5599EE0B01FC50B9BEA4C2D02C8BFC1CE24287E9867DBC9FA0158DB883BAB9FC`;
Weights v10 i wszystkie v13 również zachowały wcześniejsze hashe.

| PSO | SHA-256 lokalnego DXIL |
|---|---|
| rgb16 | `E0B673DE1C16BE5C1100CCDBAC751F601BC8A2D275A70596C9101164F7CBBD3F` |
| guide16 | `F55DADBCF9B48E11D7E623843E28606F6BD0D37AE136D6CEB87084EED8F48C3A` |
| both16 | `73797FFCD504EE9692439741709B2DFB7D92E13AF702B786973ED6D874161CD0` |
| weights20 | `BC271790E50653362E17365CF386EABE4E175978926DF0FB91BE7092579D7003` |
| rgb20 | `9A99555DBED96A3FD3D7C449E992B9AE9D9C664EA1D10AFB2F3FC6C584FF0B9C` |
| guide20 | `ACFD45C4DC53600399EE0B4ADA30D3125EB844A98C1A168AC490B1EDB1BA94E2` |
| both20 | `36D51FEF697BD70ED21F413F44292FA13057FFB46B249BD60D876AD56EC8EACF` |

Lokalny WARP na tym komputerze kończy się błędem procesu `0xC0000374`;
nie jest raportowany jako zaliczona kontrola. Do kontroli shaderów użyto
rzeczywistej RTX 4090. Harness obsługuje WARP jako domyślny backend oraz
`--hardware` dla pierwszego sprzętowego adaptera. Testy CPU i pełny build
Windows pozostają częścią workflow. Test zgodności DXIL nie włącza opcji
ani nie modyfikuje konfiguracji uruchomionej gry.

## Benchmark

Menu: `NR v14: one sweep P50-P65, 63 cases (CSV)`.
Zachowano 28 konfiguracji v13, dodano RGB/guide-one/both w P65 i po osiem
konfiguracji w P55/P59/P60/P61. Są tu: off, automatyczny Compact16 weights
OFF/ON, legacy Linear20 weights ON i cztery warianty v14 Linear20.
Wszystkie mają sześć passów, osobną rozgrzewkę i jeden pomiar per konfigurację.
CSV zawiera nowe żądane flagi; różnica względem off ma bazę z tej samej skali.
Ustawienia użytkownika, w tym nowe opcje, są przywracane także po anulowaniu.
Guide strength pozostaje wartością użytkownika: aby zmierzyć specjalizację
guide-one, przed benchmarkiem ustawić dokładnie 1. Bez tego wynik opisuje
fallback do wariantu bez tej specjalizacji.

Do pomiaru zysku utrzymać stałą scenę i ustawienia, sprawdzić log PSO,
porównać medianę, średnią, P95 i zmienność. Jeden sweep jest użyteczny do
wstępnego wyboru; potwierdzenie drobnego zysku wymaga wielokrotnego A/B/ABBA.

## Zweryfikowana paczka Windows

[Build Release Windows](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/actions/runs/38047569410)
z commita `c88b3c565a272c06cab3abef0a02ef2047d5fcc4` zakończył się sukcesem.
Obejmował testy inter-pass, kompilację dawnych i siedmiu nowych shaderów,
kontrolę LDS/unikalności/hashów, kompilację harnessu DX12, pełny build i ZIP.
W CI harness został skompilowany; test na urządzeniu wykonano lokalnie na RTX 4090.

[Pobierz ZIP v14](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/releases/download/nr-interpass-v14-rgb-linear20-20261010/OptiScaler-NR-nr-interpass-v14-rgb-linear20-20261010.zip).
Pobrany asset ma 135 502 909 bajtów; jego SHA-256 zgadza się z digestem GitHub:
`db04278b1aece02fe05e87b8623dfb756308e096e10c8579e08f15594258e949`.
Sprawdzono CRC ZIP, wszystkie 62 wpisy SHA256SUMS, PE x64, etykietę benchmarku
v14 oraz obecność dokładnych 15 blobów standard/v10/v12/v13/v14 w DLL.
DLL SHA-256: `a7ea793a74da61f4de576f5c0e6a7feee1693b9406e804345760862d01805850`.
Skrypt pakowania pozostaje lokalnie w kwarantannie; nie przywracano go ani
nie włączano jego usunięcia do commita. Paczka pochodzi z czystego checkoutu CI.
