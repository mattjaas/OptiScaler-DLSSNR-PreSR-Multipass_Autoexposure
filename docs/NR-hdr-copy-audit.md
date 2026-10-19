# Audyt kopii HDR w Superior Scaling — 2026-10-20

**Decyzja: zachować obecną kopię w produkcji.** Prototyp bez zapisu kopii
jest bitowo zgodny w sprawdzonym zwykłym resolve HDR, ale spowalnia FP16
z soft knee. FP32 daje obiecujący wynik do osobnego wdrożenia i testu w grze.
Ten commit dodaje audyt, narzędzia i wyniki; nie zmienia DLL, produkcyjnego
HLSL, selektora PSO ani menu. Nie jest wydaniem v57.

Baza: v56 `1ce30f76cd6c3193dc6d32e1ca89cd5851885225`.

## Co rzeczywiście robi kopia

`DlssNr_Dx12_Models.cpp` tworzy `colorCopy` i `hdrCopy` w natywnym
`desc.Format`. Mode 0 w `dlssnr.hlsl` zapisuje do `hdrCopy` oryginalne,
podpisane RGB i alpha dla HDR (`Passthrough=0`). Nie jest to konwersja
FP32 → FP16. SDR ma inną semantykę: kopia przycina ujemne RGB do zera.

W `EncodeInput` źródłem jest `target`, a oba wyjścia są osobnymi UAV.
W zwykłym torze źródłowy `target` nie jest nadpisywany między encode
a końcowym resolve. `Run.cpp` wybiera wtedy `hdrCopy` jako oryginał
i `target` jako cel resolve. Odczyt z UAV tego samego piksela przed jego
zapisem może więc zastąpić odczyt oryginału z osobnego SRV.

Prototyp wykonuje wyłącznie trzy zmiany kompilacyjne:

- encode nie zapisuje `gKeep`;
- resolve czyta `gTarget[id.xy]` zamiast `gOriginal.Load(id.xy)`;
- specjalizacja wymusza `gReplaceDetailStrength=0`, zgodnie z badanym
  przypadkiem bez odtwarzania detalu z natywnych sąsiadów.

Nie ma współdzielonego aliasu SRV/UAV celu. Tablica kandydata wiąże
niezmienny zastępczy zasób pod t2; DXIL wszystkich trzech PSO resolve
nie zawiera odczytu ani bindingu t2. Zawiera natomiast wymaganie
`Typed UAV Load Additional Formats`. Fixture sprawdza obsługę
`D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD` dla badanego formatu.

To nie jest ogólne pozwolenie na usunięcie `hdrCopy`. Oryginał musi
pozostać dostępny m.in. dla porównania side-by-side, odtwarzania detalu
z sąsiednich pikseli, końcowego dopasowania koloru i celu bez UAV.
SDR, inne formaty, tablice/mipy oraz routing asynchroniczny i ścieżki
awaryjne wymagają odrębnej obsługi. Ewentualne wdrożenie potrzebuje
zgodnego selektora encode/resolve i bezpiecznego fallbacku po błędzie PSO;
nie można pominąć zapisu kopii, a potem odczytać jej w zwykłym shaderze.

## Zgodność obrazu

RTX 4090, sterownik NVIDIA 616.56, D3D12:
**1080 przypadków, 1 764 263 040 porównanych słów 32-bitowych, zero różnic**.
Porównano surowe bity proxy, obrazu po Area i końcowego wyniku z alpha.
Nie używano tolerancji ani tylko porównania klasy NaN/Inf.

- 1050 małych przypadków / 17 325 000 słów: siedem geometrii, m.in.
  3×3 → 1×1, nieparzyste rozmiary, częściowe grupy i inna skala jednej osi;
  natywne RGBA16F/RGBA32F, pięć trybów mapowania HDR, pięć wzorców
  i trzy warianty parametrów.
- Wzorce: deterministyczne signed RGB, małe/duże wartości, NaN/Inf,
  ±0, subnormal, granice zakresu oraz surowe sNaN/qNaN z payloadami.
- Parametry: pre-exposure 0,125/1/8, tryby ekspozycji 0/1/3, trim 1/5,
  strength 0,7/0,9 i pary gainów Standard/Natural/Cinematic.
- 30 przypadków 4K / 1 746 938 040 słów: oba formaty i pięć mapowań,
  work 1920×1080, 2266×1274, 2496×1404, zwykły signed wzorzec.

To dowód dla tych danych i urządzenia, nie wszystkich gier/GPU.
Nie testowano ruchu ani prawdziwego wyjścia NGX: model zastępuje stały
rezydualny obraz testowy. Mechanizm usunięcia kopii nie zmienia modelu
ani arytmetyki NR. Wspólne PSO low/Area pochodzą z v56.

## Pomiar GPU

RTX 4090, NVIDIA 616.56, natywne 3840×2160, P50/P59/P65,
Cinematic high/mid 0,95 i low 0,784, bez porównania/detalu natywnego.
Mapowania: 0 = soft knee, 2 = Neutwo replace, 4 = hybrid replace.
Ekspozycja jest teksturą 1×1 o wartości 0,5; ExposureMode=3,
pre-exposure=1, trim=1, white point=2, bez kotwic, strength=1.

Znaczniki GPU obejmują **encode + Area + końcowy low + resolve + bariery**.
Nie obejmują DLSS/DLAA/NGX, interpassów ani pomiaru automatycznej ekspozycji.
To nie jest pomiar czasu całego Superior Scaling ani konkretnej gry.
Obie strony używają tego samego fizycznego celu. Oryginał jest odtwarzany
kopią przed każdym pomiarem, poza znacznikami; może to ogrzewać cache.
Kandydat zachowuje alokację i bariery `hdrCopy`, aby izolować zapis
encode i odczyt resolve. Nie zmierzono usuwania alokacji/barier.

Dwie niezależne serie A/B i jedna kontrola A/A. Każda: 18 konfiguracji,
8 bloków, przed każdym blokiem rozgrzewka 16 kwartetów, potem 40 kwartetów
naprzemiennie ABBA/BAAB. **640 próbek na wariant i konfigurację**,
łącznie 69 120 zapisanych próbek. Jeden pełny przebieg na znacznik,
bez dzielenia czasu wielu powtórzeń przez ich liczbę.

Na czas każdej serii dots.tts został chwilowo wstrzymany i następnie
wznowiony, bez restartu procesu/wyładowania modelu; wykorzystanie GPU przed
testem spadło do 6–7%. Zachowano około 18,2 GB zajętego VRAM i kontekst
CUDA. Nie blokowano zegarów GPU. Wyrywkowe odczyty podczas testów:
465–1575 MHz rdzenia, 5001/10251 MHz pamięci, P2/P3, 46–50°C.
Nie są to minima/maksima z pełnego logu telemetrycznego. Warunki z modelem
w VRAM ograniczają przenoszenie wyniku na grę i wcześniejsze benchmarki.
Powtórka i A/A służą ocenie stabilności w tych warunkach.

W tabeli A/B to mediany czasu pierwszej serii. Δ to mediana sparowanej
różnicy średnich A−B w każdym kwartecie; w nawiasie IQR tych różnic.
Wszystko w ms; **dodatnia Δ oznacza przyspieszenie**. Różnica dwóch
globalnych median może odbiegać od sparowanej Δ wskutek dryfu.

| Format / mapowanie / P | Mediana A / B | Δ seria 1 (IQR) | Δ powtórka (IQR) | Δ A/A (IQR) |
|---|---:|---:|---:|---:|
| fp16 / 0 / 50 | 0.997376 / 1.025024 | -0.027136 (0.002560) | -0.026624 (0.003072) | +0.000000 (0.002048) |
| fp16 / 0 / 59 | 1.053696 / 1.080320 | -0.025856 (0.003584) | -0.026112 (0.002688) | -0.000256 (0.003072) |
| fp16 / 0 / 65 | 1.067008 / 1.092608 | -0.025088 (0.003072) | -0.025088 (0.004608) | +0.000000 (0.003072) |
| fp16 / 2 / 50 | 0.791552 / 0.783360 | +0.008704 (0.003072) | +0.008704 (0.003072) | -0.000256 (0.002560) |
| fp16 / 2 / 59 | 0.989184 / 0.983040 | +0.001536 (0.003072) | +0.001024 (0.003072) | +0.000000 (0.003072) |
| fp16 / 2 / 65 | 1.018880 / 1.017856 | +0.001536 (0.004096) | +0.001536 (0.003584) | +0.000000 (0.003584) |
| fp16 / 4 / 50 | 0.799744 / 0.789504 | +0.009728 (0.003072) | +0.010240 (0.003200) | +0.000000 (0.002560) |
| fp16 / 4 / 59 | 0.990208 / 0.989184 | +0.001536 (0.003072) | +0.001536 (0.003072) | +0.000000 (0.003072) |
| fp16 / 4 / 65 | 1.019904 / 1.017856 | +0.001536 (0.003584) | +0.001536 (0.003072) | +0.000000 (0.003072) |
| fp32 / 0 / 50 | 1.536000 / 1.375232 | +0.160256 (0.004736) | +0.160256 (0.004096) | +0.000000 (0.006144) |
| fp32 / 0 / 59 | 1.672192 / 1.515520 | +0.157184 (0.005120) | +0.157184 (0.006144) | +0.000000 (0.007168) |
| fp32 / 0 / 65 | 1.728512 / 1.572864 | +0.155648 (0.006656) | +0.156160 (0.006656) | +0.000512 (0.007168) |
| fp32 / 2 / 50 | 1.404928 / 1.217536 | +0.187392 (0.004608) | +0.186368 (0.005632) | +0.000000 (0.004608) |
| fp32 / 2 / 59 | 1.529856 / 1.364992 | +0.165376 (0.006784) | +0.165376 (0.006144) | +0.000000 (0.006272) |
| fp32 / 2 / 65 | 1.589248 / 1.424384 | +0.164352 (0.006272) | +0.164352 (0.006656) | +0.000512 (0.006272) |
| fp32 / 4 / 50 | 1.405952 / 1.219584 | +0.186368 (0.005120) | +0.183808 (0.005632) | -0.000000 (0.005120) |
| fp32 / 4 / 59 | 1.529856 / 1.363968 | +0.165376 (0.005632) | +0.165120 (0.005632) | -0.000512 (0.005632) |
| fp32 / 4 / 65 | 1.589248 / 1.424384 | +0.164096 (0.006144) | +0.164352 (0.006144) | +0.000512 (0.006656) |

FP16 z soft knee konsekwentnie przegrywa o około 0,025–0,027 ms
(sparowana Δ); to znacznie więcej niż przesunięcie A/A. Przy P50 w trybach
2/4 zyskuje około 0,009–0,010 ms. Przy P59/P65 zysk 0,001–0,002 ms jest
mały wobec IQR 0,003–0,004 ms i nie uzasadnia ogólnego wdrożenia.
FP32 zyskuje około 0,156–0,187 ms, powtarzalnie w obu seriach.
Nie należy traktować tych wartości jako prognozy oszczędności w grze.

## Odtworzenie

Z x64 Native Tools Command Prompt dla Visual Studio, w repozytorium:

```powershell
powershell -NoProfile -File tools/audits/hdr_copy/run.ps1
# Po odciążeniu GPU, osobno dwie serie A/B i A/A:
powershell -NoProfile -File tools/audits/hdr_copy/run.ps1 -Timing
# Zachowaj CSV przed ponownym uruchomieniem A/B, bo nazwa jest ta sama.
powershell -NoProfile -File tools/audits/hdr_copy/run.ps1 -Timing -Control aa
python tools/audits/hdr_copy/summarize.py docs/data/NR-hdr-copy-audit.samples.csv artifacts/nr-hdr-copy-audit/summary.csv
```

Skrypt przygotowuje prototyp z przypiętej bazy v56 i kompiluje cztery
izolowane PSO oraz dwa fixture C++17. Wymaga DXC z repo, Windows SDK,
D3D12 i obsługi typed UAV loads dla RGBA16F/RGBA32F. Nie modyfikuje
produkcyjnych shaderów, nagłówków ani ustawień GPU/procesów.
Wyniki są w ignorowanym `artifacts/nr-hdr-copy-audit`.
Zregenerowane cztery PSO są bajtowo identyczne z przebadanym prototypem;
fixture po przeniesieniu ponownie przeszedł 1050 małych przypadków.
Kontrola zmian potwierdza brak zmian w produkcyjnych shaderach i runtime.

[Próbki](data/NR-hdr-copy-audit.samples.csv) i
[podsumowanie](data/NR-hdr-copy-audit.summary.csv) zawierają obie serie A/B
oraz A/A. `base` to v56 z kopią, `candidate` to prototyp bez zapisu kopii;
w serii `aa` obie etykiety wykonują identyczny bazowy shader.
