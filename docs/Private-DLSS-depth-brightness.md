# Transport jasności według głębi i oryginalne guides prywatnego DLAA

Opcje dotyczą D3D12 Superior Scaling z prywatnym DLSS/DLAA. Domyślnie są wyłączone. Głębia jest względna 0–1, nie w metrach. Dwa tryby jasności mogą działać osobno, razem oraz z dotychczasowym mnożnikiem, saturation, depth colour i pełną odpowiedzią NR. Nie zmieniają ekspozycji gry.

## Kontrolki

- **Depth-weighted positive edit multiplier** zastępuje jednolity mnożnik dodatniego editu maską głębi. Główny **Positive NR edit transport multiplier** określa maksimum blisko kamery (co najmniej 1× w tym trybie), w oddali mnożnik zmierza do 1×. Ujemny edit pozostaje bez mnożenia. Po DLSS dzielimy dodatnią część przez ten sam przestrzenny mnożnik.
- **Add near-camera brightness before DLSS** dodaje offset do całego nośnika, także miejsc neutralnych i przyciemnianych. **Near-camera brightness strength**: 0..4, **Exact near-camera brightness**: 0..64. Domyślnie 0,1. Po DLSS offset jest odejmowany przed odwracaniem pozostałych transformacji.
- **Depth brightness across full range**: płynna maska na całej głębi. Po wyłączeniu **Full strength until relative depth** określa koniec pełnej mocy, a **Fade out by relative depth** jej wygaśnięcie. Granice 0..1, minimalny odstęp 0,0001; 0 oznacza blisko, 1 daleko po przekształceniu rozkładu.
- **Depth brightness distribution**: 0,03125..64, domyślnie 0,25. Przekształca perspektywiczną głębię przed testem zakresu; nie jest liniową odległością. Maska i zakres są wspólne dla obu opcji.
- **Original depth and MV for private DLSS (experimental)**: osobny eksperyment przekazujący oryginalne czytelne tekstury depth/MV do NGX. UI pokazuje rozdzielczości i ewentualny fallback. Kontrolka włączenia jest wyszarzona podczas aktywnego Compress screen edges; aktywny przełącznik można zawsze wyłączyć.

Auto i podgląd mnożnika wstrzymują pomiar przy aktywnych nowych trybach jasności lub zaznaczonym eksperymencie original guides, podobnie jak przy saturation/depth colour. Obecna kalibracja przewiduje jednolity mnożnik przy dopasowanym MV; nie wyznacza poprawnego przestrzennego wzmocnienia. Ręczny mnożnik nadal działa. Wyłączenie nowych trybów przywraca wcześniejszą ścieżkę, także ręczne wartości poniżej 1×.

## Maska i odwracanie

Dla prawidłowego depth `d`:

```
proximity = reversedZ ? d : 1-d
p = proximity ^ distribution
w = fullRange ? p : 1-smoothstep(near, far, 1-p)
gain = depthWeighted ? 1 + w*(max(1, globalMultiplier)-1) : globalMultiplier
bias = additiveEnabled ? w*strength : 0
```

NaN/Inf i depth poza [0,1] otrzymują wagę 0 (daleka płaszczyzna). Obie konwencje Z, zakres i parametry mają zabezpieczenia po stronie CPU i HLSL. Przy wyłączonych opcjach nie odczytujemy dodatkowej głębi. Włączone opcje korzystają z tego samego bieżącego punktu dopasowanej głębi podczas encode/decode, także przy eksperymencie oryginalnych guides.

Kolejność: gain dodatniej części editu, saturation podpisanego RGB, baza 0,5 lub bieżący proxy dla pełnej odpowiedzi, depth colour i bias, ograniczenie do nieujemnego FP16. Przy bias > 0 dodajemy go przed ograniczeniem: może przenieść również ujemną wartość nośnika powyżej zera. Bias 0 zachowuje dotychczasową kolejność clippingu. Decode odejmuje bieżący bias i colour, odwraca saturation, a następnie gain. Nie dodano arbitralnego limitu 2; granica górna pozostaje 65504.

To odwrotność transportu dla bieżącej głębi, nie odwrócenie działania temporalnego DLSS. Historia może przenosić offset z poprzedniej powierzchni; ruchome krawędzie głębi, clipping i FP16 mogą pozostawić różnice także przy edicie 0. Zmiana efektywnych parametrów transportu resetuje historię prywatnego DLSS i obliczanej ekspozycji. Nie dodano resetu od każdej zmiany render subrect ani nowych pełnoekranowych przebiegów/tekstur maski. Zaktualizowano także wariant łączący rekonstrukcję guides i carrier, z ABI przypiętym static_assertami i nazwanymi setterami; zamrożone źródła/bloby v59 są zachowane.

## Eksperyment z oryginalnymi depth/MV

Dotychczasowe testy tej gałęzi badały rekonstrukcję i łączenie guides, nie podawanie oryginalnych wskaźników do prywatnego NGX. Nowa opcja przekazuje `ReadableGuide` o oryginalnych wymiarach (dla nieczytelnego formatu może to być istniejący klon tej samej wielkości). Nie tworzy dodatkowego przeskalowanego MV dla NGX: carrier używa wtedy mode 37, bez zapisu converted MV z mode 39. NR i odwracalne efekty nadal korzystają z dopasowanych guides, więc nie jest to usunięcie całego kosztu dopasowania w torze.

MVScale konwertuje jednostki/direction gry do pikseli prywatnego wejścia; nie zmienia współrzędnych próbkowania tekstury. Zachowujemy niezależne origins depth i MV. Flaga MVLowRes pochodzi z opisu źródła i jest częścią tożsamości instancji, niezależnie od IsHDR, AutoExposure i DepthInverted. Zmiana przełącznika odtwarza prywatną instancję przez dotychczasowe odroczone zwalnianie; shutdown nie pozostawia latcha sesji.

[Podręcznik NVIDIA DLSS, sekcje 3.6.2–3.6.3](https://raw.githubusercontent.com/NVIDIA/DLSS/main/doc/DLSS_Programming_Guide_Release.pdf) rozróżnia motion w rozdzielczości render/output oraz konwersję MVScale. Prywatne DLAA ma render=output na swojej siatce. NGX nie udostępnia tu niezależnych wymiarów wejścia głębi/MV, dlatego **nie ma potwierdzenia poprawnego mapowania większych buforów**: runtime może je odrzucić albo czytać jako wycinek, zamiast użyć całego obrazu. To test jakości i kompatybilności, nie potwierdzona poprawa.

Przed przekazaniem sprawdzamy rzeczywistą alokację, aktywne regiony oraz skończone, niezerowe MVScale. Nie podajemy regionów mniejszych od prywatnego wejścia (NGX może odczytać poza aktywną częścią), nie używamy tego trybu przy nieliniowej kompresji ani Hold. W tych przypadkach używane są matched guides. Zmiana faktycznej ścieżki raw/matched resetuje historię. Odrzucenie evaluate przez NGX zachowuje bieżący obraz gry i zatrzaskuje matched fallback do ponownego off/on lub odtworzenia generacji. Sam sukces evaluate nie dowodzi zgodności przestrzennej. W UI zawsze sprawdź status.

Nie uruchomiono tej ścieżki z modelem DLSS w grze. WARP nie wykonuje NGX. Testy CPU sprawdzają granice regionów i konwersję jednostek; testy shaderów nie walidują interpretacji większych buforów przez zamknięty runtime.

## Zrzuty i konfiguracja

**Save pre-DLSS edit** zapisuje signed FP32 edit po przestrzennym gain, przed bias/saturation/colour/clipping. Przy depth-weighted gain zapisuje dodatkowo `.depth.dds`, również przy przycisku edit-only, żeby dało się odtworzyć lokalny mnożnik. JSON zawiera `depth_weighted_positive_gain`, `depth_additive_brightness`, parametry maski, `depth_included` i rzeczywiste wymiary/origins/MVScale wejść NGX. `positive_gain` jest szczytem, nie jednolitą wartością każdego piksela, gdy weighting jest aktywne. `guides_included` nadal oznacza kompletny pakiet depth+motion.

Przy original guides przycisk **Save edit + object guides** jest wyszarzony, a runtime odrzuca wcześniej zakolejkowany pakiet po włączeniu tej opcji: dotychczasowy writer nie zapisuje dowolnych formatów/rozmiarów oryginalnych wejść. Edit-only pozostaje dostępny. Ewentualna dodatkowa `.depth.dds` to matched transport depth (`guide_lattice=transport_matched`, `captured_guides_match_private_dlss_inputs=false`), nie oryginalna tekstura NGX. Pozostałe captures zachowują `guide_lattice=private_dlss_input`. Starsze narzędzia analizy tej gałęzi jawnie odrzucają spatial gain/bias/raw-guides, zamiast błędnie dzielić wszystko przez jeden gain.

Wszystkie nowe klucze `[DlssNr]` mają deklarację/default, odczyt/zapis, zastosowanie/reset profilu i dokładnie jedną listę filtra (`kKeptKeys`):

```ini
SuperiorDepthWeightedGain=false
SuperiorDepthBrightness=false
SuperiorDepthBrightnessFullRange=true
SuperiorDepthBrightnessNear=0
SuperiorDepthBrightnessFar=1
SuperiorDepthBrightnessCurve=0.25
SuperiorDepthBrightnessStrength=0.1
SuperiorOriginalGuides=false
```

## Weryfikacja i koszt

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
