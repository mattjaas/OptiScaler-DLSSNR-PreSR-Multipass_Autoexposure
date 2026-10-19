# NR v52 — dokładny shader kodowania wejścia Superior Scaling

Kodowanie natywnego wejścia korzysta z osobnego PSO ze stałym Mode 0.
To ten sam blok HLSL: proxy oraz kopia HDR powstają z identycznych operacji,
z tą samą ekspozycją, anchorami, trim, tonemappingiem, alpha i konwersją
do formatu wyjścia. Dotyczy SDR i wszystkich pięciu mapowań HDR, także
domyślnego HDR Off/soft knee. Nie zmienia NR, liczby passów, harmonogramu,
gainów, Structure/Intensity, carriera, DLAA ani końcowego resolve v51.

Dochodzi jeden PSO tworzony leniwie i przechowywany. Nie dodaje dispatchy,
tekstur, kopii, barier, timestampów ani alokacji w każdej klatce. Root signature,
deskryptory, constant buffer i grupy 8×8 są te same. Nowy cso ma 4680 bajtów
wobec 328648 bajtów głównego shadera. Nie zawiera LDS; główny shader ma
trzy tablice po 256 floatów dla nieużywanych w encode redukcji.
Nie zmierzono osobno zajętości SM/rejestrów; redukcja bytecode/LDS to cechy
wariantu, a nie ilościowe przypisanie przyczyny zysku.

## Wybór i fallback

`EncodeInput` oznacza wejście flagą Superior tylko przy Enlargement=11.
Wspólny helper `SuperiorScaling::InputEncode` wymaga Mode 0, dokładnych flag,
Passthrough 0/1, mapowania 0–4, dodatnich wymiarów oraz identycznych rozmiarów
źródła i dispatchu. Większa alokacja niż aktywny region, niewłaściwe metadane
lub inne Enlargement zachowują główny shader. Ekspozycja i anchory są nadal
dynamiczne. Wybór nie zależy od ustalonego procentu skali ani od autotuningu.

Przy błędzie tworzenia PSO Mode 0 pozostaje na głównym shaderze.
Istotne: nigdy nie używa wtedy normal PSO Mode 1. Fallback do normal v50
pozostaje ograniczony do dotychczasowych specjalizacji końcowego resolve.
Przechowywanie i zwalnianie nowego PSO korzysta z istniejącej tablicy i czasu
życia właściciela. Pierwsze tworzenie PSO jest poza pomiarem rozgrzanego toru.

## Zgodność obrazu

Na RTX 4090: **3840/3840 przypadków zgodnych bitowo**, 66528000 słów 32-bitowych.
Porównywane są niezależnie trzy obrazy: natywne proxy, zachowana kopia HDR
i wynik pomniejszenia Area. Ten ostatni jest liczony z materializowanego proxy,
z zachowaniem FP16/FP32 zapisu i użyciem niezmienionego głównego shadera Area.
Fixture wykonuje produkcyjny helper wyboru encode na testowanych metadanych.

Macierz obejmuje sześć geometrii (1×1/native, nieparzyste wymiary, redukcja
obu lub jednej osi), źródło FP16/FP32 i niezależnie wyjście FP16/FP32,
SDR i pięć HDR mapping. Sprawdza ręczny white point, ekspozycję auto/game,
trim, osiem anchorów, mały white point oraz zwykłe, małe, duże i niefinitywne
dane: signed RGB, NaN, ±Inf, -0, subnormale, max-float i alpha.
Kwantyzacja danych wejściowych do FP16 służy tylko budowie fixture;
produkcyjny shader zachowuje odczyt oryginalnej tekstury gry.

Test C++ polityki sprawdza prawidłowe SDR/HDR, pozostałe etapy, flagi legacy
i dodatkowe flagi, niewłaściwy Passthrough/mapowanie, większy/mniejszy rozmiar
źródła, zerowe wymiary i poprawne 1×1. Test Python sprawdza oznaczenie etapu,
wybór PSO i ograniczenie fallbacku do właściwego Mode. Kontrole NR,
interpassów i benchmarku przechodzą. DXC kompiluje 15 wariantów; wszystkie
dotychczasowe 14 cso są binarnie niezmienione. Nowy encode SHA256:
`0365ED0809CF5BB76A3532ACA410508C1D5FB4C99F035772AD56D56F35B27F53`.

Pełna DLL wymaga v143 z workflow; lokalny v142 kompiluje fixture C++17.
Workflow kompiluje DLL oraz nowe fixture. Nie wykonano walidacji gry,
inferencji NGX ani wszystkich GPU/sterowników. Zgodność bitowa dotyczy
opisanych przypadków na RTX 4090.

## Pomiar GPU: v51 → v52

RTX 4090, NVIDIA 616.56, 2026-10-19. Syntetyczna nieruchoma scena sinusoidalna
z signed RGB i alpha .7, native 3840×2160; work P50=1920×1080,
P59=2266×1274, P65=2496×1404. W tabelach źródło, proxy, kopia HDR i wynik
Area są FP16; scalar ekspozycji to FP32 .5. HDR mapping=0 (soft knee),
WhitePoint=2, PreExposure=1, ExposureMode=3, Trim=1, anchory wyłączone.
SDR używa Passthrough=1. A/K, modele i passy nie uczestniczą w tym etapie.

Mierzono osobno: stage 0 = encode obu obrazów natywnych; stage 1 = samo
niezmienione Area na identycznych, wcześniej przygotowanych proxy (kontrola);
stage 2 = **encode + Area razem**. Timestampy obejmują przejścia zapisanych
obrazów do SRV i przywrócenie do UAV na kolejną sekwencję. Natywne źródło
pozostaje SRV; przejścia stanu oryginalnej tekstury gry są poza fixture.
Zasoby/PSO są rezydentne; upload, alokacje, fence CPU i pierwsza kompilacja
PSO są poza timestampami. Nie mierzono ekspozycji wyliczanej przed encode,
guides, NR, interpassów, carriera, NGX/DLAA ani końcowego low/resolve.

Osiem krótkich bloków, naprzemiennie ABBA/BAAB. Każde okno ma 32 próbki
rozgrzewki i 20 zapisanych próbek; 32 okna, 320 próbek na wariant/scenariusz.
Jedna próbka obejmuje cztery sekwencje, czas dzielony przez cztery.
Powtórzono pełną serię z zamienionymi zestawami buforów A/B, aby sprawdzić
wpływ adresów zasobów. Osobna kontrola A/A używa głównego shadera po obu
stronach, ale zachowuje oddzielne zestawy zasobów. GPU pracuje z dynamicznymi
zegarami; na początku odczyt wynosił 4%, 1514 MiB, 27.04 W.

Mediany **powtórzenia z zamianą buforów**, ms:

| Profil | Skala | Encode v51 → v52 | Encode + Area v51 → v52 | Zysk całego przygotowania | Sparowana delta / dodatnie bloki |
|---|---:|---:|---:|---:|---:|
| HDR soft knee | 50 | .188672 → .138240 | .276224 → .196864 | .079360 | .079488 / 8/8 |
| HDR soft knee | 59 | .188160 → .137728 | .312832 → .232192 | .080640 | .080064 / 8/8 |
| HDR soft knee | 65 | .188416 → .137984 | .333952 → .249344 | .084608 | .081920 / 8/8 |
| SDR | 50 | .159232 → .146944 | .253440 → .197888 | .055552 | .055776 / 8/8 |
| SDR | 59 | .159232 → .147584 | .288512 → .232704 | .055808 | .055648 / 8/8 |
| SDR | 65 | .159232 → .147072 | .307968 → .249088 | .058880 | .058304 / 8/8 |

Sparowana delta to mediana ośmiu różnic: średnia dwóch median okien A
minus średnia dwóch median okien B w danym bloku. Pierwsza seria daje zysk
encode+Area HDR .078848/.079232/.081152 ms i SDR .054272/.055296/.056192 ms
dla P50/P59/P65. Samo encode jest natywne 4K przy każdej skali; powtórzenia
sprawdzają ten sam etap w kontekście różnych alokacji części roboczej.

Rozrzut encode+Area w powtórzeniu, ms:

| Profil / skala | p10–p90 v51 | p10–p90 v52 | Odch. std. v51/v52 |
|---|---:|---:|---:|
| HDR 50 | .274176–.392448 | .194304–.293376 | .050728/.041286 |
| HDR 59 | .310016–.431616 | .229376–.335616 | .059233/.044090 |
| HDR 65 | .328960–.449280 | .246528–.358400 | .053085/.047986 |
| SDR 50 | .249088–.365568 | .193280–.290048 | .050512/.041687 |
| SDR 59 | .285184–.404736 | .228864–.327424 | .052793/.046773 |
| SDR 65 | .304128–.424960 | .246016–.350976 | .053948/.045918 |

Kontrola Area ma identyczne mediany A/B: P50 .067328, P59 .097536,
P65 .114432 ms dla obu profili w powtórzeniu. Kontrola całego encode+Area A/A
ma różnice zbiorczych median A−B HDR .000512/-.001408/-.000128 ms i SDR
-.000256/-.000256/-.000256 ms. Sparowane delty A/A mają moduł poniżej .0006 ms.
Nie tłumaczą obserwowanego zysku nowego encode. Pojedyncze próbki mają duży
rozrzut i sporadyczne skoki; dlatego wnioski oparto też na sparowanych blokach.

Różnica całego przygotowania nie musi być sumą izolowanych etapów: kolejność,
zmiana PSO i stan cache wpływają na wynik. Nie dodawać tej delty do wyników
v50/v51 z innych sesji ani nie przedstawiać jej jako oszczędności całej klatki.
Pełny tor NR w grze pozostaje niezmierzony. Wstępny osobny pomiar ze źródłem
FP32 również wskazywał zysk, ale nie jest podstawą tabel FP16 powyżej.

[Surowe próbki obu serii i A/A](NR-v52-superior-input-GPU.samples.csv)
zawierają stage 0/1/2, profil, format źródła i etykietę serii. W `control_aa`
etykiety v51/v52 oznaczają pozycje A/B; oba PSO są identyczne.

## Odrzucone Area i odtworzenie

Specjalizacja Area zmieniała wynik FP32 o kilka ULP: dla native 17×9,
work 9×5, HDR soft knee, ręcznego white point i zwykłych danych pierwsza
różnica wyniosła `3eeec396 → 3eeec398`. Dotyczyło to zarówno stałego
filtra Area, jak i wariantu ze stałym tylko Mode 2. Nie włączono tych PSO.
Nie pominięto materializacji ani kwantyzacji proxy. Kod i binary Area pozostały
bez zmian; jedyna modyfikacja HLSL produkcyjnego to definicja Mode 0 dla encode.

1. Zachować `DlssNr_Shader.cso` z v51
   `e505a07acbf845d7eb96946ca1d2f1281b101e15` w katalogu baseline.
2. Uruchomić `tools/compile_nr_interpass.ps1`.
3. Skompilować fixture `superior_input_quality.cpp` i `superior_input_timing.cpp`
   w MSVC x64 `/std:c++17 /EHsc /O2`, linkując `d3d12.lib dxgi.lib`.
4. `superior_input_quality.exe baseline OptiScaler/shaders/dlssnr/precompile`.
5. `superior_input_timing.exe baseline OptiScaler/shaders/dlssnr/precompile wynik.csv normal fp16`.
6. Powtórzyć z `swap fp16`, potem kontrolę `aa fp16`. Opcja `fp32` pozwala
   sprawdzić drugi format źródła, przy tych samych wyjściach FP16.
7. Pełny zysk NR zmierzyć osobno w grze, z identycznymi passami, presetem A/K,
   skalą, HDR i sceną, rozgrzewką, ABBA i timestampami całego toru.
