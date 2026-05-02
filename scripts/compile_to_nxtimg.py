#!/usr/bin/env python3
"""
compile_to_nxtimg.py — Converte immagini PNG/JPG in file .nxtimg (testo ANSI)
Uso: python3 compile_to_nxtimg.py input.png output.nxtimg [--width 80] [--height 24]

Se non passi --width/--height, vengono usate le variabili d ambiente COLUMNS e LINES
(quando la shell le esporta) per adattare il raster alla finestra corrente.

Output: un file di testo con codici ANSI che può essere:
  - Caricato da termimg.nx in NEXS
  - Montato nel VFS come sd02/logos/nomefile.nxtimg
  - Visualizzato con `termimg nomefile.nxtimg` dalla shell minios
"""

import sys
import os
from PIL import Image

def rgb_to_ansi_256(r, g, b):
    """Converte RGB a codice ANSI 256 colori."""
    if r == g == b:
        if r < 30:
            return 16  # nero
        if r > 225:
            return 231  # bianco
        # scala di grigi 232-255
        gray_idx = round((r / 255.0) * 23)
        return 232 + gray_idx
    
    # Cubo 6x6x6 (16-231)
    r_idx = round((r / 255.0) * 5)
    g_idx = round((g / 255.0) * 5)
    b_idx = round((b / 255.0) * 5)
    return 16 + r_idx * 36 + g_idx * 6 + b_idx


def image_to_nxtimg(input_path, output_path, term_width=80, term_height=24):
    """
    Converte un'immagine in formato .nxtimg (testo ANSI raw).
    Usa il carattere block inferiore ▀ (U+2580) per raddoppiare la risoluzione verticale.
    """
    try:
        img = Image.open(input_path).convert('RGBA')
    except Exception as e:
        print(f"[ERR] Impossibile aprire '{input_path}': {e}")
        return False
    
    # Converti in RGBA per gestire la trasparenza
    img = img.convert('RGBA')
    
    # Ridimensiona all'altezza richiesta (x2 per mezzo blocco)
    display_height = term_height * 2
    aspect_ratio = img.width / img.height
    new_width = int(display_height * aspect_ratio)
    new_height = display_height
    
    if new_width > term_width:
        new_width = term_width
        new_height = int(new_width / aspect_ratio)
        display_height = new_height
    
    img = img.resize((new_width, new_height), Image.Resampling.NEAREST).convert('RGBA')
    
    output_lines = []
    
    # Processa ogni riga di caratteri
    term_height_actual = (new_height + 1) // 2
    for char_y in range(term_height_actual):
        line_chars = []
        last_fg = None
        last_bg = None
        
        for char_x in range(new_width):
            img_x = int((char_x / new_width) * img.width)
            img_y_top = int(((char_y * 2) / display_height) * img.height)
            img_y_bottom = int(((char_y * 2 + 1) / display_height) * img.height)
            
            img_x = min(img_x, img.width - 1)
            img_y_top = min(img_y_top, img.height - 1)
            img_y_bottom = min(img_y_bottom, img.height - 1)
            
            top_pixel = img.getpixel((img_x, img_y_top))
            bottom_pixel = img.getpixel((img_x, img_y_bottom))
            
            top_a = top_pixel[3]
            bottom_a = bottom_pixel[3]
            
            # Thresholds
            top_visible = top_a > 20
            bottom_visible = bottom_a > 20
            
            top_opaque = top_a > 153
            bottom_opaque = bottom_a > 153
            
            if not top_visible and not bottom_visible:
                # Fully transparent
                if last_fg is not None or last_bg is not None:
                    line_chars.append("\033[0m")
                    last_fg = None
                    last_bg = None
                line_chars.append(" ")
                
            elif top_visible and not bottom_visible:
                # Top visible, bottom transparent -> Use ▀ with FG only
                fg_code = rgb_to_ansi_256(*top_pixel[:3])
                if fg_code != last_fg or last_bg is not None:
                    line_chars.append(f"\033[0m\033[38;5;{fg_code}m")
                    last_fg = fg_code
                    last_bg = None
                line_chars.append("▀")
                
            elif not top_visible and bottom_visible:
                # Top transparent, bottom visible -> Use ▄ with FG only
                fg_code = rgb_to_ansi_256(*bottom_pixel[:3])
                if fg_code != last_fg or last_bg is not None:
                    line_chars.append(f"\033[0m\033[38;5;{fg_code}m")
                    last_fg = fg_code
                    last_bg = None
                line_chars.append("▄")
                
            else:
                # Both are visible.
                if top_opaque and bottom_opaque:
                    # Both opaque -> Use ▀ with FG for top and BG for bottom
                    fg_code = rgb_to_ansi_256(*top_pixel[:3])
                    bg_code = rgb_to_ansi_256(*bottom_pixel[:3])
                    
                    if last_bg is None or fg_code != last_fg or bg_code != last_bg:
                        line_chars.append(f"\033[0m\033[38;5;{fg_code}m\033[48;5;{bg_code}m")
                        last_fg = fg_code
                        last_bg = bg_code
                    line_chars.append("▀")
                else:
                    # At least one is semi-transparent.
                    # Use only half-block (no background) to fake transparency!
                    if top_a >= bottom_a:
                        fg_code = rgb_to_ansi_256(*top_pixel[:3])
                        if fg_code != last_fg or last_bg is not None:
                            line_chars.append(f"\033[0m\033[38;5;{fg_code}m")
                            last_fg = fg_code
                            last_bg = None
                        line_chars.append("▀")
                    else:
                        fg_code = rgb_to_ansi_256(*bottom_pixel[:3])
                        if fg_code != last_fg or last_bg is not None:
                            line_chars.append(f"\033[0m\033[38;5;{fg_code}m")
                            last_fg = fg_code
                            last_bg = None
                        line_chars.append("▄")
        
        # Reset a fine riga
        if last_fg is not None or last_bg is not None:
            line_chars.append("\033[0m")
        
        output_lines.append("".join(line_chars))
    
    # Scrivi il file .nxtimg
    try:
        with open(output_path, "w", encoding="utf-8", newline='\n') as f:
            f.write("\n".join(output_lines))
        print(f"[OK] Compilato: {output_path} ({term_width}x{term_height})")
        return True
    except Exception as e:
        print(f"[ERR] Impossibile scrivere '{output_path}': {e}")
        return False


def main():
    # Handle batch logo compilation
    if "--logos" in sys.argv:
        # Determine paths relative to the script
        script_dir = os.path.dirname(os.path.abspath(__file__))
        src_dir = os.path.join(script_dir, "logo")
        dst_dir = os.path.join(script_dir, "..", "example", "minios", "logos")
        
        # Ensure destination exists
        if not os.path.exists(dst_dir):
            os.makedirs(dst_dir, exist_ok=True)
            
        if not os.path.exists(src_dir):
            print(f"[ERR] Sorgente loghi non trovata in: {src_dir}")
            sys.exit(1)
            
        print(f"[*] Batch mode: Compilazione loghi da {src_dir}...")
        count = 0
        for f in os.listdir(src_dir):
            if f.lower().endswith(('.png', '.jpg', '.jpeg')):
                in_p = os.path.join(src_dir, f)
                out_name = os.path.splitext(f)[0] + ".nxtimg"
                out_p = os.path.join(dst_dir, out_name)
                if image_to_nxtimg(in_p, out_p):
                    count += 1
        
        print(f"[*] Completato. {count} loghi compilati in {dst_dir}")
        sys.exit(0)

    if len(sys.argv) < 3:
        print("Uso:")
        print("  python3 compile_to_nxtimg.py input.png output.nxtimg [--width 80] [--height 24]")
        print("  python3 compile_to_nxtimg.py --logos (Batch mode for scripts/logo/)")
        sys.exit(1)
    
    input_path = sys.argv[1]
    output_path = sys.argv[2]
    
    term_width = 80
    term_height = 24
    # Allinea alla shell corrente se COLUMNS/LINES sono impostati
    try:
        cw = os.environ.get("COLUMNS")
        cl = os.environ.get("LINES")
        if cw and cw.isdigit():
            w = int(cw)
            if w >= 20: term_width = w
        if cl and cl.isdigit():
            h = int(cl)
            if h >= 5: term_height = h
    except Exception:
        pass

    # Parse optional arguments
    for i in range(1, len(sys.argv)):
        if sys.argv[i] == "--width" and i + 1 < len(sys.argv):
            term_width = int(sys.argv[i + 1])
        elif sys.argv[i] == "--height" and i + 1 < len(sys.argv):
            term_height = int(sys.argv[i + 1])
    
    if not os.path.exists(input_path):
        print(f"[ERR] File non trovato: {input_path}")
        sys.exit(1)
    
    success = image_to_nxtimg(input_path, output_path, term_width, term_height)
    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
