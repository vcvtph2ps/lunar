.text

.option push
.option arch, +d

.globl arch_fpu_save
.balign 16
.type arch_fpu_save, @function

# @brief Saves the current hart's floating point state into an area
# @param area (a0) area to save into
arch_fpu_save:
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fsd f\n, \n*8(a0)
    .endr
    frcsr t0
    sd t0, 256(a0)
    ret

.size arch_fpu_save, . - arch_fpu_save

.globl arch_fpu_load
.balign 16
.type arch_fpu_load, @function

# @brief Loads a hart's floating point state from an area
# @param area (a0) area to load from
arch_fpu_load:
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fld f\n, \n*8(a0)
    .endr
    ld t0, 256(a0)
    fscsr t0
    ret

.size arch_fpu_load, . - arch_fpu_load
.option pop
