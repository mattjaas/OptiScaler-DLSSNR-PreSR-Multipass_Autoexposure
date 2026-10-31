# Kontrast między obiektami a dodatni mnożnik editu

2026-10-31. Hipoteza użytkownika: podobna jasność obiektów na wejściu prywatnego DLSS sprzyja ghostingowi, a większe zróżnicowanie go usuwa. Celem pozostaje wspólna lekka funkcja dla CNN i transformera, ewentualnie z różnymi progami; zmienia ona tylko dodatni mnożnik. Pierwsze dobre ustawienie jest dolną granicą, niewielki dodatni zapas jest dopuszczalny. Nie potwierdzono jeszcze takiego regulatora.

## Co sprawdzono na istniejących 16 DDS

Odtworzono zwykły nośnik `C(rho)=max(0.5+min(E,0),0)+rho*max(E,0)`, gdzie E jest zapisanym editem już po mnożniku, a rho zmienia wyłącznie ten mnożnik. Dla użytych zakresów górny limit FP16 nie jest osiągany; w tym wstępnym badaniu pominięto zaokrąglenie FP16. To obowiązuje przy potwierdzonym wejściu residualnym, bez nasycenia i kolorowania głębi.

Z dokładnych średnich prostokątów zbudowano siatki 16×9, 32×18, 64×36 i 128×72. Zbadano średnią/RMS bezwzględnej różnicy i kontrastu Michelsona pomiędzy sąsiadami, kwantyle tego kontrastu, globalne CV, kontrast kwantyli 10/90 i 25/75 oraz CV sąsiedztwa 3×3. Osobno RGB i jasność `0.25R+0.5G+0.25B`. Cel dla rodziny musi odrzucić wszystkie złe zapisy i osiągać wszystkie pierwsze dobre; badany zapas jest tylko w górę.

W zgrubnym przeszukaniu rho najlepsza z tych miar potrzebowała w najgorszym dobrym zapisie **3× dotychczasowego mnożnika**; dokładne optimum dla takiej miary leży powyżej 2×, do 3× w badanej siatce. To nie jest mały zapas. Nie uznajemy średniego/globalnego kontrastu za rozwiązanie. Przykładowa miara `16x9_luma_difference_p1` ma wartości na dobrych granicach CNN 0,01987–0,03667, a transformera 0,02874–0,05677. Sam rozdział złych i dobrych etykiet nie wystarcza do doboru minimalnego mnożnika.

To **nie obala kontrastu na granicach rzeczywistych obiektów**: prostokąty mieszają obiekty, tekstury i tło. Z samych starych editów nie znamy ich granic ani ruchu. Jedna statyczna klatka nie potwierdza temporalnego wyniku DLSS. Nie wybrano progu automatu na podstawie tych 16 klatek.

[Podsumowanie wszystkich kandydatów](validation/nr-edit-coarse-contrast.json). To eksploracja na tych samych danych, bez niezależnego testu jakości.

```powershell
python tools/analyze_nr_edit_contrast.py `
  'E:/Gry/Marvels SpiderMan 2/dlssnr-edit/game*-*.dds' `
  'F:/Gry/The Last of Us - Part I/dlssnr-edit/game*-*.dds' `
  --output-dir artifacts/edit-analysis/coarse-contrast
```

## Nowy zapis do sprawdzenia granic obiektów

Obok `Save pre-DLSS edit` jest **`Save edit + object guides`**. Ta sama nazwa pliku tworzy w `dlssnr-edit` przy DLL cztery pliki:

- `nazwa.dds`: dotychczasowy signed FP32 RGBA edit po dodatnim mnożniku, przed kolorem, przycięciem i dodaniem 0,5. Brak editu = 0.
- `nazwa.depth.dds`: R32_FLOAT, głębia faktycznie przekazywana prywatnemu DLSS.
- `nazwa.motion.dds`: R32G32_FLOAT, te same wektory co dla prywatnego DLSS, w pikselach jego siatki, skala [1,1].
- `nazwa.json`: ustawienia transportu i ekspozycji, preset prywatny i NR, odwrócenie Z, wymiary, numer klatki/epoki, reset historii, held/packed i maski passów. Parametry trim są kontrolkami, **nie odczytem rzeczywistego GPU white point**; JSON jawnie oznacza jego brak.

Wszystkie trzy obrazy mają tę samą aktywną siatkę i początek (0,0), także przy packed guides i DRS. Stan ustawień zamrożony jest podczas nagrywania klatki, nie późniejszego zapisu plików. Bez nadpisywania; kolizja dowolnego z czterech plików anuluje komplet. Nie ma nowych kluczy INI.

Potrzebny pierwszy test w grze: jedna para z identyczną jasnością wejścia NR i resztą opcji, zmieniony tylko dodatni mnożnik: wyraźny ghosting oraz pierwsza dobra wartość. Każdy zapis po ustabilizowaniu obrazu; przy porównywalnym ruchu/scenie. Zachować cały czteroplikowy komplet. To pozwoli sprawdzić kontrast po obu stronach granic głębi zamiast traktować każdy kwantyl obrazu jak obiekt. Następnie potrzebny niezależny test drugiej rodziny modelu i gry. Głębia nie rozdzieli wszystkich obiektów ani przezroczystości; nie jest sama w sobie detektorem ghostingu.

## Koszt i kontrola poprawności

Bez żądania zapisu nie ma nowych dispatchy, kopii ani zasobów GPU; produkcyjne HLSL, bloby i ABI są niezmienione. Po żądaniu dwa dodatkowe snapshoty depth/MV, a odczyt wszystkich trzech obrazów jest jednym prywatnym command listem i fence na rzeczywistej kolejce gry. Snapshoty pozostają żywe dopóki nagranie gry może być odtworzone; prywatne readbacki są niezmienne po pierwszym submit. Limit 512 MiB obejmuje retained snapshoty i bieżący komplet. Nagranie snapshotów zaczyna i kończy się w COPY_SOURCE, także przy replay. Zasoby są zachowywane również po częściowym niepowodzeniu nagrywania.

`nr_edit_dump_smoke`: nagłówki i payload trzech formatów, padding, błędny format, bezpieczna nazwa, JSON z niezależną lokalizacją i obsługą NaN. `edit_dump_completion`: pojedynczy i potrójny odczyt, DIRECT/COMPUTE, otwarty cykl życia nagrania, replay bez dodatkowej naprawy stanów, niezmienność readbacków i payload DDS; PASS RTX 4090 i WARP. `private_exposure_quality`: 1402 przypadki przeciw niezależnej referencji, PASS na obu. Testy Superior/polityk PASS. Zapis uruchamiany menu nadal wymaga potwierdzenia w grze.

Syntetyczny pomiar samych kopii: RTX 4090, sterownik 616.56, rezydentne proceduralne dane, 8 osobnych rozgrzewek na wariant, 4 bloki × 8 kwartetów ABBA = 64 okna GPU na wariant/rozmiar. A: dotychczasowy snapshot editu → readback; B: dodatkowe snapshoty depth/MV i trzy readbacki. Liczone są bariery i kopie. Bez NR, NGX, produkcji editu mode 43, alokacji, uploadu, CPU/dysku i opóźnienia pomiędzy submitami gry. Nie jest to benchmark całego toru gry.

| Rozmiar | A mediana ms (P10–P90) | B mediana ms (P10–P90) | B−A ms |
|---|---:|---:|---:|
| 2240×1264 | 1,719712 (1,717306–2,109901) | 3,168352 (3,042269–3,535818) | +1,448640 |
| 2496×1404 | 2,131104 (2,127552–2,234570) | 3,810000 (3,794803–4,007818) | +1,678896 |

[Surowe timestampy](validation/edit-dump-guides-timing.csv), odtwarzanie: kompilacja `tests/nr_gpu/edit_dump_copy_timing.cpp` z MSVC C++17, d3d12.lib i dxgi.lib. Jest to koszt jednorazowej diagnostyki, nie koszt metryki ani automatu. Nie mierzono kosztu całego toru ani poprawy ghostingu w grze. Brak aktywnej gry podczas prac uniemożliwia taki pomiar; należy porównać zapis zwykły i rozszerzony w tej samej scenie po rozgrzewce, timestampami GPU, z wieloma oknami ABBA.
