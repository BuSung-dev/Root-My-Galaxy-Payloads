# Target Generator (`target.h`)

Tools to extract kernel symbols, configs, and function offsets directly from an uncompressed kernel binary (`Image`) to auto-patch `target.h`.

> [!NOTE]
> This target generator is **only used to generate `target.h` for Samsung Galaxy S22 / S22 Ultra (b0q)** devices because this repository's exploit is only ported for S22 / S22 Ultra.
> `generate_target.py` extracts a few dynamic offsets while other struct and system definitions remain identical throughout firmware versions.

---

## 📋 Step-by-Step Instructions

### Step 1: Install Dependencies
Installs the Capstone disassembler module required by the Python script to analyze kernel instructions.
```bash
pip install capstone
```

### Step 2: Compile the `kallsyms` Extractor
Compiles `kallsyms.c` into an executable tool used to locate and parse symbol table structures inside the kernel binary.
```bash
gcc -O2 kallsyms.c -o kallsyms
```

### Step 3: Extract Symbol Table (`kallsyms.txt`)
Scans the kernel `Image` binary and generates `kallsyms.txt` containing symbol names and addresses.
```bash
./kallsyms Image
```

### Step 4: Extract Kernel Config (`config.txt`)
Extracts compressed `CONFIG_*` settings from the kernel `Image` and saves them to `config.txt`.
```bash
./extract-ikconfig Image > config.txt
```

### Step 5: Generate & Patch `target.h`
Calculates dynamic offsets using `kallsyms.txt`, `config.txt`, and disassembly from `Image`, then updates placeholder defines in `target.h`.
```bash
python3 generate_target.py kallsyms.txt config.txt Image --template target.h -o target.h
```

### Step 6: Create Target Directory and Compile
After generating `target.h`:
1. Create a folder in `src/targets/` named exactly after your firmware version (e.g. `src/targets/S908WVLS8FYG7`).
2. Move your generated `target.h` into that newly created folder (`src/targets/<YOUR_FIRMWARE_VERSION>/target.h`).
3. Run `make` passing your firmware version as `PROJECT`:
```bash
make PROJECT=<YOUR_FIRMWARE_VERSION>
```

---

## ⚡ All-in-One Command

Run the complete pipeline sequentially in one line:
```bash
gcc -O2 kallsyms.c -o kallsyms && ./kallsyms Image && ./extract-ikconfig Image > config.txt && python3 generate_target.py kallsyms.txt config.txt Image --template target.h -o target.h
```

---

## 📁 Files Reference

- `generate_target.py` — Main script that resolves kernel offsets and patches the header file.
- `target.h` — C header template containing target definitions to be patched.
- `kallsyms.c` — Source code for extracting the symbol table from raw kernel binary data.
- `extract-ikconfig` — Shell script to unpack kernel configuration settings (`CONFIG_*`).

