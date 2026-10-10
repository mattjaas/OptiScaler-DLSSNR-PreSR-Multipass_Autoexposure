# NR v20: automat według pomiarów poniżej 50%

Inter-pass optimized wybiera teraz RGB20 przy dokładnym wyrównaniu P40 oraz
od zaokrąglonego P42. Pozostałe niższe skale zachowują Classic optimized.
Dokładne P50 ma pierwszeństwo specjalizacji Fused optimized, od rzeczywistego
P60 wybierany jest RGB16, powyżej P90 pozostaje Classic. Reguły stosują się
do Area/radius 1/aktywnego prowadzenia; inne filtry i promienie zachowują
dotychczasowy Classic z dokładnym filtrem. UI nadal ma Off, Classic reference,
Fused reference i Inter-pass optimized.

## Wyniki stanowiące podstawę decyzji

Źródła: NR-v18-low-20261010-213951.csv/.samples.csv/.txt oraz
NR-v19-boundary-20261010-223844.csv/.samples.csv/.txt dostarczone przez użytkownika.
The Last of Us Part I, RTX 4090, wyjście 3840×2160, 6 przebiegów,
Area/radius 1/guide strength 1. Czas całego NR, obejmujący NGX, przejścia
i overlap. Każde okno: 90 świeżych próbek rozgrzewki / 160 pomiarów GPU.
Trzej kandydaci w A/B/C/C/B/A, po 320 próbek na kandydaturę i skalę.
Taktowanie/temperatura nie były kontrolowane; pozostałe ustawienia i scena
pochodzą z testu użytkownika. Nie porównujemy absolutnych czasów różnych skal
jako zysku optymalizacji.

| Skala | Classic optimized mediana ms | Fused optimized mediana ms | RGB20 mediana ms | Wybrany tor |
|---|---:|---:|---:|---|
| 33% | 18,89690 | 19,36486 | 19,38637 | Classic |
| 40% | 21,05907 | 21,14867 | 20,48358 | RGB20 |
| 41% | 21,83680 | 22,68006 | 22,33958 | Classic |
| 42% | 22,50394 | 23,34464 | 22,06874 | RGB20 |
| 45% | 23,31802 | 24,33331 | 22,77478 | RGB20 |
| 49% | 27,34182 | 28,20506 | 26,84774 | RGB20 |

Przy P41 Classic wygrywa z RGB20 o 0,50278 ms (RGB20 jest o 2,30% wolniejszy).
Przy P42 RGB20 skraca czas wobec Classic o 0,43520 ms (1,93%).
Dotychczasowy automat v19 miał przy P42 medianę 22,50189 ms, a wymuszony
zwycięzca 22,06874 ms: różnica 0,43315 ms. To pomiar wymuszonej ścieżki;
nie przedstawiamy go jako już wykonanego pomiaru automatu z nowej paczki v20.

| Skala | Classic P95 / SD ms | RGB20 P95 / SD ms | Mediany okien Classic / RGB20 ms |
|---|---|---|---|
| 41% | 22,07232 / 0,10984 | 22,56486 / 0,10963 | 21,82554–21,85011 / 22,33242–22,34368 |
| 42% | 22,72461 / 0,11457 | 22,43174 / 0,15087 | 22,48858–22,51981 / 22,06157–22,07949 |

Ranking jest zgodny w obu powtórzeniach oraz dla P95. Najbardziej ostrożne
porównanie median osobnych okien nadal daje Classic co najmniej 0,48231 ms
przewagi przy P41 i RGB20 co najmniej 0,40909 ms przy P42. Sprawdzono
14 konfiguracji, 20 okien, 3200 dodatnich skończonych próbek, pełne indeksy
0..159 oraz zgodność median, średnich, P95 i SD z surowymi próbkami.
Wyniki wcześniejszego v18 opisano w dokumentacji v19.

## Reguła wyboru i jej granice

DlssNr_InterPassPolicy.h używa rzeczywistych wymiarów tekstur i uint64:

1. Off / Classic reference / Fused reference zachowują pełną izolację
   od optymalizacji; automat to tryb 3.
2. Dokładne work*2=native na obu osiach wybiera Fused optimized P50.
3. Dokładne work*5=native*2 na obu osiach wybiera RGB20 P40: grupy 8×8
   obejmują wyrównane native 20×20 bez overflow.
4. work*100+50 ≥ native*42 na obu osiach dopuszcza RGB20 od zaokrąglonego
   P42. Pół texela uwzględnia np. 3840×2160 → 1613×907, gdzie wysokość
   rzeczywista jest nieznacznie poniżej 42%. Jedna oś poniżej warunku
   zachowuje Classic. P40 z innym zaokrągleniem niż dokładne 2/5 również
   zachowuje Classic.
5. Zachowany górny limit: work*10 ≤ native*9 na obu osiach. RGB16 jest
   wybierany przy work*5 ≥ native*3 na obu osiach; pozostałe RGB używa 20×20.
6. Pozostałe przypadki wybierają Classic optimized.

Shader nadal sprawdza jednolicie footprint całej grupy przed barierą; zbyt
duży fragment używa bezpośredniego Mode28 z isolated weights. Przy P42
nie wszystkie grupy mieszczą się w kaflu, ale cały NR jest już szybszy
w dostarczonym pomiarze. Zabezpieczenie działa także przy innych rozdzielczościach.

Pomiary potwierdzają konkretne punkty P33/P40/P41/P42/P45/P49 na tej karcie,
scenie i ustawieniach. Przedziały między nimi, szczególnie ułamkowe P41..P42,
używają konserwatywnej reguły, nie runtime autotuningu. Nie dowodzimy, że
42% jest dokładnym fizycznym punktem przecięcia czasów ani że ta polityka
jest zawsze najszybsza na każdej karcie i przy każdym ustawieniu.
Nie dodano pętli po grupach, odczytów GPU ani tekstur do wyboru ścieżki;
to kilka porównań CPU raz na klatkę. HLSL, precyzja RGB FP32, alfa, HDR,
filtry i PSO nie uległy zmianie. Zmiana dotyczy wyboru sprawdzonych ścieżek.

## Weryfikacja automatu w grze

**Verify optimized inter-pass below 50% (CSV)** uruchamia P33/P40/P41/P42/P49
oraz bieżący niższy procent, jeśli jest inny. P45 pominięto jako ponowne
sprawdzanie toru RGB20, który reprezentuje P49.

Każda skala: Off, Classic reference, Fused reference, alternatywny zachowany
Classic/RGB20, a następnie wymuszony oczekiwany tor / automat / automat /
wymuszony oczekiwany tor. Każde okno ma osobną rozgrzewkę.
To 30 konfiguracji / 40 okien, 6400 zapisanych próbek przy ustawieniu 160.
Z dodatkowym procentem: 36 / 48. Przegrywającego generic Fused optimized
poniżej 50% usunięto z tych benchmarków. Fused reference oraz specjalizacja
P50 pozostają, podobnie jak wymagane ścieżki awaryjne shadera.

Niezależna tabela kontroli jest oparta na pomiarach, a nie na wyniku Select:
P33/P41 Classic; P40/P42/P49 RGB20. Historical gain podaje różnicę z ostatniego
pomiaru względem Fused reference; nie oznacza oczekiwanego czasu absolutnego.
P45 również zachowuje zmierzoną kontrolę, gdy zostanie dodane jako bieżąca
skala. Inne, niezmierzone skale mają historyczne nan i przewidywany tor kontroli. Przy innych
wymiarach native, zwłaszcza niewyrównanym P40, kontrola z profilu 4K może
różnić się od automatu — porównuj actual selected_path i alternate.

Skrócony przycisk **Verify optimized inter-pass 41% / 42% (CSV)** wykonuje
tylko P41/P42, bez dopisywania bieżącej skali: 12 konfiguracji / 16 okien,
2560 pomiarów. Regularny benchmark P50/P59/P65 pozostaje: 18 / 24 lub
24 / 32 z dodatkowym procentem, także gdy ten procent jest niższy od 50.
Wszystkie profile używają ABBA. Raporty mają prefiksy NR-v20-low-,
NR-v20-boundary- i NR-v20-auto-. Ustawienia są przywracane po zakończeniu
lub anulowaniu; kontrola niezmienności geometrii pozostaje aktywna.

W tej samej nieruchomej scenie co poprzednie testy porównaj czas całego NR,
selected_path, mediany okien, P95 i SD. Automat powinien wybierać tę samą
ścieżkę co wymuszony zwycięzca przy zmierzonej geometrii, a różnica czasu
powinna być na poziomie zmienności między powtórzeniami. Alternatywny tor
sprawdza, czy wcześniejszy ranking pozostał aktualny. Sam path_matches_control
nie gwarantuje najszybszego czasu.

## Weryfikacja lokalna

Przeszły wszystkie pięć zachowanych testów arytmetyki i integracja benchmarku.
Dynamic geometry oraz tiled Area rozszerzono o P41/P42. Test C++ potwierdza
niezależną tabelę sześciu zmierzonych zwycięzców, izolację referencji,
wyrównane/niewyrównane P40, zaokrąglenie obu osi P42, priorytet P50,
granice P60/P90, filtry/promienie oraz bezpieczną arytmetykę dużych wymiarów.
Sprawdził 37622 osi kafli, w tym 21 wymagających zabezpieczonego overflow.

Na RTX 4090 faktyczny test DXIL, rozszerzony o P41/P42, przeszedł 6528
porównań i 53 543 552 składowe RGBA. Obejmuje FP32/FP16, signed HDR,
alfa, NaN, cienie/shaping, nieparzyste wymiary i niepełne grupy. RGB20
poniżej P50 porównuje się bitowo z tą samą sprawdzoną specjalizacją isolated
weights/Mode28 bez kafla, z wyjątkiem payloadu NaN; matematyka i precyzja
nie uległy zmianie. Użyto identycznych shaderów z v18, ponieważ zmiana
polityki nie zmienia DXIL. Wydajność automatu v20 w całej grze pozostaje
do sprawdzenia przez przygotowany benchmark ABBA.
