# Analiza benchmarku NR v13

RTX 4090, The Last of Us Part I, native 3840×2160; P50 1920×1080, P65 2496×1404; 6 passów, Area, radius 1, guide strength 1.0. 90 warmup + 160 pomiarów na konfigurację. Jeden sekwencyjny sweep. Czas całego przedziału GPU NR, obejmujący NGX, inter-pass i zależności kolejek.

Zweryfikowano 28 konfiguracji i 4480 próbek; indeksy bez luk i duplikatów. Metryki podsumowania zgodne z raw do 0,000006 ms. SD populacyjne, P5/P95 nearest-rank, trimmed mean po odrzuceniu 10% z każdego końca. Trend: średnia ostatnich 40 minus pierwszych 40 próbek. Autokorelacja lag-1.

## P50

| Wariant | Mean | Median | P5 | P95 | Min | Max | SD | Trim10% | Δ off | Lag1 | Trend |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| No inter-pass | 25.56623 | 25.54010 | 25.16378 | 26.02906 | 25.04397 | 26.20621 | 0.25792 | 25.55767 | 0.00000 | 0.30277 | -0.04626 |
| Classic reference | 27.85464 | 27.84410 | 27.45446 | 28.20096 | 27.38790 | 28.57677 | 0.23257 | 27.85222 | 2.30400 | 0.43098 | 0.11003 |
| Classic optimized | 27.05838 | 27.05971 | 26.74074 | 27.40941 | 26.63936 | 27.60909 | 0.20896 | 27.05451 | 1.51962 | 0.12194 | 0.01846 |
| Classic source cache v10 | 27.33333 | 27.30957 | 26.98650 | 27.72787 | 26.89843 | 27.89069 | 0.21859 | 27.32402 | 1.76947 | 0.42541 | 0.00404 |
| Fused reference | 27.97286 | 27.98387 | 27.63981 | 28.24294 | 27.55174 | 28.67098 | 0.18403 | 27.97462 | 2.44378 | 0.21023 | -0.00722 |
| Fused optimized | 26.20531 | 26.17344 | 25.97683 | 26.52979 | 25.92256 | 26.74176 | 0.16934 | 26.19206 | 0.63334 | -0.25536 | 0.02168 |

## P65

| Wariant | Mean | Median | P5 | P95 | Min | Max | SD | Trim10% | Δ off | Lag1 | Trend |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| No inter-pass | 35.52576 | 35.56608 | 34.98803 | 36.03661 | 32.55706 | 36.44928 | 0.37544 | 35.54948 | 0.00000 | 0.43703 | -0.28157 |
| Classic reference | 38.60849 | 38.61146 | 38.28634 | 38.90790 | 38.09075 | 39.12806 | 0.18380 | 38.60888 | 3.04538 | 0.36822 | -0.03277 |
| Classic optimized | 37.54571 | 37.52755 | 37.29818 | 37.82144 | 37.22752 | 37.86957 | 0.14856 | 37.54211 | 1.96147 | 0.00194 | -0.03049 |
| Classic source cache v10 | 38.00908 | 38.02317 | 37.80198 | 38.20851 | 37.69549 | 38.34880 | 0.12541 | 38.00954 | 2.45709 | 0.09537 | 0.01513 |
| Fused reference | 41.50623 | 41.49606 | 41.32659 | 41.68294 | 40.82688 | 41.82835 | 0.12591 | 41.50741 | 5.92998 | -0.18206 | 0.00952 |
| Fused optimized | 39.16026 | 39.17210 | 38.94682 | 39.33798 | 38.81984 | 39.51002 | 0.12061 | 39.16144 | 3.60602 | -0.02700 | 0.01219 |
| Tiled linear 20 | 37.41652 | 37.39853 | 37.14765 | 37.69139 | 37.01350 | 37.94330 | 0.17272 | 37.41018 | 1.83245 | 0.12590 | 0.01920 |
| Tiled linear 16 | 37.31828 | 37.30637 | 37.05037 | 37.59923 | 36.90394 | 37.76410 | 0.16468 | 37.31627 | 1.74029 | 0.08683 | -0.03028 |
| Tiled strided 20 | 37.55677 | 37.53523 | 37.30534 | 37.81018 | 37.14150 | 37.98630 | 0.16011 | 37.55693 | 1.96915 | 0.07910 | 0.02056 |
| Tiled strided 16 | 37.40986 | 37.41491 | 37.19168 | 37.61664 | 36.97869 | 37.84704 | 0.13748 | 37.41068 | 1.84883 | 0.00184 | -0.06254 |
| Linear 16 + isolated weights v10 | 37.27059 | 37.25978 | 37.01248 | 37.53984 | 36.74726 | 37.61152 | 0.16639 | 37.27323 | 1.69370 | 0.07757 | 0.06269 |
| Linear 16 + compact cache v10 | 38.21196 | 38.20083 | 38.02624 | 38.42765 | 37.84909 | 38.53005 | 0.13036 | 38.20990 | 2.63475 | 0.05717 | 0.00003 |
| Linear 16 + isolated both v10 | 38.19975 | 38.18547 | 38.00371 | 38.42150 | 37.84397 | 38.61606 | 0.13225 | 38.19682 | 2.61939 | 0.23542 | 0.03389 |
| Linear 16 + weights + spatial v11 | 37.29336 | 37.29050 | 37.01146 | 37.60742 | 36.88346 | 38.10509 | 0.19031 | 37.28571 | 1.72442 | 0.17188 | -0.00323 |
| Linear 16 + weights + quadfill v11 | 37.51282 | 37.51834 | 37.29715 | 37.73747 | 37.16301 | 37.84090 | 0.13444 | 37.51283 | 1.95226 | 0.09536 | 0.04431 |
| Linear 16 + weights + both v11 | 37.53215 | 37.53421 | 37.31968 | 37.73747 | 37.27360 | 37.81325 | 0.12378 | 37.53222 | 1.96813 | -0.07585 | 0.00873 |
| Linear 16 + weights + interior v12 | 37.32436 | 37.31354 | 37.06470 | 37.56954 | 36.88448 | 37.78253 | 0.15982 | 37.32460 | 1.74746 | -0.04390 | 0.03310 |
| Linear 16 + weights + axis preclamp v12 | 37.29904 | 37.30893 | 37.08621 | 37.51014 | 37.01760 | 37.65658 | 0.12463 | 37.29876 | 1.74285 | -0.13355 | 0.00146 |
| Linear 16 + weights + interior and axes v12 | 37.30844 | 37.30944 | 37.07904 | 37.50707 | 36.90496 | 37.77126 | 0.12574 | 37.30919 | 1.74336 | -0.16564 | -0.01336 |
| Linear 16 + weights + bounded Area v13 | 37.26671 | 37.25978 | 36.90394 | 37.66682 | 36.61107 | 37.98528 | 0.22762 | 37.25963 | 1.69370 | 0.58348 | -0.02921 |
| Linear 16 + weights + dedicated Mode28 v13 | 37.10668 | 37.06573 | 36.64384 | 37.60742 | 36.38784 | 37.75693 | 0.26759 | 37.09940 | 1.49965 | 0.48548 | -0.03901 |
| Linear 16 + weights + Area and Mode28 v13 | 37.11011 | 37.09952 | 36.75136 | 37.47942 | 36.56090 | 37.75693 | 0.23743 | 37.10186 | 1.53344 | 0.26665 | 0.01718 |

## Porównanie v12, v13

Δ = wariant minus referencja; ujemna wartość oznacza krótszy czas.

| Wariant | Δ median v10 | Δ mean v10 | Δ P95 v10 | Δ median Linear16 | Δ mean Linear16 | Δ P95 Linear16 | SD/v10 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Linear 16 + weights + interior v12 | 0.05376 | 0.05377 | 0.02970 | 0.00717 | 0.00608 | -0.02970 | 0.96052 |
| Linear 16 + weights + axis preclamp v12 | 0.04915 | 0.02845 | -0.02970 | 0.00256 | -0.01924 | -0.08909 | 0.74898 |
| Linear 16 + weights + interior and axes v12 | 0.04966 | 0.03786 | -0.03277 | 0.00307 | -0.00984 | -0.09216 | 0.75569 |
| Linear 16 + weights + bounded Area v13 | 0.00000 | -0.00388 | 0.12698 | -0.04659 | -0.05157 | 0.06758 | 1.36797 |
| Linear 16 + weights + dedicated Mode28 v13 | -0.19405 | -0.16391 | 0.06758 | -0.24064 | -0.21160 | 0.00819 | 1.60820 |
| Linear 16 + weights + Area and Mode28 v13 | -0.16026 | -0.16047 | -0.06042 | -0.20685 | -0.20817 | -0.11981 | 1.42693 |

## Niepewność

Circular moving-block bootstrap: 10000 niezależnych resamplingów na wariant, seed 1213. Przedziały opisują zmienność wewnątrz dostarczonego sweepu. Nie obejmują dryfu między konfiguracjami, temperatury/taktowania, zmian sceny ani różnic między sesjami.

- Linear 16 + weights + interior v12, block 8: Δ median 95% CI [-0.00563, 0.09576] ms.
- Linear 16 + weights + interior v12, block 16: Δ median 95% CI [0.00051, 0.08909] ms.
- Linear 16 + weights + interior v12, block 32: Δ median 95% CI [0.00358, 0.08909] ms.
- Linear 16 + weights + axis preclamp v12, block 8: Δ median 95% CI [-0.00102, 0.08243] ms.
- Linear 16 + weights + axis preclamp v12, block 16: Δ median 95% CI [0.00410, 0.08141] ms.
- Linear 16 + weights + axis preclamp v12, block 32: Δ median 95% CI [0.00512, 0.08294] ms.
- Linear 16 + weights + interior and axes v12, block 8: Δ median 95% CI [0.00154, 0.08243] ms.
- Linear 16 + weights + interior and axes v12, block 16: Δ median 95% CI [0.00512, 0.08294] ms.
- Linear 16 + weights + interior and axes v12, block 32: Δ median 95% CI [0.00512, 0.08346] ms.
- Linear 16 + weights + bounded Area v13, block 8: Δ median 95% CI [-0.09524, 0.05888] ms.
- Linear 16 + weights + bounded Area v13, block 16: Δ median 95% CI [-0.08142, 0.04352] ms.
- Linear 16 + weights + bounded Area v13, block 32: Δ median 95% CI [-0.07834, 0.04200] ms.
- Linear 16 + weights + dedicated Mode28 v13, block 8: Δ median 95% CI [-0.26573, -0.10650] ms.
- Linear 16 + weights + dedicated Mode28 v13, block 16: Δ median 95% CI [-0.24934, -0.12339] ms.
- Linear 16 + weights + dedicated Mode28 v13, block 32: Δ median 95% CI [-0.24320, -0.12698] ms.
- Linear 16 + weights + Area and Mode28 v13, block 8: Δ median 95% CI [-0.24372, -0.10342] ms.
- Linear 16 + weights + Area and Mode28 v13, block 16: Δ median 95% CI [-0.22989, -0.11059] ms.
- Linear 16 + weights + Area and Mode28 v13, block 32: Δ median 95% CI [-0.22835, -0.11110] ms.

## Przebieg w czasie

Średnie kolejnych bloków po 40 próbek; ms. Indeksy są lokalne dla konfiguracji, bez absolutnego czasu klatki.

| Wariant | 0–39 | 40–79 | 80–119 | 120–159 |
|---|---:|---:|---:|---:|
| No inter-pass | 35.61646 | 35.57688 | 35.57481 | 35.33489 |
| Tiled linear 16 | 37.33133 | 37.33268 | 37.30806 | 37.30104 |
| Linear 16 + isolated weights v10 | 37.21802 | 37.27009 | 37.31351 | 37.28072 |
| Linear 16 + weights + interior v12 | 37.30327 | 37.33087 | 37.32692 | 37.33637 |
| Linear 16 + weights + axis preclamp v12 | 37.29738 | 37.29866 | 37.30127 | 37.29884 |
| Linear 16 + weights + interior and axes v12 | 37.31241 | 37.32872 | 37.29359 | 37.29905 |
| Linear 16 + weights + bounded Area v13 | 37.29042 | 37.27603 | 37.23917 | 37.26121 |
| Linear 16 + weights + dedicated Mode28 v13 | 37.11227 | 37.13467 | 37.10651 | 37.07325 |
| Linear 16 + weights + Area and Mode28 v13 | 37.10303 | 37.12579 | 37.09143 | 37.12020 |

## Pochodzenie

- NR-v13-20261010-123610.csv: SHA-256 `d28f280d6f2f7fa648a48a92919b0b1e74acecdbe761826ff8626bbc4d100120`
- NR-v13-20261010-123610.samples.csv: SHA-256 `c2fbe1a5bec980f834582d7cd7a5f3a891108ed94472b6708622047f85d31ece`
- NR-v13-20261010-123610.txt: SHA-256 `3baf81fb739881b0cc130d7472528de2e39bc7115f52982938fa9f0cb4b0101c`
