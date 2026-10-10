# NR v12: wnioski i eksperymenty v13

## Dane i decyzje

Pełne 25 konfiguracji, metryki surowych 4000 próbek, trendy i block bootstrap:
[NR-v12-20261010-analysis.md](NR-v12-20261010-analysis.md).

Pomiar użytkownika: RTX 4090, The Last of Us Part I, 3840×2160; P65 2496×1404,
P50 1920×1080; sześć passów, Area, radius 1, guide strength 1.0. Scena nie ma
identyfikatora w metadanych. Taktowanie, temperatura i ich zmienność nie są
zarejestrowane. Każda konfiguracja: 90 warmup i 160 próbek, jeden sweep.

| P65 v12 | Δ mediana vs Weights v10 | Δ średnia | Δ P95 | SD | Decyzja |
|---|---:|---:|---:|---:|---|
| Interior | -0,03942 ms | -0,03907 ms | +0,11776 ms | 0,26640 ms | Zachować eksperymentalnie, bez przekonującego dowodu korzyści |
| Axes | -0,08141 ms | -0,07007 ms | +0,04608 ms | 0,23011 ms | Najlepszy kompromis w tej sesji, rozwijać i ponowić pomiary |
| Both | -0,11827 ms | -0,10215 ms | +0,15053 ms | 0,30092 ms | Najniższy typowy czas, ale największy rozrzut; zachować do A/B |

Axes i Both mają korzystny trimmed mean i ujemne przedziały bootstrap mediany
dla bloków 8/16/32. Interior obejmuje zero. To dowód korzystnego rozkładu
w tej sesji, nie dowód powtarzalnej poprawy w każdych warunkach. P95 pogarsza
się we wszystkich v12. Lag-1 rośnie z 0,009 w Weights do 0,613 / 0,381 / 0,515
w Interior / Axes / Both. Próbek nie należy traktować jako 160 niezależnych
obserwacji. Obniżona mediana Both nie wynika wyłącznie z pojedynczego minimum:
trimmed mean wynosi 37,14650 ms wobec 37,25690 ms w Weights. Pierwszy blok
40 próbek Both jest jednak wolniejszy niż pozostałe, a pojedynczy sweep nie
oddziela dryfu sesji od wpływu wariantu.

Linear16: mediana 37,27155 ms. Weights v10: 37,25517 ms, różnica -0,01638 ms.
Spatial v11 względem Weights: mediana -0,00922 ms, średnia +0,00404 ms.
Quad Fill i Both v11: mediana +0,21965 i +0,25498 ms. Source cache v10:
około +0,95–0,97 ms. Nie rozwijamy ponownie Quad Fill ani source LDS cache.

P50: bez inter-pass 25,59949 ms, Fused optimized 26,23744 ms, narzut 0,63795 ms.
Wartość bez inter-pass praktycznie odpowiada opisanej historycznej v11
(25,603 ms). Fused jest około 0,037 ms wolniejszy niż historyczne 26,200 ms;
bez raw z tej wcześniejszej sesji nie jest to dowód regresji.

P65: bez inter-pass 35,64646 ms (średnia 35,56054 ms, SD 0,38242 ms).
Średnia ostatnich 40 próbek spada o 0,34811 ms względem pierwszych 40, minimum
32,97382 ms. Z tego powodu różnica względem off jest szczególnie wrażliwa na
niestabilną bazę. Both v12 ma narzut mediany 1,49043 ms względem tego off,
ale nie jest to czas samej rekonstrukcji. Historia v9 35,602 ms różni się
o +0,04446 ms; historyczne v10/v11 około 35,5–35,7 ms obejmuje obecną wartość.
Brak dowodu regresji standardowego NR. Standardowy shader w nowej kompilacji
ma niezmieniony oczekiwany hash, co nie gwarantuje identycznego czasu GPU.

Wszystkie v12 pozostają OFF domyślnie. Nie zatwierdzamy bitowej identyczności
obrazu na podstawie testów adresowania ani tych timingów.

## Przegląd toru GPU

Zweryfikowana baza GitHub: `a1efcc4d8ce10f13586bfbbbec82cc5b338fdb17`,
zawierająca kod v12 i późniejszą dokumentację. Bazowy build
[38041320461](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/actions/runs/38041320461)
zakończył się sukcesem dla `836342317af1869f900016ab112d76aae8460151`.

- `DlssNr_Dx12_Run.cpp`: sześć odrębnych NGX feature histories i pięć
  inter-passów. Residual to zawsze `Ncurrent - originalPassBase`; baza pozostaje
  niezmienna. Fused Area wykonuje jeden dispatch rekonstrukcji i zapisuje
  jedynie obraz working, bez materializacji P100. Geometria jest buforowana
  na CPU przy zmianie rzeczywistych wymiarów. Scratch jest ponownie używany,
  a tworzony przy zmianie wymiarów/formatu.
- Dodatkowy low-field dispatch jest potrzebny przy aktywnym nierównym shaping
  high/low; istniejący exact wariant usuwa go dla równych gainów. Jego usunięcie
  bez uwzględnienia shaping zmieniłoby obraz. Debug copy inter-pass działa
  tylko w debug view 7; benchmark ustawia view 0.
- Przejścia UAV→SRV synchronizują wynik modelu lub rekonstrukcji z następnym
  konsumentem; SRV→UAV poprzedza ponowne użycie scratch. Nie znaleziono podstaw
  do bezpiecznego usunięcia tych zależności. Pięć inter-passów w tej pętli nie
  dodaje osobnych queue Signal/Wait ani CopyResource. Async/final composition
  mają odrębne zależności poza tą pętlą; nie zmieniono ich.
- Benchmark pobiera `gpuTime->ReadGpuTime()`, a nie węższy przedział `ngxTime`.
  `LastCompletedSequence()` zapobiega wielokrotnemu zbieraniu tego samego
  zakończonego timestampu. Uśrednianie czasu UI nie jest źródłem CSV.
- `DlssNr_Dx12.cpp`: v12 dopuszczone tylko dla compact Linear16 + Weights,
  bez cache, w Mode28/Area. Wymagania dynamic taps, radius 1 i rzeczywistych
  tekstur są ustawiane wcześniej w Run. Trzy PSO są lazy-created.
  Sukces PSO wybiera właściwy blob, niepowodzenie loguje warning i uruchamia
  fallback. Przy jednoczesnych flagach v11/v12, awaria v12 może wybrać v11.
  CSV zapisuje flagi żądane, a nie faktycznie wykonane PSO: same pliki v12
  nie potwierdzają wykonania konkretnego binarium. Należy sprawdzić log gry.
- Guided 3×3: dziewięć Load source, dziewięć Load model, dziewięć range/spatial
  wag `exp2`, równoległy bilinear residual i niezmienione shaping/sanitization.
  Pętle są już rozwinięte kompilacyjnie. Weights ma statyczne indeksy po
  rozwinięciu, bez source/model LDS. DXIL zawiera `Exp` dla exp2; nie uzyskano
  SASS, liczby rejestrów ani occupancy. DXIL nie jest kodem maszynowym NVIDIA.
- Tiled: grupa 8×8, linearny fill z dynamicznym div/mod szerokości kafelka.
  Strided i Quad Fill mają już niekorzystne pomiary. FP32 `float4` corrected tile
  zachowuje brak pośredniej kwantyzacji. Nie zmieniono typu ani synchronizacji.
- Zaskakujący koszt statyczny: Weights v10 deklaruje w DXIL corrected tile
  4096 B plus trzy tablice reduction po 1024 B dla innych trybów CSMain.
  Rozmiar binarium 323736 B. Tryby i ich LDS można usunąć osobną kompilacją,
  bez ingerencji w standardowy shader. To przesłanka do eksperymentu v13.

## Niezależne eksperymenty v13

Oba wymagają istniejącej eligible ścieżki compact Linear16 + Weights,
source cache OFF, v11/v12 OFF. Inne konfiguracje zachowują poprzedni wybór.
Oba są domyślnie OFF, konfigurowalne w menu i INI. Bez dodatkowego dispatchu,
tekstury, bariery ani alokacji GPU per frame. Każdy wariant ma własny lazy PSO.

1. **Area** (`DLSSNR_TILED_V13_AREA`, flaga 1048576): bounded/unrolled 3×3
   integracja z warunkami dla aktywnych wierszy/kolumn. Compact guard <=5/3
   gwarantuje maksymalnie trzy texele na oś. Przy nieoczekiwanym większym
   footprincie wraca do oryginalnego Mode28 po wspólnej barierze. Adresy,
   wagi, kolejność akumulacji, alpha i końcowy clamp pozostają zachowane.
   Nie rozwija ciężkiej rekonstrukcji Guided cztery razy jak Quad Fill.
   Ryzyko: dodatkowe predykaty lub rejestry mogą przewyższyć zysk z pętli.
2. **Mode28** (`DLSSNR_TILED_V13_MODE28`, flaga 2097152): po deklaracji cbuffer
   kompilacyjnie przyjmuje `gMode=28u`. Układ cbuffer zostaje niezmieniony.
   Te same funkcje Guided, tiled Area i pełny oryginalny fallback Mode28.
   DXC usuwa inne tryby i ich LDS. Lokalnie: DXIL 47612 B i tylko corrected
   tile 4096 B. Nie specjalizuje gainów, exp2, guide strength ani HDR.
3. **Both**: kombinacja dwóch powyższych czynników, osobny blob i PSO.

Brak pomiarów GPU v13 i porównania obrazów w grze. Nie przypisujemy redukcji
rozmiaru DXIL/LDS konkretnego zysku w ms. NVIDIA Nsight Compute jest obecne,
ale nie zmierzono nim tego dispatchu DX12 ani SASS/occupancy. Potrzebny capture
graficzny, np. Nsight Graphics/PIX, dla rzeczywistego toru gry.

## Weryfikacja i użycie

Wszystkie `tests/test_nr_interpass_*.py` przeszły lokalnie. Nowe testy obejmują
16924 footprinty z rounding FP32, brzegami, odd dimensions, losowymi rasterami
do 16384, ułamkowymi skalami, dowolnym podpisanym HDR float4, kolejnością wag
i sum, fallback dla większego footprintu, indeksy PSO i kompletność przywracania
konfiguracji. To nie zastępuje testu GPU obrazu, ruchu ani FP16 UAV w grze.

Przywracanie `DlssNrEnabled` jest osobną poprawką konfiguracji benchmarku.
Dotychczas start odrzucał wyłączony NR, więc brak pola nie dowodzi uszkodzenia
tej sesji v12. Teraz przy zakończeniu i anulowaniu przywraca również zapisany
stan enabled. Pozostałe zmieniane pola są objęte testem Take/Apply/Restore.

Workflow `.github/workflows/build_nr_detail_quality_experiments.yml` kompiluje
warianty, tworzy wymagane nagłówki i binaria oraz buduje Windows Release ZIP.
Tak jak w v12, wygenerowane eksperymentalne nagłówki/binaria pochodzą z workflow.
Sprawdza unikalność v13 i 4 KiB LDS w Mode28/Both oraz hashe referencyjne:

- Standard NR: `5599EE0B01FC50B9BEA4C2D02C8BFC1CE24287E9867DBC9FA0158DB883BAB9FC`
- Weights v10: `40C5297A43E201B0816E01E368A36913FC672638C5FBA0D67C069733A3923A4E`
- v12 Interior: `EC387783E4732C5E01DCD6EFB7DF860C21DCA3DC17B600F68FA2CB1FD5DA01CA`
- v12 Axes: `A5CF888DD991772B08698DFED99479A0C609851DD270592B08505C3DE0D80988`
- v12 Both: `BBD1E1429DCC744C6210556FEF5EA911B0A9235B59C92D33E1673E95AC24635E`

Lokalna kompilacja nowych blobów zakończyła się sukcesem:

| Wariant | DXIL bytes | Deklarowane LDS | SHA-256 |
|---|---:|---:|---|
| Area | 325516 | 7168 B | `50FCEAF78A24F233AAE3D695CC0C528BF40C5C5C937DDB1DE52AC6726B4E608A` |
| Mode28 | 47612 | 4096 B | `070A8097764D36FB4625C054D6F04E50C73B1C790E18E1D565B476E7454A2715` |
| Both | 49228 | 4096 B | `027C4BB72EB9E01E985ECEA8385B2CBFBD7E3E3DE30ADDD0812E81EA37F463F9` |

Menu: **NR v13: one sweep P50 + P65 (CSV)**. 28 konfiguracji (6 P50, 22 P65),
po 90 warmup + 160 measured, 4480 próbek, każda konfiguracja raz. Porównanie
Area/Mode28/Both do Weights v10 w tej samej sesji. Następnie weryfikacja
powtarzalności na tej samej scenie i ustawieniach, z kolejnymi sweepami lub
manualnym A/B/ABBA, oraz capture obrazu na krawędziach, SDR/HDR i w ruchu.
Nie zmieniono wymaganego pojedynczego sweepu w automatycznym benchmarku.

Największy kolejny potencjał: izolowanie trybów/danych PSO (obecny eksperyment),
profilowanie realnych rejestrów i zależności, a dopiero później hoisting
geometrii/dimensions między rekonstruowanymi texelami. `exp2` i jakość pozostają
bez przybliżeń. Żaden z tych dalszych kierunków nie ma przypisanego pomiaru ms.
