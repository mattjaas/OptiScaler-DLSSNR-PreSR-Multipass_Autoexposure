# NR v46 — gainy Superior Scaling ze zdjęcia

Stałe bazowe gainy P50 w Superior Scaling:

| Styl | High/mid | Low przed (v45) | Low teraz (v46) |
| --- | ---: | ---: | ---: |
| Standard | 0.800 | 0.609 | 0.661 |
| Natural | 0.750 | 0.741 | 0.787 |
| Cinematic | 0.950 | 0.780 | 0.784 |

Wartości odpowiadają zdjęciu użytkownika. Wspólny helper dostarcza je
zarówno do planowania i wykonywania interpassów, jak i końcowego resolve.
Nadal obowiązują przeliczenie rozdzielczości, Structure × Intensity,
iloczyn high/mid w bloku oraz średnia low po przeliczeniu każdego modelu.
Próg rosnącej różnicy pozostaje 0.15. Zmiana low może zmienić harmonogram
i liczbę interpassów, zależnie od modeli i ustawień.

Nie zmieniono HLSL, binariów shaderów ani kalibracji starszych trybów.
Zaktualizowano istniejące testy polityki C++ i integracji Python;
kontrole harmonogramu i low nadal sprawdzają współdzieloną arytmetykę.
Workflow buduje osobną paczkę v46, zachowując wydanie v45 do porównań.
Nie wykonano nowego pomiaru GPU ani oceny wizualnej w grze dla tych gainów.
Pomiary carriera z v45 nie mierzą wpływu tej zmiany na pełny czas NR.
