#!/bin/bash
# PS4: fails when an ELF still has .data.*/.tdata.*/.tbss.* sections of their own:
# create-fself (pacbrew) leaves them out of the loaded data segment (see cmake/ps4-link.x).
set -euo pipefail
status=0
for elf in "$@"; do
    orphans=$(readelf -SW "$elf" | sed -n 's/^ *\[ *[0-9]*\] \([^ ]*\) .*/\1/p' |
        grep -E '^\.(data|tdata|tbss)\.' |
        grep -vxE '\.data\.rel\.ro|\.data\.sce_process_param|\.data\.sce_module_param' || true)
    if [ -n "$orphans" ]; then
        echo "::error::$elf: sections outside the loaded data segment: $(echo $orphans)"
        status=1
    else
        echo "$elf: sections OK"
    fi
done
exit $status
