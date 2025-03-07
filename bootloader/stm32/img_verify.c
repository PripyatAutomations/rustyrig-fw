/*
 * do some verification that the uploaded image is indeed valid, before calling it
 */
static void main_exe_app(void) {
    uint32_t sp_adr; // app's stack pointer value
    uint32_t pc_adr; // app's reset address value
 
    // Does vector table at start of app have valid stack pointer value?
    sp_adr = *(uint32_t *)MAIN_APP_ADR_START;
    if( (sp_adr < MAIN_SRAM_ADR_START) || (sp_adr > MAIN_SRAM_ADR_END) )
    {
        // No. Return to bootloader
        return;
    }
    // Does vector table at start of app have valid reset address value?
    pc_adr = *(uint32_t *)(MAIN_APP_ADR_START + 4);
    if( (pc_adr < MAIN_APP_ADR_START) || (pc_adr >= MAIN_APP_ADR_END) )
    {
        // No. Return to bootloader
        return;
    }
    // Reset SRAM command so that bootloader will be executed again after reset
    main_magic = 0;
    // Rebase the stack pointer
    __set_MSP(sp_adr);
    // Rebase vector table to start of app (even though app may do it too)
    SCB->VTOR = MAIN_APP_ADR_START;
    // Jump to application reset address
    __asm__ __volatile__("bx %0\n\t"::"r"(pc_adr));
}