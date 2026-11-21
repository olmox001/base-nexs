# P8 — Fuzzing reale syscall, 9P e allocazioni (21 novembre 2026)

Status: **verifica hosted parziale, non conclusione di P2–P8**.
Questa evidenza integra e non sostituisce END_TO_END_REDESIGN_PLAN.md.
La build NexsOS1 rimane bloccata come richiesto.

## Fuzzer del linguaggio .nx (runtime, non simulazione)

- `tests/fuzz/nx_language_runtime_fuzz.py` genera ed esegue script
  **sull'eseguibile reale** `nxscript` in sottoprocessi con timeout:
  aritmetica, funzioni, registry, array, condizioni, loop, stringhe,
  filesystem, input sintatticamente ostili e input runtime ostili.
- Le otto classi valide sono confrontate con output oracolo calcolato
  indipendentemente dalla runtime NEXS; nessun successo è dedotto dal solo
  codice d'uscita. Per i casi maligni vengono controllati crash, segnali,
  timeout ed errori di invocazione: errori semantici ordinari sono ammessi,
  **non** sono conteggiati come operazioni corrette.
- Seed riproducibile, SHA-256 del corpus, sorgenti falliti preservati,
  tempo per esecuzione, p50/p95/p99 per famiglia e codice d'uscita in
  `build/assurance/nx_language_fuzz_full/{cases.csv,report.json}`.
  Il profilo Power12 blocca il codice produttivo, **non** la generazione
  di input antagonisti, che è per definizione fuori dal trusted runtime.
- `make test-language-fuzz` è la smoke suite; `make fuzz-language
  NX_LANGUAGE_FUZZ_CASES=4096 NX_LANGUAGE_FUZZ_SEED=0x20261121`
  è la campagna estesa. Non copre ancora l'intero spazio di programmi
  Turing-completi, tutte le ABI native né AOT/bytecode; queste rimangono
  attività P6/P8 esplicite.

## Programmi reali esercitati

- `tests/fuzz/system_syscall_fuzz.c`: inizializza la runtime NEXS reale,
  scheduler, VFS, server 9P e NXOLMO StateStore. Esegue tutti i 21 verb
  presenti nella tabella reale in `kernel/syscall.c`: open, read, write,
  close, stat, mkdir, create, remove, bind, mount, fork, exec, wait, exit,
  chdir, getpid, sleep, pipe, dup, seek, fstat.
- Per OGNI verbo esercita tre percorsi C realmente collegati:
  `direct/verb` (dispatch originale `kmsg_handle`);
  `wire/verb` (codifica/decodifica KernelMsg reali e dispatch);
  `REG_IPC/verb` (codifica, REG IPC su pipe POSIX reale, decodifica,
  dispatch). I risultati sono separati, non aggregati artificialmente.
- `9P_SERVER`: server reale `fs/9p.c` con richieste Tversion valide
  e input random troncati/malformati, risultato e frame verificati.
- `tests/test_full_9p_vfs_roundtrip.c`: la runtime completa attraversa
  il VFS, i nodi canonici NXOLMO e le richieste reali Tattach/Twalk/
  Topen/Twrite/Tread/Tclunk. Verifica dati, permessi, offset di lettura/
  scrittura, identità stabile e revisioni NXOLMO. Il test ha rilevato
  un bug reale: il server ignorava offset 9P. Bug corretto.
- `tests/test_syscall_fuzz_coverage.py`: se si aggiunge un nuovo verb
  alla tabella reale, il test fallisce finché non è nel corpus.
- `make test-system-syscall-fuzz`: prova breve di tutti i percorsi nel
  normale `make test`; il target esteso è `make fuzz-system-syscalls`.

## Misurazioni prodotte per syscall

Output CSV sotto `build/assurance/system_syscall_fuzz.csv`:
`calls`, `rejected`, `assertion_failures`, `total_ns`, `min_ns`, `mean_ns`,
`max_ns`, `p50_upper_ns`, `p95_upper_ns`, `p99_upper_ns`,
`new_registry_nodes`, `buddy_requests`, `buddy_success`, `buddy_failures`,
`buddy_frees`, `buddy_bytes`, `page_requests`, `page_success`,
`page_failures`, `page_frees`, `page_request_pages`.

I provider Buddy e pagine sono quelli del codice reale, con profilo
opt-in. I byte/pagine sono quelli **effettivamente ammessi**; una richiesta
fallita di dimensione SIZE_MAX conta come errore, non come un'impossibile
allocazione. Le liberazioni di Value restituiti sono contate insieme
all'operazione, ma il tempo del dispatch esclude la distruzione lato
chiamante. Il campionamento dei contatori è single-thread e non costituisce
una snapshot atomica di più campi in SMP.

Le latenze percentili sono upper-bound di bucket logaritmici, NON
percentili esatti; clock a granularità limitata può generare campioni 0.
A livello processo sono registrati seed, wall time, oggetti NXREG ancora
vivi, minor/major page fault, picco RSS (raw, macOS byte / Linux KiB),
context switch volontari/involontari e primo caso fallito.

## Evidenza iniziale: 100.000 dispatch diretti + 25.000 richieste 9P

Mac hosted, seed `0xa11ce20261121`, profiling attivo:
- stat: 4.805 chiamate, 241,22 us medi, 26.553 richieste Buddy.
- fstat: 4.779 chiamate, 240,58 us medi, 26.239 richieste Buddy.
- open: 4.763 chiamate, 197,10 us medi, 21.538 richieste Buddy.
- create: 4.829 chiamate, 197,24 us medi, 21.781 richieste Buddy.
- Profiling attivo: 7,468 s; disattivato: 7,094 s sul medesimo seed.
  Una singola coppia di campioni non è una misura statistica stabile.

**Collo di bottiglia visibile nel codice:** `kernel/vfs.c:path_to_ino`
scandisce linearmente tutte le inode e chiama `reg_get` per ogni
tentativo. Le statistiche dimostrano volume allocativo e latenze per
verbo, non attribuiscono ancora causalmente tutti i nanosecondi a
una sola funzione. Servono tracing per sottocomponente e benchmark
di scala con condizioni controllate.

## Comandi di riproduzione

`make test-allocator-observation`
`make test-full-9p-vfs-runtime`
`make test-system-syscall-fuzz`
`make fuzz-system-syscalls NX_SYSCALL_FUZZ_CASES=1000000 NX_SYSCALL_FUZZ_SEED=0xa11ce20261121`
`make fuzz-system-syscalls NX_SYSCALL_FUZZ_CASES=100000 NX_SYSCALL_FUZZ_SEED=0x5eeda7 NX_SYSCALL_FUZZ_ALLOC_PROFILE=0`
`make power12-phase0`
`make test -j4`

## 9P end-to-end: reali commit e identita, non soli codec

- Il VFS originale kernel/vfs.c ora verifica le modalita FD,
  applica gli offset di scrittura e controlla overflow, buffer null,
  NUL in payload e fallimento del commit al registry, prima di
  confermare una scrittura. Il backend temporaneo resta basato su
  Value stringa e non implementa ancora extent sparsi o byte binari:
  questa NON e la futura implementazione di storage NXOLMO.
- Il server di fs/9p.c ora controlla i FID doppi, rifiuta aperture
  ripetute, applica i diritti di lettura/scrittura e verifica tutti
  i componenti del walk contro il vero VFS. I Qid sono proiezioni
  della identita/revisione NXOLMO anziche byte fittizi. Tstat rimane
  esplicitamente non supportato; una risposta Rstat vuota non viene
  piu falsamente dichiarata valida.
- tests/test_full_9p_vfs_roundtrip.c verifica i rifiuti, la risposta
  Qid, percorsi assenti e mutazioni con readback e revision NXOLMO
  **per ogni vero commit**. Il conteggio si configura con
  NEXS_P9_FUZZ_ROUNDS, massimo ammissibile 1e9.
- make perf-p9-full-commits NX_P9_FULL_COMMITS=1000000 esegue
  un milione di commit REALI e un milione di letture effettive.
  La prova e molto diversa dai microbenchmark da 1e9.

## Risultati end-to-end e verifica di sicurezza (2026-11-21)

- Campagna macOS hosted: **1.000.000 scritture/commit effettive**
  e **1.000.000 letture 9P**, identita canonica verificata per ogni
  commit, revisione incrementata e contenuto di risposta controllato.
  Throughput end-to-end (comprende validazione stato e generazione
  input, NON solo il tempo dentro la syscall): **167.237 operazioni/s**
  per questa esecuzione, con 9.066 ns/call osservati per write e
  2.249 ns/call per read (misure interne alle chiamate).
- Campagna estesa di 100.000 input ostili al server reale: 8 verb,
  casi validi ma semanticamente negati e mutazioni dei frame, tutti
  respinti; nessun cambiamento alla identita, revisione o payload
  canonici del file. p99 upper-bound e max reali per verbo sono nel
  log build/assurance/p9_hostile_100k_e2e.log. Alcuni errori sono
  segnalati localmente e non ancora con un Rerror 9P wire: non
  dichiariamo conformita protocollo.
- Target ASan/UBSan full-product test-9p-e2e-sanitizers: PASS
  per 1.000 commit reali + 1.000 readback e 10.000
  richieste ostili. Tutti i veri sorgenti prodotti sono ricompilati
  con sanitizer; niente mock kernel/VFS. TSan, LeakSanitizer su
  macOS e tutto il sistema OS1 sono ancora aperti.
- Fuzzer reale del server: Tattach duplicate, Twalk non esistente,
  Topen fid stale, Tread non autorizzato, Twrite non autorizzato,
  Tclunk stale, Tversion con msize invalido, Tstat non implementato;
  alterna corruzione del frame wire, riproducibile da PRNG.
  Ciascuna classe registra chiamate, rifiuti, media/max e upper-bound
  p99; nessun commit o revision deve avvenire su input negato.
- Campagna linguaggio .nx: 256 casi reali (198 oracoli validi
  confrontati; 58 casi di input ostili); zero errori in quella
  campagna. Tempi per categoria e sorgenti di fallimento nei report.
  La maggior parte del tempo di esecuzione per programma include il
  bootstrap runtime: NON equivale al costo di una singola espressione.
- Esiste anche un target ASan/UBSan che ricompila il prodotto completo:
  make test-9p-e2e-sanitizers. La verifica leak sanitizer non e inclusa
  nel target macOS e resta un gate distinto.

## Profilo completo delle 21 syscall reali

Il report P8_SYSCALL_PROFILE_2026_11_21.md cataloga tutte le 21
syscall x 3 percorsi (direct, wire, REG_IPC): frequenza, tempi medi,
p99 upper-bound e richieste di allocazione Buddy per chiamata,
in ordine dei reali colli di bottiglia. Verifica il codice e log
corrispondenti, compresi i 63 dispatch di preflight originali:
**1.000.063 chiamate reali** (1.000.000 random + 63 reali
preflight), piu 250.000 richieste 9P separate.
Il comando make report-real-syscall-fuzz verifica la coerenza e
rifiuta campioni o segnali di fallimento mancanti, senza riempitivi.

## Base baremetal interattivo reale

- test_baremetal_qemu_interactive.py usa QEMU UART reale e un
  verificatore che distingue il redraw del prompt dal completamento.
  AMD64 e ARM64 PASS con quattro probe funzionali: arithmetic=5,
  registry roundtrip=731, user-function=21, string length=9.
- Non e NexsOS1, non e il boot HCD NXEC e non e l'intero
  sistema P7. Il primo tentativo di verifier anticipava il
  completamento al redraw: test corretto, risultati non falsificati.
- make test-baremetal-qemu-interactive richiede la compilazione
  effettiva dei due ELF e riesegue le prove su QEMU.

## Limitazioni esplicite

- I 21 verb vengono invocati, ma non sono coperti tutti i rami di
  ciascuna syscall, tutti i servizi USERKIT, API hosted `nexs_*`,
  IPC kernel notification, syscall hardware o il kernel NexsOS1 reale.
- `exec` qui esercita percorsi inesistenti, non una compilazione/esecuzione
  positiva nel dispatcher. I 17 esempi reali sono verificati separatamente.
- `fork` crea veri oggetti processo del kernel Base, ma è limitato a
  128 creazioni per campagna; i tentativi successivi verificano il
  rifiuto del PID 0 (non miliardi di processi reali).
- `wait`, `sleep` ed `exit` vengono raggiunti senza un task guest
  corrente; non provano risveglio, scheduling o teardown reale.
- La coda REG usa una vera pipe POSIX, ma non è ancora il servizio NXEC
  REG con autenticazione/crediti/revoca; i file restano sull'attuale
  vecchio VFS con limiti fd/FID, non ancora 9P2000.L completo.
- Il backend temporaneo stringhe rifiuta payload contenenti NUL,
  anziché dichiarare erroneamente una scrittura binaria riuscita.
  Le pagine/buffer NXOLMO devono ancora sostituirlo.
- I 2 miliardi di operazioni dei benchmark precedenti sono codec 9P
  e singole modifiche bit: NON miliardi di syscall, commit, transazioni.
- Non sono misurati TFLOPS, cache miss, NUMA, IOMMU, page fault per
  singola syscall, isolamento hardware, runqueue reali o latenza IRQ.
  Non è provata la conformità strict Power12 né la sicurezza SMP.

Ogni fallimento del fuzz stampa seed/caso e restituisce stato diverso
da zero. Nessun'operazione saltata viene contata come PASS.
