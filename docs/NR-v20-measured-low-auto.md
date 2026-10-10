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
polityki nie zmienia DXIL. Wydajność automatu v20 w całej grze potwierdza
dostarczony później raport ABBA opisany poniżej.

## Potwierdzenie automatu w grze: NR-v20-low-20261010-230734

Źródła: NR-v20-low-20261010-230734.csv, .samples.csv i .txt dostarczone
przez użytkownika. TLOU Part I, RTX 4090, native 3840×2160, sześć przebiegów,
Area/radius 1/guide strength 1. Te same warunki benchmarku: 90 świeżych
próbek rozgrzewki na każde okno, 160 pomiarów na okno; automat oraz
wymuszony oczekiwany tor po 320 próbek w ABBA. Alternatywny tor i referencje
po 160. Mierzony jest cały NR z NGX, przejściami i overlapem.
Taktowanie i temperatura nie były kontrolowane.

| Skala / working | Actual selected_path automatu | Automat mediana ms | Wymuszony zwycięzca mediana ms | Automat minus kontrola ms | Zysk automatu wobec alternatywnego zoptymalizowanego toru ms |
|---|---|---:|---:|---:|---:|
| 33% / 1267×713 | Classic optimized | 18,89638 | 18,90150 | −0,00512 | 0,46285 |
| 40% / 1536×864 | Fused RGB20 | 20,59469 | 20,58598 | +0,00871 | 0,55654 |
| 41% / 1574×886 | Classic optimized | 21,81376 | 21,83526 | −0,02150 | 0,51302 |
| 42% / 1613×907 | Fused RGB20 | 22,07232 | 22,07795 | −0,00563 | 0,41677 |
| 49% / 1882×1058 | Fused RGB20 | 26,85645 | 26,86669 | −0,01024 | 0,48486 |

Różnice w tabeli obliczono z zaokrąglonych median CSV; zapisane bezpośrednio
kolumny delta mogą różnić się o 0,00001 ms. Przy P33/P41 alternatywą jest RGB20,
przy P40/P42/P49 Classic optimized. Wszystkie pięć automatów ma
path_matches_control=1 i rzeczywistą ścieżkę identyczną z wymuszoną kontrolą.
Nie raportowano Mixed routes ani zmiany na awaryjny PSO. Jednolite zabezpieczenie
footprintu grup w RGB20 nadal może używać bezpośredniego Mode28, szczególnie
przy P42; zgodność PSO nie oznacza LDS w każdej grupie.

| Skala | Mediany okien automatu ms | Mediany okien kontroli ms | SD automat / kontrola ms | P95 automat / kontrola ms |
|---|---|---|---|---|
| 33% | 18,89280 / 18,90406 | 18,89126 / 18,90611 | 0,05820 / 0,05732 | 18,99930 / 18,99930 |
| 40% | 20,59315 / 20,59469 | 20,59162 / 20,57728 | 0,08738 / 0,08700 | 20,77286 / 20,74419 |
| 41% | 21,79379 / 21,82605 | 21,84346 / 21,83322 | 0,10231 / 0,09400 | 22,00678 / 22,03341 |
| 42% | 22,07744 / 22,06874 | 22,07488 / 22,07898 | 0,13225 / 0,13665 | 22,38259 / 22,37645 |
| 49% | 26,84518 / 26,86054 | 26,85747 / 26,87130 | 0,21010 / 0,20853 | 27,22202 / 27,26298 |

Największa bezwzględna różnica median automat/kontrola wynosi 0,02150 ms;
największa dodatnia różnica 0,00871 ms przy P40. Jest to poziom obserwowanej
zmienności; pomiar nie wskazuje istotnej straty GPU po automatycznym wyborze.
P95 również pozostaje blisko kontroli (różnice od −0,04096 do +0,02867 ms).
Nie interpretujemy niewielkich ujemnych delta jako dodatkowej optymalizacji:
automat i kontrola wybierają ten sam shader i tor.

Zyski automatu względem Fused reference: P33 1,76487 ms, P40 1,55648 ms,
P41 2,69722 ms, P42 3,25478 ms, P49 3,04333 ms. Względem Classic reference:
0,70605 / 1,29587 / 0,77926 / 1,22419 / 1,33939 ms. Wszystkie alternatywne
zoptymalizowane tory pozostają wolniejsze. Różnice wobec historycznej
oszczędności względem Fused reference mieszczą się poniżej 0,065 ms.

Audyt surowych danych: 30 konfiguracji, 40 okien, 6400 dodatnich skończonych
próbek GPU; każde okno ma dokładnie indeksy 0..159. Potwierdzono kolejność
ostatnich czterech okien na skalę jako kontrola/automat/automat/kontrola,
320 próbek każdej kontroli i automatu oraz zgodność median, średnich, P95,
populacyjnego SD i median osobnych okien z podsumowaniem. Native, working
i liczba przebiegów są zgodne z profilem. SHA256 źródeł:

- CSV: 2928e65ddfb4af9959a533c62b92656e163f94607e1edfb541f33314aba65254
- samples.csv: 910439e857d91517b682f307951bff34453a4c2924c65c46a011d2e86bd9fed0
- TXT: c6c2f9833a01745f6aebabbac32fe0f18de7deda6ddf3e940032686bafd2e4fe

Weryfikacja v20 poniżej 50% jest zakończona dla tych pięciu skal i warunków.
Nie ma podstaw do kolejnej zmiany kodu ani ponownego wykonywania tego samego
zestawu. To nie rozszerza dowodu na niezmierzone ułamkowe skale ani inne GPU,
sceny i ustawienia; istniejąca reguła oraz rutynowy benchmark pozostają.

## Paczka i publikacja

Źródło paczki: dba469345e26b79530f8eae1035af29a82cbbad7,
gałąź codex/nr-interpass-v13-bounded-area.
[Build 38085143645](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/actions/runs/38085143645)
zakończył się sukcesem: testy arytmetyki, benchmark, wykonany test C++ polityki,
kompilacja trzech shaderów i fixture GPU, pełna DLL, pakowanie oraz publikacja.

[ZIP v20](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/releases/download/nr-interpass-v20-measured-low-auto-20261010/OptiScaler-NR-nr-interpass-v20-measured-low-auto-20261010.zip),
131 090 935 bajtów.

- ZIP SHA256: d4e8897f428142e7ef3ec2733641ecf5e2dc0604a43a9ffa92e95c6e67ecafd0
- DLL SHA256: 6cd09aacff621963774ee175fffe6f7c56204056a8879c6e70fb0426c5417dd2
- Pobrany ZIP: CRC i wszystkie 62 wpisy SHA256SUMS.txt poprawne.
- DLL Windows x64 zawiera cztery tryby inter-pass, nowe przyciski weryfikacji
  i prefiksy raportów. Trzy DXIL są identyczne z shaderami v18 użytymi
  w lokalnym rozszerzonym teście GPU. Nie zawiera 36 odrzuconych blobów inter-pass.

Lokalnie usunięty przez Bitdefender package_release.ps1 pozostawiono poza
commitem. Nie przywracano go z kwarantanny; Actions użył wersji repozytorium.
