# Kontynuacja audytu P65 — stan 2026-10-11

## Aktualny wynik v22 z gry

Raport `NR-v22-p65-revision-20261011-005612` został dostarczony i sprawdzony:
[NR-v22-P65-game-results.md](NR-v22-P65-game-results.md). Wszystkie 1120 próbek
są kompletne, metryki odtworzone z raw, faktyczne current/v14 PSO bez fallbacku.
Off 35,765760 ms; obecny Optimized 37,245440 ms; v14 37,233152 ms.
Zysk v14 0,012288 ms; sąsiednie pary AB/BA dają −0,001536 / +0,024064 ms.
Same stare shadery nie odtworzyły historycznych 37,020160 ms. Nie wykazano
powtarzalnego zysku uzasadniającego cofnięcie produkcyjnych shaderów/polityki.
Historyczna różnica narzutu nadal nie ma ustalonej przyczyny. Poniższe
checkpointy zachowują wcześniejszy stan; ich wzmianki o brakującym pomiarze
v22 są już nieaktualne. Nie czekać ponownie na te raporty ani nie ponawiać
shader-only testów. Ewentualna nowa hipoteza wymaga porównania całych DLL
przy uzgodnionych ustawieniach/modelu/scenie. W tej aktualizacji tylko dokumentacja.

## Aktualizacja v22

Przygotowano celowany benchmark gry: **Compare current / v14 Optimized at 65% (CSV)**.
Kod obejmuje tylko P65 i zachowuje bieżący transfer/shaping. Off + oba referencyjne
tryby, następnie obecny/v14/v14/obecny; siedem okien. Archiwalne standard+RGB16
są w `precompile/benchmark_v14/`, ze zweryfikowanymi hashami i nagłówkami.
PSO historyczne są leniwe i ograniczone do tego profilu. Zmiana rewizji resetuje
historię, błędna geometria lub brak PSO anuluje test. Raport jest
`NR-v22-p65-revision-*`. Szczegóły: [NR-v22-P65-revision-benchmark.md](NR-v22-P65-revision-benchmark.md).
Lokalne testy Python/kompilacja bieżących shaderów PASS, GPU Mode18 parity PASS
(32 porównania / 3520 komponentów). Pomiar nowego profilu w grze pozostaje
do uruchomienia przez użytkownika. Nie powtarzać wcześniejszych synthetic pomiarów.

Build kodu `e0543ba47510f2ed1d2081a3d69a7992aecbec00` zakończony sukcesem:
[Actions 38091799380](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/actions/runs/38091799380).
[Paczka v22 jest opublikowana](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/releases/tag/nr-interpass-v22-p65-revision-20261011).
Pobrany ZIP zweryfikowano: CRC, 62 pliki manifestu, DLL x64, cztery bieżące
i dwa archiwalne shadery, brak 35 odrzuconych blobów — PASS. Sumy i link
do ZIP są w dokumencie v22. Następny krok: użytkownik uruchamia profil
w grze przy 4K i dostarcza `NR-v22-p65-revision-*.csv/.samples.csv/.txt`.
Nie ogłoszono poprawy wydajności; brak jeszcze pomiaru nowego A/B w grze.

Ten plik jest checkpointem na prośbę użytkownika. Dochodzenie nie jest zakończone.
Pierwotny checkpoint obejmował tylko audyt; v22 dodaje historyczne shadery
wyłącznie do celowanego benchmarku. Produkcyjny tor pozostaje bez zmian.
Zapisano wyniki i źródła, aby kontynuacja
nie wymagała ponownego wykonywania gotowych analiz ani pomiarów.

Aktualizacja po zapisie checkpointu: główny raport
`docs/NR-v21-P65-optimized-audit.md` uzupełniono o pełny diff źródłowy,
różnice profilu benchmarku/history reset oraz shaping-chain GPU. Krok 2
poniżej jest już wykonany. Nie znaleziono konkretnego błędu do poprawienia;
przyczyny historycznych 0,129 ms nie ustalono. Pozostaje ewentualny celowany
pomiar pełnego toru w grze, a nie ponowienie wykonanych testów syntetycznych.

## Cel i ostatnie instrukcje

Użytkownik zgłasza około 37,3 ms dla Optimized przy 65%, dawniej około 37,0 ms,
przy Off około 35,8 ms. Po pierwszym audycie zapytał:
„no to nie sprawdziłęś czy w v14 było coś inaczej?”. Przyznano, że pierwszy audyt
obejmował shader RGB16 i część otoczenia, a nie cały tor. Teraz trzeba dokończyć
pełne porównanie v14 z obecną wersją, dopisać wyniki do głównego audytu
i odpowiedzieć po polsku, bez przedstawiania nieustalonej przyczyny jako faktu.

## Repozytorium i ograniczenia

- Checkout: `E:/AI/codex/optiscaler_wijslo2_fork`.
- Origin: `https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure`.
- Gałąź i upstream: `codex/nr-interpass-v13-bounded-area`.
- HEAD przed checkpointem: `3a2cae7d025507da10e37fc1ea492bca22e9b921`.
  To pierwszy audyt; produkcyjne źródło pozostaje
  `3de18608d09dee551542a50d24c7a8581f69f584` (v21).
- Tag v14 `nr-interpass-v14-rgb-linear20-20261010` wskazuje
  `c88b3c565a272c06cab3abef0a02ef2047d5fcc4`.
- `git fetch origin` wykonano w tej części pracy; przed checkpointem upstream
  nie zawierał nowych zmian. Przed kolejną publikacją sprawdzić ponownie.
- Niezwiązane ` D package_release.ps1` wynika z kwarantanny Bitdefendera.
  Nie przywracać, nie stage'ować, nie commitować tego usunięcia.
- Stosować AGENTS.md / CONTRIBUTING.md. Commit/push zmian zadania jest stale
  autoryzowany. Nie tworzyć agentów bez wyraźnej instrukcji. Odpowiadać po polsku.
- Podczas oczekiwania na build/pomiar usypiać wykonanie; nie analizować
  niezmienionego statusu. Dla samej dokumentacji nie uruchamiać pełnego buildu.

## Pierwszy audyt: wykonane i opublikowane

Plik `docs/NR-v21-P65-optimized-audit.md` oraz
`docs/NR-v21-P65-revision-GPU.samples.csv`, commit `3a2cae7d`.

Raporty użytkownika w
`F:/Gry/The Last of Us - Part I/OptiScaler-NR-Benchmarks/`:

| Raport | Off mediana ms | RGB16/Optimized mediana ms | Narzut względem Off ms |
|---|---:|---:|---:|
| NR-v14-20261010-134037 | 35,649536 | 37,020160 | 1,370624 |
| NR-v16-20261010-164041 | 35,725824 | 37,221888 | 1,496064 |
| NR-v21-fast-20261010-235743 | 35,763712 | 37,263360 | 1,499648 |

Każdy ma `.csv`, `.samples.csv`, `.txt`. Surowa kolumna czasu to
`total_nr_gpu_ms`, nie `gpu_ms`. Sprawdzono 160 próbek na okno, brak luk
i duplikatów; v21 Optimized ma dwa okna, 320 próbek. Metryki podsumowań
zgodne z raw do 0,000004 ms. RTX 4090, 4K→2496×1404, sześć passów,
Area/radius 1/guide 1. v14/v16 były pojedynczymi sweepami bez ABBA;
v21 to Optimized/Fast/Fast/Optimized, nie old/new RGB16.

Od v14 do v21 po odjęciu różnicy Off pozostaje 0,129024 ms.
Od v16 do v21 różnica narzutu to tylko 0,003584 ms.
Nie uznawać automatycznie pozostałej różnicy za szum.

Mode 3 przy P65 wybiera prawidłowo RGB16. Wszystkie grupy mieszczą się
w kaflu: maksimum 14 texeli na osi, 312×176 grup, brak overflow/fallbacku.
Zainstalowany `F:/Gry/The Last of Us - Part I/dxgi.dll` jest identyczny
z DLL pobranej paczki v21: SHA256
`18e1994fad61929c5844b03efca6f9f608ac474bae0acbae40e01d0e981b5e13`.
Aktualny RGB16 blob jest w nim obecny dokładnie raz.

RGB16 v14/v16: 47580 B, SHA256
`e0b673de1c16be5c1100ccdbac751f601bc8a2d275a70596c9101164f7cbbd3f`.
RGB16 v17/v18/v20/v21: 44040 B, SHA256
`0cca2aafeef242cf007c80e31135ae631d3a2bc7ac07049853be4a35d3432f71`.
Standard v21: 318328 B, SHA256
`9e880602a86eca022dc72ac904a39a6a59278254e4a6ac483cbc84bf7a41d205`.

Poprzedni synthetic RGB16-only pomiar miał shaping wyłączony, sigma 1,2;
trzy ABBA, 960 próbek na wariant: mediana v14 0,404672 / current 0,404288 ms.
Nie zastępuje pomiaru całego NR ani aktywnego shaping.
Polityka C++ PASS; rzeczywisty GPU parity v21 vs v14 PASS:
6528 porównań, 53 543 552 składowych RGBA, NaN według kontraktu fixture.

## Rozszerzone porównanie źródeł — już wykonane

Pełna lista zmian produkcyjnych v14→v21 obejmuje Config, menu, benchmark,
politykę i inter-pass shader/dispatch. Nie zmieniono innych plików produkcyjnych
toru NR. Przejrzano pełny diff `DlssNr_Dx12_Run.cpp`.

- `PassProfiles.h`, stan NR i `DlssNr_Dx12_Status.cpp` są identyczne.
  Gainy, skalowanie gainów, per-pass ustawienia, przygotowanie wejść, NGX,
  final resolve i początek/koniec pomiaru mają te same źródła.
- `DispatchCompute` i nadrzędny `Dispatch` są identyczne po pominięciu
  białych znaków/komentarzy. `DispatchPass` tylko przypisuje `_pipelineState`
  do lokalnej zmiennej przed tym samym wywołaniem.
- `MakeInterPassConstants` zachowuje radius, sigmy, confidence, style gains,
  shaping i shadow gate. Indywidualne stare przełączniki zostały zastąpione
  wyborem zachowanego toru. Stare flagi wyboru PSO usunięto; nie są dowodem
  zgubienia optymalizacji: nowy PSO jest specjalizowany w czasie kompilacji.
- Przy aktywnym shaping z nierównymi low/high gains nadal wykonywany jest
  ten sam Mode18, ceil(work/8), przed Mode28. Nie znaleziono dodatkowego
  dispatchu, kopii ani bariery na zachowanym torze P65.
- Porównano 85 funkcji HLSL v14 i 84 v21. Zmienione wspólne funkcje:
  `InterPassGuideBlend`, `InterPassTiledFusedArea`, `InterPassTileValue`,
  `InterPassCorrectedP100LoadDynamic`, `InterPassCorrectedClassicSharedStencil`,
  `CSMain`. Usunięto nieużywane Classic cache / DynamicPair; dodano Fast.
  Inne wspólne funkcje są identyczne po pominięciu komentarzy/whitespace.
- Ciała Mode0, Mode3, Mode5..26, Mode29..31 są identyczne, w tym Mode18.
  Mode27 usuwa odrzucony Classic cache; Mode28 usuwa odrzucony paired path.
- v14 i v16 kontrolny RGB16 miały ten sam blob i ten sam aktywny tor CPU;
  nowe flagi wide/pair/low v16 były w kontrolnym przypadku wyłączone.
- Ważna różnica history reset: v14 śledziło sam tryb 0/1/2, więc zmiany
  eksperymentalnych flag przy tym samym trybie nie resetowały historii.
  Obecny kod śledzi wybrany `Path` i resetuje przy zmianie faktycznej ścieżki.
  Nie udowodniono wpływu tego na zgłoszoną różnicę po rozgrzewce.
- Ważna różnica benchmarku: v14 nie wymuszało transferu i nie zapisywało go;
  v21 FastQuality wymusza Transfer=9 (Temporal DLAA residual + P100-guided).
  v14 nie wymuszało guide strength, lecz jego raport zapisał guide 1;
  obecne profile wymuszają guide 1. v14 nie zapisało sigm, shaping, gains,
  per-pass ustawień ani shadow gate. Nie można potwierdzić identyczności
  tych historycznych ustawień. To różnica metodologii, nie ustalona przyczyna.
- Porównano wszystkie wspólne DLL w ZIP v14 i v21: poza OptiScaler.dll
  są identyczne SHA256. nvngx_dlssnr.dll nie jest w tych paczkach,
  więc jej historycznej wersji z tych ZIP nie ustalono.

Źródłowe snapshoty i fragmenty diff są już w `artifacts/nr-p65-audit/`:
`c88b3c56.hlsl`, `7d1a639d.hlsl`, `3de18608.hlsl`,
`<commit>-DlssNr_Dx12_Run.cpp`, `<commit>-DlssNr_Dx12.cpp`,
`<commit>-DlssNr_Status.cpp`, `mode27.diff`, `mode28.diff`, `DispatchPass.diff`.

## Nowy pomiar GPU z aktywnym shaping — już ukończony

Źródło lokalnego runnera: `artifacts/nr-p65-audit/shaped_chain_timing.cpp`.
EXE/OBJ obok. Wyniki: `artifacts/nr-p65-audit/shaped-chain.samples.csv`;
kopia do publikacji: `docs/NR-v21-P65-shaped-chain-GPU.samples.csv`.

Runner korzysta z istniejącego `tests/nr_gpu/interpass_timing_fixture.h`.
Rezydujące 4K→2496×1404, FP32 wejścia / FP16 wyjście i pole low 312×176.
Area, radius 1, guide 1, range sigma 0,015, spatial sigma 3,0.
Aktywny shaping: bezpośrednie stałe high=0,75 / low=0,74; shadow gate wyłączony.
To stałe fixture, nie odtworzone historyczne per-style gainy użytkownika.

Porównuje rzeczywistą parę standard+RGB16 v14 z parą standard+RGB16 v21.
Jeden wynik zawiera pięć przebiegów Mode18, pięć rekonstrukcji Mode28 i bariery.
Osiem powtórzeń tego fragmentu w przedziale timestamp, wynik dzielony przez osiem.
Trzy bloki ABBA, 90 warmup i 160 próbek na okno, 960 próbek na wariant.
Tekstury/PSO/CBV i upload poza timestamp; alokacje/fence/readback poza timestamp.
Nie zawiera NGX, końcowego resolve, temporalnego DLAA ani sprzężenia zwrotnego
przez modele NR: używa stałego synthetic answer dla pięciu przejść.
Nie jest pomiarem całego NR ani dowodem identycznego czasu w każdej scenie.

| Wariant | Mediana pięciu przejść ms | P95 ms | SD ms |
|---|---:|---:|---:|
| v14 | 3,113152 | 3,220480 | 0,073269 |
| v21 | 3,113664 | 3,193856 | 0,064827 |

Różnice median v21−v14 w trzech blokach:
+0,013312 / +0,001216 / −0,003264 ms. Brak powtarzalnej regresji w tym fixture.
1920 próbek, 12 okien, indeksy 0..159 kompletne bez duplikatów.
GPU przed testem: RTX 4090, utilization 2%, 44°C; zegary nie były blokowane.
Nie mnożyć tych wyników dodatkowo przez pięć — już zawierają pięć przejść.

## Zachowane pliki i narzędzia

- Stare prawdziwe shadery: `artifacts/nr-v14/generated/`:
  `DlssNr_Shader.cso`, `dlssnr_tiled_v14_rgb16_Shader.cso`, RGB20.
- Aktualne shadery do fixture: `artifacts/nr-p65-audit/generated/`:
  standard wyekstrahowany z aktualnego nagłówka, RGB16/RGB20 skopiowane
  z `artifacts/nr-v21/generated/`; wszystkie znalezione w zainstalowanym DLL.
- Nie pomylić `artifacts/nr-v21/baseline/` z v14: ten folder zawiera bazę v20
  przemianowaną na historyczne nazwy! Prawdziwa baza v14 jest powyżej.
- ZIP v14: `artifacts/nr-v14/download/`; ZIP v21: `artifacts/nr-v21/release/`.
- Bundled Python:
  `C:/Users/zdzis/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe`.
- VS2019 vcvars:
  `C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Auxiliary/Build/vcvars64.bat`.
  Runner kompilowano cl `/std:c++17 /EHsc /O2 /I tests/nr_gpu`,
  link `d3d12.lib dxgi.lib`. CPP zaczyna się od `#include "pch.h"`.
- Istniejący parity EXE: `artifacts/nr-v21/interpass_parity.exe`.
  Sprawdzony wcześniej z `artifacts/nr-p65-audit/generated artifacts/nr-v14/generated --hardware`.

Pliki `artifacts/` są ignorowane przez Git; zachowano je lokalnie,
nie usuwać i nie nadpisywać potrzebnych starych shaderów.

## Następne kroki

1. Nie powtarzać powyższych pomiarów bez nowej hipotezy.
2. Uzupełnić `docs/NR-v21-P65-optimized-audit.md` o pełny audyt źródeł,
   różnicę wymuszanego transferu/history reset i nowy shaping-chain pomiar.
   Oddzielić brak znalezionego błędu od braku identycznego historycznego configu.
3. Ocenić, czy pozostała konkretna różnica kodu wymaga sprawdzenia;
   dotychczas nie znaleziono przyczyny 0,129 ms. Nie zmieniać produkcyjnej
   selekcji na podstawie samego syntetycznego pomiaru.
4. Jeśli potrzebny jest test w grze, porównać wyłącznie P65: stare rzeczywiste
   standard+RGB16 v14 versus obecne w tym samym pełnym torze i ustawieniach,
   ABBA + osobna rozgrzewka, z Off. Automat kontra wymuszenie tego samego
   aktualnego RGB16 nie rozstrzyga historycznej różnicy. Takiego nowego
   profilu w kodzie nie dodano i w grze go nie uruchomiono.
5. Commit/push tylko plików zadania, sprawdzić remote HEAD i podać wynik.
   Nie uruchamiać pełnego buildu, jeśli końcowy zakres pozostanie dokumentacyjny.

Ostatnia informacja przekazana użytkownikowi: rozszerzony fragment ze shaping
dał około 3,1132 / 3,1137 ms, a porównanie źródeł nie wykazało dodatkowej pracy
GPU; różnica profilu benchmarku i niepełne historyczne metadata ograniczają wniosek.
