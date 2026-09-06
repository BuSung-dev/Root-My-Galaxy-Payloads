.section .rodata
.balign 16

.global embedded_exp32_start
.type embedded_exp32_start, %object
embedded_exp32_start:
.incbin "build/embed/cve_exp32_arm32"

.global embedded_exp32_end
.type embedded_exp32_end, %object
embedded_exp32_end:
.byte 0
