# The fonts of std.gui (D440): Inter, regular and semibold, from third_party/inter,
# in the archive of std.gui and so in the executables with windows, which have
# nothing to look for on the system (D433). The build gives that folder with -I.

#ifdef _WIN32
    .section .rdata, "dr"
#else
    .section .rodata
#endif

    .globl cromo_gui_inter_regular
    .p2align 4
cromo_gui_inter_regular:
    .incbin "Inter-Regular.ttf"

    .globl cromo_gui_inter_semibold
    .p2align 4
cromo_gui_inter_semibold:
    .incbin "Inter-SemiBold.ttf"

#ifndef _WIN32
    .section .note.GNU-stack, "", @progbits
#endif
