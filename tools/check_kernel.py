import struct
import os
import sys

def deep_check(path):
    if not os.path.exists(path):
        print(f"[-] Errore: {path} non esiste.")
        return

    with open(path, 'rb') as f:
        data = f.read()

    print(f"[*] Analisi di: {path} ({len(data)} byte)")
    
    # 1. Verifica Header ELF
    if len(data) < 64:
        print("[-] File troppo piccolo per essere un ELF.")
        return
        
    ident = data[:16]
    if ident[:4] != b'\x7fELF':
        print("[-] Non è un file ELF valido.")
        return
        
    is_64 = (ident[4] == 2)
    print(f"[*] Tipo ELF: {'64-bit' if is_64 else '32-bit'}")
    
    # Entry Point
    if is_64:
        e_entry = struct.unpack_from('<Q', data, 24)[0]
        e_phoff = struct.unpack_from('<Q', data, 32)[0]
        e_phentsize = struct.unpack_from('<H', data, 54)[0]
        e_phnum = struct.unpack_from('<H', data, 56)[0]
    else:
        e_entry = struct.unpack_from('<I', data, 24)[0]
        e_phoff = struct.unpack_from('<I', data, 28)[0]
        e_phentsize = struct.unpack_from('<H', data, 42)[0]
        e_phnum = struct.unpack_from('<H', data, 44)[0]
        
    print(f"[*] Entry Point: {hex(e_entry)}")
    
    # 2. Analisi Segmenti
    print("[*] Segmenti di Caricamento (LOAD):")
    for i in range(e_phnum):
        off = e_phoff + (i * e_phentsize)
        if is_64:
            p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = struct.unpack_from('<IIQQQQQQ', data, off)
        else:
            p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align = struct.unpack_from('<IIIIIIII', data, off)
            
        if p_type == 1: # PT_LOAD
            print(f"    - LOAD: Offset {hex(p_offset)}, VAddr {hex(p_vaddr)}, FileSz {hex(p_filesz)}, MemSz {hex(p_memsz)}, Flags {hex(p_flags)}")
            if p_memsz > p_filesz:
                print(f"      (BSS rilevata: {hex(p_memsz - p_filesz)} byte)")

    # 3. Cerca i Magic ovunque nel file
    m1 = data.find(struct.pack('<I', 0x1BADB002))
    m2 = data.find(struct.pack('<I', 0xe85250d6))
    pvh = data.find(b"Xen")

    if m1 != -1:
        print(f"[+] Multiboot 1 trovato a offset {hex(m1)} {'[OK]' if m1 < 8192 else '[TROPPO LONTANO]'}")
    else:
        print("[-] Multiboot 1 NON presente.")

    if m2 != -1:
        # Verifica checksum
        magic, arch, length, checksum = struct.unpack_from('<IIII', data, m2)
        valid = (magic + arch + length + checksum) & 0xFFFFFFFF
        print(f"[+] Multiboot 2 trovato a offset {hex(m2)} {'[OK]' if m2 < 32768 else '[TROPPO LONTANO]'}")
        print(f"    Checksum: {hex(checksum)} {'(VALIDO)' if valid == 0 else '(ERRORE!)'}")
    else:
        print("[-] Multiboot 2 NON presente.")

    if pvh != -1:
        print(f"[+] Nota PVH (Xen) trovata a offset {hex(pvh)}")
    else:
        print("[-] Nota PVH NON presente.")

def check_iso(path):
    if not os.path.exists(path):
        print(f"[-] Errore: {path} non esiste.")
        return

    print(f"[*] Analisi ISO: {path}")
    with open(path, 'rb') as f:
        # 1. Verifica MBR (Limine signature)
        f.seek(0)
        mbr = f.read(512)
        if b"LIMINE" in mbr or b"Limine" in mbr:
            print("[+] Firma Limine trovata nell'MBR.")
        else:
            print("[-] Firma Limine NON trovata nell'MBR (iso non ibrida o bios-install non eseguito?)")

        # 2. Cerca file critici (ricerca grezza per ora)
        f.seek(0)
        data = f.read()
        if b"nexs.elf" in data:
            print("[+] Stringa 'nexs.elf' trovata nell'ISO.")
        else:
            print("[-] Stringa 'nexs.elf' NON trovata nell'ISO.")
            
        if b"limine.conf" in data:
            print("[+] Stringa 'limine.conf' trovata nell'ISO.")
        else:
            print("[-] Stringa 'limine.conf' NON trovata nell'ISO.")
            
        if b"PROTOCOL=multiboot2" in data:
            print("[+] Configurazione Multiboot 2 trovata nell'ISO.")
        else:
            print("[-] Configurazione Multiboot 2 NON trovata nell'ISO.")
            
        if b"PROTOCOL=limine" in data:
            print("[+] Configurazione limine trovata nell'ISO.")
        else:
            print("[-] Configurazione limine NON trovata nell'ISO.")

if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "build/baremetal-amd64/nexs.elf"
    if path.endswith(".iso"):
        check_iso(path)
    else:
        deep_check(path)