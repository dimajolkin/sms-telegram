// -----------------------------------------------------------------------
// APP CPU entry point (tinygo#5358).
// -----------------------------------------------------------------------
.section .text.call_start_cpu1
    .literal_position
    .align 4
.Lstack1_top_addr:
    .long _stack1_top
.Lrun_core1_entry_addr:
    .long tinygo_runCore1
.Lvector_table_addr_cpu1:
    .long _vector_table

.global call_start_cpu1
call_start_cpu1:
    // CPU1 starts from ROM with no stack contract that TinyGo can rely on.
    // Repeat the CPU-local windowed-ABI setup from CPU0, then enter Go.
    // Use slli for PS_WOE to avoid l32r literal issues (same as TinyGo 0.42 CPU0).
    rsr.ps a2
    movi a3, 1
    slli a3, a3, 18         // a3 = PS_WOE (0x40000)
    and  a3, a2, a3
    xor  a2, a2, a3         // clear WOE
    wsr.ps a2
    rsync

    rsr.windowbase  a2
    ssl  a2
    movi a2, 1
    sll  a2, a2
    wsr.windowstart a2
    rsync

    l32r a1, .Lstack1_top_addr

    rsr.ps a2
    movi a3, 1
    slli a3, a3, 18         // PS_WOE
    or a2, a2, a3
    wsr.ps a2
    rsync

    movi a2, 1
    wsr.cpenable a2
    rsync

    l32r a8, .Lvector_table_addr_cpu1
    wsr.vecbase a8
    rsync

    rsr.ps a2
    movi a3, ~0x1F
    and a2, a2, a3
    movi a3, 0x20
    or a2, a2, a3
    wsr.ps a2
    rsync

    mov  a5, a1
    l32r a4, .Lrun_core1_entry_addr
    callx4 a4

1:  j 1b

