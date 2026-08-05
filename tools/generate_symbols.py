#!/usr/bin/env python3
"""
Automated Symbol Table and Import Contract Generator for ESP32 Dynamic Module Loading.

This script:
1. Extracts unresolved (UNDEF) global symbols from a module ELF file.
2. Checks them against a host ELF file to ensure all required symbols are provided.
3. Fails at build time if any required symbol is missing from the host.
4. Generates a human-readable build artifact (.imports.txt) documenting the symbol contract.
5. Produces a C source file containing the esp_elfsym symbol table array.
"""

import argparse
import logging
import os
import re
import subprocess
import sys


def extract_module_undefined_symbols(module_elf, exclude_list=None):
    """
    Extract undefined (UND) global symbols from the module ELF file using readelf or nm.
    """
    if exclude_list is None:
        exclude_list = {'_start', '__stack', 'elf_find_sym'}

    cmd = ['readelf', '-s', '-W', module_elf]
    try:
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        # Fallback to nm if readelf is not available
        cmd = ['nm', '-u', module_elf]
        try:
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
            symbols = [line.strip() for line in res.stdout.splitlines() if line.strip()]
            return sorted(list(set(s for s in symbols if s not in exclude_list)))
        except Exception as e:
            logging.error(f"Failed to run readelf or nm on module ELF '{module_elf}': {e}")
            sys.exit(1)

    lines = res.stdout.splitlines()
    pattern = re.compile(
        r'^\s*\d*:\s*\w*\s*\d*\s*(?:NOTYPE|FUNC|OBJECT)\s+GLOBAL\s+DEFAULT\s+UND\s+(\S+)',
        re.MULTILINE
    )

    symbols = set()
    for line in lines:
        match = pattern.match(line)
        if match:
            sym = match.group(1)
            if sym and sym not in exclude_list and not sym.startswith('.L'):
                symbols.add(sym)

    return sorted(list(symbols))


def extract_host_defined_symbols(host_elf):
    """
    Extract defined global symbols from the host ELF file.
    """
    cmd = ['readelf', '-s', '-W', host_elf]
    try:
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
    except (subprocess.CalledProcessError, FileNotFoundError):
        cmd = ['nm', '--defined-only', '-g', host_elf]
        try:
            res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=True)
            symbols = set()
            for line in res.stdout.splitlines():
                parts = line.strip().split()
                if len(parts) >= 3:
                    symbols.add(parts[2])
            return symbols
        except Exception as e:
            logging.error(f"Failed to run readelf or nm on host ELF '{host_elf}': {e}")
            sys.exit(1)

    lines = res.stdout.splitlines()

    # Pattern for defined global symbols: Ndx is not UND
    pattern = re.compile(
        r'^\s*\d+:\s+[0-9a-fA-F]+\s+\d+\s+(?:FUNC|OBJECT|NOTYPE)\s+GLOBAL\s+DEFAULT\s+(?!(?:UND|ABS|DEBUG))\S+\s+(\S+)',
        re.MULTILINE
    )

    symbols = set()
    for line in lines:
        match = pattern.match(line)
        if match:
            sym = match.group(1)
            if sym:
                symbols.add(sym)

    # Also capture ABS global symbols (e.g. C library aliases or inline function symbols)
    abs_pattern = re.compile(
        r'^\s*\d+:\s+[0-9a-fA-F]+\s+\d+\s+(?:FUNC|OBJECT|NOTYPE)\s+GLOBAL\s+DEFAULT\s+ABS\s+(\S+)',
        re.MULTILINE
    )
    for line in lines:
        match = abs_pattern.match(line)
        if match:
            sym = match.group(1)
            if sym:
                symbols.add(sym)

    return symbols


def generate_c_file(symbols, table_name, output_c_path):
    """
    Generate C source file declaring the symbols and defining the esp_elfsym table array.
    """
    buf = [
        '/* Generated automatically by tools/generate_symbols.py. Do not edit. */',
        '#include <stddef.h>',
        '#include "private/elf_symbol.h"',
        '',
        '#pragma GCC diagnostic push',
        '#pragma GCC diagnostic ignored "-Wbuiltin-declaration-mismatch"',
    ]

    for sym in symbols:
        buf.append(f'extern int {sym};')

    buf.extend([
        '#pragma GCC diagnostic pop',
        '',
        f'const struct esp_elfsym {table_name}[] = {{'
    ])

    for sym in symbols:
        buf.append(f'    ESP_ELFSYM_EXPORT({sym}),')

    buf.extend([
        '    ESP_ELFSYM_END',
        '};',
        ''
    ])

    os.makedirs(os.path.dirname(os.path.abspath(output_c_path)), exist_ok=True)
    with open(output_c_path, 'w') as f:
        f.write('\n'.join(buf))


def generate_imports_artifact(symbols, module_elf, host_elf, output_artifact_path):
    """
    Generate human-readable import contract artifact (.imports.txt).
    """
    buf = [
        '# ==============================================================================',
        '# SYMBOL IMPORT CONTRACT ARTIFACT',
        f'# Module: {os.path.basename(module_elf)}',
        f'# Host:   {os.path.basename(host_elf) if host_elf else "N/A"}',
        f'# Total Imported Symbols: {len(symbols)}',
        '# ==============================================================================',
        ''
    ]

    for sym in symbols:
        buf.append(sym)

    buf.append('')

    os.makedirs(os.path.dirname(os.path.abspath(output_artifact_path)), exist_ok=True)
    with open(output_artifact_path, 'w') as f:
        f.write('\n'.join(buf))


def main():
    parser = argparse.ArgumentParser(description='Automated Symbol Table & Contract Generator')
    parser.add_argument('--module-elf', '-m', required=True, help='Path to module ELF file')
    parser.add_argument('--host-elf', '-H', required=False, help='Path to host ELF file for verification')
    parser.add_argument('--output-c', '-oc', required=True, help='Path to output generated C symbol table file')
    parser.add_argument('--output-imports', '-oi', required=True, help='Path to output imports contract text file')
    parser.add_argument('--table-name', '-t', default='gb_host_symbols', help='C variable name for symbol table')
    parser.add_argument('--exclude', '-e', nargs='+', default=[], help='Symbols to exclude')

    args = parser.parse_args()

    exclude_set = {'_start', '__stack', 'elf_find_sym'}.union(set(args.exclude))

    # 1. Extract undefined symbols from module ELF
    if not os.path.exists(args.module_elf):
        logging.error(f"Module ELF file not found: {args.module_elf}")
        sys.exit(1)

    module_symbols = extract_module_undefined_symbols(args.module_elf, exclude_list=exclude_set)

    # 2. Check against host ELF if provided
    if args.host_elf:
        if not os.path.exists(args.host_elf):
            logging.error(f"Host ELF file not found: {args.host_elf}")
            sys.exit(1)

        host_symbols = extract_host_defined_symbols(args.host_elf)
        missing = [sym for sym in module_symbols if sym not in host_symbols]

        if missing:
            print("=" * 80, file=sys.stderr)
            print("[ERROR] SYMBOL RESOLUTION CONTRACT FAILURE!", file=sys.stderr)
            print(f"Module: {args.module_elf}", file=sys.stderr)
            print(f"Host:   {args.host_elf}", file=sys.stderr)
            print(f"\nThe following {len(missing)} required symbol(s) are MISSING from the host ELF:", file=sys.stderr)
            for sym in missing:
                print(f"  - {sym}", file=sys.stderr)
            print("\nThe module cannot be loaded at runtime because the host does not provide these symbols.", file=sys.stderr)
            print("Failing the build now to prevent runtime crashes (-ENOSYS / crash).", file=sys.stderr)
            print("=" * 80, file=sys.stderr)
            sys.exit(1)
        else:
            print(f"[SYMBOLS] All {len(module_symbols)} imported symbols verified against host '{os.path.basename(args.host_elf)}'.")

    # 3. Generate Build Artifact (.imports.txt)
    generate_imports_artifact(module_symbols, args.module_elf, args.host_elf, args.output_imports)
    print(f"[SYMBOLS] Contract artifact saved: '{args.output_imports}' ({len(module_symbols)} symbols)")

    # 4. Generate C source file (.c)
    generate_c_file(module_symbols, args.table_name, args.output_c)
    print(f"[SYMBOLS] C symbol table saved: '{args.output_c}' (Table name: {args.table_name})")


if __name__ == '__main__':
    main()
