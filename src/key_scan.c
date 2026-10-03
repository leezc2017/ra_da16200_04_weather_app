/**
 * @file  key_scan.c
 * @brief 按键三态扫描实现（消抖 + 单击/双击/长按 + 事件 FIFO）
 *
 * 移植自 GD32VW553 版 4.weather_app/src/key_scan.c。
 *
 * 与 GD32 版的差异（仅两处，均为平台适配）：
 *   1. GPIO 读法：gd32vw55x_gpio.h 的 gpio_input_bit_get() -> R_IOPORT_PinRead()
 *   2. 事件输出：单个 s_event 变量 -> 8 槽 FIFO
 *      （§8.1 根因1：脉冲标志承载状态迁移会被漏检）
 *
 * 三态判定逻辑逐行保留：长按在按住期间判定并抢占（取消双击等待），
 * 短按需等双击窗口超时后才上报，两者互斥。
 */
#include <stdio.h>      /* printf: 打印引脚配置, 便于确认按键接线极性 */
#include <string.h>
#include "hal_data.h"
#include "key_scan.h"
#include "weather_cfg.h"

/* ---- 内部状态 ---- */
static uint8_t   s_debounced;        /**< 消抖后电平: 1=按下 */
static uint8_t   s_last_raw;         /**< 上一次原始读值(用于电平变化重置消抖) */
static uint32_t  s_last_poll_ms;
static uint32_t  s_press_start_ms;
static uint8_t   s_long_reported;    /**< 本次按下是否已上报长按 */
static uint32_t  s_release_ms;       /**< 上次释放时刻 */
static uint8_t   s_double_pending;   /**< 正在等待可能的第二次点击 */

static key_event_t s_fifo[KEY_EVT_FIFO_DEPTH];
static uint8_t     s_fifo_head;
static uint8_t     s_fifo_cnt;

/**
 * @brief 事件入队（满则丢最旧，保最新）
 * @param e 事件
 */
static void fifo_push (key_event_t e)
{
    uint8_t idx;

    if (s_fifo_cnt < KEY_EVT_FIFO_DEPTH)
    {
        idx = (uint8_t)((s_fifo_head + s_fifo_cnt) % KEY_EVT_FIFO_DEPTH);
        s_fifo_cnt++;
    }
    else
    {
        idx = s_fifo_head;
        s_fifo_head = (uint8_t)((s_fifo_head + 1U) % KEY_EVT_FIFO_DEPTH);
    }
    s_fifo[idx] = e;
}

/**
 * @brief 读按键原始电平并归一化
 * @return 1=按下, 0=松开
 */
static uint8_t key_raw_read (void)
{
    uint8_t raw;

    /* R_BSP_PinRead 读 PFS 的 PIDR: 1=高电平, 0=低电平。
     * 引脚的输入模式/上拉由 FSP 配置(ra_gen/pin_data.c)决定, 这里只读。 */
    raw = (0U != R_BSP_PinRead(WEATHER_KEY_PIN)) ? 1U : 0U;    /* 1=高 */

    /* 归一化到 "1=按下" */
    if (WEATHER_KEY_ACTIVE_LOW)
    {
        raw = (uint8_t)!raw;
    }
    return raw;
}

void key_init (void)
{
    s_debounced      = 0U;
    s_last_raw       = 0U;
    s_double_pending = 0U;
    s_long_reported  = 0U;
    s_press_start_ms = 0U;
    s_release_ms     = 0U;
    s_last_poll_ms   = 0U;
    s_fifo_head      = 0U;
    s_fifo_cnt       = 0U;
    memset(s_fifo, 0, sizeof(s_fifo));
    printf("[key] init pin=%s active_low=%d\r\n",
           (WEATHER_KEY_ACTIVE_LOW ? "low" : "high"), WEATHER_KEY_ACTIVE_LOW);
}

void key_poll (uint32_t now_ms)
{
    uint8_t raw;

    if ((now_ms - s_last_poll_ms) < KEY_POLL_MS)
    {
        return;
    }
    s_last_poll_ms = now_ms;

    /* 双击等待超时 -> 判定为单击 */
    if (s_double_pending && ((now_ms - s_release_ms) >= KEY_DOUBLE_MS))
    {
        s_double_pending = 0U;
        fifo_push(KEY_EVT_SHORT);
    }

    raw = key_raw_read();

    if (raw != s_last_raw)                    /* 电平变化, 重新计时消抖 */
    {
        s_last_raw = raw;
        return;
    }

    if (raw == s_debounced)                   /* 电平未变 */
    {
        if (s_debounced && !s_long_reported &&
            ((now_ms - s_press_start_ms) >= KEY_LONG_MS))
        {
            s_long_reported  = 1U;
            s_double_pending = 0U;            /* 长按抢占, 取消双击等待 */
            fifo_push(KEY_EVT_LONG);
        }
        return;
    }

    /* 消抖确认电平翻转 */
    s_debounced = raw;
    if (raw)                                    /* 刚按下 */
    {
        s_press_start_ms = now_ms;
        s_long_reported  = 0U;
    }
    else                                       /* 刚释放 */
    {
        if (!s_long_reported && ((now_ms - s_press_start_ms) < KEY_LONG_MS))
        {
            if (s_double_pending)              /* 第二次点击 -> 双击 */
            {
                s_double_pending = 0U;
                fifo_push(KEY_EVT_DOUBLE);
            }
            else                               /* 第一次点击 -> 等第二次 */
            {
                s_double_pending = 1U;
                s_release_ms = now_ms;
            }
        }
    }
}

key_event_t key_event_get (void)
{
    key_event_t e = KEY_EVT_NONE;

    if (s_fifo_cnt > 0U)
    {
        e = s_fifo[s_fifo_head];
        s_fifo_head = (uint8_t)((s_fifo_head + 1U) % KEY_EVT_FIFO_DEPTH);
        s_fifo_cnt--;
    }
    return e;
}

uint8_t key_event_pending (void)
{
    return s_fifo_cnt;
}

void key_event_flush (void)
{
    s_fifo_head = 0U;
    s_fifo_cnt  = 0U;
}