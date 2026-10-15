//Map name popup hooks (BW popup)
//Bytes are: ldr r3,[pc,#0] ; bx r3 ; .word <new function>|1
//r3 is used (not r6) because these hooks are normal functions: r4-r7 must be preserved for the caller.

//ShowMapNamePopup(bool32 palIntoFadedBuffer)
.org 0x08098110
    .byte 0x00
    .byte 0x4B
    .byte 0x18
    .byte 0x47
    .word ShowMapNamePopup|1

//DismissMapNamePopup(void)
.org 0x080982EC
    .byte 0x00
    .byte 0x4B
    .byte 0x18
    .byte 0x47
    .word DismissMapNamePopup|1

//IsMapNamePopupTaskActive(void)
.org 0x08098320
    .byte 0x00
    .byte 0x4B
    .byte 0x18
    .byte 0x47
    .word IsMapNamePopupTaskActive|1
