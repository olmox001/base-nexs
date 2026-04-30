# NEXS — APIC & Interrupt Routing Implementation

Questo documento illustra l'architettura del sottosistema di gestione delle interruzioni hardware per la piattaforma **AMD64** (x86-64) all'interno del kernel NEXS baremetal.

## 1. Stato dell'Arte

L'implementazione attuale in [hal/amd64/apic.c](file:///Users/olmo/Documents/git/base-nexs/hal/amd64/apic.c) si appoggia su due componenti hardware fondamentali delle architetture x86 moderne:
*   **LAPIC (Local APIC):** Un controller integrato in ogni core CPU. Viene usato per generare i tick del timer di sistema (1000 Hz / 1 ms), fondamentali per lo scheduler.
*   **IOAPIC (I/O APIC):** Un chip sulla scheda madre che si occupa di ricevere i segnali elettrici (IRQ) dalle periferiche legacy del bus ISA/PCI (come tastiere, controller disco, ecc.) e smistarli ai LAPIC.

## 2. Analisi della Funzione `ioapic_route`

Nel codice sorgente è definita la funzione `ioapic_route`:
```c
static void ioapic_route(uint8_t irq, uint8_t vec, uint8_t apic_id)
```

### Perché non veniva invocata?
Attualmente NEXS gira in ambiente QEMU con supporto per l'I/O su **porta seriale UART** in polling (lettura attiva della CPU, vedi [hal/amd64/uart.c](file:///Users/olmo/Documents/git/base-nexs/hal/amd64/uart.c)), anziché tramite interrupt generati da tastiera o mouse PS/2 legacy.

Di conseguenza, l'unico interrupt configurato è quello interno del LAPIC, rendendo l'instradamento dell'IOAPIC temporaneamente superfluo.

## 3. Come Sfruttarla e Cosa Manca

Mantenere questa funzione è cruciale se l'architettura dovesse evolvere verso:
1.  **Input Guidato da Interrupt:** Passare dal polling UART a una gestione asincrona degli interrupt della tastiera seriale.
2.  **Supporto Periferiche Legacy:** Abilitare controller tastiera classica (IRQ 1 su PS/2), mouse, porte parallele, ecc.

Per sopprimere gli avvisi del compilatore senza perdere il codice è stato applicato l'attributo `__attribute__((unused))`.
