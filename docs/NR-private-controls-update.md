# Zapis z numerem, opcje MV, nadganianie up/down i dokładny prywatny preset

2026-10-31. Dotyczy eksperymentalnej gałęzi prywatnego DLAA Superior Scaling w D3D12. Zachowano pomiar przed dodatnim mnożnikiem, jedno globalne wzmocnienie i minimum Auto 1×. Brak nowego pomiaru GPU przy wyłączonym preview/Auto, a zmiany zapisu pracują wyłącznie podczas zleconego zrzutu.

## Kolejne nazwy zrzutów

**Pre-DLSS edit filename** można pozostawić na `nr-edit`. Pierwszy wolny pakiet zapisze się jako `nr-edit.dds` / `.json` i ewentualnie `.depth.dds` / `.motion.dds`. Jeśli którykolwiek znany plik tej nazwy już istnieje, wybierane są `nr-edit-001.*`, `nr-edit-002.*` itd. Wszystkie elementy pakietu mają wspólny stem. Uwzględniane są również osierocone companions, nawet dla edit-only, aby nie mieszać nowych i starych guides. UI **Saved...** pokazuje faktycznie wybraną pełną ścieżkę. Wpisana nazwa pozostaje bazą następnych zapisów.

`DlssNr_EditDumpFiles.h` rezerwuje pliki przez CREATE_NEW. Kolizja podczas rezerwacji usuwa tylko pliki utworzone przez tę próbę, a następnie próbuje następnego numeru. Inne błędy nie są traktowane jako kolizja. Odwzorowanie GPU, payload DDS/JSON, kolejka prywatnego readbacku i limit pamięci pozostają takie same. Pliki użytkownika nie są nadpisywane. Limit poszukiwania to 10000 nazw. W razie błędu zapisu helper RAII zamyka uchwyty i próbuje usunąć wyłącznie własne częściowe wyniki.

CPU test z prawdziwym filesystemem Windows sprawdza zachowanie oryginalnego payloadu, wspólne nazwy czterech elementów, dwa jednoczesne niezakończone rezerwowania, rollback i kolizję z osieroconą depth przy edit-only. Test działa we własnym nowym katalogu artifacts/validation; nie korzysta z katalogów gier ani nie usuwa ich plików.

## Granice z samej głębi

64 oznaczało minimalną liczbę **par koloru** do policzenia kontrastu; nie było to 64 MV. Globalna bramka używa 576 stałych punktów MV, a dodatkowo końce przyjętych par mogły być filtrowane przez różnicę MV. Nowe kontrolki przy **Depth-boundary contrast (experimental)**:

- **Filter depth boundaries using MV**, default On. Off wyłącza lokalne odczyty MV i warunek różnicy >2 px; pary wybiera wyłącznie ta sama nieciągłość depth. Globalny gate może dalej działać.
- **Require motion activity for boundary measurements**, default On. Off pomija globalne odczyty 576 punktów, test aktywności i test zmiany. **Require changing motion field** oraz **Preview minimum motion** są wtedy wyszarzone. Preferencje tych pól są zachowane.
- Po wyłączeniu **obu** miernik nie odczytuje MV, ani nie wymaga poprawnego MV, i działa również na statycznym obrazie. DLSS nadal korzysta ze swoich MV — opcja dotyczy miernika. Może także mierzyć menu/film, skoro gate jest świadomie wyłączony.
- **Minimum depth-boundary pairs**, suwak 8..256, default 64. Sanity INI: 8..4096. Mniejsza liczba pozwala mierzyć skąpe granice, ale zwiększa zmienność. Zachowano warunek 95% prawidłowych kandydatów.

NaN/Inf depth, błędne granice i górny clipping nośnika nadal są odrzucane. Wariant depth-only działa też przy reversed Z. Opcje są zakodowane w nazwanym setterze `SetBoundaryOptions`: bit0 reserved0 = reversed Z, bit1 = pomiń lokalny MV, bit2 = pomiń gate; offset pola i ABI stałych są przypięte static_assertami. CPU nie odczytuje payloadu baseline MV, gdy shader go nie zapisuje. Zmiany obu opcji i minimalnej liczby par unieważniają stary wynik asynchroniczny i kontekst Auto, zanim zostanie wybrany gain.

Na poprzednim zrzucie głowa/woda, GPU 64 faz, ten sam cel 0,1429128: **7,03536005× bez lokalnego filtra MV**, zarówno z globalnym gate, jak i bez niego. Wariant z MV dawał 6,18147×. Nie zmieniano celu, aby dopasować wynik do 6×. Zmiana selekcji wpływa na rekomendację; to nie dowód przewagi któregoś trybu w grze. [Wynik GPU](validation/nr-depth-only-capture-gpu.json). Łączenie statycznych faz nie symuluje ruchomej EMA.

## Szybciej w górę, wolniej w dół

W **Multiplier preview response**:

- **Preview EMA time** pozostaje wspólnym normalnym czasem wygładzania.
- **Preview catch-up up time** steruje nadganianiem ku większemu mnożnikowi. Zachowuje dotychczasową preferencję `SuperiorGainPreviewFastSeconds` (default 0,25 s).
- **Preview catch-up down time** ma osobną preferencję, default 1,0 s.

Przykład: up **0,10 s**, down **1,00 s**. Mniejszy czas oznacza szybsze nadganianie. Oba czasy są skończone, dodatnie, ograniczone 0,05..10 s i nie przekraczają normalnego EMA time. Po załadowaniu wcześniejszego profilu zachowany jest dawny fast jako up; użytkownik może mieć wcześniej wyższą własną wartość up, którą należy dobrać względem nowego down.

Dla obu metod (coverage i boundary) czas aktualizacji histogramów/momentów wybierany jest z kierunku surowej propozycji względem wyjścia, a czas końcowej EMA log gain z kierunku propozycji po pierwszym wygładzeniu. Catch-up włącza się nadal po trwałej zmianie, a zwykły EMA jest wspólny. Nie zniesiono limitu aktuatora Auto co 0,5 s ani +2% / factor 1,05 histerezy. Minimum 1× dotyczy także warmup/fallback. Zmiana samych czasów nie resetuje historii DLSS; reset następuje nadal po rzeczywistej zmianie zastosowanego gain.

Test CPU porównuje symetryczne skoki obu metod i sprawdza większy postęp w górę przy krótszym czasie up, stabilny sygnał, przemienne fluktuacje, pauzę i minimalną liczbę par.

## Dokładne nadpisanie prywatnego presetu

Pod **DLSS to temporally stabilize NR**:

- **Private CNN preset override**: **Automatic A/E**, **A**, **C**, **D**, **E**.
- **Private Transformer preset override**: **Automatic K**, **J**, **K**.

To prywatny SR/DLAA, niezależny od presetu DLSS gry i modelu NR. Kontrolka drugiej rodziny nie jest pokazywana, a jej preferencja jest zachowana. Automatic zachowuje wcześniejszy wybór A/E według guided range oraz K, wraz z zachowaniem CNN przez DRS/Auto i resetem latcha po NR/NGX shutdown. Explicit CNN nie zmienia się na E z powodu skali. Zmiana override/family odtwarza generację przez istniejące odroczone zwalnianie GPU i czyści stan niedostępności, aby można było ponowić wybór po odrzuceniu.

NGX dostaje odpowiedni hint w DLAA oraz pozostałych quality keys. Numery A=1/C=3/D=4/E=5/J=10/K=11 są sprawdzone wobec SDK. Jawny override nie uruchamia starego fallbacku CreateFeature do runtime Default po błędzie. Odrzucony preset powoduje status niedostępności Superior i powrót do dotychczasowej zwykłej ścieżki NR; inny override pozwala ponowić próbę. Automatic zachowuje dotychczasowy fallback. Sukces CreateFeature nie dowodzi, że zamknięty runtime rzeczywiście respektuje hint.

J jest klasyfikowane jako rodzina Transformer dla coverage i metadata; C/D/E jako CNN. To klasyfikacja rodziny, nie nowa kalibracja jakości: J/C/D nie zostały sprawdzone w grze. JSON zawiera żądany preset oraz oba klucze override, bez deklarowania odczytu wewnętrznego modelu NGX. Nie wykonano gry/NGX z tymi presetami; testy CPU obejmują wybór, zmianę, skalę/Auto, nieaktywny override innej rodziny i nowy wybór po wyczyszczeniu latcha.

## Konfiguracja i JSON

Nowe klucze `[DlssNr]` mają pełny obieg default/read/write/apply/reset i należą wyłącznie do kKeptKeys:

```ini
SuperiorGainPreviewFastDownSeconds=1.0
SuperiorGainPreviewBoundaryUseMotion=true
SuperiorGainPreviewBoundaryMotionGate=true
SuperiorGainPreviewBoundaryMinPairs=64
; 0=automatic; CNN: 1=A,3=C,4=D,5=E; Transformer: 10=J,11=K
SuperiorScalingCnnPresetOverride=0
SuperiorScalingTransformerPresetOverride=0
```

JSON zapisuje `gain_preview_fast_up_seconds` (oraz zgodne wstecz fast_seconds), `gain_preview_fast_down_seconds`, `gain_preview_boundary_use_motion`, `gain_preview_boundary_motion_gate`, `gain_preview_boundary_min_pairs`, `private_cnn_preset_override`, `private_transformer_preset_override`. Istniejące request/capture snapshots nadal opisują wiek i wartość propozycji; zapis DDS pozostaje po faktycznym gain.

## Walidacja i czas GPU

CPU policy/profiles z aktualnym Config.cpp, serialization DDS/JSON i realna rezerwacja plików. WARP oraz RTX 4090: **336** przypadków względem niezależnej referencji (168 FP32 + 168 FP16), trzy kombinacje lokalnego/globalnego MV, obie konwencje Z, fazy i nieprawidłowe dane. Dodatkowo probe działa przy całkowicie NaN MV, gdy oba warunki MV są wyłączone; runtime testuje wszystkie trzy warianty, replay i zachowanie zasobów. Rebuild 56 blobów/ABI oraz istniejące testy Auto/coverage.

RTX 4090, proceduralne rezydentne FP16 proxy/answer, R32 depth i RG32 MV; 256×144 prób. Timestamp obejmuje dispatch, bariery i copy-to-readback. Bez NR, NGX, gry, alokacji/uploadów i opóźnienia kolejki/fence. Osobna rozgrzewka po 16 prób A/B, 16 kwartetów ABBA, po 32 pomiary na wariant. **A = niezależny zachowany blob z d1144b59, B = bieżący**, ten sam fixture. [Próbki](validation/nr-depth-only-preview-timing.csv), [statystyki](validation/nr-depth-only-preview-timing.json). Historyczne etykiety `off_ms` w lokalnym logu harnessu oznaczają w tym porównaniu A; CSV określa warianty A/B, nie Off.

| B | Rozdzielczość | A średnia±SD ms | B średnia±SD ms | Różnica |
|---|---|---:|---:|---:|
| Domyślne MV On/On | 2240×1264 | 0,033095±0,000396 | 0,033536±0,000350 | +0,000441 |
| Domyślne MV On/On | 2496×1404 | 0,033220±0,000380 | 0,033521±0,000425 | +0,000301 |
| Tylko lokalny MV Off | 2240×1264 | 0,033395±0,000386 | 0,030601±0,000296 | −0,002794 |
| Tylko lokalny MV Off | 2496×1404 | 0,033379±0,000342 | 0,030699±0,000452 | −0,002680 |
| Oba MV Off | 2240×1264 | 0,033233±0,000389 | 0,026118±0,000348 | −0,007115 |
| Oba MV Off | 2496×1404 | 0,033112±0,000376 | 0,025997±0,000445 | −0,007115 |

Nie przedstawiamy drobnej różnicy domyślnej ścieżki jako istotnej zmiany całego toru. Dla capture suffixes, CPU smoothing i preset hints nie ma nowego stałego dispatchu; różnice zachowania modeli/historii mogą wpływać na koszt klatki. Porównanie całego toru wymaga rozgrzanego ABBA w grze z tym samym presetem, sceną, ruchem i timestamp NR+DLSS+composition. Obserwacja użytkownika o niezłym działaniu na Transformerze dotyczy wcześniejszego wariantu z MV, nie jest potwierdzeniem depth-only lub nowych presetów.
