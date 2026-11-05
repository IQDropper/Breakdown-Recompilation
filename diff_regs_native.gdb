# diff_regs_native.gdb - guest registers at entry of the 9th sub_0017DDF0 (VsHeap_New) call + its stack args
set pagination off
handle SIGSEGV stop print
break sub_0017DDF0
ignore 1 8
run
printf "NATIVE 9th entry: eax=%08x ecx=%08x edx=%08x ebx=%08x esp=%08x ebp=%08x esi=%08x edi=%08x\n", *(unsigned*)&g_eax, *(unsigned*)&g_ecx, *(unsigned*)&g_edx, *(unsigned*)&g_ebx, *(unsigned*)&g_esp, *(unsigned*)&g_ebp, *(unsigned*)&g_esi, *(unsigned*)&g_edi
eval "x/6wx 0x%llx", (unsigned long long)g_memory_offset + *(unsigned*)&g_esp
kill
quit
