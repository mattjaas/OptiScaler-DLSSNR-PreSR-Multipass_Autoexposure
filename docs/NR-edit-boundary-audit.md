# Doprecyzowanie: próbki bez ghostingu są na granicy

2026-10-31. Użytkownik doprecyzował, że pliki `noghosting` przedstawiają praktycznie pierwszy moment zaniku ghostingu, bez zapasu. W jednej parze zmieniał się wyłącznie dodatni mnożnik przy stałej Model input brightness, w drugiej wyłącznie Model input brightness przy stałym mnożniku. Nie podano przypisania tych dwóch procedur do numerów plików; nie zakładamy go.

**Wspólny wskaźnik RGB S nie nadaje się do wyznaczania najniższego skutecznego mnożnika przy jednym celu na rodzinę modelu.** Jego wcześniejsze 16/16 pozostaje prawdziwym opisem separacji dostarczonych etykiet, ale nie waliduje regulatora. Progi 0,052 i 0,084 nie są celami, do których należy automatycznie sprowadzać edit.

## Właściwy test regulatora

Przyjmując zgodnie z doprecyzowaniem, że każdy `noghosting` leży blisko granicy dla swoich bieżących ustawień, regulator powinien proponować mnożnik bliski temu użytemu w takim zapisie. Nie trzeba znać jego wartości liczbowej: dla dodatnio jednorodnego wskaźnika S błąd można policzyć względnie. Przy niezmienionym bazowym edicie i jasności wejścia NR:

```text
S(m) = m * S(1)
proponowany_mnożnik / mnożnik_użyty_na_granicy = cel / S_zapisu_granicznego
```

W parze zmieniającej jasność bazowy edit też się zmienia. Nie wolno traktować przejścia pomiędzy jej plikami jako skalowania samym mnożnikiem. Względny test powyżej dotyczy lokalnej propozycji mnożnika przy ustawieniach konkretnego granicznego zapisu. Granica jest przybliżona; nie znamy rozmiaru kroków suwaków ani dokładnego czasu stabilizacji historii.

Wspólna funkcja i jeden cel na rodzinę wymagają odpowiednio zbliżonych wyników na granicznych zapisach **w jednostkach proponowanego mnożnika**. Samą monotoniczną kompresją S do [0,1] można sztucznie zmniejszyć rozrzut wyników, ale nie naprawić błędu po odwróceniu funkcji do mnożnika.

## Jednostronny zapas jest dopuszczalny

Kolejne doprecyzowanie użytkownika: niewielkie przekroczenie pierwszej dobrej wartości jest dopuszczalne; wartości z ghostingiem są niedopuszczalne. Głównym kryterium jest więc przedział **[1, 1+zapas]** proponowanego mnożnika względem pierwszego dobrego zapisu, a nie symetryczny błąd wokół 1. Symetryczne wartości poniżej pozostają wyłącznie historyczną diagnostyką rozrzutu.

Dla jednorodnego S minimalny wspólny cel to maksimum wyników dobrych oraz wartość ściśle powyżej maksimum wyników złych. Minimalny konieczny zapas wynosi `cel/min(S_dobre)-1`. Poprzedni S wymaga **33,34% dla CNN i 216,19% dla transformera**. Dopuszczenie małego jednostronnego zapasu nie rozwiązuje tego problemu. Skrypt zapisuje też wykonalność przykładowych limitów 2/5/10/15/20/30%; nie są to limity jakości narzucone przez użytkownika. S2 wymagałby już na pełnym obrazie około 27,4%, a rzadkie próbkowanie dodatkowo go destabilizuje.

Użytkownik potwierdził wejście: edit wokół 0,5, nasycenie bez zmiany, bez kolorowania głębi. Automat może regulować tylko dodatni mnożnik. Dalsze badanie kontrastu: [NR-object-contrast-study.md](NR-object-contrast-study.md).

## Dlaczego poprzedni S odpada

| Model | S w granicznych `noghosting` | Największy/najmniejszy | Propozycja mnożnika przez wcześniejszy cel, względem użytego na granicy |
|---|---|---:|---|
| CNN | 0,072918; 0,054686; 0,055973; 0,057610 | 1,3334× | 0,7131×; 0,9509×; 0,9290×; 0,9026× |
| Transformer | 0,286594; 0,092342; 0,166489; 0,090640 | 3,1619× | 0,2931×; 0,9097×; 0,5045×; 0,9267× |

Kolejność: game1-noghosting, game1-noghosting2, game2-noghosting1, game2-noghosting2, z odpowiednim modelem w nazwie. Użyto wcześniejszych celów klasyfikacji 0,052/0,084.

Na pierwszym granicznym transformerze cel 0,084 zaproponowałby około **71% mniej** mnożnika, mimo że zapis nie ma zapasu. Dla pierwszego granicznego CNN byłoby to około **29% mniej**. To istotna sprzeczność z przeznaczeniem regulatora.

Zmiana samych celów tego nie naprawia. Dla ustalonego S najlepszy stały cel w sensie najgorszego błędu multiplikatywnego wynosi `sqrt(S_min*S_max)`:

| Model | Taki cel, wyłącznie diagnostyczny | Zakres propozycji mnożnika względem granicznego |
|---|---:|---:|
| CNN | 0,063148 | 0,8660×–1,1547× |
| Transformer | 0,161173 | 0,5624×–1,7782× |

Nawet najlepszy taki kompromis dla transformera oznacza około −44% w jednym przypadku i +78% w drugim. Wybranie celu równego największemu granicznemu S, aby osiągnąć wszystkie zaobserwowane granice, dałoby z kolei aż **3,16×** mnożnika w innym przypadku; dla CNN do **1,33×**. To nie jest wyszukiwanie najmniejszej skutecznej wartości.

## Sprawdzona normalizacja przestrzenna

Po doprecyzowaniu sprawdzono też strukturę przestrzenną dodatniego editu. Jedna ze wspólnych funkcji, dopasowanych eksploracyjnie do pełnych granicznych obrazów, miała postać:

```text
p = max(edit.rgb, 0)
C = RMS(p) / RMS((p(x+1,y)-p(x,y), p(x,y+1)-p(x,y)))
S2 = C * (3,25*mean_lower_chroma + 0,5*occupancy_correction + pixel_min_mean)
     + amplitude/128
```

Nazwy składników są z [definicji wspólnego S](NR-edit-common-score.md). RMS gradientów liczy oba kierunki i trzy kanały, z całym mianownikiem i clampingiem na brzegu. C jest niezmiennicze względem dodatniego mnożnika, więc S2 nadal rośnie liniowo z nim. Stały/pusty sygnał wymaga stanu „brak podstaw do pomiaru”, bez dzielenia przez zero.

Na pełnych obrazach S2 zmniejsza rozrzut granicznych wyników do około 1,274× w obu rodzinach. To nadal około −11,4% do +12,9% błędu mnożnika nawet dla najlepszego stałego celu. Dodatkowo **rzadkie próbkowanie jest niestabilne**:

| Rodzina | Siatka | Najmniejsza/największa propozycja mnożnika na granicznych zapisach |
|---|---|---:|
| CNN | 64×36 | 0,2143×–1,5753× |
| CNN | 128×72 | 0,4089×–1,5265× |
| Transformer | 64×36 | 0,2499×–1,6191× |
| Transformer | 128×72 | 0,3938×–1,5175× |

Gradienty są zdominowane przez krawędzie, które takie siatki potrafią ominąć lub nadmiernie reprezentować. S2 odrzucono przed integracją z GPU/runtime; nie ma pomiaru jego kosztu GPU ani potwierdzenia jakości w grze. Dalsza eksploracja różnic przestrzennych z odstępami 4/8/16/32 pikseli również nie dała podstaw do precyzyjnego, stabilnego regulatora na rzadkiej siatce. Nie wybieramy kolejnej funkcji tylko dlatego, że można ją dopasować do tych samych ośmiu granic.

## Aktualny wynik prac

Z dotychczas sprawdzonych lekkich wskaźników nie potwierdzono jeszcze wspólnej funkcji z jednym celem na CNN i jednym na transformer, która trafnie wyznacza najniższy skuteczny mnożnik. Nie jest to dowód, że taka heurystyka nie istnieje. Wynik 16/16 dotyczył innego, słabszego kryterium.

Dalsza walidacja powinna oceniać zgodność z oznaczoną granicą, błąd proponowanego mnożnika, oddzielne zmiany jasności i mnożnika, stabilność próbkowania oraz nowe sekwencje w ruchu po ustabilizowaniu historii. Potrzebna będzie też obserwacja wyjścia prywatnego DLSS, aby odróżnić rzeczywisty zanik ghostingu od wzrostu statystyki wejścia. Nie ma podstaw do wdrożenia dotychczasowych progów jako automatu „bez ghostingu”.

## Odtwarzanie

Skrypt korzysta z istniejącego raportu i wymaga jawnego potwierdzenia interpretacji etykiet. Opcjonalne DDS odtwarzają odrzucony eksperyment S2; porównywane są ich SHA-256 ze źródłowym raportem. Oryginalne obrazy nie są publikowane.

```powershell
python tools/analyze_nr_edit_boundary.py `
  --study docs/validation/nr-edit-common-study.json `
  --first-clear-is-boundary `
  --output artifacts/edit-analysis/boundary-audit.json `
  --dds 'E:/Gry/Marvels SpiderMan 2/dlssnr-edit/game1-cnn-*.dds' `
        'F:/Gry/The Last of Us - Part I/dlssnr-edit/game2-cnn-*.dds' `
        'E:/Gry/Marvels SpiderMan 2/dlssnr-edit/game1-transformer-*.dds' `
        'F:/Gry/The Last of Us - Part I/dlssnr-edit/game2-transformer-*.dds'
```

[Wyniki audytu](validation/nr-edit-boundary-audit.json). Audyt sam nie zmienia renderowania ani shaderowych prototypów. Osobny zapis diagnostyczny opisano w badaniu kontrastu obiektów.
