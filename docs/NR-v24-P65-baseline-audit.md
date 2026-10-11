# NR v24 — audyt baseline P65 po wynikach v23

## Wniosek i zakres

Wyniki historyczne rzeczywiście pokazują niższy czas **Off** w v13/v14.
Nie znaleziono jednak dodatkowej pracy GPU przy Off ani zmiany jego algorytmu,
która dowodziłaby regresji. Nie wprowadzono domniemanej poprawki produkcyjnej.
v24 dodaje kontrolowane porównanie głównego shadera przy Off i Optimized,
aby oddzielić wpływ jego binarium od różnic między sesjami gry.

## Wynik v23: Guide16 nie daje powtarzalnej przewagi

Źródła: `F:/Gry/The Last of Us - Part I/OptiScaler-NR-Benchmarks/`
`NR-v23-p65-guide16-20261011-020122.{csv,samples.csv,txt}`.
RTX 4090, sterownik DXGI 32.0.16.1656; 3840×2160 → 2496×1404, sześć modeli,
Area, radius 1, guide 1, Transfer 9, sigma range 0,015 / spatial 3, shaping 2.
Zapisane ustawienia v22 i v23 są zgodne. Scena nie ma identyfikatora,
zegary GPU nie były kontrolowane. Zweryfikowano wszystkie 1120 próbek v23,
kompletność siedmiu okien oraz zgodność statystyk z CSV.

| Wariant | Próbki | Mediana ms | P95 ms | SD ms |
|---|---:|---:|---:|---:|
| Off | 160 | 35,828224 | 36,073472 | 0,133037 |
| Obecny Optimized RGB16 | 320 | 37,267968 | 37,677056 | 0,214652 |
| Archiwalny Guide16 ze screena | 320 | 37,280768 | 37,583872 | 0,184146 |

Guide16 ma medianę o 0,012800 ms wyższą, ale średnią o 0,004496 ms niższą.
Mediany okien ABBA: 37,282304 / 37,272576 / 37,283328 / 37,258240 ms.
Różnice obecny−Guide16 zmieniają znak: +0,009728 / −0,025088 ms.
Nie ma podstaw do przełączenia produkcji na Guide16.

## Historia baseline — pomiary, nie kontrolowane A/B

Te same katalogi i stem raportu poniżej; statystyki przeliczono z `samples.csv`.
Wspólna geometria P65, sześć modeli. Starsze raporty nie zapisują pełnych
ustawień transferu/shaping/poszczególnych modeli, więc nie można uznać
różnicy pomiędzy sesjami za sam koszt zmiany kodu.

| Raport | Off mediana ms | Off P95 ms | Off SD ms | Optimized/RGB16 mediana ms |
|---|---:|---:|---:|---:|
| NR-v13-20261010-123610 | 35,566080 | 36,036608 | 0,375444 | — |
| NR-v14-20261010-134037 | 35,649536 | 36,218880 | 0,433490 | 37,020160 |
| NR-v16-20261010-164041 | 35,725824 | 36,067328 | 0,446293 | 37,221888 |
| NR-v21-fast-20261010-235743 | 35,763712 | 36,241408 | 0,389806 | 37,263360 |
| NR-v22-p65-revision-20261011-005612 | 35,765760 | 35,969024 | 0,131781 | 37,245440 |
| NR-v23-p65-guide16-20261011-020122 | 35,828224 | 36,073472 | 0,133037 | 37,267968 |

Off v23 jest wyżej o 0,262144 ms względem v13 i 0,178688 ms względem v14.
Narzut obecnego Optimized nad Off w v23 wynosi 1,439744 ms.
Między v22 a v23 Off wzrósł o 0,062464 ms, a Optimized o 0,022528 ms;
ich różnica **zmalała** o 0,039936 ms. Sam wzrost baseline nie dowodzi więc
automatycznego wzrostu narzutu inter-pass.

## Audyt v13/v14 → bieżąca wersja

Porównano źródła z tagów `nr-interpass-v13-bounded-area-20261010`,
`nr-interpass-v14-rgb-linear20-20261010` i bazowego HEAD `cc0c2a35`.

- Przygotowanie wejść, pętla modeli NGX, pięć ClampProxy przy Off, temporalny
  transfer, końcowy resolve, zasoby i timestampy zachowują dotychczasowy tor.
  Nie znaleziono dodatkowych dispatchów, kopii, barier ani alokacji przy Off.
- `DlssNr_Dx12_Encode.cpp`, Models, State, Enlarge, PassProfiles, Proxy,
  implementacja wywołań NGX i `Dx12_Status.cpp` nie zmieniły baseline od v13.
  Zmiany Run.cpp v13→v14 dodawały wyłącznie flagi inter-pass;
  zmiany późniejsze dotyczą selekcji/implementacji tych ścieżek.
- Usuwanie starych opcji Config i migracja do Optimized dotyczą włączonego
  inter-pass; Off=0 zachowano. Nie znaleziono zmiany domyślnych parametrów
  modeli baseline. Ogólne projekty kompilacji/renderer pozostały bez zmian.
- Dziewięć wspólnych bibliotek DLL poza OptiScaler.dll jest bajtowo identycznych
  w paczkach v13/v14/v23. Prywatna `nvngx_dlssnr.dll` nie jest częścią tych ZIP;
  nie ustalono jej historycznej wersji z paczek.
- Ciała gałęzi baseline HLSL pozostały równoważne, lecz główne binarium DXIL
  zmieniło się po usunięciu odrzuconych inter-pass i dodaniu Mode33/Fast.
  To pozostawia hipotezę wpływu kompilacji całego shadera na baseline.

**Rzeczywisty wydany standard DXIL w v13 i v14 jest identyczny**:
318704 B, SHA256 `5599ee0b01fc50b9bea4c2d02c8bfc1ce24287e9867dbc9fa0158db883bab9fc`.
Potwierdzono dokładnie jedno wystąpienie w każdym wydanym OptiScaler.dll.
Historyczny plik shaderowy zapisany w Git jest starszy (193484 B,
`b05dfca16103f1e83ee25990c708e45d02fd4e6a750875e0e0c08756c02768ef`)
i **nie stanowi poprawnej kontroli wydanej wersji**.
v24 używa rzeczywistego binarium już zachowanego w `precompile/benchmark_v14/standard`.

Obecny standard: 318328 B,
SHA256 `9e880602a86eca022dc72ac904a39a6a59278254e4a6ac483cbc84bf7a41d205`.
Źródła/shadery produkcyjne i polityka Optimized nie są zmieniane w v24.

## Lokalna weryfikacja GPU

`tests/nr_gpu/nr_baseline_parity.cpp`: RTX 4090, 480 porównań,
2 871 808 składowych głównego wyjścia RGBA — PASS (wyłączono porównanie payload NaN).
Tryby 0/1/2/7/8/9/10/18/20/21/22/23/24/25/26, cztery geometrie,
cztery wejścia stress, FP16 i FP32. Nie jest to test całej sekwencji NGX,
temporalnej historii ani dodatkowego wyjścia u1.

Dodatkowy lokalny pomiar pojedynczego ClampProxy (Mode8): rezydujące
2496×1404, wejścia FP32 / wyjście FP16, RTX 4090, trzy ABBA,
90 rozgrzewki i 160 próbek na okno, 960 próbek na wariant.
Timestamp obejmuje dispatch i barierę, po osiem powtórzeń dzielonych przez osiem;
PSO, upload i alokacje poza timestamp. Zegary nieblokowane, wejście syntetyczne.

| Standard PSO | Mediana ms | P95 ms | SD ms |
|---|---:|---:|---:|
| Obecny | 0,078976 | 0,105472 | 0,008504 |
| v13/v14 | 0,078720 | 0,079744 | 0,007582 |

Różnice median obecny−stary w blokach: +0,001088 / +0,000128 / 0 ms.
Nie wykazało to regresji rzędu 0,1 ms w samym ClampProxy. Wynik nie zawiera
NGX, resolve ani całego NR; nie dowodzi identycznego czasu tych etapów w grze.
Runner/próbki lokalnie: `artifacts/nr-baseline-audit/clamp_timing.cpp`,
`clamp.samples.csv`; audyt źródeł i danych: `inspect.py`, `hlsl.diff`.

## Nowy test v24 w grze

Przycisk: **Compare current / v13-v14 baseline at 65% (CSV)**.
Tylko 4K → 2496×1404, sześć modeli, Area/radius 1/guide 1.
Obecne ustawienia transferu, shaping i modeli są wspólne i zapisane w TXT.

1. Off: obecny standard A / archiwalny standard B / B / A.
2. Classic reference i Fused reference, oba obecne bez optymalizacji.
3. Optimized: obecny standard A / archiwalny standard B / B / A.

Sześć konfiguracji, dziesięć okien, osobna rozgrzewka każdego okna;
zmiana rewizji standardu resetuje historię również przy Off.
Oba Optimized zachowują **obecny RGB16**, więc zmienia się tylko standard PSO,
a nie algorytm/PSO inter-pass. Stary PSO tworzy się leniwie tylko dla testu;
współdzielenie z v22 nie nadpisuje ani nie przecieka poprzedniego PSO.
Błąd PSO/niepoprawna geometria lub fallback Optimized przerywają pomiar.
Po zakończeniu/anulowaniu przywracane są ustawienia i obecny standard.

Raport `NR-v24-p65-baseline-*` zachowuje próbki i mediany okien ABBA.
`shader_revision=v13-v14-standard` oznacza stare binarium głównego shadera,
nie całą starą DLL ani stary RGB16. Nowe kolumny:

- `delta_vs_matching_off_median_ms`: narzut względem Off **tej samej rewizji**.
- `gain_vs_current_same_mode_ms`: dodatnia wartość oznacza szybszą starą
  rewizję przy takim samym Off/Optimized; dla referencji NaN.

Dotychczasowa kolumna `delta_vs_off_median_ms` nadal odnosi się do obecnego Off.
Do porównania narzutów obu rewizji należy użyć nowej kolumny matching.
Interpretować również dwa okna każdego wariantu, P95 i SD; pojedyncza różnica
median nie wystarcza do przypisania regresji. Pozostaw grę w tej samej statycznej scenie.

## Stan realizacji

Nowy test strukturalny baseline oraz istniejące testy Guide16, revision
i regularnego benchmarku przeszły lokalnie. Fixture baseline skompilowano
i uruchomiono na GPU; workflow kompiluje je ponownie obok pozostałych kontroli.
Pełny build [38098151771](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/actions/runs/38098151771)
zakończył się sukcesem dla commita `8b2cd744050f0070015d286488a1d1e35ec59d1d`.
Testy arytmetyki/benchmarków, polityka C++, kompilacja shaderów i wszystkich
fixture oraz pełne MSBuild/pakowanie/publikacja release — PASS.

[Release v24](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/releases/tag/nr-interpass-v24-p65-baseline-20261011),
[ZIP](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/releases/download/nr-interpass-v24-p65-baseline-20261011/OptiScaler-NR-nr-interpass-v24-p65-baseline-20261011.zip).

- Pobrany ZIP: 131389371 B, SHA256
  `895284a33e6629c3ff9eca0fde88bba3fba2daeb43d14cf6d4fd5e32ab370ff7`;
  zgodny z digestem assetu GitHub.
- OptiScaler.dll Windows x64, SHA256
  `a498019778b3ab14b040e1f4e1a19522a510ae1c2755620eec4ff03c601ff528`.
- CRC ZIP i wszystkie 62 pliki manifestu SHA256 — PASS.
- Nowy przycisk/prefix, identyfikator rewizji i kolumny matching Off — obecne.
- Dokładnie siedem wymaganych blobów, każdy po jednym wystąpieniu w DLL;
  cztery produkcyjne nadal identyczne z v23, trzy archiwalne zachowane.
  34 odrzucone warianty eksperymentalne nie występują w DLL.
- Weryfikacja lokalna: `artifacts/nr-v24/verify_zip.py`; paczka w `release/`
  wewnątrz tego katalogu artifacts.

Kontrolowany pomiar całego NR w grze v24: oczekuje na raport użytkownika.
Do tego czasu regresja baseline nie jest potwierdzona ani uznana za naprawioną.
