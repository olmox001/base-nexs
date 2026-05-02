# Logo & Image System per NEXS minios

## Architettura

Il sistema di loghi/immagini per NEXS minios funziona in questa catena:

```
PNG/JPG (input)
    ↓
compile_to_nxtimg.py (Python script)
    ↓
.nxtimg (testo ANSI raw, 80x24)
    ↓
init_logos.nx (carica nel VFS)
    ↓
sd02/logos/*.nxtimg (filesystem virtuale)
    ↓
termimg.nx (viewer)
    ↓
shell: termimg logo.nxtimg
    o
nxed: CTRL-X → [v] → visualizza .nxtimg
```

## Componenti

### 1. compile_to_nxtimg.py

Script Python che converte immagini PNG/JPG in formato `.nxtimg` (testo ANSI).

**Uso:**
```bash
python3 compile_to_nxtimg.py input.png output.nxtimg [--width 80] [--height 24]
```

**Funzionamento:**
- Ridimensiona l'immagine a 80x24 caratteri
- Usa il carattere block inferiore `▀` (U+2580) per raddoppiare la risoluzione verticale
- Converte RGB → ANSI 256 colori
- Output: file di testo UTF-8 con codici ANSI puri (compatibile con VFS NEXS)

### 2. init_logos.nx

Script NEXS eseguito durante il boot di minios (Fase 3).

**Cosa fa:**
- Crea la directory `sd02/logos` nel VFS
- Legge i file `.nxtimg` dal root del VFS
- Scrive i file nella cartella `sd02/logos` per accesso rapido
- Registra i loghi nel registry (`/sys/logos/*`)

**Integrazione in boot.nx:**
```nx
exec("example/minios/init_logos.nx")  # Carica loghi nel VFS
```

### 3. termimg.nx

Visualizzatore di immagini NEXS native che legge file `.nxtimg`.

**Funzionalità:**
- Carica e visualizza `.nxtimg` nel terminale
- Supporta hotkey per reload dinamico (`[r]`)
- Exit con `[q]` o `[ESC]`
- Gestisce la modalità raw del terminale
- Ripristina il cursore all'uscita

**Funzione wrapper:**
```nx
fn nxed_termimg(filename) {
    # Risolve il percorso e chiama termimg_start()
}
```

### 4. nxed_editor.nx

Editor di testo NEXS esteso per supportare i loghi.

**Nuova hotkey nel menu:**
- `[v]` - Visualizza file `.nxtimg`
- Prompt per il percorso del file immagine
- Integrazione con `termimg_start()`

### 5. shell.nx

Shell minios estesa con comando `termimg`.

**Nuovo comando:**
```
termimg [file]      - Visualizzatore immagini NEXS (.nxi)
```

**Uso:**
```bash
nexs-os:/> termimg sd02/logos/logo.nxtimg
```

## Flusso di utilizzo

### A. Da shell (modo semplice)
```bash
nexs-os:/> termimg sd02/logos/logo.nxtimg
# Visualizza il logo
# Premi [q] per tornare alla shell
```

### B. Da nxed_editor (modo integrato)
```
1. Apri nxed: nxed [file]
2. Premi CTRL-X per il menu
3. Premi [v] per visualizzare immagine
4. Inserisci percorso: sd02/logos/logo.nxtimg
5. Visualizza → Premi [q] per tornare all'editor
```

### C. Compilazione manuale di loghi
```bash
# Da host (macOS/Linux)
cd /Users/olmo/Downloads/termimg-main

# Compila una singola immagine
python3 compile_to_nxtimg.py mia_immagine.png mia_immagine.nxtimg

# Usa make (dal repo base-nexs)
cd base-nexs
make compile-logos  # Compila tutti i .png in example/minios/logos/
```

## Struttura file

```
base-nexs/
├── compile_to_nxtimg.py          # Script compilatore (host side)
├── example/minios/
│   ├── logos/
│   │   └── logo.nxtimg           # Logo compilato (22 righe ANSI)
│   ├── init_logos.nx             # Caricatore loghi (boot)
│   ├── boot.nx                   # Modificato per exec(init_logos.nx)
│   ├── nxed_editor.nx            # Modificato con hotkey [v]
│   ├── shell.nx                  # Modificato con comando termimg
│   └── termimg.nx                # Viewer immagini
└── Makefile                      # Target compile-logos aggiunto
```

## Processo di boot

Durante il boot di minios:

```
1. [INIT] Fase 1: Caricamento servizi (stdlib, fs, pm, auth, tty, ui, p9, hw_dt, textbuf)
2. [INIT] Fase 2: Inizializzazione sottosistemi (hw_dt_init, p9_mnt_init)
3. [INIT] Fase 3: Popolamento FS e caricamento loghi
   → exec("example/minios/init_logos.nx")
   → Crea sd02/logos e carica logo.nxtimg
   → Registra in /sys/logos/main
4. [INIT] Caricamento demoni (nxed_editor, termimg, shell)
5. [INIT] Boot completato → shell_loop()
```

## Espansione futura

### Aggiungere nuovi loghi
1. Salva l'immagine PNG in `base-nexs/example/minios/logos/`
2. Esegui `make compile-logos`
3. I file `.nxtimg` vengono generati automaticamente
4. Caricheranno al boot tramite `init_logos.nx`

### Loghi dinamici
I loghi possono essere generati al runtime:
```nx
# Genera asciiart dinamicamente
fn gen_ascii_logo() {
  out "NEXS"
  out "███████"
}

# Oppure carica da rete/device
logo_data = vfs_read("sd01/remote_logo.nxtimg")
```

### Integrazione con splash screen
Modificare `boot.nx` per visualizzare un logo splash:
```nx
if file_exists("sd02/logos/splash.nxtimg") {
  exec("example/minios/termimg.nx")
  termimg_start("sd02/logos/splash.nxtimg")
}
```

## Limitazioni attuali

- Max risoluzione: 80x24 (standard ANSI terminal)
- Colori: limitati a ANSI 256 (riduzione RGB)
- Formato: solo testo ANSI (no binary image data nel VFS)
- File size: ~10KB per logo (efficiente per il VFS)

## Troubleshooting

### Logo non appare
1. Verifica che `logo.nxtimg` esista in `example/minios/logos/`
2. Controlla che `init_logos.nx` sia in `boot.nx`
3. Verifica: `nexs-os:/> ls sd02/logos`

### termimg non risponde ai tasti
1. Assicurati che `rawon()` sia stato chiamato
2. Verifica `readkey()` implementation nel kernel
3. Prova in modalità REPL: `readkey()` manualmente

### Caratteri corrutti
1. Verifica encoding UTF-8 (file .nxtimg)
2. Controlla che il terminale supporti `▀` (U+2580)
3. Risalva il file `.nxtimg` con encoding UTF-8 senza BOM
