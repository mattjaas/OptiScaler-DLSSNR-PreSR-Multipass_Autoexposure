# CNN: sprawdzenie reguły znalezionej na transformerze

2026-10-31. **Reguła P90 z progiem 0,14 nie działa dla dostarczonych próbek CNN.** Poprawnie oznacza cztery przypadki z ghostingiem, ale błędnie odrzuca wszystkie cztery bez ghostingu: 4/8. Nie wystarczy zmienić samego progu P90, ponieważ wartości klas nakładają się także wewnątrz drugiej gry. Wynik ten ogranicza wniosek [pierwszego badania transformera](NR-edit-ghosting-study.md).

Przeanalizowano osiem nowych podpisanych DDS po mnożniku dodatniego editu, przed kolorami, dodaniem neutralnego 0,5 i clippingiem. Format, rozmiary, skończoność RGB i hashe zweryfikowano tak jak poprzednio. Etykiety CNN/ghosting pochodzą od użytkownika; pliki nie zawierają metadanych identyfikujących konkretny preset, wersję DLSS ani suwaki. Nie przypisujemy im na tej podstawie A/E/F. Oryginalne obrazy pozostają poza repozytorium.

## Wyniki

W obu przypadkach mierzymy `A=max(0, edit.r, edit.g, edit.b)` po wszystkich pikselach. P50 oznacza medianę, a P90 90. percentyl. Używana definicja to element o indeksie `ceil(q*N)-1` w uporządkowanej tablicy.

| Plik CNN, bez `.dds` | Etykieta | P90(A) | P50(A) |
|---|---|---:|---:|
| game1-cnn-ghosting | ghosting | 0,044139 | 0,016685 |
| game1-cnn-ghosting2 | ghosting | 0,043335 | 0,018066 |
| game1-cnn-noghosting | brak | 0,112988 | 0,041895 |
| game1-cnn-noghosting2 | brak | 0,074707 | 0,039551 |
| game2-cnn-ghosting1 | ghosting | 0,045107 | 0,011743 |
| game2-cnn-ghosting2 | ghosting | 0,093750 | 0,015625 |
| game2-cnn-noghosting1 | brak | 0,087581 | 0,023705 |
| game2-cnn-noghosting2 | brak | 0,113281 | 0,023438 |

Konkretny kontrprzykład dla P90: `game2-cnn-ghosting2` ma 0,09375, a `game2-cnn-noghosting1` 0,087581. Słabszy górny zakres dodatniego editu nie oznacza tu automatycznie ghostingu. Również średnia i RMS dodatnich kanałów mają nakładające się wyniki.

**Mediana wygląda lepiej w tym zbiorze:** ghosting 0,011743–0,018066, brak ghostingu 0,023438–0,041895. Przykładowy próg 0,021 rozdziela pełne obrazy 8/8. Równoważny test pokrycia, z zastrzeżeniem dokładnych remisów na progu, to co najmniej połowa pikseli z `A>0,021`. P50 wybrano już po obejrzeniu etykiet CNN, więc to nowa hipoteza, nie niezależne potwierdzenie detektora.

CNN w przekazanych obrazach bywa bez ghostingu przy P90 znacznie niższym niż transformer. Nie wynika z tego uniwersalna różnica tolerancji modeli ani stały przelicznik między mnożnikami. Nie należy porównywać liczbowo targetu P90=0,15 z targetem P50=0,025 jako dowodu, że mnożnik CNN może być sześć razy mniejszy: są to inne statystyki różnych editów.

## Dlaczego nie wystarczy teraz wdrożyć mediany

Przy wyznaczaniu progu mediany na jednej grze i sprawdzaniu drugiej:

* CNN game2 → game1: próg 0,01953125, poprawne 4/4.
* CNN game1 → game2: próg 0,02880859, poprawne 2/4; oba przypadki bez ghostingu zostały odrzucone.

Łącznie 6/8, mimo pełnej separacji po dopasowaniu progu do całego zbioru. Jest to ten sam prosty sposób kalibracji, którego użyto w pierwszym raporcie. Wynik nie dowodzi, że żaden wspólny próg nie może działać — 0,021 rozdziela obecne pełne obrazy — ale pokazuje, że procedura kalibracji nie przenosi się niezawodnie między grami.

Przy ustalonym progu 0,021 i teście `fraction(A>0,021) >= 50%` sprawdzono te same 64 fazy każdej siatki:

| Siatka | Poprawne etykiety: 8 plików × 64 fazy |
|---|---:|
| 32×18 | 487/512 |
| 64×36 | 489/512 |
| 128×72 | 505/512 |

Wszystkie błędy dla 64×36 i 128×72 dotyczą `game2-cnn-noghosting2`. W pełnym obrazie tylko 50,2553% pikseli przekracza 0,021. Dla siatki 64×36 pokrycie waha się od 49,2188% do 51,7361%, więc werdykt potrafi się zmienić wraz z fazą. Większa siatka nie daje jeszcze pełnej stabilności przy tym progu.

Mediana dla 64×36 w tym pliku waha się od 0,01953125 do 0,0234375 wobec pełnego wyniku 0,0234375. Reguła `m_docelowe=target/P50` przy najniższym odczycie zaproponuje mnożnik o 20% większy niż przy pełnym pomiarze, nawet przed kwantyzacją histogramu. To istotne dla celu użytkownika: zbyt silny mnożnik może pogarszać stabilność obrazu. Zwiększanie częstotliwości tej samej siatki bez zmiany fazy nie usuwa błędu próbkowania.

Można dopasować niższy próg do tych samych danych, ale nie byłby to dowód, że usunięto problem w nowych scenach. Nie wybieramy kolejnego progu wyłącznie po to, aby ponownie otrzymać 512/512.

P50 nie jest też zamiennikiem dla transformera: w `game1-transformer-noghosting2` mediana wynosi około 0,002441, mimo braku ghostingu. Nie znaleziono jednego wspólnego progu dla obu modeli w sprawdzonych prostych statystykach. Eksploracja obejmowała percentyle dodatniej amplitudy 50/75/90/95/98/99/99,5/99,9%, luminancję dodatnią, pokrycia dla kilkunastu progów, średnią/RMS i wybrane normalizacje przez ujemny edit. Nie jest to dowód niemożliwości skonstruowania bogatszego wskaźnika.

## Koszt GPU i sprawdzenie samego pomiaru

Testowy shader histogramu dostał parametr rangi percentyla zamiast stałego P90. Cbuffer nadal ma 32 bajty; jego ostatnie pole jest teraz jawnie nazwane `QuantilePermille`. Produkcyjne shadery, ABI, ustawienia i DLL nie zmieniają się. Test CPU niezależnie wybiera element przez `nth_element`.

Na RTX 4090 oraz WARP przeszło po **13455 przypadków liczbowych**: 16 dostarczonych DDS i 5 syntetycznych tekstur × 2 percentyle (P50/P90) × 64 fazy × 5 metod. Liczniki i liczba próbek są porównywane dokładnie, a P50/P90 muszą mieścić się w przedziale zwróconym przez histogram. Do 13440 regularnych przypadków dochodzi 15 sprawdzeń niepoprawnej rangi percentyla; shader stosuje wtedy P90. Dla P50 licznik używa progu 0,021, dla P90 0,14. Poprawność obliczenia statystyki nie oznacza poprawności prognozy ghostingu.

Nowy pomiar etapu: RTX 4090, sterownik 616.56, D3D12, residentne rzeczywiste DDS RGBA FP32, DXC cs_6_0 -O3. Osobne rozgrzewki 64 powtórzenia, 4×16 kwartetów ABBA, 128 okien na wariant, 8 powtórzeń na okno. A=brak pomiaru, B=pomiar z barierami UAV. Mediana A wynosi 0 ms przy rozdzielczości użytych znaczników. Nie jest to bazowy czas całej klatki.

| Źródło | Pomiar P50 | A mediana ms | B mediana ms (P10–P90) | B−A ms |
|---|---|---:|---:|---:|
| game1-cnn-ghosting, 2240×1264 | licznik tiled64 | 0 | 0,003100 (0,002944–0,003253) | +0,003100 |
| game1-cnn-ghosting, 2240×1264 | histogram hist256 | 0 | 0,005596 (0,005504–0,005888) | +0,005596 |
| game2-cnn-ghosting1, 2496×1404 | licznik tiled64 | 0 | 0,002864 (0,002816–0,003072) | +0,002864 |
| game2-cnn-ghosting1, 2496×1404 | histogram hist256 | 0 | 0,005054 (0,004864–0,005248) | +0,005054 |

Koszt obliczenia mediany jest mały; problemem pozostają wybór i stabilność wskaźnika. Pomiar nie obejmuje NR/NGX, odczytu dwóch żywych tekstur model/proxy, sterowania mnożnikiem ani jego zastosowania. Upload, alokacje, readback i oczekiwanie CPU są poza oknami. Surowe dane zawierają również P90 i wszystkie pięć wariantów testowych; nie są pomiarem w grze ani potwierdzeniem minimalnego narzutu finalnej integracji.

## Wniosek dla automatu

Nie stosować wspólnej reguły P90=0,15 do CNN. Mechanizm taniego histogramu można współdzielić, ale wybór percentyla/progu musi być świadomy aktualnego modelu i zweryfikowany niezależnie. Dla CNN P50 jest kandydatem do dalszego sprawdzania, bez podstaw do ogłoszenia niezawodnego regulatora. W szczególności nie wdrażać bezwarunkowo `m=0,025/P50` jako funkcji „usuń ghosting”.

Potrzebne są oznaczone przejścia ghosting→brak ghostingu przy mnożnikach wokół granicy, w ruchu i w nowych scenach, z automatycznym zapisem ustawień. Z obecnych zapisów nie wyznaczymy najmniejszego skutecznego mnożnika. Zmiana modelu, ekspozycji i trybu wejścia DLSS może wymagać innej kalibracji. Te ograniczenia dotyczą również wcześniejszej hipotezy dla transformera.

## Odtwarzanie i dane

```powershell
# Najpierw zamrożona reguła transformera, bez dopasowywania do CNN:
python tools/analyze_nr_edit_ghosting.py `
  'E:/Gry/Marvels SpiderMan 2/dlssnr-edit/game1-cnn-*.dds' `
  'F:/Gry/The Last of Us - Part I/dlssnr-edit/game2-cnn-*.dds' `
  --output artifacts/edit-analysis/cnn-study-fixed.json

# Dopiero później eksploracyjny kandydat mediany:
python tools/analyze_nr_edit_ghosting.py `
  'E:/Gry/Marvels SpiderMan 2/dlssnr-edit/game1-cnn-*.dds' `
  'F:/Gry/The Last of Us - Part I/dlssnr-edit/game2-cnn-*.dds' `
  --threshold .021 --coverage-threshold .5 --target .025 `
  --output artifacts/edit-analysis/cnn-study-median.json

$files = @(Get-ChildItem -LiteralPath `
  'E:/Gry/Marvels SpiderMan 2/dlssnr-edit', `
  'F:/Gry/The Last of Us - Part I/dlssnr-edit' `
  -Filter 'game*-cnn-*.dds' | Sort-Object FullName | ForEach-Object FullName)
./tools/test_nr_edit_signal_probe.ps1 -DdsPaths $files `
  -TimingCsv artifacts/edit-analysis/cnn-gpu-timing.csv
./tools/test_nr_edit_signal_probe.ps1 -Warp -DdsPaths $files
# Użyj wszystkich 16 ścieżek CNN i transformera do pełnych 13455 przypadków.
```

[Pełne podsumowanie obu reguł, hashe i fazy](validation/nr-edit-ghosting-cnn-study.json), [cechy CNN](validation/nr-edit-ghosting-cnn-study.csv), [surowe okna GPU](validation/nr-edit-signal-probe-cnn-timing.csv). Raport JSON przechowuje granice faz i liczby trafień; szczegółowe fazy odtwarza skrypt. Wersja 2 raportu jawnie zapisuje wybrany percentyl, próg pokrycia i informację, czy dany próg rzeczywiście rozdziela klasy. Zachowano historyczne wyniki pierwszego badania transformera.
