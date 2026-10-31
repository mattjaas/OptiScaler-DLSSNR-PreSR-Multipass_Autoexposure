# Rozpiska wcześniejszego pomiaru na R / G / B

2026-10-31. Odpowiedź na pytanie o kanały stojące za wcześniejszymi nadwyżkami +33,2% CNN / +38,5% transformer. **Tamten wskaźnik był medianą trzech odsetków**, nie trzema niezależnymi regulatorami. Wszystkie poniższe propozycje kanałowe są hipotetycznym wynikiem oceny; w obrazie nadal obowiązuje jeden wspólny dodatni mnożnik. Nie wprowadzono oddzielnego mnożenia R/G/B.

## Dokładna rozpiska tamtego wskaźnika

Użyto tego samego epsilon **0,0087917918** i tych samych pełnych obrazów. Dla kanału c:

```text
f_c = count(E_c > epsilon) / count(E_c > 0)
wskaźnik = median(f_R, f_G, f_B)
```

E jest zapisanym editem już po mnożniku, więc neutralny edit = 0, a odpowiadający poziom nośnika = 0,5. Podczas szukania propozycji skaluje się tylko dodatnie E. To rozpiska wcześniejszego badania bez rounding FP16, nie nowy pomiar ekspozycji na rzeczywistych shaderach. Cel CNN >0,9332132657, transformer >0,9734622296.

Każda kolumna R/G/B odpowiada na pytanie: **jakiego względnego mnożnika zażądałby sam ten kanał, aby osiągnąć wspólny cel tamtej mediany?** Mediana trzech takich propozycji odtwarza poprzedni wynik; sprawdza to asercja w narzędziu. Wartości <1 to hipotetyczne propozycje redukcji oparte wyłącznie na danym kanale, nie zmiany zastosowane do obrazu.

| Pierwszy dobry zapis | Tylko R | Tylko G | Tylko B | Wynik mediany / kanał wyznaczający |
|---|---:|---:|---:|---:|
| game1-cnn-noghosting | 1.154x | 1.731x | 0.879x | 1.154x (R) |
| game1-cnn-noghosting2 | 1.500x | 1.000x | 0.750x | 1.000x (G) |
| game1-transformer-noghosting | 1.002x | 1.029x | 0.424x | 1.002x (R) |
| game1-transformer-noghosting2 | 0.857x | 1.385x | 9.003x | 1.385x (G) |
| game2-cnn-noghosting1 | 1.034x | 1.332x | 1.705x | 1.332x (G) |
| game2-cnn-noghosting2 | 1.125x | 1.125x | 0.818x | 1.125x (R/G) |
| game2-transformer-noghosting1 | 0.972x | 1.166x | 1.372x | 1.166x (G) |
| game2-transformer-noghosting2 | 1.125x | 1.125x | 0.563x | 1.125x (R/G) |

Największe wcześniejsze nadwyżki wynikają z G:

- `game2-cnn-noghosting1`: R 1,034×, G **1,332×**, B 1,705×; mediana 1,332×.
- `game1-transformer-noghosting2`: R 0,857×, G **1,385×**, B 9,003×; mediana 1,385×.

Przykładowo przy pierwszym dobrym mnożniku 6× druga pozycja oznacza hipotetyczne R=5,14×, G=8,31×, B=54,02×, a wybrany **jeden wspólny** mnożnik to 8,31×. W tym badaniu niczego nie ustawiono w grze. Sam słabszy odsetek B nie dowodzi ghostingu: ten plik użytkownik oznaczył jako pierwszy dobry.

Pełne odsetki R/G/B i propozycje także dla ośmiu złych zapisów: [JSON](validation/nr-edit-channel-breakdown.json), [CSV](validation/nr-edit-channel-breakdown.csv).

## Trzy osobne kryteria z własnymi progami

To osobne pytanie: gdyby każdy kanał miał własny próg, jak duże nadwyżki proponowałby jako jedyny regulator? Przy tym samym epsilon dla każdego modelu/kanału cel przyjęto ponad maksimum wszystkich pierwszych dobrych i złych odsetków. Otrzymano maksima:

| Wybór jednego kanału | CNN | Transformer |
|---|---:|---:|
| R | 1,4404× (+44,0%) | 1,2004× (+20,0%) |
| G | 1,7313× (+73,1%) | 1,2861× (+28,6%) |
| B | 2,3983× (+139,8%) | 18,0056× (+1700,6%) |

To nie jest tabela poprzednich nadwyżek mediany. Progi są teraz inne: CNN R/G/B = 0,9320763 / 0,9332133 / 0,9527245; transformer = 0,9771080 / 0,9712244 / 0,9890455. Wszystkie pojedyncze kanały przy tym epsilon mają nakładanie wyników dobrych i złych; nie dają pełnego klasyfikatora etykiet.

## Dodatkowe dopasowanie szerokości pasma dla pojedynczego kanału

Żeby nie odrzucić kanału wyłącznie przez epsilon dobrany wcześniej dla mediany, sprawdzono R/G/B osobno na 100 logarytmicznych epsilon od 0,00025 do 2, siatka wstępna 512×288. Wybrane epsilon oraz wcześniejszy medianowy sprawdzono na pełnych obrazach (łącznie cztery epsilon × trzy kanały). Najlepsze z tych pełnoobrazowych wyników:

| Kanał | Epsilon | Maksymalny względny mnożnik CNN | Transformer |
|---|---:|---:|---:|
| R | 0,00787219 | 1,4019× | 1,1289× |
| G | 0,00879179 | 1,7313× | 1,2861× |
| B | 0,29723628 | 1,4902× | 2,0635× |

R jest najrówniejszy z tych pojedynczych kryteriów i ma mniejszą nadwyżkę w transformerze niż wcześniejsza mediana, ale większą w CNN. To wynik selekcji na tych samych 16 etykietach, nie niezależne potwierdzenie jakości. Duża zmiana wyniku B po zmianie epsilon podkreśla, że 18× z poprzedniej tabeli nie jest uniwersalną właściwością B.

Na 64 fazach punktowej siatki 64×36 przy nowych pełnoobrazowych celach R propozycje na pierwszych dobrych zapisach sięgają **0,4886×–4,0306×**. Nie potwierdza to gotowego lekkiego regulatora. Nie ma pomiaru kosztu GPU ani testu temporalnej jakości dla tego kryterium. [Pełne wyniki pojedynczych kanałów](validation/nr-edit-channel-alone-full.json), [fazy R](validation/nr-edit-channel-alone-r-phases.json).

## Odtwarzanie

```powershell
python tools/analyze_nr_edit_channel_coverage.py `
  'E:/Gry/Marvels SpiderMan 2/dlssnr-edit/game*-*.dds' `
  'F:/Gry/The Last of Us - Part I/dlssnr-edit/game*-*.dds' `
  --channel-breakdown-reference docs/validation/nr-edit-channel-coverage-full.json `
  --output artifacts/edit-analysis/channel-breakdown.json
```

Do osobnego przeszukania kanałów: `--modes positive_active --aggregations r g b`, opcjonalnie `--full --epsilon ...`. Do faz wybranego kryterium: `--phase-reference <pełny_raport.json>`. Jest to wyłącznie analiza danych i narzędzie CPU. DLL, HLSL, profile i konfiguracja pozostają bez zmian.

Wykonane w tym samym zadaniu [obliczenia wszystkimi bieżącymi miernikami ekspozycji](NR-edit-exposure-study.md) są osobnym badaniem, z rzeczywistą kwantyzacją FP16 oraz kontrolą produkcyjnych PSO na RTX/WARP.

Sprawdzono następnie średnią R/G oraz średnią dwóch najmniejszych wyników: [NR-edit-rg-mean-study.md](NR-edit-rg-mean-study.md). R<G<B występuje tylko w 3 z 8 pierwszych dobrych zapisów; w pozostałych B jest najmniejszy.
