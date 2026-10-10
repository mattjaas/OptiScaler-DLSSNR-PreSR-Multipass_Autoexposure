# NR v17: automatyczny wybór inter-pass

Ten dokument opisuje paczkę v17. Test poniżej 50%, aktualne nazwy kontroli
i nowe raporty v18 opisano w [NR-v18-below-50.md](NR-v18-below-50.md).

UI ma cztery pozycje: Off, Classic reference, Fused reference i Inter-pass optimized.
Oba tryby referencyjne wyłączają optymalizacje inter-pass. Optimized dobiera zachowaną
ścieżkę według rzeczywistych wymiarów tekstur, filtra i promienia prowadzenia.
Stare checkboxy i ich opcje w Config usunięto; wczytanie starej konfiguracji
Classic/Fused z ExactOptimized przenosi ją na tryb 3. Zapis INI usuwa stare klucze
InterPass poza InterPassReconstruction.

## Zachowane ścieżki

Profil oparto na pomiarach użytkownika na RTX 4090 w The Last of Us Part I,
3840×2160, sześć passów, Area, radius 1, guide strength 1. To najlepsze znane
warianty tego profilu; nie jest to autotuner mierzący każdy GPU/scenę w tle.

| Warunek przy Area, radius 1 i dodatnim guide strength | Automatyczny tor |
|---|---|
| Obie osie dokładnie 50% | Fused optimized, specjalizacja P50/shared bilateral/bilinear |
| Obie osie >50,5% i ≤90%, ale nie spełniają Compact16 | Fused RGB20, isolated weights + Mode28 |
| Obie osie >50,5% i ≤90% oraz native/work ≤5/3 w obu osiach | Fused RGB16, isolated weights + Mode28 |
| Pozostałe skale, inne filtry/promienie lub prowadzenie wyłączone | Classic optimized z dokładnym filtrem i fallbackami |

Próg Compact16 wynosi 60% rzeczywistych wymiarów, niezależnie dla obu osi.
Zaokrąglenie np. 65×37 do 39×22 przy nominalnym 60% wymaga RGB20.
Przed użyciem tiled sprawdzane są również rzeczywiste rozmiary wszystkich wejść.
Niebezpieczna geometria lub błąd tworzenia PSO zachowują standardowy, dokładny
tor. Benchmark raportuje faktycznie użyty PSO oraz fallback.

Classic optimized zachowuje wspólny stencil guided radius-one i fuzję
downsample+clamp dla obsługiwanych filtrów lokalnych. Ta fuzja odtwarza pośrednie
zaokrąglenie FP16. Filtry zewnętrzne zachowują własne skalery, większe promienie
oryginalną matematykę. HDR, alfa, bramka cieni, shaping i sanitize pozostają.
Zmiana wybranego toru resetuje historię kolejnych modeli bez tworzenia NGX od nowa.

Usunięto pary odczytów v4/v16, wide 16×8, osobny Mode18, source/model LDS cache,
strided/quadfill, v11 spatial, v12 interior/axes, bounded Area v13, guide-one v14,
stare eksperymentalne PSO, opcje konfiguracyjne, ich kompilatory i testy.
Nie ingerowano w inne niezależne funkcje NR. Historyczne raporty pozostają zapisem
pomiarów, a nie instrukcją używania usuniętych wariantów.

Poza standardowym shaderem potrzebne są tylko dwa lazy PSO: RGB16 (3072 B LDS)
i RGB20 (4800 B LDS). Narzędzie tools/compile_nr_interpass.ps1 generuje trzy
bloby i nagłówki, sprawdza LDS oraz wypisuje SHA256. Workflow używa tego narzędzia.

## Dane uzasadniające wybór

Ostatni v16: RTX 4090, TLOU, 4K, 90 warmup / 160 próbek, pojedynczy sweep.
Czas całego przedziału GPU NR, nie pojedynczego inter-pass.

| Skala | Fused reference mediana ms | Zachowany zwycięzca mediana ms | Zysk ms |
|---|---:|---:|---:|
| 50% | 27,94445 | 26,20006 | 1,74439 |
| 59% | 36,88499 | 33,34605 | 3,53894 |
| 65% | 41,58003 | 37,22189 | 4,35814 |

Wide przy 59/65%: mediana lepsza jedynie o 0,00461/0,06554 ms, P95 gorszy
o 0,14951/0,11980 ms. Pary odczytów pogarszały medianę o 2,89587/3,14931 ms,
wide+pair o 2,27686/3,01414 ms, osobny Mode18 o 0,14592/0,08448 ms.
Zysk wide nie był stabilny. Wcześniejsze v14 pokazało przewagę RGB/Mode28 także
przy 55/59/60/61% oraz Fused optimized nad Classic optimized przy 50%.
Nie przenosimy regresji starego, niespecjalizowanego weights poniżej 60%
na odrębny i sprawdzony RGB20/Mode28. Dane v14 i wcześniejsze analizy są
w docs/NR-v14-20261010-analysis.md i docs/NR-v13-20261010-analysis.md.
Granice poza bezpośrednio zmierzonymi procentami to ostrożna polityka, którą
weryfikuje dodatkowy przypadek bieżącej skali; nie dowód na najszybszy tor
w każdym możliwym ustawieniu.

## Benchmark weryfikacyjny

Przycisk: Verify automatic inter-pass performance (CSV).

Skale 50/59/65%, a gdy bieżący procent jest inny, także on (zaokrąglony, 25–99%).
Na skalę: Off, Classic reference, Fused reference, drugi zachowany tryb optimized,
a następnie A/B/B/A: wymuszony znany zwycięzca / automat / automat / zwycięzca.
To sześć unikalnych konfiguracji i osiem okien na skalę:
18 konfiguracji / 24 okna, albo 24 / 32 z dodatkową skalą.
Słabe eksperymenty nie wracają. Powtórzenia służą ograniczeniu wpływu dryfu.

Każde okno ma własną rozgrzewkę 90 świeżych timestampów i 160 próbek domyślnie.
Wyniki ABBA są łączone po nazwie/procencie; surowe próbki zachowują indeks okna.
Benchmark ustawia sześć passów, Area, radius 1, guide strength 1, wyłącza spatial
compression i debug. Shaping/cienie pozostają ustawione przez użytkownika.
Zakończenie lub anulowanie przywraca wszystkie zmienione ustawienia i usuwa
wewnętrzny override ścieżki.

Raporty NR-optimized-*.csv/.samples.csv/.txt zapisują adapter GPU, rzeczywiste
wymiary, wybraną ścieżkę, liczbę passów/próbek, medianę, P95 i SD. Kolumny
delta_vs_known_winner_ms oraz path_matches_control sprawdzają automat wobec
bezpośredniej kontroli ABBA. Różnica czasu powinna mieścić się w rozrzucie,
a ścieżka powinna być identyczna. Porównaj również Classic optimized control,
aby wychwycić zmianę najlepszego trybu dla konkretnej sceny/GPU.
historical_gain_vs_fused_ms pokazuje historyczny zysk w tabeli powyżej;
actual_gain_vs_fused_ms nowy pomiar. Historyczny wynik nie jest gwarancją
czasu w innej scenie. Utrzymuj nieruchomą scenę i takie same ustawienia HDR;
taktowanie/temperatura nie są kontrolowane. Brak dodatkowych query GPU
w domyślnej ścieżce renderowania.

## Weryfikacja

- Faktyczne DXIL na RTX 4090: 4608 porównań, 41 118 464 składowe RGBA.
  Nowe standard/RGB16/RGB20 zgodne bitowo ze sprawdzonymi shaderami v16/v14
  dla FP32/FP16, z wyjątkiem payloadu NaN. Skale 25/40/50/51/55/59/60/61/65/90/95/99%,
  nieparzyste rozmiary, niepełne grupy, HDR ze znakiem, alfa, cienie i shaping.
  Runner: tests/nr_gpu/interpass_parity.cpp; wymaga folderu referencyjnych blobów
  v16 oraz nowych blobów, argumenty: new-dir v16-reference-dir --hardware.
  To kontrola shaderów, nie całego temporalnego łańcucha NGX. WARP nie sprawdzano.
- Test rzeczywistego nagłówka polityki w C++: wszystkie procenty, tryby
  referencyjne, progi P50/P60/P90, filtry/promienie i 32 887 osi kafli — PASS.
- Testy zachowanej arytmetyki i benchmarku — PASS. Kompilacja trzech DXIL — PASS.

Nowy pomiar całego NR w grze wymaga uruchomienia tego benchmarku.
Dotychczasowe wyniki są podstawą wyboru; nie przedstawiamy ich jako nowego
pomiaru paczki v17.

Lokalny smoke wydajności po usunięciu kodu: RTX 4090, syntetyczny signed HDR,
native 1920×1080, FP16, Area/radius 1/guide 1, sigma spatial 1,2/range 0,015.
Timestamp obejmuje osiem dispatchy z barierą UAV i jest dzielony przez osiem;
upload/readback pozostają poza przedziałem. Po rozgrzewce obu PSO wykonano
trzy bloki ABBA (sześć próbek na PSO).

| Skala | Poprzedni shader mediana ms | v17 mediana ms | Δ ms | Min–max poprzedni / v17 ms |
|---|---:|---:|---:|---|
| 50% | 0,408576 | 0,490362 | +0,081786 | 0,171392–1,692288 / 0,181248–0,790784 |
| 59% | 0,791576 | 0,833340 | +0,041764 | 0,399744–1,667840 / 0,389376–1,174272 |
| 65% | 0,496448 | 0,417600 | −0,078848 | 0,076032–1,738624 / 0,076268–16,638720 |

SD poprzedni/v17: P50 0,520449/0,245714 ms, P59 0,449527/0,257159 ms,
P65 0,523375/6,077399 ms. Rozrzut i pojedynczy długi stall uniemożliwiają
wniosek o zysku lub regresji na tym pomiarze. To nie pomiar gry ani całego NR.
Surowe dane: docs/NR-v17-GPU-cleanup.samples.csv. Nowy benchmark w statycznej
scenie jest potrzebny do oceny rzeczywistego czasu zoptymalizowanego toru.
Powtórzenie smoke: runner interpass_parity.exe new-dir v16-reference-dir --hardware --timing.

## Opublikowana paczka

Źródło: f07947a39d539721d6db232daf858e8c42232e80,
gałąź codex/nr-interpass-v13-bounded-area.
[Build 38063008643](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/actions/runs/38063008643)
przeszedł testy, kompilację shaderów i fixture, pełny build DLL oraz publikację.

[ZIP v17](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/releases/download/nr-interpass-v17-auto-optimized-20261010/OptiScaler-NR-nr-interpass-v17-auto-optimized-20261010.zip),
131 085 544 bajty.

- ZIP SHA256: 4adce9636239a2bf30fc3279a2e9214a981944b2c8c4d56d7f5c160ef4ed010b
- DLL SHA256: 5485e2651a545584ca45b8d66913ba67636d7fffc0ce168678285fb4d4d4d47b
- Pobrany ZIP: CRC i wszystkie 62 wpisy SHA256SUMS.txt poprawne.
  DLL Windows x64 zawiera cztery tryby, nowy benchmark i trzy bloby identyczne
  z lokalnie sprawdzonymi DXIL. Nie zawiera 36 wcześniejszych blobów inter-pass.

Usunięty lokalnie przez Bitdefender package_release.ps1 pozostał poza commitem.
Nie przywracano pliku z kwarantanny; Actions używał istniejącej wersji z repozytorium.
