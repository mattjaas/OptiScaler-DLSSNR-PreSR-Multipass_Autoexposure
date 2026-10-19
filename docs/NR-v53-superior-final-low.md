# NR v53 — dokładny końcowy filtr low w Superior Scaling

Końcowe pole low frequency (Mode 18, zdekodowany signed residual) ma osobny,
leniwie tworzony i zapamiętywany PSO z grupami 8×4. Pozostają te same 64
biliniowe próbki, współrzędne, wagi, kolejność sumowania, sanitizacja oraz
zapis FP16/FP32. Nie zmieniono gainów, średniej low po passach, harmonogramu
interpassów, modeli, Structure/Intensity ani HDR. Low36/cache w interpassie
pozostaje niezmieniony, podobnie jak optymalizacje v50/v51/v52.

Nie dochodzi dispatch, tekstura, kopia, bariera, pomiar ani alokacja w każdej
klatce. Dochodzi jeden PSO; gotowy shader zajmuje 10148 bajtów i nie używa LDS.
Ogólny shader ma 328648 bajtów i zachowuje redukcje LDS nieużywane w Mode 18.
Nie zmierzono bezpośrednio zajętości SM ani rejestrów; mniejszy bytecode nie
jest samodzielnym dowodem ani ilościowym wyjaśnieniem zysku.

## Selekcja i fallback

Tylko końcowe low w Superior dostaje flagę 65536. Produkcyjny helper
`SuperiorScaling::LowResidual` wymaga Mode 18, Transfer 1, dokładnych flag,
niezerowego źródła i rozmiaru wyjścia ceil(work/8). Metadane spoza tego
kontraktu, inne Enlargement oraz interpassy zachowują dotychczasowy tor.
Shader nadal sprawdza zgodność wymiarów source/model i zwraca zerowy residual
przy niezgodności. Nie zakłada na stałe rozdzielczości ani formatu.

Wyłącznie poprawnie utworzony nowy PSO ma dispatch 8×4. Błąd utworzenia wraca
do ogólnego Mode 18 z dispatch 8×8, nigdy do resolve Mode 1. Destruktor
zwalnia PSO przez istniejącą tablicę. Pozostałe etapy nadal używają 8×8.
Stałe, descriptor table i root signature nie zmieniają ABI.

## Weryfikacja obrazu i kompilacji

RTX 4090: **192/192 porównań bitowo zgodnych, 6411840 słów 32-bitowych**:

- 144 przypadki małych/nieparzystych rozmiarów, 1×1, jednej osi i częściowych
  grup; FP16/FP32 źródło niezależnie od FP16/FP32 celu, signed RGB, alpha,
  dane bardzo małe/duże oraz NaN/Inf/subnormale.
- 48 przypadków przy native 3840×2160: work 1920×1080, 2266×1274,
  2496×1404, z tymi samymi czterema kombinacjami formatów i wzorcami danych.

Porównywane jest całe pole low wraz z alpha. Końcowy resolve korzysta
z niezmienionych binariów i tych samych zasobów; bitowo identyczne pole
low jest jedynym zmienianym wejściem tego etapu. To walidacja opisanej
macierzy na jednym GPU/sterowniku, nie gwarancja wszystkich gier i urządzeń.

C++ test polityki sprawdza tryby, Transfer, flagi, zerowe wymiary i granice
ceil(work/8). Python sprawdza oznaczenie końcowego etapu, dobór PSO i
geometrię fallbacku; zachowano testy interpassów i benchmarku.
DXC kompiluje 16 wariantów bieżącego workflow. Wszystkie 17 wcześniej
śledzonych plików cso (włącznie z innymi shaderami) pozostają binarnie takie
same. Nowy low SHA256:
`670e8983eab98ba1a9e6808d0f9d71c8ef3048d643e89c53e4a6f7777ff3ea67`.

Lokalny MSVC v142 kompiluje fixture C++17 i test polityki; pełna DLL wymaga
v143 z GitHub Actions. Workflow kompiluje DLL i nowe fixture, publikuje ZIP
oraz osobny `OptiScaler.dll`. Nie wykonano pełnego pomiaru NR w grze.

## Pomiar GPU: v52 → v53

RTX 4090, NVIDIA 616.56, 2026-10-19. Syntetyczna nieruchoma scena sinusoidalna,
native 3840×2160. Work P50=1920×1080, P59=2266×1274, P65=2496×1404.
Proxy i oryginał FP32; zdekodowany residual, pole low i wynik FP16.
Residual powstał przed pomiarem z niezmienionego kodowania/dekodowania carriera.
Guided radius 1, range sigma .015, spatial sigma 3, strength 1, high=.8,
low=.661, WhitePoint=1, TransferStrength/ColourStrength=1, MaxRatio=2,
MaxDarkening=100, skin/environment strength=1; inspector i przywracanie
natywnego detalu wyłączone. Gainy są celowo równe w obu wariantach przy
każdej skali; nie jest to pomiar przeliczania gainów od rozdzielczości.

Timestampy obejmują osobno low oraz **low + końcowy resolve z barierami**.
To fragment całego NR. Poza pomiarem: encode wejścia, Area, ekspozycja,
guides, modele NR, interpassy, DLAA, carrier, upload, alokacje, fence CPU
oraz pierwsze tworzenie PSO. A/K i liczba passów nie uczestniczą w tym teście.
Zasoby rezydentne, te same obiekty dla A/B, bez różnic ich umiejscowienia.

Osiem bloków, naprzemiennie ABBA/BAAB. Każda czwórka jest nagrywana razem
w jednym command list, aby ograniczyć dryf między CPU submissions. Blok ma
16 czwórek rozgrzewki i 40 czwórek pomiarowych: **640 próbek na wariant,
skalę i etap/profil**. Jedna próbka mierzy cztery sekwencje, wynik dzielony
przez cztery. Wykonano pełne powtórzenie i identyczny test A/A z tym samym
ogólnym shaderem i dispatch 8×8 po obu stronach. Zegary dynamiczne; przed
pracą GPU miało 3% użycia / 210 MHz / 27.03 W. W trakcie fixture osiągało
99%, 1980–2130 MHz i około 270–274 W.

Mediany **drugiej pełnej serii**, ms, etap low + resolve:

| Profil | Skala | v52 | v53 | Oszczędność median | Sparowana delta bloków | Dodatnie bloki |
|---|---:|---:|---:|---:|---:|---:|
| sdr | 50 | 0.343552 | 0.333568 | 0.009984 | 0.009376 | 8/8 |
| sdr | 59 | 0.360960 | 0.348928 | 0.012032 | 0.012128 | 8/8 |
| sdr | 65 | 0.384256 | 0.371072 | 0.013184 | 0.010816 | 8/8 |
| hdr0 | 50 | 0.391552 | 0.382976 | 0.008576 | 0.009600 | 8/8 |
| hdr0 | 59 | 0.408960 | 0.397440 | 0.011520 | 0.010592 | 8/8 |
| hdr0 | 65 | 0.430080 | 0.420352 | 0.009728 | 0.011456 | 8/8 |
| hdr2 | 50 | 0.335360 | 0.327168 | 0.008192 | 0.009216 | 8/8 |
| hdr2 | 59 | 0.352128 | 0.342784 | 0.009344 | 0.010944 | 7/8 |
| hdr2 | 65 | 0.360704 | 0.347776 | 0.012928 | 0.010688 | 8/8 |
| hdr4 | 50 | 0.332288 | 0.324352 | 0.007936 | 0.008672 | 8/8 |
| hdr4 | 59 | 0.345856 | 0.337920 | 0.007936 | 0.010144 | 8/8 |
| hdr4 | 65 | 0.365952 | 0.354304 | 0.011648 | 0.010560 | 8/8 |

Sparowana delta to mediana ośmiu różnic: średnia median okien A minus
średnia median okien B, osobno w każdym bloku. W pierwszej pełnej serii
wszystkie 12 kombinacji profilu/skali miało zysk w 8/8 bloków; mediany
oszczędności całego low+resolve wyniosły .007808–.014080 ms.
Nie należy dodawać tego zysku do wyników v50/v51/v52 z innych sesji.

Samo low w powtórzeniu (nie sumować osobno z low+resolve):

| Skala | v52 | v53 | Oszczędność median | Sparowana delta |
|---|---:|---:|---:|---:|
| 50 | 0.039680 | 0.024064 | 0.015616 | 0.015520 |
| 59 | 0.023296 | 0.016384 | 0.006912 | 0.006912 |
| 65 | 0.033792 | 0.026112 | 0.007680 | 0.007872 |

Rozrzut pojedynczych próbek low+resolve w powtórzeniu (ms):

| Profil / skala | p10–p90 v52 | p10–p90 v53 | Odch. std. v52/v53 |
|---|---:|---:|---:|
| sdr / 65 | 0.349184–0.427520 | 0.337920–0.416768 | 0.044976/0.044287 |
| hdr0 / 65 | 0.390400–0.496128 | 0.378368–0.472832 | 0.052629/0.049891 |
| hdr2 / 65 | 0.340736–0.403430 | 0.330240–0.393651 | 0.041097/0.039942 |
| hdr4 / 65 | 0.336384–0.402944 | 0.325632–0.394112 | 0.045907/0.041651 |

W kontroli A/A maksymalna bezwzględna różnica median całego low+resolve
wyniosła 0.004608 ms, a maksymalna bezwzględna sparowana delta bloków
0.002208 ms. Jest mniejsza niż obserwowany zysk, ale duży rozrzut
pojedynczych próbek wymaga porównań bloków i powtórzenia. Bez kontroli A/A
sam pojedynczy wynik całego etapu byłby niewystarczający.

## Odrzucone warianty i odtworzenie

Sama izolacja Mode 18 z grupami 8×8 dała regresję samego low przy 59/65%.
Sprawdzono też dynamiczny Transfer, zmianę rozwinięcia pętli oraz grupy
16×4, 32×2, 16×8 i 16×16. Nie uzyskały przekonującego wyniku na wszystkich
trzech skalach. Wybrany 8×4 zachowuje oryginalnie rozwinięte pętle.
Wczesne pomiary w oddzielnych command list miały niespójne wyniki całego
low+resolve. Dopiero przeplatane czwórki z kontrolą A/A ograniczyły wpływ
kolejności i dryfu; wcześniejszych wyników nie użyto do oceny zysku.
Nie zmieniono automatu Classic/RGB20 ani reguł interpassu.

[Surowe próbki obu pełnych serii i kontroli A/A](NR-v53-superior-low-GPU.samples.csv).

1. Zachować ogólny `DlssNr_Shader.cso` z v52
   `51c1396c46266904bb5bcb93fac520463df5d5e3` w katalogu baseline.
2. Uruchomić `tools/compile_nr_interpass.ps1`.
3. W MSVC x64 `/std:c++17 /EHsc /O2` skompilować `superior_policy.cpp`,
   `superior_low_quality.cpp` i `superior_low_timing.cpp`; fixture GPU
   linkują `d3d12.lib dxgi.lib`.
4. Uruchomić test polityki, potem `superior_low_quality.exe baseline candidate`
   oraz ten sam test z `--4k`.
5. Przy wolnym GPU uruchomić `superior_low_timing.exe baseline candidate wynik.csv normal`,
   powtórzyć, następnie wykonać `superior_low_timing.exe baseline candidate kontrola.csv aa`.
6. Cały NR porównać osobno w grze v52/v53, w tej samej scenie, z tymi samymi
   ustawieniami, po rozgrzewce i w kolejności ABBA. Mały zysk tego etapu może
   być trudny do odróżnienia w odczycie całego NR.
