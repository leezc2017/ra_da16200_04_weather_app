#ifndef __OLED_H
#define __OLED_H

#include <stdint.h>

#ifndef u8
#define u8  uint8_t
#endif
#ifndef u16
#define u16 uint16_t
#endif
#ifndef u32
#define u32 uint32_t
#endif

#define OLED_CMD  0	//写命令
#define OLED_DATA 1	//写数据

/*func def */
typedef int (*gpio_set_mode)(u8);
typedef int (*gpio_set_reset)(u8);
typedef int (*gpio_spi_write)(void *data, int len);

typedef struct _spi_func
{
    gpio_set_mode spi_set_mode;
    gpio_set_reset spi_set_rst;
    gpio_spi_write spi_b_write;
}spi_func_list;

//OLED操作函数
void delay_ms(unsigned int ms);
void OLED_ColorTurn(u8 i);
void OLED_DisplayTurn(u8 i);
void OLED_WR_Byte(u8 dat,u8 cmd);
void OLED_Set_Pos(u8 x, u8 y);
void OLED_Display_On(void);
void OLED_Display_Off(void);
void OLED_Clear(void);
void OLED_ShowChar(u8 x,u8 y,u8 chr,u8 sizey);
u32 oled_pow(u8 m,u8 n);
void OLED_ShowNum(u8 x,u8 y,u32 num,u8 len,u8 sizey);
void OLED_ShowString(u8 x,u8 y,u8 *chr,u8 sizey);
void OLED_ShowChinese(u8 x,u8 y,u8 no,u8 sizey);
void OLED_DrawBMP(u8 x,u8 y,u8 sizex, u8 sizey,u8 BMP[]);
void OLED_Init(spi_func_list *init_func);

#endif


