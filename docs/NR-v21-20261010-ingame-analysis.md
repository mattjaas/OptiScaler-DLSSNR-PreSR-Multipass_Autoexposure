# NR v21: Fast w The Last of Us Part I

W dostarczonym pomiarze całego toru NR Fast wygrał z dokładnym Optimized przy
50%, 59% i 65%. Użytkownik po ocenie obrazu zgłosił 11 października 2026:
„nie widziałem problemów z wyglądem”. To potwierdzenie użyteczności Fast w sprawdzonej
scenie i konfiguracji. Nie oznacza bitowej zgodności ani jednakowej jakości
we wszystkich scenach, grach i podczas dowolnego ruchu.

Nie zmieniamy matematyki Optimized, domyślnego wyboru ani zakresu selekcji Fast.
Zachowany kod v21 jest punktem odniesienia. Ta aktualizacja zapisuje wyniki z gry
oraz ocenę użytkownika i nie wprowadza kolejnego wariantu shadera.

## Warunki pomiaru

- Gra: The Last of Us Part I. Nazwa konkretnej sceny/kamery nie została zapisana
  (`scene=unspecified`). Nie ma informacji o przebiegu oceny obrazu w ruchu ani o trybie HDR.
- GPU: NVIDIA GeForce RTX 4090, sterownik DXGI `32.0.16.1656`.
- Native: 3840×2160. Working: P50 1920×1080, P59 2266×1274, P65 2496×1404.
- Temporal DLAA residual + P100-guided (`Transfer=9`), 6 rzeczywiście wykonanych
  passów, Area, radius 1, guide strength 1, range sigma 0,015, spatial sigma 3,0.
- Manual shaping (`shaping=2`), shadow gate wyłączony. Szczegółowe wartości gainów
  i ustawień poszczególnych passów nie znajdują się w dostarczonych metadanych.
- 90 próbek rozgrzewki i 160 pomiarowych w każdym oknie. Ręczne zatrzymywanie
  okien wyłączone. Zegary i temperatura nie były kontrolowane.
- Dla każdej skali: Off, Classic reference, Fused reference,
  Optimized / Fast / Fast / Optimized. Łącznie 15 konfiguracji, 21 okien i 3360 próbek.
- Timestamp obejmuje cały przedział NR na GPU, z NGX i zależnościami toru.
  Wyników nie przypisujemy wyłącznie shaderowi inter-pass ani czasowi całej klatki gry.

## Czas całego NR

Mediany zbiorcze Optimized i Fast obejmują po 320 próbek na skalę.
SD jest odchyleniem standardowym populacyjnym pojedynczych próbek w ms.
Procent to skrócenie mediany przedziału NR, nie wzrost FPS.

| Skala | Optimized ms | Fast ms | Zysk ms | Skrócenie NR | SD Optimized / Fast ms | P95 Optimized / Fast ms |
|---|---:|---:|---:|---:|---:|---:|
| P50 | 26,32858 | 26,11917 | 0,20941 | 0,80% | 0,19557 / 0,18035 | 26,64243 / 26,41408 |
| P59 | 33,35066 | 32,73370 | 0,61696 | 1,85% | 0,17343 / 0,17005 | 33,66093 / 33,01990 |
| P65 | 37,26336 | 36,58598 | 0,67738 | 1,82% | 0,17999 / 0,19415 | 37,60947 / 37,00326 |

P95 jest niższe dla Fast we wszystkich trzech skalach. Przy P65 SD Fast jest nieco
wyższe niż Optimized, mimo niższych mediany i P95. Nie utożsamiamy niższego czasu
z niższą zmiennością każdej miary.

## Kontrola ABBA i dryftu

Poniższe liczby to mediany kolejnych okien po rozgrzewce.
Symetryczny zysk to `(A1 + A2) / 2 - (B1 + B2) / 2`, liczony z median okien,
nie z mediany wszystkich próbek. Te dwa estymatory nie muszą dać identycznej liczby.

| Skala | A1 Optimized ms | B1 Fast ms | B2 Fast ms | A2 Optimized ms | Symetryczny zysk ms | Rozrzut A / B ms |
|---|---:|---:|---:|---:|---:|---:|
| P50 | 26,31629 | 26,11712 | 26,11917 | 26,33370 | 0,20685 | 0,01741 / 0,00205 |
| P59 | 33,33632 | 32,73421 | 32,73114 | 33,35270 | 0,61184 | 0,01638 / 0,00307 |
| P65 | 37,25261 | 36,58598 | 36,58803 | 37,27309 | 0,67584 | 0,02048 / 0,00205 |

Przewaga Fast występuje po obu stronach ABBA: porównania A1−B1 i A2−B2 dają
odpowiednio 0,19917 / 0,21453 ms dla P50, 0,60211 / 0,62157 ms dla P59
i 0,66662 / 0,68506 ms dla P65. Zysk jest dużo większy od rozrzutu median okien,
a wynik symetryczny jest bliski zbiorczemu. Dane nie wskazują, by zwycięstwo Fast
było skutkiem wyłącznie jednokierunkowego dryftu podczas tego przebiegu.

To jeden przebieg i dwa okna na wariant w każdej skali. Pojedyncze próbki nie są
automatycznie niezależnymi powtórzeniami eksperymentu. Nie wyznaczamy na tej podstawie
uniwersalnej gwarancji wydajności ani niekontrolowanego przedziału ufności.

## Ścieżki i odniesienia

Raportowane Optimized: P50 `Fused optimized P50/general`, P59 `Fused RGB20`,
P65 `Fused RGB16`. Fast we wszystkich trzech skalach: `Fast guided (approximate)`.
Nie wystąpił raportowany fallback ani mieszanie ścieżek w oknach.
`path_matches_control=0` przy Fast jest oczekiwane: porównuje nazwy ścieżek,
nie jakość czy poprawność obrazu.

| Skala | Off ms | Classic reference ms | Fused reference ms | Zysk Fast vs Fused reference ms |
|---|---:|---:|---:|---:|
| P50 | 25,62662 | 27,98797 | 28,11802 | 1,99885 |
| P59 | 31,95034 | 34,78579 | 37,03296 | 4,29926 |
| P65 | 35,76371 | 38,79373 | 41,75206 | 5,16608 |

Fast nadal kosztuje więcej niż Off: różnice median całego NR wynoszą
0,49254 / 0,78336 / 0,82227 ms dla P50/P59/P65. Off nie wykonuje rekonstrukcji
inter-pass i nie jest odniesieniem o tej samej jakości. Off i referencje mają
po jednym wcześniejszym oknie, więc te porównania mają słabszą kontrolę dryftu
niż bezpośrednie ABBA Optimized/Fast.

## Wniosek i zakres potwierdzenia

Dla sprawdzonej konfiguracji Fast daje użyteczny zysk przy wszystkich trzech
skalowaniach, przy braku problemów wizualnych zauważonych przez użytkownika.
Największe oszczędności wystąpiły przy P59 i P65, około 0,62–0,68 ms całego NR.
P50 również wykazuje powtarzalną przewagę, około 0,21 ms. Wcześniejszy syntetyczny
test pojedynczego dispatchu nie potwierdzał zysku P50. Nie ma sprzeczności:
pomiar z gry obejmuje cały tor i używa innych ustawień (spatial sigma 3,0,
manual shaping, rzeczywiste wejścia modeli).

Ocena jakości pochodzi od użytkownika. Nie wykonano automatycznego porównania
obrazu Optimized/Fast z dostarczonych plików, ponieważ zawierają tylko czasy
i metadane. Niezmierzone skale, inne sceny, ustawienia i liczby passów pozostają
niepotwierdzone. Obserwacja nie uzasadnia zmiany dokładnego Optimized na przybliżenie.

## Audyt danych

Zweryfikowano wszystkie 3360 próbek i 21 okien: indeksy 0–159 w każdym oknie,
bez luk i duplikatów, poprawna kolejność, wymiary, sześć passów i raportowane ścieżki.
Z raw odtworzono mean, medianę, P95 nearest-rank, min/max, SD populacyjne,
mediany okien, ich rozrzut i wszystkie delty względem Off, Fused oraz Optimized.
Maksymalna różnica względem zaokrąglonego raportu to 0,000004981 ms,
zgodna z precyzją zapisu. Historyczne zyski pozostają NaN.

Niezmienione źródła pomiaru z 10 października 2026, 23:57:43 czasu z nazwy pliku:

- [Podsumowanie CSV](NR-v21-fast-20261010-235743.csv),
  SHA256 `f38b43a57a6886cbab17cc128df1167a5046945794ed6483acbbdf3e71896f1d`.
- [Surowe próbki CSV](NR-v21-fast-20261010-235743.samples.csv),
  SHA256 `8df90ff3e6ae03058d962dffbb23ac87677bf0142ed7b8e7116b2e11e920a10f`.
- [Metadane TXT](NR-v21-fast-20261010-235743.txt),
  SHA256 `360a9e93eaf3d1553a2994b3e30d42dd52b6993d7d1381ce341dfbe13d8e78b8`.
