# NR v35 — harmonogram 74–84%, benchmarki do 65%

Aktualny harmonogram dla wszystkich włączonych trybów inter-pass:

| Żądany WorkingScale | Korekcje przy sześciu modelach |
| --- | --- |
| poniżej 74% | po modelach 1, 2, 3, 4, 5 |
| 74–84% włącznie | po modelach 2 i 4 |
| powyżej 84% | inter-pass wyłączony |

Progi odnoszą się do żądanej skali przed zaokrągleniem wymiarów tekstur.
Ostatni model nie ma korekcji inter-pass. Pominięte przejścia używają ClampProxy.
Przy pięciu modelach w zakresie 74–84% high/mid obejmuje bloki g₁·g₂, g₃·g₄
i końcowe g₅; przy sześciu końcowe g₅·g₆. Bez inter-pass końcowy resolve
obejmuje wszystkie modele.

Reguła gainów z [v33](NR-v33-per-pass-shaping.md) pozostaje zachowana: każdy
model wnosi gain własnego stylu i skonfigurowanego Structure. Po korekcji skali
high/mid = `1+(high-1)*clamp(Structure,0,1)`. Structure powyżej 1 nie wzmacnia
gainu, a Low nie jest mnożone. Low36 i Cross5 pozostają częścią Fast.

Na prośbę użytkownika **żaden benchmark nie testuje procentów powyżej 65%**.
Usunięto przycisk oraz profil 74/84/85%, jego konfiguracje i specjalne raportowanie.
Regularny profil nie dopisuje bieżącej skali większej niż 65%. Z listy syntetycznego
benchmarku komponentów także usunięto 90/91/95/99%.

- Fast: dokładnie 50/59/65%, 15 konfiguracji / 21 okien; Off, obie referencje
  i Optimized/Fast/Fast/Optimized w ABBA.
- Niższy Fast: 33/40/41/42/49% oraz odmienna bieżąca skala poniżej 50%;
  25 konfiguracji / 35 okien, albo 30 / 42 z dodatkową skalą.
- Regularny Optimized: 50/59/65% i odmienna bieżąca skala do 65%;
  18 konfiguracji / 24 okna, albo 24 / 32 z dodatkową skalą.
- Osobne Below50 i Boundary zachowują swoje niższe skale i niezależne kontrole.

Pozostają rozgrzewka każdego okna, pomiar całego NR znacznikami GPU, raporty CSV/TXT,
ABBA, opcjonalne zatrzymanie do oględzin i przywracanie ustawień po zakończeniu/anulowaniu.
Prefixy Fast to `NR-v35-fast-*` i `NR-v35-fast-low-*`.

Zmiany nie dodają dispatchy ani zasobów GPU i nie modyfikują HLSL/bytecode.
Testy granic harmonogramu powyżej 65% są testami poprawności CPU, nie oknami benchmarku.
Nie ma nowego pomiaru GPU w grze ani oceny obrazu dla harmonogramu 74–84%.

## Weryfikacja wydania

Źródła wydania: `7a0fca484146ab01213f342cd82f806499b62ba0`.
[GitHub Actions 38145058337](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/actions/runs/38145058337)
zakończył się sukcesem: wszystkie testy Python, test polityki C++, kompilacja shaderów,
fixture GPU, DLL i paczka. Lokalnie przeszły testy benchmarku i harmonogramu oraz C++.

[Paczka v35](https://github.com/mattjaas/OptiScaler-DLSSNR-PreSR-Multipass_Autoexposure/releases/tag/nr-interpass-v35-scale-cadence-20261011)
ma 131120629 bajtów. Zweryfikowano CRC ZIP, wszystkie 62 wpisy SHA256SUMS,
identyfikator commita w DLL, brak przycisku wysokich skal, obecność limitu P65,
gainów per-pass i konfiguracji 74–84%. Sześć payloadów CSO w DLL jest identycznych z v27.
Kompilacja fixture GPU nie oznacza wykonania benchmarku w grze.

SHA256 ZIP: `aa7a087ae7aa1c3a8a2673e16df1312240049a82289e9a58532aecaa5d17a5c8`.
