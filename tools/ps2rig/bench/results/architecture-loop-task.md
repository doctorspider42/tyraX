# Zadanie: architektura TyraX ograniczona tylko sprzętem PS2

## KROK 0: ustaw cel sesji

Zanim zrobisz cokolwiek innego, poproś mnie (użytkownika), żebym ustawił cel
sesji. Jako `/goal` mam wkleić CAŁĄ treść sekcji "CEL" poniżej. Nie zaczynaj
pracy, dopóki cel nie jest ustawiony.

## CEL

Przebudować silnik TyraX (StaPip i ścieżka rysowania, fizyka, przygotowanie
klatki) tak, żeby w każdej scenie referencyjnej jedynym limitem był sprzęt
PS2, a nie architektura.

Uznajemy cel za osiągnięty, gdy dla każdej sceny referencyjnej, zmierzonej na
fizycznym PS2, zachodzi jedno z dwóch:
- (a) klatkę ogranicza GPU (VU1 albo GS), czyli EE czeka na GPU, a nie na
  odwrót;
- (b) EE jest wąskim gardłem, ale wykonuje pożyteczną pracę, a nie czeka na
  pamięć:
  - w podsystemach, które dominują jego czas, chybienia cache (liczniki PMU)
    to mniej niż ok. 30% tego czasu,
  - dla żadnego zidentyfikowanego defektu architektury zmierzony lub
    oszacowany sufit zysku nie przekracza 0,5 ms na klatkę.

Sceny referencyjne:
1. Przejazd przez pole skrzyń: projekt użytkownika `vehicle-playground-5`,
   kopia `C:\tx\qp\U5src`, nagrany przejazd.
2. Motor District: przykład `examples/vehicle-playground`, ciężkie miejsce
   (-119.3, 0.3, 0.3), noc i dzień.
3. Fizyka: sterta i stos po zderzeniu, oraz 82 obudzone pudła (`C:\tx\qp\P1`).
4. `two-players` z podzielonym ekranem, w rozgrywce, nie na ekranie tytułowym.
5. `showcase`.

Nie gonimy tysięcznych części milisekundy. Szukamy defektów architektury
(złego układu danych, złego podziału pracy między EE, VU0, VU1, GS, DMA i
scratchpad, złego przepływu klatki) i piszemy te fragmenty od nowa.

## Kontekst: przeczytaj na start

Repozytorium: `F:\Projects\tyra-editor`.

**Gałąź:** pracujesz na istniejącej gałęzi `claude/perf-integration`, na
której ta praca się zaczęła. Ma ona nagrywanie klatki i dokumenty DOD. Jest
już wyciągnięta w worktree `F:\Projects\tyra-editor\.tmp\worktrees\dod-submission`.
Pracuj w tym worktree i na tej gałęzi:
- nie zakładaj nowej gałęzi od `main`;
- nie przełączaj gałęzi w głównym checkoucie;
- przed startem zrób `git pull`.

Jeśli subagent potrzebuje osobnego miejsca, dostaje własny worktree z
gałęzią odbitą od `claude/perf-integration`. To, co się opłaci, wraca na
`claude/perf-integration`.

**Zasada zbiorcza:** wszystko, co się udało, trafia na `claude/perf-integration`,
wypychane na bieżąco. Nic nie merguj do `main` bez mojej wyraźnej zgody.

Przeczytaj, zanim cokolwiek zaproponujesz:
- `docs/ps2-hardware-utilisation.md`: mapa użycia sprzętu, zmierzone
  kierunki, ślepe uliczki;
- `docs/dod-submission.md`: plan przebudowy pod dane, fazy, wyniki, dlaczego
  fazy osobno nie płacą;
- `docs/stapip-bag-cost.md`: koszt bagu, PMU, sufit przetwarzania etapami;
- `docs/physics.md` oraz notatki o fizyce w pamięci (memory) projektu;
- skille `tyra-engine-dev`, `tyra-editor-dev`, `tyra-testing`, `tyra-docs`,
  `tyra-pr`.

Stan na start (2026-11-05):
- Klatka jest ograniczona przez EE, a EE głównie czeka na pamięć. StaPip
  spędza 86-100% czasu na chybieniach cache. EE ma 16 KB I-cache i 8 KB
  D-cache, bez L2. VU1 i GS są bezczynne przez 44-51% klatki.
- Nagrywanie klatki (StaPip, 2.11.0, na `claude/perf-integration`, PR #314)
  dało -0,5..-0,6 ms pracy na polu skrzyń. Kontraktu (gra nie zmienia bagu,
  info ani modelu między nagraniem a wysłaniem) pilnuje
  `TYRA_STAPIP_RECORD_VERIFY`.
- Zmierzone bez zysku: kolejność bagów, ułożenie kodu, `-Os`, prefetch,
  przebiegi etapami dla bounds i prepare (chybienia płacone dwa razy), lean
  stamp (~0,14 ms), szybszy powrót ciał do batcha, solver z dwoma pasami,
  trzymanie wysp, solver na VU0 (macro i micro).
- Niesprawdzone duże kierunki:
  - pełna przebudowa ścieżki rysowania pod dane (zwarte rekordy, rysowanie z
    rekordów, strumieniowanie przez DMA do scratchpadu), naraz, nie fazami;
  - cull i clip w jednym programie VU1;
  - przebudowa danych fizyki (struktura tablic, VU0 z przygotowaniem danych
    przy ustawianiu kontaktów, nakładanie pracy);
  - mniej bagów na poziomie treści.

Rig na konsoli: `C:\tx\qp` (skrypty `mk_marm2.sh`, `deploy_arm.sh`,
`ab_sum.py`, `warm_sum.py`, `engine_*.py`, `patch_*.py`; silnik rigu w
`C:\tx\qp\tyra`; przełącznik wariantów przez `host:arm.txt` i
`StaPipCore::probeArm`). Konsola: PS2 192.168.100.150 przez ps2link.

## Format pracy: pętla

1. **Sesja nadrzędna wybiera pomysły.**
   - Na podstawie dokumentów i ostatnich wyników wybierz najwyżej dwa pomysły
     na raz.
   - Każdy pomysł to hipoteza o defekcie architektury z oszacowanym sufitem
     zysku i z tym, jak go zmierzyć.
   - Zapisz je w `docs/architecture-loop.md` (dziennik pętli) przed
     wysłaniem.
2. **Najwyżej dwóch subagentów naraz, jeden pomysł każdy.**
   - Każdy dostaje pełny brief: co ma zweryfikować, jakie pliki, jak mierzyć,
     co już jest ślepą uliczką, kryterium "opłaca się / nie".
   - Subagent buduje prototyp albo sondę sufitu (najpierw najtańszy pomiar
     rozstrzygający) i przygotowuje warianty do A/B.
   - Konsolę obsługuje sesja nadrzędna, jeden przebieg na raz, bo dwóch
     agentów na jednym PS2 się zablokuje.
   - Subagent wraca z opisem zmiany, wariantami gotowymi do uruchomienia i
     tym, czego się spodziewa.
3. **Test na konsoli i wnioski w sesji nadrzędnej.**
   - Ten sam ELF, wariant przełączany przy starcie, co najmniej 2 przebiegi
     na wariant.
   - Liczby: praca p50 i p95, pominięte klatki, czas podsystemu, chybienia z
     PMU, zajętość GPU.
   - Wynik i wniosek zapisz w dzienniku.
   - Pomysł, który się opłaca, dopracuj (dokumentacja w tym samym commicie,
     weryfikacja kontraktów, spis na scenach) i wypchnij na
     `claude/perf-integration`.
   - Pomysł, który się nie opłaca, cofnij i zapisz jako ślepą uliczkę z
     liczbami.
4. **Zwrot sytuacji → świeże spojrzenie.** Jeśli wynik zmienia obraz (nowe
   wąskie gardło, obalona hipoteza, podsystem przestał dominować), wyślij
   nowego subagenta, który bez Twoich założeń:
   - przeczyta dziennik i dokumenty,
   - przeanalizuje aktualny rozkład klatki,
   - wróci z następnymi rozsądnymi krokami.
   Jego propozycje wchodzą do kroku 1.
5. **Powtarzaj**, aż każda scena referencyjna spełnia CEL. Po każdej rundzie
   zaktualizuj w dzienniku tabelę scen: co ogranicza klatkę i ile czasu
   zostało w czekaniu na pamięć.

## Zasady stałe

- **Język:** odpowiadaj mi po polsku. Dokumentacja, commity i PR po
  angielsku.
- **Commity:**
  - każda zmiana aktualizuje dokumentację w tym samym commicie (skill
    `tyra-docs`);
  - pliki platformowe edytuj parami;
  - zmiany silnika: tylko LF, oznaczone "Modified by TyraX", lustrzane do
    silnika rigu `C:\tx\qp\tyra`, gdy rig ich potrzebuje.
- **Konsola:**
  - nigdy nie zabijaj procesów po nazwie;
  - konsola wiesza się po ok. 15-20 resetach (`freepad: DMA Busy`), a wtedy
    poproś mnie o wyłączenie i włączenie zasilania i po restarcie uruchom bez
    resetu;
  - PCSX2 tylko za moją zgodą, bo otwiera okno na moim ekranie.
- **Pomiary:**
  - liczy się tylko PS2, PCSX2 nie ma D-cache;
  - porównania tylko w obrębie jednego ELF;
  - przed oceną zmiany kontraktu uruchom spis (np. `RECORDVERIFY`) na
    scenach.
- **Pułapki rigu** (szczegóły w pamięci projektu):
  - przykład z repo nie ma upieczonych zasobów w `bin/`, więc najpierw
    `--build`;
  - `--refresh-gen` nadpisuje pliki wygenerowane, więc najpierw regeneracja,
    potem łatki;
  - `\n` w heredoku Pythona zamienia się na prawdziwy znak nowej linii, więc
    skrypty edycji pisz narzędziem Write;
  - `build.ps1` zwraca 0 nawet po błędzie, więc szukaj w logu `OK:`.
- **Cel ustawiony przez `/goal`** trzyma się tej treści. Nie ogłaszaj sukcesu
  bez tabeli scen, która spełnia CEL liczbami z konsoli.
