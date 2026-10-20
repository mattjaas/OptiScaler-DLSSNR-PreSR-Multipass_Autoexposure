# NR v59 — wyższy kafel końcowego low

Wdrożono niewielką optymalizację końcowego low Superior Scaling:
**grupa 8×16 i współdzielony kafel 36×68 FP32** zamiast 8×8 / 36×36.
Baza: v58 `c241e38477578ec38af230ce11a441539f975d41`.
Pozostałe sprawdzone pomysły nie trafiają do ścieżki produkcyjnej.

## Zmiana produkcyjna i koszt

Pełna grupa obsługuje 128 wyników i współdzieli 2448 próbek. Liczba
próbek na wynik spada z 20,25 do 19,125, czyli o 5,56%. To redukcja
próbkowania w tym etapie, nie procent przyspieszenia całego NR.
Każdy wynik nadal używa wszystkich 64 tapów, tych samych wag,
kolejności sumowania, normalizacji, sanitizacji i clampu.
Cache i arytmetyka pozostają FP32.

LDS na grupę rośnie z 15 552 do **29 376 B**, poniżej limitu 32 KiB.
To istotny kompromis dotyczący zajętości GPU; wynik na innej architekturze
może być gorszy i nie został tu zmierzony. Nie wprowadzono wyboru według
procentu skali ani producenta GPU.

Host wysyła grupy 8×16 wyłącznie przy udanym dedykowanym PSO low.
Nieudane utworzenie PSO nadal daje główny shader i grupy 8×8.
Wszystkie wątki, również poza ostatnią częściową grupą, przechodzą przez
synchronizację przed odrzuceniem wyniku. Niezgodne wymiary wejść zachowują
zerowy wynik. Wymagania pozostają SM6.0, bez natywnych działań FP16.

Nie dochodzi PSO, dispatch, tekstura, kopia, bariera zasobu, alokacja
w klatce, pomiar ani opcja menu. Zmienia się tylko istniejący low CSO/H;
**pozostałe 21 śledzonych CSO są bajtowo identyczne z v58**.
Nowy SHA256 low: `0fdad72b4c7b21a77196151656889e5099ff3ee92d2ef1a1962bc2d6d8ea2185`.
Interpassy, modele, gainy, średnia low, HDR i specjalizacja soft knee v58
pozostają zachowane.

## Obraz i kontrole

RTX 4090 / NVIDIA 616.56, D3D12: **264 przypadki, 8 816 280 słów
32-bitowych, zero różnic bitowych** względem v58, bez tolerancji.

- 198 małych przypadków / 54 648 słów: dziewięć wymiarów od 1×1 do
  257×143, w tym wiersz, kolumna, nieparzyste rozmiary i częściowe grupy.
- 66 przypadków / 8 761 632 słowa: work 1920×1080, 2266×1274,
  2496×1404, odpowiadające native 4K przy P50/P59/P65.
- Niezależnie FP16/FP32 źródło i wynik, signed HDR, małe/duże wartości,
  NaN/Inf oraz surowe wzorce binary16: ±0, subnormal, granice zakresu,
  sNaN/qNaN i signed szum. Porównano też alpha i surowe niefinitywne słowa.

Produkcyjny shader jest bajtowo identyczny z mierzonym prototypem.
Nowe fixture C++ kompilują się w MSVC v142; powtórzony mały test na
gotowym shaderze także przeszedł. Wszystkie dziewięć kontroli interpassów,
benchmark, integracja Superior i kompilacja shaderów przeszły lokalnie.
Workflow sprawdza grupę 8×16 i pojedynczy cache 7344 floatów oraz buduje DLL v143.

Historyczne pomiary mają jawne rozmiary grup odniesienia: fixture v58
używa low 8×8, a fixture v53/v54 low 8×4. Zmiana bieżącej stałej hosta
nie może powodować wysyłania zbyt małej liczby grup starego shadera.

Zgodność dotyczy tych danych i GPU. Nie przeprowadzono nowego pełnego
testu NGX/NR, oceny ruchu w grze ani pomiaru innych kart.

## Czas GPU — bardzo mały zysk

RTX 4090 / NVIDIA 616.56, native 3840×2160, P50/P59/P65 jak wyżej.
Rezydentny syntetyczny obraz sinusoidalny i residual, Standard 0,8/0,661,
guide radius 1, range 0,015, spatial 3, strength 1; SDR i HDR mapping 0/2/4.
Residual, low i wynik są RGBA16F, proxy/original RGBA32F.

Timestampy D3D12 obejmują **końcowy low + niezmieniony resolve i bariery**.
Nie obejmują inicjalizacji, uploadu, PSO, carrier encode/decode,
odczytu wyników, CPU ani inferencji NR/DLAA. Cztery powtórzenia na
timestamp, podział czasu przez cztery. Osiem bloków naprzemiennych ABBA/BAAB,
w każdym 16 kwartetów rozgrzewki i 40 pomiarowych: **320 parowanych
kwartetów / 640 próbek na stronę i konfigurację**. Dwie serie i osobne A/A.
Zegary są dynamiczne; nie wymuszano częstotliwości. Wygaszacz Bubbles
zamknięto przed końcowymi seriami; wcześniejsze zakłócone pomiary odrzucono.

Δ to mediana różnic A−B wewnątrz kwartetów; dodatnia oznacza szybciej.
IQR używa kwartylów inclusive. Dwie globalne mediany A/B mogą mieć inną
różnicę niż wynik parowany. Wszystkie czasy w ms.

| Profil / skala | Baza / v59, seria 1 | Δ seria 1 (IQR) | Δ seria 2 (IQR) |
|---|---:|---:|---:|
| SDR / 50 | 0,354304 / 0,353664 | 0,000512 (0,002592) | 0,000768 (0,003520) |
| SDR / 59 | 0,370368 / 0,367552 | 0,001024 (0,001696) | 0,001152 (0,001792) |
| SDR / 65 | 0,386432 / 0,384768 | 0,002816 (0,002336) | 0,003072 (0,002944) |
| HDR0 / 50 | 0,312064 / 0,310272 | 0,000512 (0,001664) | 0,000512 (0,003328) |
| HDR0 / 59 | 0,323264 / 0,323264 | 0,001152 (0,003104) | 0,001152 (0,003392) |
| HDR0 / 65 | 0,336576 / 0,332800 | 0,003264 (0,002688) | 0,003328 (0,004352) |
| HDR2 / 50 | 0,265984 / 0,265344 | 0,000768 (0,002080) | 0,000640 (0,002048) |
| HDR2 / 59 | 0,281792 / 0,281600 | 0,001024 (0,002560) | 0,001024 (0,002432) |
| HDR2 / 65 | 0,294912 / 0,293632 | 0,001920 (0,003072) | 0,002048 (0,002976) |
| HDR4 / 50 | 0,264896 / 0,264576 | 0,000640 (0,002432) | 0,000512 (0,002432) |
| HDR4 / 59 | 0,280832 / 0,280320 | 0,001024 (0,002688) | 0,001024 (0,002304) |
| HDR4 / 65 | 0,293504 / 0,292096 | 0,001920 (0,002976) | 0,002048 (0,002816) |

A/A daje mediany od -0,000128 do +0,000128 ms; IQR 0,002048–0,004512 ms.
Zysk pozostaje bliski zmienności, zwłaszcza przy P50. Powtarzalne dodatnie
mediany P59/P65 są małym wynikiem syntetycznym, bez potwierdzenia czasu
całego NR w grze. Nie należy sumować ich z zyskami poprzednich wersji.
Surowe serie i podsumowanie: [measurements/nr-v59](measurements/nr-v59).

## Odrzucone próby i odtworzenie

[Audyt pozostałych prób](NR-post-v58-optimization-audit.md) opisuje
współdzielenie sum poziomych, Area, redukcje wave i przybliżenia sRGB.
Ich źródła są odtwarzalne offline i nie trafiają do DLL.

`python tools/audits/post_v58/prepare.py` odtwarza bazę v58 i prototypy
w `artifacts/nr-post-v58`, wraz z przypiętymi nagłówkami. Nowe produkcyjne
fixture `superior_tall_low_quality.cpp` i `superior_tall_low_timing.cpp`
kompiluje się jak pozostałe testy z `d3d12.lib dxgi.lib`.
Jakość: `quality.exe baseline candidate [--4k]`;
czas: `timing.exe baseline candidate output.csv normal|aa pilot|full`.
Folder `candidate` musi zawierać bieżący low CSO, a baza low v58.
