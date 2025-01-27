#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/spi.h"

int main() {
    spi_inst_t *spi = spi0;
    
    stdio_init_all();
    printf("Hello, world!\n");
    return 0;
}