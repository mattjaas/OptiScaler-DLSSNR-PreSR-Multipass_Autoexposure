# Dalsze próby optymalizacji po v58 — 2026-10-20

Baza: `c241e38477578ec38af230ce11a441539f975d41`, RTX 4090,
NVIDIA 616.56, D3D12. **Do v59 trafił wyłącznie większy kafel FP32**,
opisany w [raporcie v59](NR-v59-superior-tall-low.md). Poniższe próby
pozostają offline; nie dodają ścieżek ani diagnostyki do DLL.

## Współdzielone sumy poziome low

Cache 36×36 zachowuje 1296 próbek. Dodatkowe 288 sum poziomych 8-tapowych
jest liczone raz i współdzielone przed pionowym filtrem. Dochodzi 3456 B
LDS i jedna synchronizacja. Pozostają wszystkie wagi, ale zmienia się
kolejność dodawania. Nie jest to identyczna arytmetyka FP32.

198 porównań / 54 648 słów: wynik FP32 ma max błąd znormalizowany
7,49826e-5, ponad dotychczasowy limit 1e-5. Wynik FP16 zmienia 3 z 24 852
skończonych kanałów; max błąd 0,000488281. Klasy niefinitywne i alpha pasują.
Pilot low+resolve P65 jest wolniejszy o około 0,0033 ms we wszystkich
czterech profilach, przy IQR 0,0023–0,0036 ms. P50/P59 są blisko zera.
Nie ma uzasadnienia do wdrożenia.

## Area

`area_unroll` rozwija małe footprinty do maksymalnie 3×3, zachowując
oryginalne współrzędne, wagi i kolejność dodawania. Większe footprinty
korzystają z wcześniejszej pętli. `area_guard` ma cztery bezpośrednie
odczyty przy dokładnym 2:1, ale dopiero po sprawdzeniu rzeczywistych
zmiennoprzecinkowych krawędzi boxa. Odczyt alpha zachowuje oryginalny wzór.

Oba przeszły **5120 przypadków / 12 355 200 słów bez różnic bitowych**.
Sprawdzono osiem geometrii, niezależnie FP16/FP32 źródło i wynik,
SDR/pięć mappingów HDR, ekspozycję, anchory, trim, stress i NaN.
Przybliżenie bez sprawdzania krawędzi zostało odrzucone wcześniej:
zaokrąglenie granicy przy nominalnym 2:1 może zmienić zasięg odczytów
i propagację NaN, mimo matematycznie całkowitych współrzędnych.

Pilot samego Area daje zwykle 0–0,0008 ms różnicy; encode+Area ma wyniki
mieszane, około -0,0022 do +0,0022 ms. Nie potwierdza to przewagi całego
przygotowania. HDR0/P50 pierwszej serii `area_unroll` ma duży IQR
0,059456 ms; nie jest dowodem zysku. Nie wdrożono selektora P50.

## Redukcja ekspozycji przez wave

`wave64` zachowuje grupę 8×8, sumuje wewnątrz wave, zapisuje wkład tylko
u liderów, synchronizuje grupę i zbiera 64 wpisy w wątku 0.
`wave32` używa 8×4 i szybkiej sumy przy dokładnie 32 aktywnych lane;
pozostały przypadek ma grupową redukcję w LDS. Są to próby SM6.0 WaveOps.
[Microsoft określa kolejność działań WaveActiveSum jako niezdefiniowaną](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/waveallsum),
więc nie zakładamy uniwersalnej zgodności bitowej ani konkretnego układu lane.

`wave64`: 2880 przypadków / 217 294 560 słów ma zero różnic w sprawdzonym
małym zestawie meter/white/proxy/HDR/Area. Nie jest to test wszystkich
rozdzielczości. Pilot pełnego przygotowania jest blisko zera:
-0,00032 do +0,00032 ms dla źródła FP16, IQR około 0,003–0,005 ms.

`wave32`: 24 przypadki 4K / 1 395 594 336 słów. Meter ma max błąd
1,78814e-7, ale white/proxy/HDR/Area pozostają identyczne w tym zestawie.
Mimo ograniczenia synchronizacji pełne przygotowanie jest wolniejsze
o **0,0178–0,0189 ms**, IQR 0,0034–0,0072 ms. Oba warianty odrzucono.
Źródło FP32 w tych testach zachowuje identyczną ogólną ścieżkę po obu
stronach i stanowi kontrolę; jego zmienność nie jest zyskiem WaveOps.

## Wielomian zamiast potęgowania sRGB

Próby dotyczą wyłącznie dekodowania w końcowym normalnym HDR soft knee,
z zachowaniem saturate, liniowego segmentu czerni i dokładnego końca 1.
Współczynniki dopasowano offline do segmentu 0,04045..1 metodą Chebysheva,
następnie oceniano Hornerem FP32. Przypięte patche zawierają gotowe
współczynniki; odtworzenie nie wymaga NumPy ani ponownego dopasowania.

Stopnie 7 i 9 przekroczyły limit FP32 1e-5 w resolve. Przesunięty stopień
13 również dał max błąd znormalizowany 1,43033e-5. Sam stopień wielomianu
nie gwarantuje mniejszego błędu przy skończonej precyzji i dalszej kompozycji.

Przesunięty stopień 11 przeszedł **7776 przypadków / 43 830 720 słów**:
FP32 max normalized 4,64916e-6; FP16 0,00097561. To test liczbowy,
nie gwarancja percepcyjna. Pilot low+resolve jest wolniejszy:
P50/P59/P65 o 0,004224/0,003712/0,003072 ms, IQR
0,006016/0,002208/0,003680 ms. A/A jest blisko zera. Nie wdrożono.

## Metoda i odtworzenie

Pomiary są pilotażowe: native 3840×2160, P50/P59/P65, rezydentne dane
sinusoidalne. Low/resolve: SDR i HDR 0/2/4, Standard 0,8/0,661,
guide radius 1, range 0,015, spatial 3, strength 1. Ekspozycja:
protection 0/100, source FP16 oraz kontrolny FP32. Area: HDR0 i SDR,
osobno Area oraz encode+Area. Dokładne A/B, IQR i wszystkie parametry
grupowania są w [surowych CSV i summary](measurements/post-v58).

Warianty mają 2 bloki ABBA/BAAB po 20 kwartetów, czyli 40 kwartetów /
80 próbek na stronę i konfigurację. Low, resolve i ekspozycja zapisują
cały kwartet na jednej command liście; Area zachowuje starszy schemat
osobnych okien z rozgrzewką 16 próbek przed każdym oknem. Dlatego
nie należy bezpośrednio porównywać rozrzutów tych fixture.
Wczesne pomiary zakłócone przez Bubbles odrzucono. Wygaszacz zamknięto
przed końcowymi powtórkami i sprawdzono jego brak po serii kontrolnej.
Nie ma pomiaru całego NR ani inferencji NGX nad zmienionym wejściem.

`python tools/audits/post_v58/prepare.py` odtwarza dziesięć shaderów
i dziesięć fixture w `artifacts/nr-post-v58`. Bazę i zależne nagłówki
przypina do v58. Wszystkie odtworzone CSO są bajtowo identyczne z badanymi.
Pliki produkcyjne nie są edytowane. Fixture kompiluje się MSVC C++17,
z include `artifacts/nr-post-v58/include/tests/nr_gpu` i bibliotekami
`d3d12.lib dxgi.lib`. Patche zachowują pierwszy include `pch.h`.
Argumenty są w komunikatach Usage; `low_timing`/`tall_timing` używają
historycznej etykiety `half8-v-float8` dla bazowej grupy 8×8.

`low_quality ... float8` celowo zgłasza przekroczenie budżetu dla `row`;
`tall_quality ... float8 [--4k]` sprawdza ścisłą zgodność większego kafla.
`poly_quality ... normal-pair` sprawdza wyłącznie HDR soft knee.
`summarize.py <csv...> --out summary.csv` liczy mediany różnic parowanych
i IQR inclusive, sprawdzając kompletność kwartetów.
