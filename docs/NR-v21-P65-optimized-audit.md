# Audyt Optimized przy 65% — 2026-10-11

Sprawdzono źródło `3de18608d09dee551542a50d24c7a8581f69f584`, zgodne z
upstream `codex/nr-interpass-v13-bounded-area`. Zgłoszenie dotyczyło około
37,3 ms dla Optimized wobec zapamiętanego wyniku około 37,0 ms i Off około
35,8 ms. Nie znaleziono błędnego wyboru ścieżki ani dowodu regresji najnowszego
shadera. Historyczna różnica po uwzględnieniu Off pozostaje niewyjaśniona;
nie można jej automatycznie uznać za szum ani za potwierdzoną regresję kodu.

## Pomiary całego NR w grze

Źródła: `NR-v14-20261010-134037`, `NR-v16-20261010-164041` oraz
`NR-v21-fast-20261010-235743`, każdy raport `.csv`, `.samples.csv` i `.txt`
z folderu `OptiScaler-NR-Benchmarks` w The Last of Us Part I.
RTX 4090, sterownik DXGI 32.0.16.1656, native 3840×2160, working 2496×1404,
6 passów, Area, radius 1, guide strength 1; 90 próbek rozgrzewki na okno.
To cały przedział GPU NR, z NGX, przejściami zasobów i zależnościami,
a nie suma izolowanych kosztów shaderów.

| Raport / wariant | Off mediana ms | RGB16 mediana ms | RGB16 minus Off ms | RGB16 P95 ms | RGB16 SD ms | Próbki Off / RGB16 |
|---|---:|---:|---:|---:|---:|---:|
| v14 / Linear16 Mode28 + RGB v14 | 35,649536 | 37,020160 | 1,370624 | 37,307392 | 0,184768 | 160 / 160 |
| v16 / Linear16 Mode28 + RGB v14 | 35,725824 | 37,221888 | 1,496064 | 37,467136 | 0,161944 | 160 / 160 |
| v21 / Inter-pass optimized, Fused RGB16 | 35,763712 | 37,263360 | 1,499648 | 37,609472 | 0,179988 | 160 / 320 |

Odtworzono medianę, nearest-rank P95 i populacyjne SD z surowych próbek.
Liczby próbek i indeksy wszystkich sześciu porównywanych serii są poprawne,
bez luk i duplikatów. Różnica metryk wobec podsumowań nie przekracza 0,000004 ms.

Od v14 do v21 RGB16 wzrósł o 0,243200 ms, a Off o 0,114176 ms.
Pozostaje 0,129024 ms różnicy narzutu względem Off. Od v16 do v21
RGB16 wzrósł o 0,041472 ms, Off o 0,037888 ms, a narzut tylko o 0,003584 ms.
Wynik około 37,2 ms występował więc już przed automatyczną selekcją v17.

v14 i v16 mierzyły każdą konfigurację raz, sekwencyjnie. v21 mierzył
Optimized / Fast / Fast / Optimized; mediany dwóch okien Optimized przy P65
to 37,252608 i 37,273088 ms. Fast jest przybliżeniem i nie stanowi kontroli
starego dokładnego RGB16. Zegarów i temperatur nie kontrolowano. Scena v21
jest zapisana jako `unspecified`; v14/v16 nie rejestrowały transferu i pełnych
ustawień shaping. Podobny Off nie dowodzi identycznego kosztu NGX po zmianie
wejścia kolejnych passów. SD pojedynczych próbek nie jest granicą istotności
różnicy median między sesjami.

## Faktycznie wykonywana ścieżka

- `DlssNr_InterPassPolicy.h`: mode 3, Area, radius 1, aktywne prowadzenie,
  working 2496×1404 przy 3840×2160 wybiera `Rgb16`.
- `DlssNr_Dx12_Run.cpp`: zachowany fused Mode28, wspólne wagi, dynamiczna
  geometria, flagi kafla RGB i Compact16. Przy sześciu passach jest pięć
  rekonstrukcji pomiędzy passami. Opcjonalny istniejący Mode18 pozostaje
  potrzebny przy aktywnym shaping z różnymi low/high gains.
- `DlssNr_Dx12.cpp`: osobny RGB16 PSO jest tworzony leniwie i ponownie używany.
  Raport v21 potwierdza `Fused RGB16`, bez fallbacku PSO/geometrii.
- HLSL: grupy 8×8, kafel 16×16 RGB FP32, LDS 3072 B. Maksymalny native
  footprint przy P65 wynosi 14 texeli na każdej osi: 312 grup w poziomie,
  176 w pionie, w tym niepełna dolna grupa. Wszystkie mieszczą się w kaflu;
  kontrola na arytmetyce FP32 nie wykazała przejścia na bezpośredni fallback.
- Nie dodano dispatchu, kopii, bariery ani alokacji GPU do dokładnej ścieżki
  P65 w v21. Fast wybierany jest osobno przez mode 4; warunek nie obejmuje mode 3.

Zainstalowany `dxgi.dll` jest identyczny SHA256 z DLL paczki v21:
`18e1994fad61929c5844b03efca6f9f608ac474bae0acbae40e01d0e981b5e13`.
Zawiera dokładnie jeden egzemplarz sprawdzonego aktualnego RGB16 DXIL.

| Blob RGB16 | SHA256 |
|---|---|
| v14 i v16, 47580 B | `e0b673de1c16be5c1100ccdbac751f601bc8a2d275a70596c9101164f7cbbd3f` |
| v17, v18/v20 i v21, 44040 B | `0cca2aafeef242cf007c80e31135ae631d3a2bc7ac07049853be4a35d3432f71` |

Sprawdzono też stary blob bezpośrednio w DLL pobranej paczki v14.
v14 → v16 zachowało ten sam shader i stałe kontrolnego wariantu RGB16;
nowe flagi wide/pair/low były w tej konfiguracji wyłączone.
v21 → v20 nie zmienia binarnego RGB16 ani polityki mode 3. Standardowy shader
v21 zmienił się przez dodanie Mode33; nie oznacza to identyczności wszystkich
shaderów całego toru z v20.

## Kontrola na GPU

Polityka C++: PASS, w tym 37622 osi kafli. Fixture
`tests/nr_gpu/interpass_parity.cpp`, z aktualnym standardowym shaderem v21
oraz RGB16/RGB20 i rzeczywistą bazą v14: PASS, 6528 porównań,
53 543 552 komponentów RGBA. FP16/FP32, HDR, shaping, cienie,
nieparzyste wymiary i brzegi; równość bitowa z wyjątkiem payloadu NaN.
To kontrola shaderów, nie temporalnego łańcucha NGX.

Dodatkowo wykonano celowane porównanie starego RGB16 v14 z aktualnym RGB16,
wykorzystując `tests/nr_gpu/interpass_timing_fixture.h` / `LowFixture`.
RTX 4090, synthetic static 4K, P65, FP32 wejścia / FP16 wyjście, Area,
radius 1, guide 1, range sigma 0,015, spatial sigma 1,2, shaping i shadow gate
wyłączone. Te ustawienia nie odtwarzają wszystkich ustawień sceny użytkownika.
Tekstury, PSO i stałe pozostają rezydujące; alokacje, uploady i odczyt wyników
są poza przedziałem GPU. Trzy bloki ABBA, 90 rozgrzewki i 160 pomiarów na okno;
jeden timestamp obejmuje osiem dispatchy z barierami UAV, wynik dzielony przez osiem.

| Wariant | Próbki | Mediana ms | P95 ms | SD ms |
|---|---:|---:|---:|---:|
| RGB16 v14 | 960 | 0,404672 | 0,490752 | 0,036398 |
| Aktualny RGB16 | 960 | 0,404288 | 0,500992 | 0,035849 |

Różnice median aktualny minus v14 w kolejnych blokach:
+0,014016 / −0,010624 / −0,000256 ms. Nie widać powtarzalnego pogorszenia
czasu aktualnego shadera. Nie należy mnożyć tej różnicy przez pięć i przedstawiać
jej jako pomiaru całego NR. Wyniki nie wykluczają różnicy zależnej od shaping,
formatów wejścia albo pracy równoległej w grze.

Surowe 1920 próbek: [NR-v21-P65-revision-GPU.samples.csv](NR-v21-P65-revision-GPU.samples.csv).
Lokalny runner i pełne wyniki audytu zachowano w `artifacts/nr-p65-audit/`.
Runner tworzy oba PSO przed pomiarem, instancję `LowFixture(runner,65)`,
i wywołuje `batch(Path::Rgb16, oldPSO, selectedRGB16, count)` dla kolejnych
stary / nowy / nowy / stary. Nie zmienia stałych między wariantami.

## Uzupełnienie: cały tor źródłowy v14 i aktywny shaping

Pierwsza część audytu sprawdzała zachowany RGB16 i jego bezpośrednie otoczenie.
Po uwadze użytkownika porównano również cały produkcyjny diff od tagu
`nr-interpass-v14-rgb-linear20-20261010` (`c88b3c56`) do v21 (`3de18608`).
Zapis stanu kontynuacji, źródeł i narzędzi jest w
[NR-v21-P65-audit-continuation.md](NR-v21-P65-audit-continuation.md).

### Różnice poza samym RGB16

| Obszar | Wynik porównania v14 → v21 |
|---|---|
| Przygotowanie wejść i model NGX | Bez zmian źródłowych; zachowany immutable original base i cumulative answer |
| Obliczanie gainów i scale correction | `PassProfiles.h` bez zmian; te same per-style gainy i reguły shaping przy takim samym configu |
| Stałe rekonstrukcji | Radius, sigmy, confidence, gains, shadow gate i dane geometrii zachowane; stare flagi wyboru eksperymentalnego PSO zastąpione wyborem specjalizowanego PSO |
| Pole low-frequency | Ten sam Mode18 i ceil(work/8); przy nierównych low/high gains ten sam dodatkowy przebieg przed każdą rekonstrukcją |
| Dispatch i bariery | `DispatchCompute` i nadrzędny `Dispatch` bez zmian; na zachowanym P65 bez dodatkowego przebiegu, kopii i bariery |
| Zakres timestampów | `DlssNr_Dx12_Status.cpp` bez zmian, ten sam surowy GPU timestamp i jednorazowa konsumpcja zakończonej sekwencji |
| Reset historii | v14 reagowało na zmianę trybu 0/1/2; obecny kod reaguje na zmianę faktycznie wybranego `Path` |
| Profil benchmarku | v14 zostawiało bieżący transfer; v21 FastQuality wymusza Transfer=9 |
| Metadata benchmarku | v14 nie zapisało transferu, sigm, shaping, gains, per-pass ustawień ani shadow gate; v21 zapisuje więcej parametrów, lecz nie pełny config |

v14 nie wymuszało też guide strength, ale dla dostarczonego raportu zapisano
guide strength 1. Obecny benchmark wymusza 1. Różnica resetu historii może
wpływać na zachowanie temporalne po zmianie konfiguracji; jej wpływu na
zgłoszone 0,129 ms po rozgrzewce nie ustalono. Nie należy twierdzić,
że historyczny transfer albo shaping faktycznie były inne — brak tych danych.

Porównanie funkcji HLSL wykazało zmiany ograniczone do inter-pass i `CSMain`:
usunięto odrzucone Classic cache / DynamicPair, uproszczono wybór wag/kafli,
dodano osobny Fast. Wspólne funkcje pozostałych etapów oraz ciała Mode18,
encode i final resolve są identyczne po pominięciu komentarzy/białych znaków.
Nie oznacza to identycznego binarnego standardowego shadera: wcześniejsze
usunięcie eksperymentów i późniejsze dodanie Mode33 zmieniły cały DXIL.

Wszystkie wspólne DLL dostarczone w ZIP v14 i v21 poza OptiScaler.dll są
identyczne SHA256. Paczki nie zawierają nvngx_dlssnr.dll; nie ustalono z nich
historycznej wersji prywatnej biblioteki modelu używanej przez grę.

### Dodatkowy pomiar obejmujący low-frequency

Porównano rzeczywiste pary standard+RGB16 v14 i v21 na RTX 4090:
rezydujące synthetic 4K→2496×1404, wejścia FP32, wyjście/pole low FP16,
low 312×176, Area, radius 1, guide 1, range sigma 0,015, spatial sigma 3,0.
Shaping aktywny: bezpośrednie stałe high=0,75 / low=0,74; shadow gate wyłączony.
To ustawienia fixture, a nie odtworzony historyczny config użytkownika.

Jeden wynik obejmuje pięć przebiegów Mode18, pięć rekonstrukcji Mode28 i bariery.
Osiem powtórzeń tego fragmentu na timestamp, wynik dzielony przez osiem.
Trzy ABBA, 90 rozgrzewki i 160 próbek na okno, 960 próbek na wariant.
Upload, alokacje, PSO, CBV, odczyt i oczekiwanie na fence są poza timestamp.

| Wariant | Mediana pięciu przejść ms | P95 ms | SD ms |
|---|---:|---:|---:|
| v14 | 3,113152 | 3,220480 | 0,073269 |
| v21 | 3,113664 | 3,193856 | 0,064827 |

Różnice median v21 minus v14 w kolejnych blokach:
+0,013312 / +0,001216 / −0,003264 ms. Nie wykazano powtarzalnego spowolnienia
zmienionych shaderów w tym fixture, również z aktywnym shaping i polem low.
Wyniku nie mnożyć przez pięć: już obejmuje pięć przejść.

Surowe [1920 próbek](NR-v21-P65-shaped-chain-GPU.samples.csv): 12 okien,
kompletne indeksy 0..159 bez duplikatów. Runner
`artifacts/nr-p65-audit/shaped_chain_timing.cpp` pozostaje zapisany lokalnie.
Nie obejmuje NGX, temporalnego DLAA ani końcowego resolve; używa stałego
syntetycznego answer dla wszystkich przejść, bez sprzężenia zwrotnego przez
model NR. Nie zastępuje porównania pełnego toru w grze i nie wyklucza
regresji zależnej od innych parametrów, sceny albo równoległej pracy GPU.

## Wniosek po rozszerzeniu audytu

Nie ma podstaw do przełączenia produkcyjnego P65 na RGB20, Classic albo
przybliżony Fast, ani do cofnięcia zachowanych optymalizacji. Zgłoszone 37,3 ms
jest zgodne z dostarczonym pomiarem, a historyczne około 37,0 ms jest prawdziwe.
Obecny narzut odpowiada v16; przyczyny pozostałych 0,129 ms wobec v14
nie ustalono. Kod renderowania pozostawiono bez zmian.

Jeżeli różnica utrzymuje się w tej samej nieruchomej scenie i ustawieniach,
rozstrzygający następny pomiar powinien porównać tylko P65: archiwalny PSO
RGB16 v14 i aktualny PSO RGB16 w tym samym pełnym torze najnowszego NR,
w ABBA z osobną rozgrzewką, oraz bieżący Off. Automat kontra wymuszenie
tego samego aktualnego RGB16 sprawdza selekcję, ale nie wyjaśni różnicy
między rewizjami shadera. Tego celowanego porównania całego toru w grze
w tym audycie nie wykonano.
