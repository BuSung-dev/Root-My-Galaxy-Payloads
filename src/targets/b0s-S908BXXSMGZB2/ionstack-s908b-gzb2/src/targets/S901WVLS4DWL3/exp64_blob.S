.section .rodata
.balign 16

.global embedded_exp64_start
.type embedded_exp64_start, %object
embedded_exp64_start:
.incbin "build/embed/cve_exp64_arm64"

.global embedded_exp64_end
.type embedded_exp64_end, %object
embedded_exp64_end:
.byte 0
