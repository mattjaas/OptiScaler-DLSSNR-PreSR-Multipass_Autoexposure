# Różne wagi kanałów

2026-10-31. Na prośbę użytkownika przeszukano wspólne wagi R/G/B. Wagi są nieujemne i sumują się do 1, dzięki czemu ważenie nie odwraca monotoniczności odsetka przy wzroście dodatniego mnożnika. **Ten sam wektor wag dla CNN i transformera**, osobny próg wyniku dla każdej rodziny. Nie zmienia się kolorów obrazu ani mnożnika per kanał.

## Ważenie odsetków przed wyznaczaniem mnożnika

```text
S = wR*fR + wG*fG + wB*fB
f_c = count(E_c > epsilon) / count(E_c > 0)
epsilon = 0.008791791791791792
```

Zachowano epsilon poprzedniego badania. Wykorzystano pełne obrazy, siatkę wag co 2,5 punktu procentowego (861 kombinacji), punkty referencyjne 1/3 i wagi 0,2126/0,7152/0,0722 oraz lokalne doprecyzowanie co 0,5 pp w okolicy najlepszego punktu. Nie jest to dowód globalnego optimum dla dowolnych wag/epsilon. Kryterium główne: najmniejsza największa propozycja względnego mnożnika na pierwszych dobrych obrazach, z osobnymi progami na CNN/transformer i zakazem redukowania dobrej granicy. Remisy rozstrzyga suma nadwyżek obu rodzin, potem mniejszy udział B. Cele odrzucają wszystkie dostarczone złe zapisy. Nie dobierano wag oddzielnie na grę/model.

| R | G | B | Maks. nadwyżka CNN | Transformer |
|---:|---:|---:|---:|---:|
| 100% | 0% | 0% | +44,0% | +20,0% |
| 0% | 100% | 0% | +73,1% | +28,6% |
| 0% | 0% | 100% | +139,8% | +1700,6% |
| 33,33% | 33,33% | 33,33% | +21,9% | +500,2% |
| 50% | 50% | 0% | +20,4% | +12,5% |
| 70% | 30% | 0% | +20,4% | +12,5% |
| **74,5%** | **25,5%** | **0%** | **+20,0%** | **+12,5%** |
| 75% | 25% | 0% | +24,2% | +12,5% |
| 25% | 50% | 25% | +25,9% | +260,1% |
| 21,26% | 71,52% | 7,22% | +38,5% | +80,1% |
| 60% | 20% | 20% | +14,2% | +200,1% |

Wagi odsetków przypominające wagi luminancji są tu tylko kolejną kombinacją statystyk; nie zastosowano mieszania RGB do Y przed progowaniem. Wyniki pojedynczego B dotyczą tego stałego epsilon, nie najlepszego pasma B z osobnego przeszukania.

Najlepszy testowany wektor 0,745/0,255/0 ma progi **>0,9261922955 CNN / >0,9731907018 transformer**. Propozycje na czterech dobrych granicach:

- CNN: 1,1979×; 1,2004×; 1,0134×; 1,0003×.
- Transformer: 1,0289×; 1,0003×; 1,0005×; 1,1253×.

To maksymalnie 6×→7,20× CNN / 6,75× transformer, albo 10×→12,00× / 11,25× — przykłady przeliczenia, nie znane suwaki dawnych plików. Poprawa względem równego R/G jest **tylko około 0,4 pp dla CNN**, bez zmiany maksimum transformera. Zmiana udziału R z 74,5 do 75% zwiększa maksimum CNN do +24,2%; dyskretne wartości editu i odsetków tworzą uskoki. Nie interpretujemy precyzyjnego 74,5% jako uniwersalnej wagi dla DLSS.

## Ważenie gotowych propozycji mnożnika

Oddzielnie sprawdzono `wR*m_R+wG*m_G+wB*m_B` na wcześniejszych trzech propozycjach dla wspólnego celu mediany, a następnie jeden dodatkowy współczynnik na rodzinę, tak aby żadna pierwsza dobra granica nie została obniżona i złe stany nie były przyjęte.

Najlepszy zbadany kompromis: **67% R / 31,5% G / 1,5% B**, maksima **1,1884× CNN / 1,1437× transformer** (+18,84% / +14,37%). Prostszy wcześniejszy punkt 67,5/32,5/0 dał +19,23% / +11,34%, więc wynik zależy też od przyjętego kompromisu między rodzinami.

To inna reguła i bogatsza kalibracja: odziedziczyła modelowe cele odsetków starej mediany i dodała modelowe skale końcowe. Nie przedstawiamy jej jako wspólnego wskaźnika z wyłącznie jednym progiem na rodzinę. Nie należy podmieniać jej liczb do wzoru S z pierwszej sekcji.

## Kontrole i ograniczenia

1. Numerycznie sprawdzono warunek, że cel pełnoobrazowy nie proponuje mnożnika <1 na żadnej pierwszej dobrej granicy. Cel i solver używają tej samej skalarnej kolejności dot; różnica redukcji macierzowej o jeden ULP mogłaby inaczej pozornie przekroczyć płaski schodek odsetka. Próg jest przesuwany ku górze, a warunek kontrolowany asercją.
2. Wariant 74,5/25,5/0 na 64 fazach siatki **256×144** daje propozycje **0,7502×–1,2861×** na pierwszych dobrych zapisach. Równe R/G na tej samej siatce miało 0,8184×–1,2591×. Dopasowanie wag na pełnych obrazach nie naprawia problemu próbkowania.
3. Przeniesienie progu skalibrowanego tylko na game1 do game2, przy wybranych wagach, **akceptuje znany zły `game2-cnn-ghosting2.dds`**. Proponuje też 0,8721× / 0,7502× na dwóch pierwszych dobrych CNN game2. Wagi były wybrane na całości, więc to audyt przenoszenia progu, nie niezależny holdout selekcji wag. Wyniku dopasowania do 16 danych nie traktujemy jako gotowej reguły między grami.

Ważenie sprawdzono na surowym edicie, jak poprzednie odsetki, bez rounding FP16; nie jest to test z nową inferencją DLSS ani potwierdzenie braku ghostingu. Nie zmieniono DLL/HLSL/INI i nie dodano kosztu GPU do gry. Nadal potrzeba stabilnego lekkiego pomiaru oraz niezależnych sekwencji i oceny wyjścia DLSS. Mniej kosztowny do implementacji wariant R/G pozostaje sensownym punktem odniesienia; ta selekcja wag nie przyniosła przełomu.

[Wyniki i skrócony wykaz wszystkich testowanych wag](validation/nr-edit-channel-weights.json), [fazy próbkowania](validation/nr-edit-channel-weights-phases.json). Wszystkie dane źródłowe pozostają tymi samymi 16 plikami z wcześniejszego raportu; obrazów nie publikujemy.

## Odtwarzanie

```powershell
python tools/analyze_nr_edit_channel_weights.py `
  'E:/Gry/Marvels SpiderMan 2/dlssnr-edit/game*-*.dds' `
  'F:/Gry/The Last of Us - Part I/dlssnr-edit/game*-*.dds' `
  --breakdown docs/validation/nr-edit-channel-breakdown.json `
  --output artifacts/edit-analysis/channel-weights.json

python tools/analyze_nr_edit_channel_coverage.py `
  'E:/Gry/Marvels SpiderMan 2/dlssnr-edit/game*-*.dds' `
  'F:/Gry/The Last of Us - Part I/dlssnr-edit/game*-*.dds' `
  --phase-reference artifacts/edit-analysis/channel-weights.json --phase-grid 256 144 `
  --output artifacts/edit-analysis/channel-weights-phases.json
```

Narzędzie zapisuje pełną listę kandydatów; raport w repo zachowuje najlepszych, punkty porównawcze i skrócone wyniki całej siatki, aby ograniczyć rozmiar. CPU NumPy, bez nowych shaderów. [Poprzednie porównanie średnich](NR-edit-rg-mean-study.md), [ekspozycja obliczona produkcyjnymi PSO](NR-edit-exposure-study.md).
