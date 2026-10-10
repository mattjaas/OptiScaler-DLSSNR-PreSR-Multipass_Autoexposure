# NR v19: celowany benchmark 41% i 42%

Przycisk **Compare inter-pass 41% / 42% (CSV)** porównuje tylko te dwie skale.
Nie powtarza P33/P40/P45/P49 i nie dopisuje bieżącego procentu suwaka.
Reguła produkcyjnego automatu, obliczenia shaderów i cztery tryby inter-pass
pozostają takie jak w v18. Pomiar ma rozstrzygnąć przejściowy zakres przed
zmianą automatu; w tym wydaniu cały zakres poniżej 50% nadal wybiera Classic.

## Potwierdzone pomiary w grze

Źródło: dostarczone NR-v18-low-20261010-213951.csv, .samples.csv i .txt,
The Last of Us Part I, RTX 4090, native 3840×2160, 6 przebiegów, Area,
radius 1, guide strength 1. Czas całego NR, z NGX, przejściami i overlapem.
90 świeżych próbek rozgrzewki / 160 pomiarów na okno, 320 próbek na
kandydaturę po połączeniu dwóch okien A/B/C/C/B/A. Taktowanie i temperatura
nie były kontrolowane. To pomiar tej sceny i ustawień, nie gwarancja innych GPU.

| Skala | Classic optimized mediana ms | Fused optimized mediana ms | RGB20 mediana ms | Zwycięzca |
|---|---:|---:|---:|---|
| 33% | 18,89690 | 19,36486 | 19,38637 | Classic |
| 40% | 21,05907 | 21,14867 | 20,48358 | RGB20 |
| 45% | 23,31802 | 24,33331 | 22,77478 | RGB20 |
| 49% | 27,34182 | 28,20506 | 26,84774 | RGB20 |

RGB20 skraca czas wobec obecnego Classic o 0,57549 / 0,54324 / 0,49408 ms
przy P40/P45/P49 (różnice z zaokrąglonych median). Przy P33 Classic jest
szybszy od RGB20 o 0,48947 ms. Zwycięzca ma również niższy P95 w każdej skali.
Maksymalny rozrzut median powtórzeń to 0,05478 ms; kolejność zwycięzców jest
zgodna w obu oknach. Sprawdzono 28 konfiguracji, 40 okien, 6400 dodatnich
skończonych próbek GPU i zgodność median/średnich z podsumowaniem.
Automat raportował Classic we wszystkich czterech skalach, zgodnie z v18.

## Dlaczego potrzebne są P41/P42

RGB20 przechowuje w FP32 wspólną rekonstrukcję native dla grupy 8×8 working,
w kaflu 20×20. Shader liczy first=floor(begin*native/work),
limit=ceil(end*native/work). Jeżeli różnica przekracza 20 na dowolnej osi,
cała grupa używa bezpośredniej rekonstrukcji Mode28 z isolated weights.
Nie zmienia to matematyki filtra ani precyzji.

Model geometrii z operacjami FP32 przy native 3840×2160 i zaokrągleniu wymiarów
working do najbliższej liczby całkowitej daje:

| Skala / working | Grupy mieszczące się w RGB20 |
|---|---:|
| 33% / 1267×713 | 1 / 14310 |
| 40% / 1536×864 | 20736 / 20736 (100%) |
| 40,1% / 1540×866 | 78 / 21037 (około 0,37%) |
| 41% / 1574×886 | 5376 / 21867 (około 24,59%) |
| 42% / 1613×907 | 21037 / 23028 (około 91,35%) |
| 45% / 1728×972 | 26352 / 26352 (100%) |
| 49% / 1882×1058 | 31388 / 31388 (100%) |

To obliczenie geometrii, nie pomiar liczby grup na GPU ani czasu. P40 jest
szczególnym wyrównanym przypadkiem: 8/0,4=20, a każdy początek grupy
odwzorowuje się na całkowitą pozycję native. Przy nieco większej skali fragment
może objąć 21 texeli po floor/ceil. Od rzeczywistego work/native ≥8/19
(około 42,1053%) na obu osiach footprint mieści się bez względu na wyrównanie
w arytmetyce dokładnej. Wymiary i zaokrąglenia FP32 trzeba uwzględniać.
Sam warunek mieszczącego się kafla nie dowodzi zwycięstwa czasowego nad Classic.
Pomiary 41% i 42% są bardziej przydatne od 36% i 38%, gdzie pełne grupy
nie mieszczą się w kaflu. Nie zakładamy monotoniczności czasów w całym zakresie.

## Uruchomienie i interpretacja

Po instalacji DLL z v19 i ponownym uruchomieniu gry wybierz
**Compare inter-pass 41% / 42% (CSV)** w panelu NR. Utrzymuj tę samą nieruchomą
scenę co przy v18, z tymi samymi HDR/modelami/shapingiem/cieniami.
Nie trzeba przestawiać skali suwaka; jej bieżąca wartość nie rozszerza testu.

Na każdą skalę: Off, Classic reference, Fused reference, następnie
Classic optimized / Fused optimized / RGB20 guarded / RGB20 guarded /
Fused optimized / Classic optimized, na końcu obecny automat.
Łącznie 14 unikalnych konfiguracji / 20 okien, z osobną rozgrzewką każdego.
Przy domyślnych 90/160 daje to 5000 świeżych próbek razem z rozgrzewką,
w tym 3200 zapisanych pomiarów GPU. Kandydatury mają po 320 pomiarów na skalę,
referencje i automat po 160. Test mierzy cały NR, nie sam shader inter-pass.

Pliki **NR-v19-boundary-*.csv**, **.samples.csv** i **.txt** są zapisywane
w OptiScaler-NR-Benchmarks obok gry. selected_path sprawdza rzeczywisty PSO;
nie raportuje liczby grup korzystających z LDS. Expected path control nadal
oznacza aktualne ustawienie Classic, a nie potwierdzonego zwycięzcę P41/P42.
Historical gain pozostaje nan. Porównaj mediany, P95, SD i osobne mediany
dwóch okien; sama zgodność automatu z Classic nie rozstrzyga najszybszej ścieżki.

Ustawienia są przywracane po zakończeniu lub anulowaniu. Zmiana native w trakcie
testu lub working w obrębie okna anuluje zbieranie nieporównywalnych wyników.
Dotychczasowe regularne i szerokie benchmarki są dostępne osobno, z prefiksami
NR-v19-auto- oraz NR-v19-low-. Nie dodano nowych przełączników renderingowych,
PSO, tekstur ani pomiarów do zwykłej ścieżki klatki.

Po raporcie z gry można dobrać produkcyjne ścieżki według rzeczywistych wymiarów,
potwierdzonych zwycięzców i geometrii, a następnie sprawdzić automat przeciw
wymuszonym zwycięzcom w ABBA. Granice spoza zmierzonych punktów pozostają
założeniem do weryfikacji, a nie dowodem na zawsze najszybszą ścieżkę.

## Weryfikacja źródła

Lokalnie przeszło pięć zachowanych testów arytmetyki inter-pass, test
integracji benchmarku oraz skompilowany test C++ planu/polityki (32887 osi
kafli). Nowe kontrole potwierdzają dokładnie P41/P42, wcześniejszy powrót
bez dopisania bieżącej skali, zachowanie symetrycznej kolejności trzech
kandydatur i przywracanie konfiguracji. Zmiana nie dotyka HLSL ani ścieżki
produkcyjnego renderowania; test arytmetyki nie jest pomiarem wydajności.

## Build i paczka

Źródło paczki: 88b15110c986604931502ab49a1d62ca4ea874d1,
gałąź codex/nr-interpass-v13-bounded-area.
[Build 38083412109](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/actions/runs/38083412109)
przeszedł: pięć testów arytmetyki, integracja benchmarku, kompilacja trzech
shaderów, wykonanie testu C++ polityki, kompilacja fixture GPU, pełna DLL,
pakowanie i publikacja.

[ZIP v19](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/releases/download/nr-interpass-v19-boundary-41-42-20261010/OptiScaler-NR-nr-interpass-v19-boundary-41-42-20261010.zip),
131 091 025 bajtów.

- ZIP SHA256: 6318a35ef2b892ca0d1991e2b2ea2d0ff812297f4a6e76afd9091e5207ed02a8
- DLL SHA256: d21f8eee6cd1a0e22bc28829d6b86e45a6d0e424896f81913cf07b5ca687f6e7
- Pobrany ZIP: CRC i wszystkie 62 wpisy SHA256SUMS.txt poprawne.
- DLL Windows x64 zawiera przycisk 41/42%, nowy prefiks raportu i opis
  dokładnie dwóch skal. Wszystkie trzy DXIL są identyczne z v18, sprawdzonym
  na RTX 4090. W DLL nie ma 36 odrzuconych wcześniejszych blobów inter-pass.
  Nie wykonano nowego pomiaru całego NR w grze; wymaga uruchomienia benchmarku.

Lokalne usunięcie package_release.ps1 przez Bitdefender pozostawiono poza
commitem. Build użył wersji skryptu z repozytorium.
