# diff_native2.gdb - recompiled side: heap root + first heap blocks at entry of sub_00033720
set pagination off
handle SIGSEGV stop print
break sub_00033720
run
eval "x/64wx 0x%llx", (unsigned long long)g_memory_offset + 0x298a40
eval "x/192wx 0x%llx", (unsigned long long)g_memory_offset + 0x1862600
kill
quit
