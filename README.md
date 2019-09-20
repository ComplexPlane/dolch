# dolch

Simple Gamecube/Wii DOL executable utility. Currently you can add a new code section to a DOL, or display the DOL header.

## Usage

```
dolch [-a|--add-section] [in.dol] [out.dol] [section_size]
dolch [-i|--info] [in.dol]
```

## Sample output

```
$ dolch -i main.dol
DOL size: 0x00407b00

TEXT SECTIONS
Section 00: start_offset = 0x00000100, end_offset = 0x000004a0, start_addr = 0x80003100, end_addr = 0x800034a0, size = 0x000003a0
Section 01: start_offset = 0x000004a0, end_offset = 0x0037f460, start_addr = 0x800034a0, end_addr = 0x80382460, size = 0x0037efc0
Section 02: unused.
Section 03: unused.
Section 04: unused.
Section 05: unused.
Section 06: unused.

DATA SECTIONS
Section 07: start_offset = 0x0037f460, end_offset = 0x0037f6a0, start_addr = 0x80382460, end_addr = 0x803826a0, size = 0x00000240
Section 08: start_offset = 0x0037f6a0, end_offset = 0x003c2380, start_addr = 0x803826a0, end_addr = 0x803c5380, size = 0x00042ce0
Section 09: start_offset = 0x003c2380, end_offset = 0x00405120, start_addr = 0x803c5380, end_addr = 0x80408120, size = 0x00042da0
Section 10: start_offset = 0x00405120, end_offset = 0x004070a0, start_addr = 0x805e9dc0, end_addr = 0x805ebd40, size = 0x00001f80
Section 11: start_offset = 0x004070a0, end_offset = 0x00407b00, start_addr = 0x805ec9a0, end_addr = 0x805ed400, size = 0x00000a60
Section 12: unused.
Section 13: unused.
Section 14: unused.
Section 15: unused.
Section 16: unused.
Section 17: unused.

bss: start_addr = 0x80408140, end_addr = 0x805ec984, size = 0x001e4844
entry point address: 0x80003100
```
