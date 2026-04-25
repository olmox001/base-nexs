; ============================================================
; hal/amd64/boot/stage1.S — NEXS Stage1 MBR Bootloader
; ============================================================
; Exactly 446 bytes of code (+ 64-byte PT + 2-byte signature = 512).
; Assembled with: nasm -f bin -o stage1.bin stage1.S
;
; Responsibilities:
;   1. Enter real mode with clean segment state
;   2. Load 62 sectors (stage2, ~31 KB) from LBA 1 → 0x0000:0x7E00
;      via BIOS int 0x13/AH=0x42 (LBA extended read)
;   3. Jump to stage2 at 0x7E00
;
; Layout on disk:
;   Sector 0  (LBA 0): stage1.bin     (this file, 512 bytes)
;   Sectors 1–62 (LBA 1–62): stage2   (~31 KB)
;   Sector 63+: kernel image
; ============================================================

BITS 16
ORG  0x7C00

stage1_start:
    cli
    xor  ax, ax
    mov  ds, ax
    mov  es, ax
    mov  ss, ax
    mov  sp, 0x7C00         ; stack grows down from 0x7C00

    sti

    ; Save boot drive number (BIOS puts it in DL)
    mov  [boot_drive], dl

    ; ── LBA Extended Read ─────────────────────────────────────
    ; Build Disk Address Packet (DAP) at 0x0600
    mov  si, dap
    mov  word [si],     0x0010  ; packet size = 16 bytes
    mov  word [si+2],   62      ; sectors to read
    mov  word [si+4],   0x7E00  ; dest offset
    mov  word [si+6],   0x0000  ; dest segment
    mov  dword [si+8],  1       ; LBA low  = sector 1
    mov  dword [si+12], 0       ; LBA high = 0

    mov  ah, 0x42           ; extended read
    mov  dl, [boot_drive]
    int  0x13
    jc   .disk_error

    ; ── Jump to stage2 ────────────────────────────────────────
    mov  dl, [boot_drive]   ; pass boot drive in DL
    jmp  0x0000:0x7E00

.disk_error:
    mov  si, msg_err
.print_loop:
    lodsb
    test al, al
    jz   .hang
    mov  ah, 0x0E
    int  0x10
    jmp  .print_loop
.hang:
    hlt
    jmp  .hang

; ── Data ──────────────────────────────────────────────────────
boot_drive: db 0

dap:                        ; Disk Address Packet (filled above)
    times 16 db 0

msg_err:
    db "Stage1: disk error", 0x0D, 0x0A, 0

; ── Padding + Partition Table + Boot Signature ────────────────
    times 446 - ($ - stage1_start) db 0

partition_table:
    ; Entry 0: active, type 0x83 (Linux), LBA 63, size 2^17 sectors
    db 0x80             ; status: bootable
    db 0x00, 0x02, 0x00 ; CHS first sector (unused in LBA mode)
    db 0x83             ; partition type
    db 0xFF, 0xFF, 0xFF ; CHS last sector  (unused)
    dd 63               ; LBA start
    dd 0x00020000       ; sector count (~128 MB)
    times 3 * 16 db 0   ; entries 1–3 empty

    dw 0xAA55           ; boot signature
