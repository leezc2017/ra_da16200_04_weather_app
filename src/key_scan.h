/**
 * @file  key_scan.h
 * @brief 按键三态扫描（短按 / 双击 / 长按）
 *
 * 移植自 GD32VW553 版 4.weather_app/src/key_scan.h，
 * 状态机逻辑（三态判定 + 双击等待 + 长按抢占）逐行保留，
 * 仅 GPIO 读取方式从 gd32vw55x_gpio.h 换成 R_IOPORT_PinRead。
 *
 * 事件用 FIFO 而非单标志（DESIGN §8.1 根因1 对策）：
 *   10ms tick 下标志位可能在两次 poll 之间被覆盖，
 *   FIFO 保证"连接失败阻塞期间长按/单击 10/10 次即时响应"的验收。
 */
#ifndef _KEY_SCAN_H_
#define _KEY_SCAN_H_

#include <stdint.h>
#include <stdbool.h>

/** 按键事件类型（不带键号；键号由 key_event_get_id() 单独给出） */
typedef enum {
    KEY_EVT_NONE = 0,    /**< 无事件 */
    KEY_EVT_SHORT,       /**< 短按 */
    KEY_EVT_DOUBLE,      /**< 双击 */
    KEY_EVT_LONG,        /**< 长按 */
} key_event_t;

/** 按键编号（映射见 DESIGN §6） */
typedef enum {
    KEY_ID_MECH1 = 0,    /**< 机械键1 P005: 短按=刷新 / 双击=翻页 / 长按=配网 */
    KEY_ID_MECH2 = 1,    /**< 机械键2 P006: 单击=翻页 */
    KEY_ID_TOUCH1 = 2,   /**< 触摸键1 TS11: 单击=刷新      （阶段3 接入） */
    KEY_ID_TOUCH2 = 3,   /**< 触摸键2 TS12: 单击=翻页/长按配网（阶段3 接入） */
    KEY_ID_MAX
} key_id_t;

/** 事件 FIFO 深度（≥4，满足 §8.1 验收的连按不丢） */
#define KEY_EVT_FIFO_DEPTH   8U

/**
 * @brief 初始化按键 GPIO 与状态机
 *
 * 必须在主循环开始前调用一次。引脚由 weather_cfg.h 的
 * WEATHER_KEY_PORT / WEATHER_KEY_PIN 决定。
 */
void key_init(void);

/**
 * @brief 轮询按键（每 tick 调用，内部按 KEY_POLL_MS 节流）
 * @param now_ms 单调递增毫秒计数
 */
void key_poll(uint32_t now_ms);

/**
 * @brief 取一个事件（消费型）
 * @return 事件类型；无事件返回 KEY_EVT_NONE
 */
key_event_t key_event_get(void);

/**
 * @brief 取一个事件并带出按键编号（推荐用这个做映射）
 *
 * 事件入队时会把 key_id_t 一起存下，所以 4 键共用一个 FIFO 也能区分来源。
 *
 * @param out_id 输出按键编号（KEY_ID_MECH1 / KEY_ID_MECH2 / ...）
 * @return 事件类型；无事件返回 KEY_EVT_NONE
 */
key_event_t key_event_get_id(key_id_t * out_id);

/**
 * @brief 查询 FIFO 中待处理事件数
 * @return 事件个数
 */
uint8_t key_event_pending(void);

/**
 * @brief 清空 FIFO（切换页面或状态迁移时调用，避免旧事件误触发）
 */
void key_event_flush(void);

#endif /* _KEY_SCAN_H_ */