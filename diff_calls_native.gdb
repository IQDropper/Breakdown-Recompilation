# diff_calls_native.gdb - every sub_0017DDF0 (VsHeap_New) call in the recompiled build until sub_00033720: caller function
set pagination off
handle SIGSEGV stop print
break sub_00033720
break sub_0017DDF0
commands 2
  silent
  bt 2
  eval "printf \"counter=%%d\\n\", *(unsigned short*)0x%llx", (unsigned long long)g_memory_offset + 0x186280c
  continue
end
run
eval "printf \"=== reached counter=%%d ===\\n\", *(unsigned short*)0x%llx", (unsigned long long)g_memory_offset + 0x186280c
kill
quit
