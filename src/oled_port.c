/**
 * @file  oled_port.c
 * @brief OLED 硬件接口层: RA4M2 GPIO 软SPI回调 + 引脚初始化
 *
 * oled.c / gpio_spi.c / oledfont.h 从 GD32 参考工程 (4.weather_app) 原样移植
 * (回调注入架构, 零改动), 本文件是它们与 FSP r_ioport 之间唯一的适配层。
 */
#include <stdint.h>

#include "hal_data.h"

#include "bsp_hal/hal_delay.h"
#include "gpio_spi/gpio_spi.h"
#include "oled/oled.h"
#include "oled_port.h"


#define SET      (BSP_IO_LEVEL_HIGH)
#define RESET    (BSP_IO_LEVEL_LOW)

/* ============ 7-pin OLED board wiring (soft SPI) ============
 * MCU signals used here: SCLK=P001, MOSI=P000, RES=P003, DC=P007, CS=P014.
 * GND/VCC/BL are board power/backlight connections, not MCU GPIO signals.
 * configuration.xml/ra_gen pin_data.c configure all five as GPIO outputs.
 * oled_port_init() repeats the setting and establishes idle-high levels. */
#define OLED_CLK_PIN     BSP_IO_PORT_00_PIN_01          /* D0 / SCLK */
#define OLED_DAT_PIN     BSP_IO_PORT_00_PIN_00          /* D1 / MOSI  */
#define OLED_RES_PIN     BSP_IO_PORT_00_PIN_03          /* RES        */
#define OLED_DC_PIN      BSP_IO_PORT_00_PIN_07          /* DC         */
#define OLED_CS_PIN      BSP_IO_PORT_00_PIN_14          /* CS         */

#define OLED_SPI_HZ      100000          /* 软SPI时钟, 100kHz (hal_delay_us 整数us, 1MHz半周期0.5us无法表示) */

/* ---------------- GPIO 输出 ---------------- */
static int gpio_bit_write(uint32_t pin, bsp_io_level_t level)
{
    R_IOPORT_PinWrite(&g_ioport_ctrl, pin, level);
    return 0;
}

/* ---------------- 引脚回调 ---------------- */
static int cb_oled_dc(u8 v)   /* DC: 1=数据 0=命令 */
{
    gpio_bit_write(OLED_DC_PIN, v ? SET : RESET);
    return 0;
}

static int cb_oled_rst(u8 v)  /* 复位脚: 0=复位 1=释放 */
{
    gpio_bit_write(OLED_RES_PIN, v ? SET : RESET);
    return 0;
}

static int cb_spi_cs(u8 v)
{
    gpio_bit_write(OLED_CS_PIN, v ? SET : RESET);
    return 0;
}

static int cb_spi_clk(u8 v)
{
    gpio_bit_write(OLED_CLK_PIN, v ? SET : RESET);
    return 0;
}

static int cb_spi_dat(u8 v)
{
    gpio_bit_write(OLED_DAT_PIN, v ? SET : RESET);
    return 0;
}

static int oled_bus_write(void *data, int len)
{
    /* 软SPI靠忙等翻转 CLK, 期间若被中断打断只会拉长半周期, 不会改变 bit 顺序。
     * 仍按字节关/开全局中断(PRIMASK), 是为了保证一个字节的 8 个 bit 连续发完,
     * 不让耗时中断把单字节传输拉长; 字节之间恢复中断, 不影响整体吞吐。
     * (移植前的工程跑在 RTOS 下, 加锁是为了防高优先级任务抢占, 本工程为裸机。) */
    int i;
#ifdef CFG_RTOS
    sys_enter_critical();
    for (i = 0; i < len; i++) {
        spi_send_byte(((const uint8_t *)data)[i]);
    }
    sys_exit_critical();
#else
    __disable_irq();
    for (i = 0; i < len; i++) {
        spi_send_byte(((const uint8_t *)data)[i]);
    }
    __enable_irq();
#endif
    return len;
}

/* ---------------- 初始化 ---------------- */
/* Set GPIO output direction and idle-high level at runtime. Keep the same pins
 * configured as GPIO outputs in RASC so their power-on state is deterministic. */
static void oled_pin_out(uint32_t pin)
{
    R_IOPORT_PinCfg(&g_ioport_ctrl, pin,
                    (uint32_t) IOPORT_CFG_PORT_DIRECTION_OUTPUT |
                    (uint32_t) IOPORT_CFG_PORT_OUTPUT_HIGH);
}

void oled_port_init(void)
{
    oled_pin_out(OLED_CS_PIN);
    oled_pin_out(OLED_DC_PIN);
    oled_pin_out(OLED_RES_PIN);
    oled_pin_out(OLED_DAT_PIN);
    oled_pin_out(OLED_CLK_PIN);

    /* 复位脚先拉高 (释放复位) */
    gpio_bit_write(OLED_RES_PIN, SET);

    spi_init_str_t spi;
    spi.spi_set_cs   = cb_spi_cs;
    spi.spi_set_clk  = cb_spi_clk;
    spi.spi_set_sda  = cb_spi_dat;
    spi.mode         = SPI_MODE0;
    spi.clock_freq   = OLED_SPI_HZ;
    spi.bits_per_word = 8;
    spi.clock_delay_us = 0;
    gpio_spi_init(&spi);

    spi_func_list f;
    f.spi_set_mode = cb_oled_dc;
    f.spi_set_rst  = cb_oled_rst;
    f.spi_b_write  = oled_bus_write;
    OLED_Init(&f);

    OLED_ColorTurn(0);   // 0=正常显示, 1=反色
    OLED_DisplayTurn(0); // 0=正常, 1=屏幕翻转
}
