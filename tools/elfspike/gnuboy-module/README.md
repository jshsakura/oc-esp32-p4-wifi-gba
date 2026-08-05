# gnuboy elf_loader module

This standalone ESP-IDF project compiles the read-only gnuboy sources from
`retro-core/components/gnuboy` with `elf_loader`'s `project_elf()` macro. The
module entry point returns a `gnuboy_module_api_t` through `argv[0]`; this also
keeps the complete public core API reachable when the linker runs
`--gc-sections`.

Build the module for ESP32-P4:

```sh
. ~/esp/esp-idf/export.sh
idf.py set-target esp32p4
idf.py elf
```

The output is `build/gnuboy_module.app.elf`. Use the `elf` target, not the
ordinary `build` target: a normal ESP-IDF application link rejects the host
symbols that a runtime-loaded module intentionally leaves unresolved.

Verify the entry point and imports:

```sh
riscv32-esp-elf-readelf -h build/gnuboy_module.app.elf | grep Entry
riscv32-esp-elf-readelf -Ws build/gnuboy_module.app.elf \
    | awk '$7 == "UND" && $8 != "" {print $8}' | sort -u
```
