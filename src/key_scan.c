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

/* ---- 内部状态 ----
 * ⚠ 每个按键一套独立状态（GD32 版只有 1 个键，本工程板上有 2 个机械键，
 *   映射见 DESIGN §6：机械键1 走三态，机械键2 单击翻页）。
 *   绝不能共用状态 —— 共用会导致两键同时按下/先后按下时互相抢占，
 *   出现"按机械键1 却触发翻页"这类串扰。 */
#define KEY_N  2U   /**< 机械键数量（触摸键阶段3 走 CTSU，不在此列） */

typedef struct {
    uint8_t   debounced;        /**< 消抖后电平: 1=按下 */
    uint8_t   last_raw;         /**< 上一次原始读值 */
    uint32_t  press_start_ms;
    uint8_t   long_reported;    /**< 本次按下是否已上报长按 */
    uint32_t  release_ms;       /**< 上次释放时刻 */
    uint8_t   double_pending;   /**< 正在等待可能的第二次点击 */
} key_ch_t;

static key_ch_t s_ch[KEY_N];
/* FIFO 里同时存"事件 + 键号"，这样 4 键共用一个队列也能区分来源 */
typedef struct {
    key_event_t evt;
    key_id_t    id;
} key_msg_t;

static key_msg_t  s_fifo[KEY_EVT_FIFO_DEPTH];
static uint8_t    s_fifo_head;
static uint8_t    s_fifo_cnt;
static uint32_t   s_last_poll_ms;

/**
 * @brief 事件入队（满则丢最旧，保最新）
 * @param e   事件
 * @param id  按键编号
 */
static void fifo_push (key_event_t e, key_id_t id)
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
    s_fifo[idx].evt = e;
    s_fifo[idx].id  = id;
}

/**
 * @brief 读某个按键的原始电平并归一化
 * @param id 按键编号
 * @return 1=按下, 0=松开
 *
 * ⚠ 前提：引脚必须在 FSP 里配成 GPIO 输入（IOPORT_CFG_PORT_DIRECTION_INPUT）。
 *   未配置时 R_BSP_PinRead 读到的是未定义电平，按键永远不触发
 *   （2026-10-04 实测：pincfg 里没有 P005/P006 时 4 键全部无反应）。
 *   现已在 configuration.xml + ra_gen/pin_data.c 补齐。
 */
static uint8_t key_raw_read (key_id_t id)
{
    uint8_t raw;
    uint8_t active_low;
    bsp_io_port_pin_t pin;

    if (KEY_ID_MECH1 == id)
    {
        pin         = WEATHER_KEY_PIN;
        active_low  = (uint8_t)WEATHER_KEY_ACTIVE_LOW;
    }
    else
    {
        pin         = WEATHER_KEY2_PIN;
        active_low  = (uint8_t)WEATHER_KEY2_ACTIVE_LOW;
    }

    /* R_BSP_PinRead 读 PFS 的 PIDR: 1=高电平, 0=低电平。
     * 引脚的输入模式/上拉由 FSP 配置(ra_gen/pin_data.c)决定, 这里只读。 */
    raw = (0U != R_BSP_PinRead(pin)) ? 1U : 0U;    /* 1=高 */

    /* 归一化到 "1=按下" */
    if (active_low)
    {
        raw = (uint8_t)!raw;
    }
    return raw;
}

void key_init (void)
{
    memset(s_ch, 0, sizeof(s_ch));
    s_last_poll_ms = 0U;
    s_fifo_head    = 0U;
    s_fifo_cnt     = 0U;
    memset(s_fifo, 0, sizeof(s_fifo));
    printf("[key] init 2 mech keys: P005(active_low=%d) P006(active_low=%d); "
           "touch TS11/TS12 in stage3\r\n",
           WEATHER_KEY_ACTIVE_LOW, WEATHER_KEY2_ACTIVE_LOW);
    printf("[key] level now: k1=%u k2=%u (idle should be 0)\r\n",
           (unsigned)key_raw_read(KEY_ID_MECH1),
           (unsigned)key_raw_read(KEY_ID_MECH2));
}

void key_poll (uint32_t now_ms)
{
    uint8_t i;

    if ((now_ms - s_last_poll_ms) < KEY_POLL_MS)
    {
        return;
    }
    s_last_poll_ms = now_ms;

    for (i = 0U; i < (uint8_t)KEY_N; i++)
    {
        key_ch_t  * c = &s_ch[i];
        key_id_t   id = (key_id_t)i;
        uint8_t    raw;

        /* 双击等待超时 -> 判定为单击 */
        if (c->double_pending && ((now_ms - c->release_ms) >= KEY_DOUBLE_MS))
        {
            c->double_pending = 0U;
            fifo_push(KEY_EVT_SHORT, id);
        }

        raw = key_raw_read(id);

        if (raw != c->last_raw)              /* 电平变化, 重新计时消抖 */
        {
            c->last_raw = raw;
            continue;
        }

        if (raw == c->debounced)             /* 电平未变 */
        {
            if (c->debounced && !c->long_reported &&
                ((now_ms - c->press_start_ms) >= KEY_LONG_MS))
            {
                c->long_reported  = 1U;
                c->double_pending = 0U;      /* 长按抢占, 取消双击等待 */
                fifo_push(KEY_EVT_LONG, id);
            }
            continue;
        }

        /* 消抖确认电平翻转 */
        c->debounced = raw;
        if (raw)                            /* 刚按下 */
        {
            c->press_start_ms = now_ms;
            c->long_reported  = 0U;
        }
        else                                 /* 刚释放 */
        {
            if (!c->long_reported && ((now_ms - c->press_start_ms) < KEY_LONG_MS))
            {
                if (c->double_pending)       /* 第二次点击 -> 双击 */
                {
                    c->double_pending = 0U;
                    fifo_push(KEY_EVT_DOUBLE, id);
                }
                else                        /* 第一次点击 -> 等第二次 */
                {
                    c->double_pending = 1U;
                    c->release_ms = now_ms;
                }
            }
        }
    }
}

key_event_t key_event_get_id (key_id_t * out_id)
{
    key_event_t e = KEY_EVT_NONE;

    if (s_fifo_cnt > 0U)
    {
        e = s_fifo[s_fifo_head].evt;
        if (NULL != out_id)
        {
            *out_id = s_fifo[s_fifo_head].id;
        }
        s_fifo_head = (uint8_t)((s_fifo_head + 1U) % KEY_EVT_FIFO_DEPTH);
        s_fifo_cnt--;
    }
    return e;
}

key_event_t key_event_get (void)
{
    return key_event_get_id(NULL);
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