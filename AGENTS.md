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

- Stosuj `CONTRIBUTING.md`, w szczególności zasady PCH. Uruchamiaj kontrole
  odpowiednie do zakresu zmiany; nie wymagaj kompilacji dla samej dokumentacji.
- Przy zmianach inter-pass NR używaj odpowiednich testów `tests/test_nr_interpass_*.py`
  i istniejącego workflow `.github/workflows/build_nr_detail_quality_experiments.yml`.
  Dla zmian HLSL sprawdzaj także kompilację shaderów i aktualizację wymaganych
  nagłówków/binariów zgodnie z workflow. Test arytmetyki nie zastępuje pomiaru GPU.
