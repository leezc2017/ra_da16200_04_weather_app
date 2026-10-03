#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>


#include "gpio_spi/gpio_spi.h"
#include "bsp_hal/hal_delay.h"


spi_init_str_t spi_def_str={0};


// 初始化SPI设备
void gpio_spi_init(spi_init_str_t *spi_init_data) {
    // 保存配置, 并把 CS/CLK/DATA 预置到空闲电平
    memcpy(&spi_def_str, spi_init_data, sizeof(spi_def_str));
    spi_def_str.spi_set_clk(1);
    spi_def_str.spi_set_sda(1);
    spi_def_str.spi_set_cs(1);

    // 计算时钟延迟（半周期）
    spi_def_str.clock_delay_us = (1000000UL / spi_def_str.clock_freq) / 2;

    // 空闲电平: CLK 拉高
    spi_def_str.spi_set_clk(1);

    return;
}

// 发送单个比特
void spi_send_bit(u8 bit) {
    // 设置数据线
    spi_def_str.spi_set_sda(bit);

    switch (spi_def_str.mode) {
        case SPI_MODE0:  // CPOL=0, CPHA=0
            // 数据在第一个时钟边沿（上升沿）采样
            hal_delay_us(spi_def_str.clock_delay_us);
            spi_def_str.spi_set_clk(1);  // 上升沿
            hal_delay_us(spi_def_str.clock_delay_us);
            spi_def_str.spi_set_clk(0);  // 下降沿
            break;

        case SPI_MODE1:  // CPOL=0, CPHA=1
            // 数据在第二个时钟边沿（下降沿）采样
            spi_def_str.spi_set_clk(1);  // 上升沿
            hal_delay_us(spi_def_str.clock_delay_us);
            spi_def_str.spi_set_clk(0);  // 下降沿
            hal_delay_us(spi_def_str.clock_delay_us);
            break;

        case SPI_MODE2:  // CPOL=1, CPHA=0
            // 数据在第一个时钟边沿（下降沿）采样
            hal_delay_us(spi_def_str.clock_delay_us);
            spi_def_str.spi_set_clk(0);  // 下降沿
            hal_delay_us(spi_def_str.clock_delay_us);
            spi_def_str.spi_set_clk(1);  // 上升沿
            break;

        case SPI_MODE3:  // CPOL=1, CPHA=1
            // 数据在第二个时钟边沿（上升沿）采样
            spi_def_str.spi_set_clk(0);  // 下降沿
            hal_delay_us(spi_def_str.clock_delay_us);
            spi_def_str.spi_set_clk(1);  // 上升沿
            hal_delay_us(spi_def_str.clock_delay_us);
            break;
    }
}

// 发送一个字节（MSB优先）
void spi_send_byte(u8 data) {
    // 设置片选为低电平（如果使用CS）
    spi_def_str.spi_set_cs(0);
    hal_delay_us(spi_def_str.clock_delay_us * 2);

    // 发送数据位（MSB first）
    for (int i = spi_def_str.bits_per_word - 1; i >= 0; i--) {
        u8 bit = (data >> i) & 1;
        spi_send_bit(bit);
    }

    // 设置片选为高电平（如果使用CS）
    hal_delay_us(spi_def_str.clock_delay_us * 2);
    spi_def_str.spi_set_cs(1);
}

// 发送数据块
void spi_send_data(const uint8_t *data, int len) {
    for (int i = 0; i < len; i++) {
        spi_send_byte(data[i]);
    }
}
