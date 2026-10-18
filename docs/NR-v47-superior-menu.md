# NR v47 — wspólne ustawienia HDR w Superior Scaling

Naprawiono znikanie ustawień HDR mapping, White point source, Exposure trim,
Highlight protection, Trim anchors oraz Paper white przy wybranym Superior
Scaling. Wcześniejsze zakończenie funkcji po presecie A/K pomijało również
wspólny panel wejścia NR. Enlargement ma teraz osobną funkcję, a wspólne
ustawienia HDR są zawsze renderowane zgodnie z własnymi warunkami
(np. Highlight protection dla Automatic HDR exposure).

Z menu Superior Scaling usunięto całą sekcję Legacy wraz z jej nagłówkiem
i kontrolkami. Starsze tryby Enlargement zachowują dotychczasowe profile
benchmarku. Stałe parametry Superior Scaling i gainy v46 pozostają bez zmian.
Wspólne ustawienia HDR wejścia NR są niezależne od wymuszonego IsHDR
i Auto Exposure prywatnego DLSS.

Zmiana dotyczy wyłącznie menu; nie zmienia konfiguracji, renderowania,
HLSL, dispatchy ani zasobów GPU. Rozszerzony test integracji sprawdza
dostępność wspólnego HDR po obsłudze Enlargement oraz brak sekcji Legacy
w Superior. Kontrole harmonogramu, low i benchmarku nadal przechodzą.
Pełna kompilacja DLL odbywa się w workflow v47; lokalnie nie weryfikowano
wyglądu menu w działającej grze.
