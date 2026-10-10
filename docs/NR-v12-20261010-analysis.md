# Analiza benchmarku NR v12

RTX 4090, The Last of Us Part I, native 3840×2160; P50 1920×1080, P65 2496×1404; 6 passów, Area, radius 1, guide strength 1.0. 90 warmup + 160 pomiarów na konfigurację. Jeden sekwencyjny sweep. Czas całego przedziału GPU NR, obejmujący NGX, inter-pass i zależności kolejek.

Zweryfikowano 25 konfiguracji i 4000 próbek; indeksy bez luk i duplikatów. Metryki podsumowania zgodne z raw do 0,000006 ms. SD populacyjne, P5/P95 nearest-rank, trimmed mean po odrzuceniu 10% z każdego końca. Trend: średnia ostatnich 40 minus pierwszych 40 próbek. Autokorelacja lag-1.

## P50

| Wariant | Mean | Median | P5 | P95 | Min | Max | SD | Trim10% | Δ off | Lag1 | Trend |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| No inter-pass | 25.58703 | 25.59949 | 25.19142 | 26.00141 | 25.08288 | 26.32909 | 0.24202 | 25.58353 | 0.00000 | 0.15433 | -0.01528 |
| Classic reference | 27.85670 | 27.84461 | 27.45037 | 28.33715 | 27.23635 | 28.73344 | 0.27505 | 27.84474 | 2.24512 | 0.61293 | 0.03428 |
| Classic optimized | 27.06148 | 27.07251 | 26.73664 | 27.38074 | 26.61683 | 27.60704 | 0.19873 | 27.06237 | 1.47302 | 0.10032 | -0.03666 |
| Classic source cache v10 | 27.31891 | 27.28499 | 26.99059 | 27.65210 | 26.90970 | 27.91936 | 0.20264 | 27.31244 | 1.68550 | 0.23846 | 0.00996 |
| Fused reference | 27.97799 | 27.99718 | 27.67667 | 28.20608 | 27.60704 | 28.60442 | 0.16391 | 27.97869 | 2.39770 | -0.00041 | 0.02391 |
| Fused optimized | 26.25871 | 26.23744 | 25.94509 | 26.57894 | 25.78330 | 26.74381 | 0.20927 | 26.25194 | 0.63795 | -0.30210 | 0.03820 |

## P65

| Wariant | Mean | Median | P5 | P95 | Min | Max | SD | Trim10% | Δ off | Lag1 | Trend |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| No inter-pass | 35.56054 | 35.64646 | 34.82726 | 36.00998 | 32.97382 | 36.58445 | 0.38242 | 35.59683 | 0.00000 | 0.59481 | -0.34811 |
| Classic reference | 38.62785 | 38.61555 | 38.37747 | 38.94170 | 37.96480 | 39.25914 | 0.18069 | 38.62251 | 2.96909 | 0.23064 | 0.00530 |
| Classic optimized | 37.58900 | 37.57773 | 37.36781 | 37.81837 | 37.31046 | 37.93715 | 0.14130 | 37.58652 | 1.93126 | -0.01194 | 0.01444 |
| Classic source cache v10 | 37.98084 | 38.00371 | 37.74157 | 38.21261 | 37.61562 | 38.34675 | 0.14316 | 37.98218 | 2.35725 | 0.19081 | -0.01490 |
| Fused reference | 41.51194 | 41.50426 | 41.30714 | 41.71981 | 41.05523 | 41.90515 | 0.13226 | 41.51167 | 5.85779 | -0.17048 | 0.00527 |
| Fused optimized | 39.15955 | 39.18029 | 38.97242 | 39.33491 | 38.89971 | 39.42502 | 0.10916 | 39.16188 | 3.53382 | -0.08717 | 0.00261 |
| Tiled linear 20 | 37.39377 | 37.37549 | 37.17734 | 37.66886 | 37.07187 | 37.98221 | 0.15169 | 37.38450 | 1.72902 | -0.04988 | 0.01613 |
| Tiled linear 16 | 37.29085 | 37.27155 | 37.04422 | 37.57363 | 36.88858 | 38.20134 | 0.17935 | 37.28229 | 1.62509 | 0.15461 | -0.04278 |
| Tiled strided 20 | 37.52753 | 37.52806 | 37.25722 | 37.79482 | 36.93056 | 37.95763 | 0.16210 | 37.52955 | 1.88160 | 0.08282 | 0.01787 |
| Tiled strided 16 | 37.38080 | 37.37344 | 37.12410 | 37.64122 | 36.96538 | 37.94125 | 0.16451 | 37.37858 | 1.72698 | 0.18513 | 0.05640 |
| Linear 16 + isolated weights v10 | 37.25990 | 37.25517 | 37.02272 | 37.54086 | 36.66330 | 37.63302 | 0.16655 | 37.25690 | 1.60870 | 0.00852 | 0.03871 |
| Linear 16 + compact cache v10 | 38.23151 | 38.20749 | 37.99552 | 38.46144 | 37.87981 | 38.64781 | 0.14296 | 38.22893 | 2.56102 | -0.04966 | -0.01134 |
| Linear 16 + isolated both v10 | 38.22152 | 38.22797 | 37.93920 | 38.45734 | 37.66989 | 38.72973 | 0.16065 | 38.22409 | 2.58150 | 0.03310 | -0.02094 |
| Linear 16 + weights + spatial v11 | 37.26394 | 37.24595 | 37.01658 | 37.52858 | 36.92134 | 37.72723 | 0.15829 | 37.25810 | 1.59949 | -0.10380 | -0.02260 |
| Linear 16 + weights + quadfill v11 | 37.48612 | 37.47482 | 37.28691 | 37.70880 | 37.19578 | 37.92179 | 0.12644 | 37.48266 | 1.82835 | -0.02049 | -0.01229 |
| Linear 16 + weights + both v11 | 37.51300 | 37.51014 | 37.30022 | 37.73338 | 37.14458 | 37.85421 | 0.13061 | 37.51228 | 1.86368 | -0.02237 | -0.00384 |
| Linear 16 + weights + interior v12 | 37.22083 | 37.21574 | 36.76058 | 37.65862 | 36.43904 | 37.96480 | 0.26640 | 37.21794 | 1.56928 | 0.61333 | 0.01001 |
| Linear 16 + weights + axis preclamp v12 | 37.18982 | 37.17376 | 36.84352 | 37.58694 | 36.46259 | 37.92486 | 0.23011 | 37.18327 | 1.52730 | 0.38097 | -0.03026 |
| Linear 16 + weights + interior and axes v12 | 37.15775 | 37.13690 | 36.72064 | 37.69139 | 36.33459 | 38.06208 | 0.30092 | 37.14650 | 1.49043 | 0.51541 | -0.06927 |

## Porównanie v12

Δ = wariant minus referencja; ujemna wartość oznacza krótszy czas.

| Wariant | Δ median v10 | Δ mean v10 | Δ P95 v10 | Δ median Linear16 | Δ mean Linear16 | Δ P95 Linear16 | SD/v10 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Linear 16 + weights + interior v12 | -0.03942 | -0.03907 | 0.11776 | -0.05581 | -0.07002 | 0.08499 | 1.59956 |
| Linear 16 + weights + axis preclamp v12 | -0.08141 | -0.07007 | 0.04608 | -0.09779 | -0.10102 | 0.01331 | 1.38167 |
| Linear 16 + weights + interior and axes v12 | -0.11827 | -0.10215 | 0.15053 | -0.13466 | -0.13310 | 0.11776 | 1.80683 |

## Niepewność

Circular moving-block bootstrap: 10000 niezależnych resamplingów na wariant, seed 1213. Przedziały opisują zmienność wewnątrz dostarczonego sweepu. Nie obejmują dryfu między konfiguracjami, temperatury/taktowania, zmian sceny ani różnic między sesjami.

- Linear 16 + weights + interior v12, block 8: Δ median 95% CI [-0.11162, 0.03891] ms.
- Linear 16 + weights + interior v12, block 16: Δ median 95% CI [-0.08960, 0.01946] ms.
- Linear 16 + weights + interior v12, block 32: Δ median 95% CI [-0.08448, 0.01434] ms.
- Linear 16 + weights + axis preclamp v12, block 8: Δ median 95% CI [-0.15105, -0.02611] ms.
- Linear 16 + weights + axis preclamp v12, block 16: Δ median 95% CI [-0.13158, -0.03429] ms.
- Linear 16 + weights + axis preclamp v12, block 32: Δ median 95% CI [-0.12493, -0.03584] ms.
- Linear 16 + weights + interior and axes v12, block 8: Δ median 95% CI [-0.19866, -0.03174] ms.
- Linear 16 + weights + interior and axes v12, block 16: Δ median 95% CI [-0.17715, -0.05274] ms.
- Linear 16 + weights + interior and axes v12, block 32: Δ median 95% CI [-0.16896, -0.05888] ms.

## Przebieg w czasie

Średnie kolejnych bloków po 40 próbek; ms. Indeksy są lokalne dla konfiguracji, bez absolutnego czasu klatki.

| Wariant | 0–39 | 40–79 | 80–119 | 120–159 |
|---|---:|---:|---:|---:|
| No inter-pass | 35.66564 | 35.63369 | 35.62529 | 35.31753 |
| Tiled linear 16 | 37.31489 | 37.32652 | 37.24987 | 37.27212 |
| Linear 16 + isolated weights v10 | 37.23382 | 37.27731 | 37.25594 | 37.27252 |
| Linear 16 + weights + interior v12 | 37.21861 | 37.23983 | 37.19624 | 37.22862 |
| Linear 16 + weights + axis preclamp v12 | 37.21405 | 37.18784 | 37.17361 | 37.18380 |
| Linear 16 + weights + interior and axes v12 | 37.21920 | 37.13288 | 37.12899 | 37.14993 |

## Pochodzenie

- NR-v12-20261010-114831.csv: SHA-256 `6ef94dc9f129accf29331e21f706b2b5f42d98f1f4e01bd131d9a65a99fa42e3`
- NR-v12-20261010-114831.samples.csv: SHA-256 `7a0d6e78f94c22e17f66fbbb60abf5b6705a0f4e86873e3c13d131b465ceadbc`
- NR-v12-20261010-114831.txt: SHA-256 `9fd178b8b42daf79283b18c041cd029ac787f7cb25aae9fc3a18eddb9f631d94`
