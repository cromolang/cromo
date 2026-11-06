# The fonts of std.gui (D440): Inter, regular and semibold, from third_party/inter,
# JetBrains Mono, regular and semibold (D492), from third_party/jetbrains-mono,
# and the icons of Lucide (D448), from third_party/lucide, in the archive of
# std.gui and so in the executables with windows, which have nothing to look for
# on the system (D433). The build gives those folders with -I.

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

    .globl cromo_gui_mono_regular
    .p2align 4
cromo_gui_mono_regular:
    .incbin "JetBrainsMonoNL-Regular.ttf"

    .globl cromo_gui_mono_semibold
    .p2align 4
cromo_gui_mono_semibold:
    .incbin "JetBrainsMonoNL-SemiBold.ttf"

    .globl cromo_gui_lucide
    .p2align 4
cromo_gui_lucide:
    .incbin "lucide-icons.ttf"

#ifndef _WIN32
    .section .note.GNU-stack, "", @progbits
#endif
