# diff_heap_native.gdb - same 64 KB (shifted by 0xD20000) + .data, at entry of sub_00012400
set pagination off
handle SIGSEGV stop print
break sub_00012400
run
eval "dump binary memory D:/Emulation/RE/pipeline/out/heap_native.bin 0x%llx 0x%llx", (unsigned long long)g_memory_offset + 0x1860000, (unsigned long long)g_memory_offset + 0x1870000
eval "dump binary memory D:/Emulation/RE/pipeline/out/data_native.bin 0x%llx 0x%llx", (unsigned long long)g_memory_offset + 0x227680, (unsigned long long)g_memory_offset + 0x29f000
kill
quit
