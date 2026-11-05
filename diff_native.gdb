# diff_native.gdb - recompiled side: same moment (entry of sub_00033720), same guest memory via g_memory_offset
set pagination off
handle SIGSEGV stop print
break sub_00033720
run
printf "=== NATIVE at sub_00033720: offset=%llx ===\n", (unsigned long long)g_memory_offset
eval "x/64wx 0x%llx", (unsigned long long)g_memory_offset + 0x298a40
eval "x/4wx 0x%llx", (unsigned long long)g_memory_offset + 0x2a8e94
kill
quit
