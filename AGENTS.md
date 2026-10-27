# Stałe instrukcje projektu

Te zasady obowiązują w każdej rozmowie Codex pracującej w tym repozytorium,
również przy nowych funkcjach, poprawkach i przeglądzie istniejącego kodu.
Nie wymagają ponownego potwierdzania przez użytkownika. Odpowiadaj po polsku.

## Repozytorium i publikowanie zmian

- Docelowy GitHub: https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure.
- Folder projektu jest checkoutem tego repozytorium, z remote `origin`.
  Przy inicjalizacji 2026-10-10 najnowszą gałęzią według daty ostatniego commita
  była `codex/nr-interpass-v12-interior-addressing`; `main` był starszy.
- Na początku pracy sprawdź gałąź, jej upstream i stan zmian. Pobierz aktualny
  stan GitHub przed integracją lub publikacją. Kontynuuj bieżącą gałąź zadania;
  nie przełączaj automatycznie na `main` ani inną gałąź tylko z powodu jej nazwy.
- Użytkownik udzielił stałej zgody: każdą ukończoną zmianę wprowadzoną w ramach
  zadania należy automatycznie zapisać w commicie i opublikować na GitHubie
  przez `git push` do właściwej gałęzi. Dotyczy to także dokumentacji i tych
  instrukcji. Nie kończ pracy wyłącznie na lokalnej edycji i nie pytaj ponownie
  o zgodę na zwykły commit/push.
- Dodawaj do commita tylko pliki należące do zadania. Zachowuj cudze i wcześniej
  istniejące niezacommitowane zmiany. Nie publikuj sekretów ani przypadkowych
  plików roboczych. Nie używaj force-push i nie nadpisuj historii.
- Jeśli gałąź zdalna się zmieniła, zintegruj jej zmiany z zachowaniem pracy.
  Dla nowej gałęzi ustaw upstream przez `git push -u origin <branch>`.
  Gdy reguły repozytorium wymagają PR, opublikuj gałąź i utwórz PR.
- Jeśli Git nie może publikować, użyj dostępnego połączenia GitHub, jeśli
  pozwala wykonać tę samą zmianę, a następnie zsynchronizuj lokalny checkout.
  Jeśli publikacja nadal jest zablokowana, zachowaj pracę i jasno podaj powód;
  nie twierdź, że zmiany są już na GitHubie.
- Po publikacji sprawdź zgodność lokalnego HEAD z gałęzią zdalną. W odpowiedzi
  podaj gałąź, commit, wynik kontroli oraz ewentualne ograniczenia.

## Priorytet: czas przetwarzania GPU

- Wydajność GPU jest kluczowym kryterium przy nowych funkcjach i przeglądach.
  Każde skrócenie czasu GPU o 0,1 ms jest istotne; analizuj także mniejsze zyski.
- Oceniaj koszt całego toru renderowania oraz zmienionych etapów. Szukaj
  zbędnych dispatchy, przebiegów, odczytów/zapisów tekstur, kopii, barier,
  synchronizacji CPU/GPU i alokacji w każdej klatce. Uwzględniaj przepustowość,
  zajętość GPU, rejestry, pamięć współdzieloną i koszt tworzenia PSO.
- Zachowuj poprawność, jakość obrazu, HDR, krawędzie obrazu i kompatybilność
  obsługiwanych API. Zmianę jakości przedstaw jako świadomy kompromis.
- Dla zmian wykonujących pracę na GPU porównuj bazę i wariant z użyciem
  znaczników czasu GPU w porównywalnych warunkach, po rozgrzewce, z wieloma
  próbkami (np. A/B lub ABBA). Podawaj GPU, scenę, rozdzielczość, ustawienia,
  czas przed/po w ms, różnicę i zmienność; sam FPS nie wystarcza.
- Nie przedstawiaj teoretycznej oszczędności jako pomiaru. Jeśli pomiar na GPU
  nie jest dostępny, zaznacz to i podaj sposób weryfikacji. Diagnostyka i pomiary
  nie powinny dodawać niepotrzebnego kosztu do domyślnej ścieżki renderowania.

## Weryfikacja zmian

- Regularny benchmark w grze ma porównywać użyteczne ścieżki, bez sztywnego
  limitu 14 testów. Zachowuj Off i oba tryby referencyjne bez optymalizacji.
  P50, P59 i P65 reprezentują trzy zachowane tory; dodaj bieżący procent,
  gdy jest inny. Automat porównuj ze znanym zwycięzcą w kolejności ABBA,
  z osobną rozgrzewką, oraz z drugim zachowanym trybem zoptymalizowanym.
  Bazowy zestaw to 18 unikalnych konfiguracji / 24 okna pomiarowe, albo 24 / 32
  z dodatkową skalą. Bieżąca skala <50% również używa ABBA oraz alternatywnego
  Classic/RGB20. Nie przywracaj do rutynowego benchmarku odrzuconych
  wariantów ani wielu bliskich skal bez konkretnej nowej hipotezy.
- Osobna weryfikacja poniżej 50% porównuje 33/40/41/42/49% oraz bieżący niższy
  procent, gdy jest inny. P45 pomijamy jako powtórzenie zachowanego toru RGB20.
  Zachowuje Off, oba tryby referencyjne, alternatywny Classic/RGB20 i automat
  przeciw niezależnemu wymuszonemu zwycięzcy w ABBA, z osobną rozgrzewką.
  To 30 konfiguracji / 40 okien lub 36 / 48 z dodatkową skalą.
  Generic Fused optimized poniżej 50% przegrał wszystkie dostarczone pomiary
  P33/P40/P41/P42/P45/P49; nie dodawaj go z powrotem bez nowej hipotezy.
  RGB20 może używać bezpośredniego fallbacku
  dla grup niemieszczących się w kaflu; raportowanie PSO nie oznacza użycia
  LDS w każdej grupie. Nie zmieniaj polityki na podstawie samego syntetycznego
  pomiaru shadera ani nie nazywaj niezmierzonych skal potwierdzonymi zwycięzcami.
- Celowany profil Boundary porównuje wyłącznie 41% i 42%, bez dopisywania
  bieżącej skali: 12 konfiguracji / 16 okien, ABBA i alternatywny tor.
  Wyniki v18/v19 z gry potwierdzają Classic przy P33/P41 oraz RGB20 przy
  P40/P42/P45/P49. Produkcyjna reguła v20 dopuszcza RGB20 przy dokładnym
  wyrównaniu native/work=5/2 na obu osiach oraz od zaokrąglonego P42
  (tolerancja pół texela); dokładne P50 ma pierwszeństwo swojej specjalizacji.
  Odcinki niezmierzone, szczególnie ułamkowe P41..P42, są konserwatywną regułą,
  nie runtime autotuningiem. Nie powtarzaj P33/P40/P45/P49 w profilu Boundary;
  dostarczone pomiary nie dowodzą
  zwycięstwa we wszystkich skalach pomiędzy tymi punktami.

- Stosuj `CONTRIBUTING.md`, w szczególności zasady PCH. Uruchamiaj kontrole
  odpowiednie do zakresu zmiany; nie wymagaj kompilacji dla samej dokumentacji.
- Przy zmianach inter-pass NR używaj odpowiednich testów `tests/test_nr_interpass_*.py`
  i istniejącego workflow `.github/workflows/build_nr_detail_quality_experiments.yml`.
  Dla zmian HLSL sprawdzaj także kompilację shaderów i aktualizację wymaganych
  nagłówków/binariów zgodnie z workflow. Test arytmetyki nie zastępuje pomiaru GPU.

## Regresje integracyjne NR / Superior — wnioski z recenzji janblade

Źródła: [PR #249 — recenzje i scalenie](https://github.com/janblade/OptiScaler-F5-DLSSNR-Multipass/pull/249),
[poprawka cyklu życia i dokumentacji #260](https://github.com/janblade/OptiScaler-F5-DLSSNR-Multipass/pull/260).
Zasady poniżej wynikają z błędów znalezionych do 2026-10-27; dotyczą także
kolejnych portów i nowych opcji, nie tylko checkboxa IsHDR.

- Nowa opcja konfiguracji wymaga sprawdzenia całego obiegu: deklaracja i default,
  odczyt/zapis INI, reset/ładowanie profilu oraz filtrowanie zapisywanych kluczy.
  W gałęziach z `OptiScaler/dlssnr/DlssNr_Profiles.h` każdy klucz `[DlssNr]`
  zapisywany przez `SaveIni` musi należeć do dokładnie jednej z list
  `kKeptKeys` / `kLeftOutKeys`. Sam wpis `NR_PROFILE_FIELD` nie wystarcza.
  Przy takiej zmianie uruchom `tests/nr_profiles_smoke.cpp` z aktualnym
  `OptiScaler/Config.cpp` jako argumentem; testy Superior nie zastępują testu profili.
- Stosuj konwencje istniejącego menu: zwykły `ImGui::Checkbox("label", &value)`
  i opis w `HelpMarker`, zamiast własnego checkboxa `##` z osobnym `TextWrapped`
  i ręcznym `IsItemClicked`. Kontrolkę bez wpływu na daną ścieżkę wyszarzaj
  zgodnie z rzeczywistą dostępnością API/funkcji (np. Vulkan lub niedziałające
  Superior). Eksperyment i kompromis jakości opisz użytkowo; szczegóły tworzenia
  instancji i resetów historii zapisuj w dokumentacji technicznej, bez obowiązku
  umieszczania ich w tooltipie. Użytkownik i janblade zaakceptowali ich pominięcie.
- Przy zmianie struktury stanu, klucza cache lub wymiaru tablicy wyszukaj wszystkie
  kopie, snapshoty i operacje save/restore/swap, również `ViewState` i ścieżki
  kilku widoków. W #249 `EdgeCompression::madeFor` urósł z 6 do 8 elementów,
  a kopia per-view została przy 6: Git scalił to bez konfliktu, ale Release
  nie kompilował się przy `std::swap`. Preferuj wspólny typ/stałą rozmiaru lub
  kontrolę zgodności typów zamiast niezależnych magicznych rozmiarów.
- Przed publikacją odczytaj aktualny base PR i pobierz tę konkretną gałąź
  docelową, także staging/release. Sprawdź wynik integracji z jej aktualnym
  stanem i wykonaj adekwatny build/testy na wyniku integracji; zielony build
  samej starej gałęzi źródłowej ani czysty merge Git nie dowodzą kompatybilności.
  Zmiany polityki CI/formatowania wydzielaj z poprawek renderowania, chyba że
  użytkownik wyraźnie zleci ich wspólne wprowadzenie.
- Rozróżniaj czas życia preferencji, wyboru presetu, instancji NGX i zasobów GPU.
  CNN A/E ma pozostać wybrane podczas zmian DRS/Auto i zwykłego odtwarzania
  zasobów, ale wybór nie może bezwarunkowo przetrwać shutdown NR/NGX oraz nowej
  sesji urządzenia/runtime. Zapewnij jawny reset takiego latcha w odpowiednich
  ścieżkach teardown; nie ukrywaj go w lokalnym `static` bez dostępu do resetu.
  Testuj zarówno zachowanie wyboru podczas DRS, jak i ponowny wybór po shutdown.
  Przy multi-view świadomie ustal, który stan należy do widoku, a który do sesji.
- Flagi tworzenia prywatnego DLSS uwzględniaj w tożsamości instancji. Ich zmiana
  wymaga prawidłowego odtworzenia i historii z bezpiecznym odroczeniem zwolnień
  GPU; samo ustawienie parametrów evaluate nie zmienia starej instancji.
  Checkbox IsHDR ma usuwać wyłącznie IsHDR: AutoExposure, MVLowRes, DepthInverted
  i ustawienia DLSS/HDR gry muszą pozostać zgodne z uzgodnionym zakresem.
- Klucz resetu historii nie może zawierać każdej zmiany aktywnego render subrect,
  gdy siatka koloru NR pozostaje taka sama. Rozdziel rozmiar alokacji, aktywny
  subrect oraz siatki koloru i depth/MV; przy zmianie samych guides nie odtwarzaj
  całego packed color. Sprawdzaj rozmiary nadnatywne, niezależne osie/origins,
  jednostki MV i rzeczywiste warunki użycia render-resolution guides.
- Testy shaderów muszą trafiać w warianty rzeczywiście wybierane przez kod hosta.
  Prześledź warunki dispatchu i usuń martwe entry pointy/bloby albo podłącz je
  do realnego ustawienia. Nie zastępuj brakującej referencji wariantem bieżącym
  i nie nazywaj porównania shadera z nim samym walidacją. Wymagaj niezależnej
  referencji oraz liczbowych granic błędu sprawdzanych asercją, nie tylko wydrukiem;
  samo fused-vs-separate nie wykrywa błędu wspólnej funkcji.
- Waliduj prawdziwe formaty głębi (w tym D16/D24), kwantyzację, reversed Z,
  brakujące dane oraz NaN/Inf. Nie zakładaj, że idealne płaszczyzny R32F
  dowodzą poprawności tolerancji dla UNORM. Niepoprawną głębię zastępuj daleką
  płaszczyzną właściwą dla konwencji Z. Granice work-list sprawdzaj przed
  odejmowaniem/dzieleniem unsigned i przed odczytami/zapisami tekstur.
- Nowe parametry HLSL muszą mieć bezpieczny fallback również po stronie shadera
  (np. `1/(sigma*sigma)` musi być skończone i dodatnie, inaczej użyj wartości dla 0.015).
  Pola stałych używane pod innym znaczeniem dokumentuj nazwanymi setterami
  i kontrolami ABI. Generator referencji musi potwierdzać trafienie każdej
  oczekiwanej podmiany, zamiast kontynuować po nietrafionym `replace`.
- Po zmianie UI aktualizuj zgodnie wszystkie cytaty etykiet, tooltipów i stanów
  wyszarzenia w dokumentacji oraz opisie PR. Przed publikacją sprawdź finalny
  diff i wymagane dla zakresu testy, również poza zestawem testów Superior.
  Rozdzielaj wynik buildu/testu syntetycznego, obserwację z gry i hipotezę
  jakościową; nie dopisuj potwierdzenia działania w grze bez takiego wyniku.

## Oczekiwanie na działania zewnętrzne

- Gdy dalsza praca zależy od zakończenia buildu, GitHub Actions lub innego
  działania na GitHubie, wstrzymaj aktywną pracę agenta do zakończenia tego
  działania, aby oszczędzać tokeny. Usypiaj wykonanie przez dostępne narzędzie
  oczekiwania. W tym czasie nie prowadź zbędnych analiz, nie wykonuj pętli
  częstego odpytywania statusu i nie generuj powtarzalnych komentarzy.
- Preferuj powiadomienie o zakończeniu lub blokujące oczekiwanie na zmianę
  stanu. Gdy ich nie ma, stosuj rzadkie kontrole rozdzielone rzeczywistym
  uśpieniem, zgodnie z limitami narzędzi. Niezmieniony status oznacza ponowne
  uśpienie, bez kolejnej analizy. Samo sprawdzanie statusu nie jest uśpieniem.
- Po zakończeniu działania wznów pracę, sprawdź wynik i kontynuuj weryfikację
  oraz publikację. Oczekiwanie nie oznacza zakończenia zadania ani zgłoszenia
  sukcesu przed otrzymaniem wyniku z GitHub.
