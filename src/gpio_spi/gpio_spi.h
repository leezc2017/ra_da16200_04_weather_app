#ifndef __GPIO_SPI__
#define __GPIO_SPI__
#include <stdint.h>

#ifndef u8
#define u8 uint8_t
#endif

// SPI模式枚举
typedef enum {
    SPI_MODE0 = 0,  // CPOL=0, CPHA=0
    SPI_MODE1,      // CPOL=0, CPHA=1
    SPI_MODE2,      // CPOL=1, CPHA=0
    SPI_MODE3       // CPOL=1, CPHA=1
} spi_mode_t;

/*func def */
typedef int (*gpio_set_cs)(u8);
typedef int (*gpio_set_clk)(u8);
typedef int (*gpio_spi_sda)(u8);

typedef struct _spi_init_str
{
    gpio_set_cs spi_set_cs;
    gpio_set_clk spi_set_clk;
    gpio_spi_sda spi_set_sda;
    int mode;
    uint32_t clock_freq;
    int bits_per_word;
    uint32_t clock_delay_us;
}spi_init_str_t;


void gpio_spi_init(spi_init_str_t *spi_init_data);
void spi_send_bit(u8 bit);
void spi_send_byte(u8 data);
void spi_send_data(const uint8_t *data, int len);

#endif