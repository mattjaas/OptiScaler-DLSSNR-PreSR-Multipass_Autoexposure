# Średnia R/G i średnia dwóch najmniejszych wyników

2026-10-31. Kolejne propozycje użytkownika sprawdzono na tych samych 16 editach. **R < G < B nie jest stałą regułą**: w 5 z 8 pierwszych dobrych zapisów B daje najmniejszą propozycję, a tylko w 3 zachodzi ścisłe R<G<B. Przykład przeciw stałej kolejności: game1-cnn-noghosting ma R=1,154×, G=1,731×, B=0,879×. Pełna [rozpiska kanałów](NR-edit-channel-breakdown.md).

## Średnia odsetków R i G

Najlepszy z porównanych tu wariantów to:

```text
fR = count(E_R > epsilon) / count(E_R > 0)
fG = count(E_G > epsilon) / count(E_G > 0)
S_RG = (fR + fG) / 2
```

To uśrednienie dwóch odsetków **przed** wyznaczaniem jednego dodatniego mnożnika. Nie jest to konwersja RGB do luminancji. E jest signed FP32 editem po dotychczasowym mnożniku; neutralny edit = 0. Przy zwiększaniu mnożnika zmieniają się wyłącznie dodatnie E wszystkich trzech kanałów, z jednym wspólnym suwakiem. Żaden kanał poniżej neutralnego poziomu nie jest rozjaśniany przez mnożnik.

Zachowano poprzedni epsilon 0,0087917918. Dla każdej rodziny cel leży ponad wszystkimi pierwszymi dobrymi i złymi odsetkami. Nie dobierano nowego epsilon do tego pełnoobrazowego wyniku. Wstępne osobne przeszukanie 100 logarytmicznych epsilon na 512×288 nie poprawiło tego wyniku; pozostano przy wcześniejszej wartości.

| Pierwszy dobry zapis | Propozycja / użyty pierwszy dobry mnożnik |
|---|---:|
| game1-cnn-noghosting | 1,2044× |
| game1-cnn-noghosting2 | 1,0592× |
| game2-cnn-noghosting1 | 1,0010× |
| game2-cnn-noghosting2 | 1,0003× |
| game1-transformer-noghosting | 1,0021× |
| game1-transformer-noghosting2 | 1,1253× |
| game2-transformer-noghosting1 | 1,0141× |
| game2-transformer-noghosting2 | 1,1253× |

Maksymalny zapas wynosi **+20,44% CNN / +12,53% transformer**, wobec +33,24% / +38,50% poprzedniej mediany R/G/B. Przykładowe przeliczenie pierwszego dobrego 6×: do 7,23× / 6,75×; 10×: do 12,04× / 11,25×. Nie są to odczyty dawnych suwaków ani sprawdzone nastawy jakości w grze. Zgodność z pierwszymi dobrymi jest lepsza na tych plikach, nie daje jeszcze gwarancji zaniku ghostingu przy takim celu na nowych klatkach.

## Średnia dwóch najmniejszych propozycji mnożnika

Dla wcześniejszych trzech propozycji R/G/B policzono `mean(sort(m_R,m_G,m_B)[:2])`. Przykład game1-transformer-noghosting2 daje `(0,8574+1,3850)/2=1,1212×`, korzystniej niż 1,3850× mediany. Jednak game1-transformer-noghosting daje `(0,4237+1,0021)/2=0,7129×`: prawie **29% poniżej już pierwszej dobrej granicy**.

W 4 z 8 dobrych zapisów ta naiwna reguła proponuje zejście poniżej 1. Po przeskalowaniu jednym współczynnikiem na rodzinę tak, aby nie obniżała żadnej dobrej granicy i odrzucała złe, maksima wynoszą **1,3520× CNN / 1,5729× transformer**. To odpowiednio +35,2% / +57,3%. Nie wdrożono tej reguły. [Wszystkie proste średnie propozycji i kalibracja](validation/nr-edit-channel-gain-means.json).

Średnia samych propozycji R/G, przy zachowaniu starego wspólnego celu mediany, jest jeszcze inną regułą. Bez korekty daje do 1,4428× / 1,1253×; po analogicznym przeskalowaniu 1,2821× / 1,1082×. To nie są wyniki S_RG z pierwszej sekcji.

## Gdy „najniższe” oznacza odsetki, a nie mnożniki

Porównano też agregację dwóch największych odsetków (dwa mocniejsze kanały, odpowiadające mniejszym propozycjom mnożnika) i dwóch najmniejszych odsetków (dwa słabsze kanały). Przy tym samym epsilon, osobno skalibrowanym celu na CNN/transformer i pełnym obrazie:

| Agregacja odsetków | Największy względny mnożnik CNN | Transformer |
|---|---:|---:|
| Stałe R/G | 1,2044× | 1,1253× |
| Dwa największe odsetki | 1,3837× | 1,6369× |
| Dwa najmniejsze odsetki | 1,2848× | 6,0019× |

[Pełne trzy warianty i ich progi](validation/nr-edit-rg-two-channel-means.json). Średnia dwóch odsetków i średnia odpowiadających im propozycji mnożnika nie są wymienne: odwrócenie funkcji odsetek→mnożnik jest nieliniowe.

## Próba lekkiego pomiaru

Dla S_RG, z celami pełnoobrazowymi, przesunięto siatkę punktów przez 64 fazy (8×8), jak we wcześniejszych badaniach:

| Siatka | Liczba próbek | Najmniejsza/największa propozycja na pierwszych dobrych |
|---|---:|---:|
| 64×36 | 2304 | 0,7502×–2,0006× |
| 256×144 | 36864 | 0,8184×–1,2591× |

[64×36](validation/nr-edit-rg-mean-phases-64.json), [256×144](validation/nr-edit-rg-mean-phases-256.json). Nawet większa siatka potrafi zaproponować około 18% mniej niż pierwsza dobra granica. To dotychczasowe próbkowanie bez nowego marginesu/akumulacji, nie gotowy regulator. Nie ma pomiaru GPU tego konkretnego wariantu ani niezależnej walidacji temporalnej. Metryka opiera się na surowym edicie przed roundingiem FP16, jak poprzednia mediana; nowy test ekspozycji na realnym FP16 jest osobnym badaniem.

## Odtwarzanie

```powershell
python tools/analyze_nr_edit_channel_coverage.py `
  'E:/Gry/Marvels SpiderMan 2/dlssnr-edit/game*-*.dds' `
  'F:/Gry/The Last of Us - Part I/dlssnr-edit/game*-*.dds' `
  --full --modes positive_active --aggregations rg_mean top2_mean bottom2_mean `
  --epsilon 0.008791791791791792 `
  --output artifacts/edit-analysis/rg-means.json

python tools/analyze_nr_edit_channel_coverage.py `
  'E:/Gry/Marvels SpiderMan 2/dlssnr-edit/game*-*.dds' `
  'F:/Gry/The Last of Us - Part I/dlssnr-edit/game*-*.dds' `
  --phase-reference artifacts/edit-analysis/rg-means.json --phase-grid 256 144 `
  --output artifacts/edit-analysis/rg-phases.json
```

Pierwszy kandydat w tym porównaniu to rg_mean, co zapisano również w raportach faz. Wszystkie wyniki są dopasowaniem/oceną na tych samych 16 etykietach; nie publikujemy źródłowych obrazów i nie zmieniamy DLL/HLSL. S_RG jest obecnie lepszym tropem do dalszej walidacji niż pozostałe porównane tu średnie, z opisanym problemem próbkowania.

Następnie sprawdzono na prośbę użytkownika [różne wagi R/G/B](NR-edit-channel-weights-study.md): wspólny wektor dla CNN/transformer, bez zmiany kolorów obrazu.
