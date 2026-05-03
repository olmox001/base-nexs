import os
import re
import bisect
from collections import defaultdict

# ===================== CONFIG =====================
ROOT = os.getcwd()

EXCLUDE_DIRS = {'.git', 'build', 'RELEASE', 'iso_stage', '__pycache__'}
IMPORTANT_EXT = ('.c', '.S', '.h', '.cpp', '.hpp', '.nx')

GROUPS = {
    "CORE": ["core/"],
    "HAL": ["hal/"],
    "KERNEL": ["kernel/"],
    "LANG": ["lang/"],
    "REGISTRY": ["registry/"],
    "RUNTIME": ["runtime/"],
    "SYS": ["sys/"],
    "FS": ["fs/"],
    "COMPILER": ["compiler/"],
    "LIBRARY": ["library/"],
    "EXAMPLE": ["example/"],
    "OTHER": []
}
# ================================================

FUNC_PATTERN = re.compile(r'^[a-zA-Z_][a-zA-Z0-9_* \t]*\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\([^;]*?\)\s*\{', re.MULTILINE)
GLOBAL_PATTERN = re.compile(r'^(?!\s*(?:extern|typedef|struct|enum|union)\s+)[a-zA-Z_][a-zA-Z0-9_* \t]*\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*(?:\[[^\]]*\])?\s*(?:=[^;]+)?\s*;', re.MULTILINE)
NX_FUNC_PATTERN = re.compile(r'^fn\s+([a-zA-Z_][a-zA-Z0-9_]*)\s*\(', re.MULTILINE)

def extract_symbols(file_path):
    functions = []
    globals_list = []
    try:
        with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()

        line_offsets = [0]
        for i, char in enumerate(content):
            if char == '\n':
                line_offsets.append(i + 1)

        def get_line_no(offset):
            return bisect.bisect_right(line_offsets, offset)

        if file_path.endswith('.nx'):
            for match in NX_FUNC_PATTERN.finditer(content):
                functions.append((match.group(1), get_line_no(match.start())))
        else:
            # Extract C/C++ functions
            for match in FUNC_PATTERN.finditer(content):
                name = match.group(1)
                if name not in ['if', 'while', 'for', 'switch', 'return']:
                    functions.append((name, get_line_no(match.start())))

            # Extract C/C++ globals
            for match in GLOBAL_PATTERN.finditer(content):
                name = match.group(1)
                if name not in ['if', 'while', 'for', 'switch', 'return']:
                    if not any(name == f[0] for f in functions):
                        globals_list.append((name, get_line_no(match.start())))

    except Exception:
        pass
    return functions, globals_list


def map_codebase():
    mapping = defaultdict(lambda: defaultdict(list))
    symbols = defaultdict(list)

    for root, dirs, files in os.walk(ROOT):
        dirs[:] = [d for d in dirs if d not in EXCLUDE_DIRS]
        
        for file in files:
            if not file.endswith(IMPORTANT_EXT):
                continue

            full_path = os.path.join(root, file)
            rel_path = os.path.relpath(full_path, ROOT)
            
            # Assegna al gruppo
            group = "OTHER"
            for g, prefixes in GROUPS.items():
                if any(rel_path.startswith(p) for p in prefixes):
                    group = g
                    break

            funcs, globs = extract_symbols(full_path)

            if funcs or globs:
                mapping[group][rel_path] = (funcs, globs)
                for f, _ in funcs:
                    symbols[f].append(rel_path)
                for g, _ in globs:
                    symbols[g].append(rel_path)

    return mapping, symbols


# ===================== GENERA MAPPA =====================
if __name__ == "__main__":
    mapping, symbols = map_codebase()

    with open("nexs_full_map.md", "w", encoding="utf-8") as f:
        f.write("# NEXS Full Architecture Map\n\n")
        f.write(f"Generated at: {__import__('datetime').datetime.now().date()}\n\n")

        # Redundancy Report
        f.write("## ⚠️ Redundancy Report\n")
        redundant = {name: list(set(paths)) for name, paths in symbols.items() if len(set(paths)) > 1}
        for name, paths in sorted(redundant.items(), key=lambda x: len(x[1]), reverse=True):
            f.write(f"- `{name}` duplicated in: {', '.join(paths)}\n")
        f.write("\n---\n\n")

        # Contenuto per gruppo
        for group in ["CORE", "HAL", "KERNEL", "LANG", "REGISTRY", "RUNTIME", "SYS", "FS", "COMPILER", "LIBRARY", "EXAMPLE", "OTHER"]:
            if group not in mapping or not mapping[group]:
                continue
            f.write(f"## {group}\n\n")
            for filepath in sorted(mapping[group].keys()):
                funcs, globs = mapping[group][filepath]
                f.write(f"### {filepath}\n")
                if funcs:
                    f.write("**Functions:** " + ", ".join([name for name,_ in funcs[:20]]) + ("..." if len(funcs)>20 else "") + "\n")
                if globs:
                    f.write("**Globals:** " + ", ".join([name for name,_ in globs]) + "\n")
                f.write("\n")
            f.write("---\n\n")

    print("✅ Mappa generata correttamente: nexs_full_map.md")