# NR v22: porównanie obecnych shaderów z v14 przy P65

Aktualizacja po raporcie `NR-v22-p65-revision-20261011-005612`:
[pomiar w grze i analiza okien](NR-v22-P65-game-results.md) są ukończone.
Obecny/v14: 37,245440 / 37,233152 ms. Nie wykazano powtarzalnego zysku
wyjaśniającego historyczną różnicę; produkcyjna ścieżka pozostaje bez zmian.

W statusie NR jest przycisk **Compare current / v14 Optimized at 65% (CSV)**.
To celowany test zgłoszenia około 37,3 ms wobec historycznych około 37,0 ms.
Nie zmienia produkcyjnej polityki Optimized. Przygotowano go po
[porównaniu źródeł i pomiarach shaderów](NR-v21-P65-optimized-audit.md), które
nie ustaliły przyczyny pozostałych 0,129 ms względem Off.

## Co porównuje

Wyłącznie DX12, native **3840×2160**, working **2496×1404**, sześć passów,
Area, radius 1, guide strength 1. Przebieg ma **pięć konfiguracji / siedem okien**:

1. Off, bieżące shadery.
2. Classic reference, bieżące shadery.
3. Fused reference, bieżące shadery.
4. Obecny Optimized — A.
5. Optimized z archiwalnym standardowym shaderem i RGB16 v14 — B.
6. Ponownie B.
7. Ponownie A.

Każde okno ma własną rozgrzewkę i pomiar: domyślnie 90 / 160 próbek.
W podsumowaniu A i B mają po 320 próbek; raw zachowuje każde okno osobno.
Zmiana rewizji resetuje historię modeli tak jak zmiana ścieżki inter-pass.
Oba warianty używają tej samej bieżącej implementacji CPU, modeli NGX,
zasobów, stałych, gainów, barier i końcowego resolve. B zmienia standardowy
PSO dla jego wszystkich wywołań oraz RGB16 inter-pass. Pozostałe osobne PSO,
np. residual/finished/spatial, pozostają bieżące.

Archiwalne binaria pochodzą z prawdziwej paczki v14, są kontrolowane SHA256
i zgodnością z osadzającymi je nagłówkami. Nie są ponownie skompilowanym
obecnym HLSL pod historyczną nazwą. Ich źródło i sumy są w
`OptiScaler/shaders/dlssnr/precompile/benchmark_v14/README.md`.
PSO v14 tworzą się leniwie tylko w historycznych oknach tego profilu.
Zakończenie lub anulowanie usuwa override i przywraca zmienione ustawienia.
Inne benchmarki nie wybierają archiwalnych shaderów. Nie wracają odrzucone
wide/pair/cache ani macierz wielu rozdzielczości.

Test pozostawia bieżące **Enlargement**, HDR, sigmy, shaping, cienie i style
passów. W szczególności nie wymusza Transfer=9 jak profil FastQuality v21.
Metadata zapisuje transfer, sigmy, shaping, progi shadow, compounding,
parametry sześciu passów i ich efektywne inter-pass gains przy P65.
Zachowaj ustawienia obrazu i nieruchomą scenę przez cały pomiar.

## Jak uruchomić i odczytać wynik

Zainstaluj paczkę v22, uruchom tę samą scenę w 4K i pozostaw zwykłe ustawienia
obrazu. Włącz działający NR i naciśnij powyższy przycisk. Można zamknąć menu.
Profil sam ustawi P65 i sześć passów; niczego nie trzeba przełączać ręcznie.
Nie zmieniaj kamery, ustawień ani rozdzielczości w trakcie pomiaru.

Raporty powstaną obok gry w `OptiScaler-NR-Benchmarks`:
`NR-v22-p65-revision-*.csv`, `.samples.csv` i `.txt`.
Nieodpowiednia geometria, brak PSO albo faktyczny fallback przerywają ten test;
wynik fallbacku nie udaje poprawnej kontroli v14.

CSV zawiera rzeczywisty czas **całego przedziału NR na GPU**, z NGX,
przejściami zasobów i zależnościami. Patrz na medianę, P95, SD i mediany
dwóch okien każdej rewizji. `gain_vs_optimized_median_ms` jest dodatnie,
gdy v14 jest szybsze od obecnego Optimized. `delta_vs_expected_path_ms`
odnosi się tu do obecnego Optimized. `shader_revision` wskazuje `current`
albo `v14`; `selected_path` dla B powinno być `Fused RGB16 (v14 shaders)`.
Różna nazwa ścieżki między A i B jest oczekiwana; `path_matches_control`
nie jest oceną jakości ani oznaką błędu tego porównania. Historyczny gain
jest NaN, aby nie udawać pomiaru starych shaderów w obecnej scenie.

Jeżeli B jest powtarzalnie szybsze, istnieje różnica w kompilowanych shaderach
ujawniająca się w tym pełnym torze; następne porównanie może oddzielić
standard od RGB16. Jeżeli A i B są równoważne w granicach zmienności okien,
same te zmiany shaderów nie wyjaśniają dawnego czasu w danej scenie.
To nadal nie odtwarza wszystkich historycznych ustawień, obciążenia ani
taktowania. Test nie jest uruchomieniem całego starego DLL v14.

## Weryfikacja przed publikacją

- Test archiwalnych hashów i nagłówków, siedmiu okien, ograniczenia profilu,
  resetu i przywracania ustawień: PASS.
- Zachowane testy arytmetyki i zestawów benchmarków: PASS.
- Kompilacja bieżących standard/RGB16/RGB20/Fast: PASS; ich SHA256 są
  identyczne z v21. HLSL i produkcyjna polityka nie zostały zmienione.
- GPU RTX 4090, dodatkowa zgodność standardowego Mode18 z v14: PASS,
  32 porównania, 3520 składowych RGBA, FP16/FP32, HDR, non-finite,
  nieparzyste rozmiary i brzegi. Obowiązuje wyjątek dla payloadu NaN.
- Dotychczasowa zgodność inter-pass Mode27/28 z rzeczywistą bazą v14:
  6528 porównań, 53 543 552 składowych RGBA (audyt v21).

Pomiar profilu w grze dostarczono i sprawdzono; szczegóły w podlinkowanej
analizie v22. Nie ogłoszono usunięcia regresji.

## Opublikowana i zweryfikowana paczka

Kod benchmarku: `e0543ba47510f2ed1d2081a3d69a7992aecbec00`, gałąź
`codex/nr-interpass-v13-bounded-area`.
[GitHub Actions 38091799380](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/actions/runs/38091799380)
zakończyło się sukcesem: testy Python, kompilacja shaderów, kontrola polityki,
fixtures GPU z Mode18, pełny MSBuild i publikacja release.

[Release v22](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/releases/tag/nr-interpass-v22-p65-revision-20261011)
zawiera [ZIP do instalacji](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/releases/download/nr-interpass-v22-p65-revision-20261011/OptiScaler-NR-nr-interpass-v22-p65-revision-20261011.zip).
Rozmiar: 131 349 011 bajtów.

- SHA256 ZIP: `9dc0959a81561e38e716b8b4978f53bc48722f5caeecb0c065655a1ed4aada9d`.
- SHA256 DLL: `128cab484e43c65d50fe8b86255aa5223226be767bdab8225640a86311c33f09`.
- Pobrany ZIP: CRC i wszystkie 62 pliki manifestu SHA256SUMS — PASS.
- DLL Windows x64: etykiety nowego profilu i sześć dokładnych blobów shaderów
  (cztery bieżące, dwa archiwalne v14), każdy osadzony raz — PASS.
- 35 odrzuconych blobów eksperymentalnych nie występuje w DLL — PASS.
- Hash pobranej paczki jest zgodny z digestem assetu GitHub — PASS.

Pomiar i analiza trzech raportów `NR-v22-p65-revision-20261011-005612` są
ukończone. Nie trzeba ponownie wykonywać gotowych kontroli syntetycznych.
Udany build sam w sobie nie rozstrzyga różnicy czasu GPU.
