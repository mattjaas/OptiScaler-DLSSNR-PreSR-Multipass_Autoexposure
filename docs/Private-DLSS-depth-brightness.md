# Oryginalne guides i wycofane transformacje transportu

Aktualizacja 2026-11-04: usunięto z UI, konfiguracji i shaderów nasycenie editu, kolorowanie głębi, mnożenie dodatniego editu według głębi oraz addytywne rozjaśnianie bliskich obszarów. Dawne klucze INI/profili są ignorowane, a przy zapisie INI usuwane. Nie blokują automatu mnożnika i nie zmieniają historii DLSS. Jednolity **Positive NR edit transport multiplier** pozostaje, włącznie z ręcznymi wartościami poniżej 1×.

Głębia i MV nadal służą NR, rekonstrukcji guides, detekcji ruchu, kontrastu granic i maskowaniu dysokluzji. Eksperyment **Original depth and MV for private DLSS (experimental)** pozostaje osobną opcją.

## Eksperyment z oryginalnymi depth/MV

Dotychczasowe testy tej gałęzi badały rekonstrukcję i łączenie guides, nie podawanie oryginalnych wskaźników do prywatnego NGX. Nowa opcja przekazuje `ReadableGuide` o oryginalnych wymiarach (dla nieczytelnego formatu może to być istniejący klon tej samej wielkości). Nie tworzy dodatkowego przeskalowanego MV dla NGX: carrier używa wtedy mode 37, bez zapisu converted MV z mode 39. NR i wykrywanie dysokluzji nadal korzystają z dopasowanych guides, więc nie jest to usunięcie całego kosztu dopasowania w torze.

MVScale konwertuje jednostki/direction gry do pikseli prywatnego wejścia; nie zmienia współrzędnych próbkowania tekstury. Zachowujemy niezależne origins depth i MV. Flaga MVLowRes pochodzi z opisu źródła i jest częścią tożsamości instancji, niezależnie od IsHDR, AutoExposure i DepthInverted. Zmiana przełącznika odtwarza prywatną instancję przez dotychczasowe odroczone zwalnianie; shutdown nie pozostawia latcha sesji.

[Podręcznik NVIDIA DLSS, sekcje 3.6.2–3.6.3](https://raw.githubusercontent.com/NVIDIA/DLSS/main/doc/DLSS_Programming_Guide_Release.pdf) rozróżnia motion w rozdzielczości render/output oraz konwersję MVScale. Prywatne DLAA ma render=output na swojej siatce. NGX nie udostępnia tu niezależnych wymiarów wejścia głębi/MV, dlatego **nie ma potwierdzenia poprawnego mapowania większych buforów**: runtime może je odrzucić albo czytać jako wycinek, zamiast użyć całego obrazu. To test jakości i kompatybilności, nie potwierdzona poprawa.

Przed przekazaniem sprawdzamy rzeczywistą alokację, aktywne regiony oraz skończone, niezerowe MVScale. Nie podajemy regionów mniejszych od prywatnego wejścia (NGX może odczytać poza aktywną częścią), nie używamy tego trybu przy nieliniowej kompresji ani Hold. W tych przypadkach używane są matched guides. Zmiana faktycznej ścieżki raw/matched resetuje historię. Odrzucenie evaluate przez NGX zachowuje bieżący obraz gry i zatrzaskuje matched fallback do ponownego off/on lub odtworzenia generacji. Sam sukces evaluate nie dowodzi zgodności przestrzennej. W UI zawsze sprawdź status.

Nie uruchomiono tej ścieżki z modelem DLSS w grze. WARP nie wykonuje NGX. Testy CPU sprawdzają granice regionów i konwersję jednostek; testy shaderów nie walidują interpretacji większych buforów przez zamknięty runtime.

## Zrzuty i konfiguracja

**Save pre-DLSS edit** zapisuje signed FP32 edit po jednolitym dodatnim mnożniku, przed offsetem 0.5 i clippingiem. Pakiet z guides dodaje depth/MV; edit-only nie dodaje głębi dla wycofanych transformacji. JSON zachowuje dawne pola z wartościami neutralnymi dla kompatybilności narzędzi. `positive_gain` jest jednolity na całym obrazie.

Przy original guides pakiet z guides jest niedostępny: writer nie zapisuje dowolnych oryginalnych formatów/rozmiarów NGX. Edit-only pozostaje dostępny. `SuperiorOriginalGuides` nadal jest odczytywany, zapisywany i obsługiwany przez profile.

Poniższe wyniki są archiwalne: nie opisują funkcji dostępnych w aktualnej wersji.

## Historyczne pomiary wycofanych opcji

Lokalnie: testy CPU policy/ABI/profili z aktualnym Config.cpp, serialization JSON/DDS, rebuild 55 blobów i byte identity v59. Test `private_exposure_quality` na WARP i RTX 4090: **1498 przypadków** względem niezależnych double-precision referencji; w tym gain/bias osobno i razem, pełny NR answer, saturation/colour, clipping, zero/ujemny edit, reversed Z, niepoprawna głębia oraz FP16/FP32 i zrzut przed bias. Domyślna ścieżka zachowała bitową zgodność z zachowanym blobem sprzed zmiany w 48 przypadkach. `private_guides_quality --warp`: **768 przypadków / 258755520 słów**, łączony przebieg kontra osobna rekonstrukcja+carrier, wariant quantized, niezależne origins, nietypowe rozmiary, skrajne/NaN dane i Hold. To dodatkowe porównanie wariantów; niezależną referencję matematyki transportu zapewnia pierwszy test.

RTX 4090, proceduralne rezydentne proxy/answer FP16, depth R32F; gain 16, saturation 1, depth colour off, bias 0,25, distribution 0,25. Znaczniki GPU obejmują encode + decode i ich bariery; osobne próbki z zewnętrznym exposure obejmują także metering/reduce. 1920×1080 i 3840×2160, po 64 rozgrzewki na wariant, 4×64 prób ABBA, po 128 próbek A/B. A: zapisany shader z 66e853bc, B: nowy; parametry maski ignorowane przez A. Brak uploadów/alokacji w oknie. **Bez NR, NGX, final composition i gry**; nie jest to pomiar całego toru ani potwierdzenie jakości.

Dla natywnego AutoExposure (w oknie tylko transport), mediany ms:

| Nowy tryb B | 1080p A → B | 4K A → B | Różnica 4K |
|---|---:|---:|---:|
| Oba wyłączone | 0,034816 → 0,034816 | 0,187904 → 0,186368 | −0,001536 |
| Depth gain, cały zakres | 0,031744 → 0,032768 | 0,185344 → 0,256000 | +0,070656 |
| Bias, cały zakres | 0,031744 → 0,032768 | 0,185344 → 0,258048 | +0,072704 |
| Gain+bias, cały zakres | 0,031744 → 0,032768 | 0,184320 → 0,258048 | +0,073728 |
| Gain+bias, ograniczony zakres | 0,031744 → 0,032768 | 0,186368 → 0,258048 | +0,071680 |

Rozrzut odchylenia standardowego 4K: A 0,0100..0,0237 ms, B 0,0249..0,0344 ms. Małe różnice Off są w obrębie rozrzutu; nie deklarujemy przyspieszenia. [Surowe próbki](validation/private-depth-transport-timing.csv) i [pełne podsumowanie](validation/private-depth-transport-timing.json) obejmują również obie metody external exposure. Nie zmierzono osobno nowej arytmetyki w fused packed wariancie ani wpływu original-guides na NGX. Weryfikacja gry: rozgrzane A/B/ABBA tej samej sceny/ruchu z GPU timestamp całego toru, to samo GPU/rozdzielczość/preset, osobno każdy tryb i kombinacje; zapisać ghosting i stabilność obok median/zmienności. Żadna z tych funkcji nie gwarantuje usunięcia ghostingu.

Aktualizacja: [trzy rodziny modeli, osobne cele i cykliczny reset przy niskim MV](NR-private-models-low-motion.md).
