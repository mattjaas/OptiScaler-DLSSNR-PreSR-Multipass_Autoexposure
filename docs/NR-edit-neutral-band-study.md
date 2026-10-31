# Udział wartości oddalonych od neutralnego 0,5 — osobno RGB

2026-10-31. Użytkownik zaproponował liczenie dostatecznej liczby pikseli/kanałów poza pasmem wokół 0,5, a następnie pomiar osobno dla R, G i B. Sprawdzono te pomysły na tych samych 16 DDS; SHA-256 wszystkich źródeł ponownie zgadza się z wcześniejszym raportem.

## Transport a pomiar

Dla potwierdzonego trybu editu, nasycenia 1 i koloru głębi wyłączonego transport działa **per kanał**:

```text
C = clamp(0.5 + min(E,0) + m*max(E,0), 0, 65504)
```

E to edit przed mnożnikiem; zapisane DDS mają już zastosowany mnożnik. Zwiększenie go nie zmienia ujemnych składowych i nie może zmniejszać odległości żadnego kanału od 0,5. Neutralny kanał pozostaje 0,5. W tym badaniu ani w DLL nie zmieniono tej zasady.

Wcześniejsze sformułowanie w rozmowie o „zbliżaniu ciemniejszych pikseli do 0,5” było mylące: dotyczyło jasności **obliczonej z całego RGB**. Przykład: RGB=(0,6; 0,3; 0,3), po podwojeniu dodatniego editu RGB=(0,7; 0,3; 0,3). G/B pozostają nietknięte, ale `Y=0,25R+0,5G+0,25B` rośnie z 0,375 do 0,4 i zbliża się do 0,5. To argument przeciw zastępowaniu odległości kanałów pojedynczym Y, a nie wada mnożnika. Pomiar kanałowy tego problemu nie ma.

## Co przetestowano

`analyze_nr_edit_neutral_band.py`: odsetek poza pasmem dla jednego/dwóch/trzech kanałów, uśrednionych składowych, średniej i RMS odległości oraz pomocniczo Y z wagami 0,25/0,5/0,25 i 0,2126/0,7152/0,0722. Pomocniczy wariant |Y−0,5| nie jest monotoniczny, więc używa sweepu wejść/wyjść z pasma, nie założenia o monotoniczności. Najlepszy wstępny wariant kanałowy wymagał do około 2,13×; na pełnych obrazach przy wybranym epsilon do 2,121× pierwszego dobrego mnożnika. Nie wybrano go jako regulatora.

`analyze_nr_edit_channel_coverage.py` liczy osobno fR/fG/fB:

- `positive`: odsetek kanału powyżej `0,5+epsilon`, w całym obrazie.
- `both`: odsetek poza pasmem `0,5±epsilon`; ujemna część jest stała podczas symulacji mnożnika.
- `positive_active`: dodatnie wartości przekraczające epsilon / wszystkie dodatnie wartości danego kanału. Zera nie powiększają mianownika.
- `both_active`: oba znaki, z wyłączeniem dokładnie zerowego editu z mianownika.

Sprawdzono min, medianę, średnią, średnią geometryczną, RMS i max trzech odsetków. Pusty kanał ma odsetek zero; brak sygnału wymaga osobnej obsługi przed integracją regulatora. Wstępna siatka 512×288, 100 logarytmicznych epsilon od 0,00025 do 2. Następnie dodatkowo 1000 epsilon 0,002–0,025 dla `positive_active/median`, wybranych po obejrzeniu poprzednich wyników: to selekcja na tych samych danych, nie niezależna walidacja.

Cel dla rodziny leży ponad wszystkimi wynikami złymi i pierwszymi dobrymi. Liczone są względne propozycje mnożnika przy niezmienionej jasności NR i bazowym edicie. Dopuszczamy wyłącznie zapas w górę; nie potrzeba od użytkownika liczbowych ustawień dawnych suwaków. Testy nie zakładają, która para zmieniała jasność, a która mnożnik.

## Poprzedni kandydat per kanał: mediana RGB

Pełnoobrazowa weryfikacja trzech wybranych epsilon wskazała `positive_active/median`, epsilon **0,0087917918**. Jest to mediana trzech odsetków, nie mediana jasności/pikseli. Jeden cel na CNN i jeden na transformer:

| Model | Cel | Propozycje względem pierwszych dobrych mnożników | Największy zapas |
|---|---:|---|---:|
| CNN | >0,9332133 | 1,1542×; 1,0003×; 1,3324×; 1,1253× | +33,24% |
| Transformer | >0,9734622 | 1,0021×; 1,3850×; 1,1662×; 1,1253× | +38,50% |

Kolejność: game1-noghosting, game1-noghosting2, game2-noghosting1, game2-noghosting2. Te wartości są błędem względem pierwszej dobrej granicy, nie odczytem użytych suwaków. Przykład przeliczenia: graniczne 6× oznaczałoby do około 8,0× dla CNN / 8,3× dla transformera; 10× do 13,3× / 13,9×. Nie ma obserwacji z gry, że akurat taki zapas powoduje niestabilność ani że jest akceptowalny.

Mniejsze cele nie dają tej samej jednostronnej gwarancji względem wszystkich pierwszych dobrych granic. Przy tym epsilon dla transformera etykiety nadal się nakładają: jeden pierwszy dobry wynik 0,9332892 jest poniżej złego 0,9393794. Sam ten odsetek nie rozpoznaje zatem wszystkich 16 stanów bez zmiany mnożnika.

Na 64 fazach punktowej siatki 64×36 przy celach z pełnych obrazów propozycje dla pierwszych dobrych zapisów sięgają **0,7202×–2,5722×**; rozrzut jest za duży do uznania takiego rzadkiego pomiaru za gotowy precyzyjny regulator. Nie wdrożono go do runtime. Nie przetestowano jeszcze kosztu GPU tego konkretnego wariantu, stabilizacji w czasie, jakości wyjścia DLSS ani niezależnej gry/sekwencji. Wyniki statyczne nie gwarantują braku ghostingu po osiągnięciu celu.

[Podsumowanie wstępnego przeszukania](validation/nr-edit-neutral-band-screen.json), [pełne obrazy](validation/nr-edit-channel-coverage-full.json), [fazy próbkowania](validation/nr-edit-channel-coverage-phases.json), [pomocnicze pełne pasma RGB/Y](validation/nr-edit-neutral-band-full.json).

## Odtwarzanie i następny test

```powershell
python tools/analyze_nr_edit_channel_coverage.py `
  'E:/Gry/Marvels SpiderMan 2/dlssnr-edit/game*-*.dds' `
  'F:/Gry/The Last of Us - Part I/dlssnr-edit/game*-*.dds' `
  --full --modes positive_active --aggregations median `
  --epsilon 0.007872193511965187 0.008791791791791792 0.016274099701472082 `
  --output artifacts/edit-analysis/channel-full.json

python tools/analyze_nr_edit_channel_coverage.py `
  'E:/Gry/Marvels SpiderMan 2/dlssnr-edit/game*-*.dds' `
  'F:/Gry/The Last of Us - Part I/dlssnr-edit/game*-*.dds' `
  --phase-reference artifacts/edit-analysis/channel-full.json `
  --output artifacts/edit-analysis/channel-phases.json
```

Domyślne uruchomienie bez `--full`, `--epsilon`, `--modes` i `--aggregations` odtwarza szerokie przeszukiwanie. Pominięto rounding FP16; górny clamp nie został osiągnięty przez omawianego kandydata. To analiza CPU, bez zmian produkcyjnych shaderów, konfiguracji i kosztu domyślnego renderowania. Niezależne kontrole potwierdziły niezmienność kanałów ujemnych i monotoniczność odległości RGB oraz osiąganie progów dla przypadków stałej ujemnej składowej.

Do sprawdzenia hipotezy w odniesieniu do faktycznych obiektów i ruchu przygotowano [zapis editu wraz z depth/MV](NR-object-contrast-study.md). DLL Release dla `f6e99081` przeszła pełny build, profile, polityki, ABI/shadery i testy GPU WARP; lokalnie testy GPU przeszły też na RTX 4090. Niniejsza dalsza analiza nie zmienia tej DLL.

Dalsze badanie na prośbę użytkownika: [wszystkie bieżące algorytmy ekspozycji na 16 editach](NR-edit-exposure-study.md), z wykonaniem rzeczywistych PSO, osobnymi R/G/B i audytem mnożnika. Nie zmienia produkcyjnego transportu ani DLL.

Rozpiska wcześniejszych +33–39% na R/G/B, porównanie trzech oddzielnych progów oraz dodatkowe pasma pojedynczych kanałów: [NR-edit-channel-breakdown.md](NR-edit-channel-breakdown.md).

Dalsza propozycja użytkownika: [średnia odsetków R/G oraz dwóch wybranych kanałów](NR-edit-rg-mean-study.md), z oddzielnym wyjaśnieniem średniej gotowych propozycji mnożnika.
