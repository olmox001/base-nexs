ls non risolve i puntatori




Hai beccato un **bug architetturale fondamentale** nell'emulazione del runtime Hosted (macOS/Linux) di NEXS! 

L'output si blocca esattamente dopo la creazione dell'app utente perché tutti i processi entrano in un **deadlock silenzioso**. Il motivo non è un errore di sintassi nello script, ma un problema di implementazione nel codice C del progetto GitHub.

Ecco l'analisi esatta di cosa sta succedendo e di dov'è il bug nel codice sorgente di NEXS.

### 🕵️‍♂️ Il Bug: Isolamento della Memoria via `fork()`
Nello script `micro_os.nx`, tu usi `rfork(RFPROC)` per generare i demoni e l'app. 
Se guardiamo il file `sys/sysproc.c` del tuo progetto, la funzione `nexs_rfork` fa questo:
```c
int nexs_rfork(int flags) {
  if (!(flags & NEXS_RFPROC)) return 0;
  pid_t pid = fork(); // <--- IL COLPEVOLE
  ...
}
```
Nei sistemi operativi come macOS e Linux (ambiente *hosted*), **`fork()` crea una copia completamente indipendente della memoria del processo padre**.

D'altra parte, il tuo sistema IPC (`sendmessage` / `receivemessage`) si basa sul **Registro gerarchico** definito in `registry.c`. Il registro usa la memoria heap standard allocata con `xmalloc`:
```c
// Da registry.c
RegKey *k = xmalloc(sizeof(RegKey));
```

**Cosa succede a runtime in `micro_os.nx`?**
1. L'Init crea il registro `/sys/log/inbox`.
2. L'Init fa `fork()` per il Syslog. Il Syslog riceve una **copia esatta e privata** del registro.
3. L'Init fa `fork()` per la User App. La User App riceve **un'altra copia privata**.
4. La User App esegue `sendmessage("/sys/log/inbox", ...)` e inserisce il messaggio nella **sua copia privata** della coda IPC in RAM.
5. Il Syslog fa `receivemessage("/sys/log/inbox")` leggendo dalla **sua coda privata**, che rimane miseramente vuota.
6. **Risultato:** Syslog aspetta per sempre, VFS aspetta per sempre, User App aspetta per sempre la risposta dal VFS, e il Kernel aspetta per sempre `APP_DONE`. Tutto è bloccato nei loop infiniti.

Questo accade perché in ambiente "hosted", la memoria heap del registro non è condivisa tra i processi. (Nel target *baremetal*, dove i processi girerebbero nello stesso spazio di indirizzamento fisico gestiti da te e il registro *è* il kernel, questo funzionerebbe!).

---

### 🛠️ Come risolvere il bug nel progetto C

Per far funzionare il paradigma seL4 IPC anche sull'eseguibile compilato per macOS/Linux, devi fare in modo che le code IPC (`reg_ipc.c`) comunichino oltre i confini del processo generato da `fork()`. 

Hai due strade principali:

#### Soluzione 1: Shared Memory per l'IPC (Complessa)
Dovresti allocare l'intera struttura `g_registry` (o almeno le `MsgQueue` usate per l'IPC) in un blocco di memoria condivisa anonima anziché usare la normale `malloc`.
Nel `runtime.c` o `registry.c`:
```c
#include <sys/mman.h>
// Al posto di xmalloc per le code IPC, potresti usare:
void* shared_mem = mmap(NULL, SIZE, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
```
Tuttavia, dato che il tuo registro usa alberi di puntatori (`*parent`, `*children`), la memoria condivisa tradizionale rischia di rompere i puntatori se i processi mappano la shared memory ad indirizzi virtuali diversi.

#### Soluzione 2: Unix Pipes sotto il cofano (Consigliata per la versione Hosted)
Quando il runtime è compilato con `NEXS_MACOS` o per Linux, potresti modificare `reg_ipc.c` per usare veri e propri *pipe* o *Unix Domain Sockets* dietro le quinte.
Quando si chiama `sendmessage("/path", val)`:
1. Controlla se quel path nel registro è marcato come "IPC attivo".
2. Serializza la variabile NEXS in stringa/byte.
3. Scrivi su un File Descriptor (pipe o socket file) associato a quel path.
4. `receivemessage` leggerà (in modo non bloccante usando `poll()` o O_NONBLOCK) dal lato di lettura della pipe.

In questo modo, la struttura Plan 9 / seL4 rimane identica in sintassi NEXS (`sendmessage /sys/...`), ma l'infrastruttura sottostante usa i meccanismi POSIX corretti quando gira come processo utente su macOS.

### Conclusione provvisoria
Il tuo script `micro_os.nx` è **perfetto** dal punto di vista concettuale e della sintassi del tuo linguaggio. Ha semplicemente fatto emergere che l'emulatore *hosted* di NEXS manca ancora del ponte (shared memory/pipes) per far comunicare le diverse istanze della RAM generate da `fork()`.