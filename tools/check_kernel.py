import struct
import os

def deep_check(path):
    if not os.path.exists(path):
        print(f"[-] Errore: {path} non esiste.")
        return

    with open(path, 'rb') as f:
        data = f.read()

    print(f"[*] Analisi di: {path} ({len(data)} byte)")
    
    # Cerca i Magic ovunque nel file
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

if __name__ == "__main__":
    deep_check("build/baremetal-amd64/nexs.elf")