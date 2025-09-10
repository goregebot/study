# SCP Firmware 修改指南

## 概述

當你需要在 SCP firmware 中新增或修改時鐘支援時，通常需要修改以下幾個部分：

1. **時鐘定義和配置**
2. **SCMI 協議配置**
3. **硬體抽象層 (HAL)**
4. **模組配置**
5. **建構系統配置**

## 📁 需要修改的檔案結構

```
product/your_platform/
├── include/
│   ├── your_platform_clock.h          # 時鐘定義
│   ├── your_platform_scmi.h           # SCMI 代理定義
│   └── your_platform_mmap.h           # 記憶體映射
├── scp_ramfw/
│   ├── config_clock.c                 # 時鐘模組配置
│   ├── config_scmi_clock.c            # SCMI 時鐘協議配置
│   ├── config_scmi.c                  # SCMI 服務配置
│   └── CMakeLists.txt                 # 建構配置
├── module/
│   └── your_platform_clock/           # 平台特定時鐘驅動
│       ├── src/
│       │   └── mod_your_platform_clock.c
│       ├── include/
│       │   └── mod_your_platform_clock.h
│       └── CMakeLists.txt
└── src/
    └── your_platform_core_clock.c     # 核心時鐘實作
```

## 🔧 修改步驟

### 1. 定義時鐘索引 (include/your_platform_clock.h)

```c
/*
 * Your Platform Clock Definitions
 */

#ifndef YOUR_PLATFORM_CLOCK_H
#define YOUR_PLATFORM_CLOCK_H

#include <fwk_macros.h>
#include <stdint.h>

/* 時鐘索引定義 */
enum your_platform_clock_idx {
    /* CPU 時鐘 */
    YOUR_PLATFORM_CLOCK_IDX_CPU0,
    YOUR_PLATFORM_CLOCK_IDX_CPU1,
    YOUR_PLATFORM_CLOCK_IDX_CPU2,
    YOUR_PLATFORM_CLOCK_IDX_CPU3,
    
    /* 系統時鐘 */
    YOUR_PLATFORM_CLOCK_IDX_SYS_CLK,
    YOUR_PLATFORM_CLOCK_IDX_AHB_CLK,
    YOUR_PLATFORM_CLOCK_IDX_APB_CLK,
    
    /* 周邊時鐘 */
    YOUR_PLATFORM_CLOCK_IDX_UART0,
    YOUR_PLATFORM_CLOCK_IDX_UART1,
    YOUR_PLATFORM_CLOCK_IDX_I2C0,
    YOUR_PLATFORM_CLOCK_IDX_I2C1,
    YOUR_PLATFORM_CLOCK_IDX_SPI0,
    YOUR_PLATFORM_CLOCK_IDX_SPI1,
    
    /* GPU 時鐘 */
    YOUR_PLATFORM_CLOCK_IDX_GPU_CORE,
    YOUR_PLATFORM_CLOCK_IDX_GPU_MEM,
    
    /* 顯示時鐘 */
    YOUR_PLATFORM_CLOCK_IDX_DISPLAY_PIXEL,
    YOUR_PLATFORM_CLOCK_IDX_DISPLAY_AXI,
    
    /* 總數 */
    YOUR_PLATFORM_CLOCK_IDX_COUNT
};

/* 時鐘頻率定義 */
#define YOUR_PLATFORM_CLOCK_RATE_CPU_MIN        (200UL * FWK_MHZ)
#define YOUR_PLATFORM_CLOCK_RATE_CPU_MAX        (2000UL * FWK_MHZ)
#define YOUR_PLATFORM_CLOCK_RATE_SYS_DEFAULT    (100UL * FWK_MHZ)
#define YOUR_PLATFORM_CLOCK_RATE_AHB_DEFAULT    (50UL * FWK_MHZ)
#define YOUR_PLATFORM_CLOCK_RATE_APB_DEFAULT    (25UL * FWK_MHZ)

/* PLL 配置結構 */
struct your_platform_pll_config {
    uint32_t ref_freq;      /* 參考頻率 */
    uint32_t multiplier;    /* 倍頻器 */
    uint32_t divider;       /* 分頻器 */
    uint32_t post_div;      /* 後分頻器 */
};

/* 時鐘配置結構 */
struct your_platform_clock_config {
    uint32_t base_address;              /* 時鐘控制器基址 */
    struct your_platform_pll_config pll_config;
    uint64_t min_rate;                  /* 最小頻率 */
    uint64_t max_rate;                  /* 最大頻率 */
    uint64_t step_size;                 /* 頻率步進 */
    bool supports_rate_change;          /* 是否支援動態調頻 */
};

#endif /* YOUR_PLATFORM_CLOCK_H */
```

### 2. 配置時鐘模組 (scp_ramfw/config_clock.c)

```c
/*
 * Clock Module Configuration
 */

#include "your_platform_clock.h"
#include "your_platform_mmap.h"

#include <mod_clock.h>
#include <mod_your_platform_clock.h>

#include <fwk_element.h>
#include <fwk_id.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>

/* 時鐘設備描述表 */
static struct fwk_element clock_dev_desc_table[] = {
    /* CPU 時鐘 */
    [YOUR_PLATFORM_CLOCK_IDX_CPU0] = {
        .name = "CPU0_CLK",
        .data = &((struct mod_clock_dev_config) {
            .driver_id = FWK_ID_ELEMENT_INIT(
                FWK_MODULE_IDX_YOUR_PLATFORM_CLOCK,
                YOUR_PLATFORM_CLOCK_IDX_CPU0),
            .api_id = FWK_ID_API_INIT(
                FWK_MODULE_IDX_YOUR_PLATFORM_CLOCK,
                MOD_YOUR_PLATFORM_CLOCK_API_IDX_DRIVER),
            .pd_source_id = FWK_ID_ELEMENT_INIT(
                FWK_MODULE_IDX_POWER_DOMAIN,
                YOUR_PLATFORM_PD_IDX_CPU0),
        }),
    },
    
    [YOUR_PLATFORM_CLOCK_IDX_CPU1] = {
        .name = "CPU1_CLK",
        .data = &((struct mod_clock_dev_config) {
            .driver_id = FWK_ID_ELEMENT_INIT(
                FWK_MODULE_IDX_YOUR_PLATFORM_CLOCK,
                YOUR_PLATFORM_CLOCK_IDX_CPU1),
            .api_id = FWK_ID_API_INIT(
                FWK_MODULE_IDX_YOUR_PLATFORM_CLOCK,
                MOD_YOUR_PLATFORM_CLOCK_API_IDX_DRIVER),
            .pd_source_id = FWK_ID_ELEMENT_INIT(
                FWK_MODULE_IDX_POWER_DOMAIN,
                YOUR_PLATFORM_PD_IDX_CPU1),
        }),
    },
    
    /* 系統時鐘 */
    [YOUR_PLATFORM_CLOCK_IDX_SYS_CLK] = {
        .name = "SYS_CLK",
        .data = &((struct mod_clock_dev_config) {
            .driver_id = FWK_ID_ELEMENT_INIT(
                FWK_MODULE_IDX_YOUR_PLATFORM_CLOCK,
                YOUR_PLATFORM_CLOCK_IDX_SYS_CLK),
            .api_id = FWK_ID_API_INIT(
                FWK_MODULE_IDX_YOUR_PLATFORM_CLOCK,
                MOD_YOUR_PLATFORM_CLOCK_API_IDX_DRIVER),
            .pd_source_id = FWK_ID_NONE_INIT,
        }),
    },
    
    /* GPU 時鐘 */
    [YOUR_PLATFORM_CLOCK_IDX_GPU_CORE] = {
        .name = "GPU_CORE_CLK",
        .data = &((struct mod_clock_dev_config) {
            .driver_id = FWK_ID_ELEMENT_INIT(
                FWK_MODULE_IDX_YOUR_PLATFORM_CLOCK,
                YOUR_PLATFORM_CLOCK_IDX_GPU_CORE),
            .api_id = FWK_ID_API_INIT(
                FWK_MODULE_IDX_YOUR_PLATFORM_CLOCK,
                MOD_YOUR_PLATFORM_CLOCK_API_IDX_DRIVER),
            .pd_source_id = FWK_ID_ELEMENT_INIT(
                FWK_MODULE_IDX_POWER_DOMAIN,
                YOUR_PLATFORM_PD_IDX_GPU),
        }),
    },
    
    /* 周邊時鐘 */
    [YOUR_PLATFORM_CLOCK_IDX_UART0] = {
        .name = "UART0_CLK",
        .data = &((struct mod_clock_dev_config) {
            .driver_id = FWK_ID_ELEMENT_INIT(
                FWK_MODULE_IDX_YOUR_PLATFORM_CLOCK,
                YOUR_PLATFORM_CLOCK_IDX_UART0),
            .api_id = FWK_ID_API_INIT(
                FWK_MODULE_IDX_YOUR_PLATFORM_CLOCK,
                MOD_YOUR_PLATFORM_CLOCK_API_IDX_DRIVER),
            .pd_source_id = FWK_ID_NONE_INIT,
        }),
    },
    
    /* 結束標記 */
    [YOUR_PLATFORM_CLOCK_IDX_COUNT] = { 0 },
};

/* 時鐘模組配置 */
struct fwk_module_config config_clock = {
    .data = &(struct mod_clock_config) {
        .pd_transition_notification_id = FWK_ID_NOTIFICATION_INIT(
            FWK_MODULE_IDX_POWER_DOMAIN,
            MOD_PD_NOTIFICATION_IDX_POWER_STATE_TRANSITION),
        .pd_pre_transition_notification_id = FWK_ID_NOTIFICATION_INIT(
            FWK_MODULE_IDX_POWER_DOMAIN,
            MOD_PD_NOTIFICATION_IDX_POWER_STATE_PRE_TRANSITION),
    },
    .elements = FWK_MODULE_STATIC_ELEMENTS_PTR(clock_dev_desc_table),
};
```

### 3. 配置 SCMI 時鐘協議 (scp_ramfw/config_scmi_clock.c)

```c
/*
 * SCMI Clock Protocol Configuration
 */

#include "your_platform_clock.h"
#include "your_platform_scmi.h"

#include <mod_scmi_clock.h>

#include <fwk_id.h>
#include <fwk_macros.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>

/* OSPM 代理的時鐘設備表 */
static const struct mod_scmi_clock_device agent_device_table_ospm[] = {
    /* CPU 時鐘 - 允許 Linux 控制 */
    {
        .element_id = FWK_ID_ELEMENT_INIT(
            FWK_MODULE_IDX_CLOCK, 
            YOUR_PLATFORM_CLOCK_IDX_CPU0),
        .starts_enabled = true,
    },
    {
        .element_id = FWK_ID_ELEMENT_INIT(
            FWK_MODULE_IDX_CLOCK, 
            YOUR_PLATFORM_CLOCK_IDX_CPU1),
        .starts_enabled = true,
    },
    {
        .element_id = FWK_ID_ELEMENT_INIT(
            FWK_MODULE_IDX_CLOCK, 
            YOUR_PLATFORM_CLOCK_IDX_CPU2),
        .starts_enabled = true,
    },
    {
        .element_id = FWK_ID_ELEMENT_INIT(
            FWK_MODULE_IDX_CLOCK, 
            YOUR_PLATFORM_CLOCK_IDX_CPU3),
        .starts_enabled = true,
    },
    
    /* GPU 時鐘 */
    {
        .element_id = FWK_ID_ELEMENT_INIT(
            FWK_MODULE_IDX_CLOCK, 
            YOUR_PLATFORM_CLOCK_IDX_GPU_CORE),
        .starts_enabled = true,
    },
    {
        .element_id = FWK_ID_ELEMENT_INIT(
            FWK_MODULE_IDX_CLOCK, 
            YOUR_PLATFORM_CLOCK_IDX_GPU_MEM),
        .starts_enabled = true,
    },
    
    /* 顯示時鐘 */
    {
        .element_id = FWK_ID_ELEMENT_INIT(
            FWK_MODULE_IDX_CLOCK, 
            YOUR_PLATFORM_CLOCK_IDX_DISPLAY_PIXEL),
        .starts_enabled = false,  /* 預設關閉 */
    },
    
    /* 周邊時鐘 - 通常不允許 Linux 直接控制 */
    /* UART, I2C, SPI 等由 SCP 內部管理 */
};

/* 受信任代理的時鐘設備表 (如果需要) */
static const struct mod_scmi_clock_device agent_device_table_trusted[] = {
    /* 系統關鍵時鐘 */
    {
        .element_id = FWK_ID_ELEMENT_INIT(
            FWK_MODULE_IDX_CLOCK, 
            YOUR_PLATFORM_CLOCK_IDX_SYS_CLK),
        .starts_enabled = true,
    },
};

/* SCMI 代理表 */
static const struct mod_scmi_clock_agent 
    agent_table[YOUR_PLATFORM_SCMI_AGENT_IDX_COUNT] = {
    
    /* PSCI 代理 - 無時鐘存取權限 */
    [YOUR_PLATFORM_SCMI_AGENT_IDX_PSCI] = { 
        .device_table = NULL,
        .device_count = 0,
    },
    
    /* OSPM 代理 - Linux kernel */
    [YOUR_PLATFORM_SCMI_AGENT_IDX_OSPM] = {
        .device_table = agent_device_table_ospm,
        .device_count = FWK_ARRAY_SIZE(agent_device_table_ospm),
    },
    
    /* 受信任代理 (可選) */
    [YOUR_PLATFORM_SCMI_AGENT_IDX_TRUSTED] = {
        .device_table = agent_device_table_trusted,
        .device_count = FWK_ARRAY_SIZE(agent_device_table_trusted),
    },
};

/* SCMI 時鐘模組配置 */
struct fwk_module_config config_scmi_clock = {
    .data = &((struct mod_scmi_clock_config) {
        .max_pending_transactions = 0,  /* 使用預設值 */
        .agent_table = agent_table,
        .agent_count = FWK_ARRAY_SIZE(agent_table),
    }),
};
```

### 4. 實作平台特定時鐘驅動 (module/your_platform_clock/src/mod_your_platform_clock.c)

```c
/*
 * Your Platform Clock Driver
 */

#include "your_platform_clock.h"
#include "your_platform_mmap.h"

#include <mod_clock.h>
#include <mod_your_platform_clock.h>

#include <fwk_assert.h>
#include <fwk_id.h>
#include <fwk_log.h>
#include <fwk_macros.h>
#include <fwk_mm.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>
#include <fwk_status.h>

#include <stddef.h>
#include <stdint.h>

/* 時鐘控制暫存器結構 */
struct clock_ctrl_reg {
    FWK_RW uint32_t PLL_CTRL;          /* PLL 控制暫存器 */
    FWK_RW uint32_t PLL_STATUS;        /* PLL 狀態暫存器 */
    FWK_RW uint32_t DIV_CTRL;          /* 分頻控制暫存器 */
    FWK_RW uint32_t GATE_CTRL;         /* 時鐘閘控制暫存器 */
};

/* PLL 控制暫存器位元定義 */
#define PLL_CTRL_ENABLE         (1U << 0)
#define PLL_CTRL_BYPASS         (1U << 1)
#define PLL_CTRL_MULTIPLIER_POS (8)
#define PLL_CTRL_MULTIPLIER_MSK (0xFFF << PLL_CTRL_MULTIPLIER_POS)
#define PLL_CTRL_DIVIDER_POS    (20)
#define PLL_CTRL_DIVIDER_MSK    (0x1FF << PLL_CTRL_DIVIDER_POS)

/* PLL 狀態暫存器位元定義 */
#define PLL_STATUS_LOCKED       (1U << 0)

/* 時鐘設備上下文 */
struct clock_dev_ctx {
    const struct your_platform_clock_config *config;
    struct clock_ctrl_reg *reg;
    uint64_t current_rate;
    bool enabled;
};

/* 模組上下文 */
struct your_platform_clock_ctx {
    struct clock_dev_ctx *dev_ctx_table;
    unsigned int dev_count;
};

static struct your_platform_clock_ctx module_ctx;

/*
 * 計算 PLL 參數
 */
static int calculate_pll_params(uint64_t target_rate, 
                               uint32_t ref_rate,
                               uint32_t *multiplier, 
                               uint32_t *divider)
{
    uint32_t m, d;
    uint64_t vco_freq;
    
    /* 簡單的 PLL 參數計算 */
    for (d = 1; d <= 511; d++) {
        for (m = 1; m <= 4095; m++) {
            vco_freq = (uint64_t)ref_rate * m / d;
            
            if (vco_freq == target_rate) {
                *multiplier = m;
                *divider = d;
                return FWK_SUCCESS;
            }
        }
    }
    
    return FWK_E_RANGE;
}

/*
 * 設定 PLL 頻率
 */
static int set_pll_rate(struct clock_dev_ctx *ctx, uint64_t rate)
{
    uint32_t multiplier, divider;
    uint32_t reg_val;
    int timeout = 1000;
    int status;
    
    /* 計算 PLL 參數 */
    status = calculate_pll_params(rate, 
                                 ctx->config->pll_config.ref_freq,
                                 &multiplier, 
                                 &divider);
    if (status != FWK_SUCCESS) {
        return status;
    }
    
    FWK_LOG_INFO("[Clock] Setting PLL rate to %llu Hz (M=%u, D=%u)", 
                 rate, multiplier, divider);
    
    /* 停用 PLL */
    ctx->reg->PLL_CTRL &= ~PLL_CTRL_ENABLE;
    
    /* 設定新的 PLL 參數 */
    reg_val = ctx->reg->PLL_CTRL;
    reg_val &= ~(PLL_CTRL_MULTIPLIER_MSK | PLL_CTRL_DIVIDER_MSK);
    reg_val |= (multiplier << PLL_CTRL_MULTIPLIER_POS) & PLL_CTRL_MULTIPLIER_MSK;
    reg_val |= (divider << PLL_CTRL_DIVIDER_POS) & PLL_CTRL_DIVIDER_MSK;
    ctx->reg->PLL_CTRL = reg_val;
    
    /* 啟用 PLL */
    ctx->reg->PLL_CTRL |= PLL_CTRL_ENABLE;
    
    /* 等待 PLL 鎖定 */
    while (!(ctx->reg->PLL_STATUS & PLL_STATUS_LOCKED) && timeout > 0) {
        timeout--;
        /* 簡單延遲 */
        for (volatile int i = 0; i < 1000; i++);
    }
    
    if (timeout == 0) {
        FWK_LOG_ERROR("[Clock] PLL lock timeout");
        return FWK_E_TIMEOUT;
    }
    
    ctx->current_rate = rate;
    
    FWK_LOG_INFO("[Clock] PLL locked at %llu Hz", rate);
    
    return FWK_SUCCESS;
}

/*
 * Clock Driver API 實作
 */

static int your_platform_clock_set_rate(fwk_id_t clock_id, 
                                       uint64_t rate, 
                                       enum mod_clock_round_mode round_mode)
{
    struct clock_dev_ctx *ctx;
    
    if (!fwk_id_is_type(clock_id, FWK_ID_TYPE_ELEMENT)) {
        return FWK_E_PARAM;
    }
    
    ctx = &module_ctx.dev_ctx_table[fwk_id_get_element_idx(clock_id)];
    
    /* 檢查頻率範圍 */
    if (rate < ctx->config->min_rate || rate > ctx->config->max_rate) {
        FWK_LOG_ERROR("[Clock] Rate %llu out of range [%llu, %llu]", 
                      rate, ctx->config->min_rate, ctx->config->max_rate);
        return FWK_E_RANGE;
    }
    
    /* 根據 round_mode 調整頻率 */
    switch (round_mode) {
    case MOD_CLOCK_ROUND_MODE_NEAREST:
        /* 找到最接近的支援頻率 */
        break;
    case MOD_CLOCK_ROUND_MODE_UP:
        /* 向上取整到支援頻率 */
        break;
    case MOD_CLOCK_ROUND_MODE_DOWN:
        /* 向下取整到支援頻率 */
        break;
    default:
        return FWK_E_PARAM;
    }
    
    return set_pll_rate(ctx, rate);
}

static int your_platform_clock_get_rate(fwk_id_t clock_id, uint64_t *rate)
{
    struct clock_dev_ctx *ctx;
    
    if (!fwk_id_is_type(clock_id, FWK_ID_TYPE_ELEMENT)) {
        return FWK_E_PARAM;
    }
    
    if (rate == NULL) {
        return FWK_E_PARAM;
    }
    
    ctx = &module_ctx.dev_ctx_table[fwk_id_get_element_idx(clock_id)];
    *rate = ctx->current_rate;
    
    return FWK_SUCCESS;
}

static int your_platform_clock_set_state(fwk_id_t clock_id, 
                                        enum mod_clock_state state)
{
    struct clock_dev_ctx *ctx;
    
    if (!fwk_id_is_type(clock_id, FWK_ID_TYPE_ELEMENT)) {
        return FWK_E_PARAM;
    }
    
    ctx = &module_ctx.dev_ctx_table[fwk_id_get_element_idx(clock_id)];
    
    switch (state) {
    case MOD_CLOCK_STATE_RUNNING:
        ctx->reg->GATE_CTRL |= (1U << fwk_id_get_element_idx(clock_id));
        ctx->enabled = true;
        FWK_LOG_DEBUG("[Clock] Clock %u enabled", fwk_id_get_element_idx(clock_id));
        break;
        
    case MOD_CLOCK_STATE_STOPPED:
        ctx->reg->GATE_CTRL &= ~(1U << fwk_id_get_element_idx(clock_id));
        ctx->enabled = false;
        FWK_LOG_DEBUG("[Clock] Clock %u disabled", fwk_id_get_element_idx(clock_id));
        break;
        
    default:
        return FWK_E_PARAM;
    }
    
    return FWK_SUCCESS;
}

static int your_platform_clock_get_state(fwk_id_t clock_id, 
                                        enum mod_clock_state *state)
{
    struct clock_dev_ctx *ctx;
    
    if (!fwk_id_is_type(clock_id, FWK_ID_TYPE_ELEMENT)) {
        return FWK_E_PARAM;
    }
    
    if (state == NULL) {
        return FWK_E_PARAM;
    }
    
    ctx = &module_ctx.dev_ctx_table[fwk_id_get_element_idx(clock_id)];
    *state = ctx->enabled ? MOD_CLOCK_STATE_RUNNING : MOD_CLOCK_STATE_STOPPED;
    
    return FWK_SUCCESS;
}

static int your_platform_clock_get_range(fwk_id_t clock_id, 
                                        struct mod_clock_range *range)
{
    struct clock_dev_ctx *ctx;
    
    if (!fwk_id_is_type(clock_id, FWK_ID_TYPE_ELEMENT)) {
        return FWK_E_PARAM;
    }
    
    if (range == NULL) {
        return FWK_E_PARAM;
    }
    
    ctx = &module_ctx.dev_ctx_table[fwk_id_get_element_idx(clock_id)];
    
    range->min = ctx->config->min_rate;
    range->max = ctx->config->max_rate;
    range->step = ctx->config->step_size;
    range->rate_type = MOD_CLOCK_RATE_TYPE_CONTINUOUS;
    
    return FWK_SUCCESS;
}

/* Clock Driver API 表 */
static const struct mod_clock_drv_api clock_driver_api = {
    .set_rate = your_platform_clock_set_rate,
    .get_rate = your_platform_clock_get_rate,
    .set_state = your_platform_clock_set_state,
    .get_state = your_platform_clock_get_state,
    .get_range = your_platform_clock_get_range,
};

/*
 * 模組框架介面
 */

static int your_platform_clock_init(fwk_id_t module_id, 
                                   unsigned int element_count,
                                   const void *data)
{
    if (element_count == 0) {
        return FWK_E_PARAM;
    }
    
    module_ctx.dev_count = element_count;
    module_ctx.dev_ctx_table = fwk_mm_calloc(element_count, 
                                            sizeof(struct clock_dev_ctx));
    
    return FWK_SUCCESS;
}

static int your_platform_clock_element_init(fwk_id_t element_id, 
                                           unsigned int unused,
                                           const void *data)
{
    struct clock_dev_ctx *ctx;
    const struct your_platform_clock_config *config = data;
    
    if (config == NULL) {
        return FWK_E_PARAM;
    }
    
    ctx = &module_ctx.dev_ctx_table[fwk_id_get_element_idx(element_id)];
    ctx->config = config;
    ctx->reg = (struct clock_ctrl_reg *)config->base_address;
    ctx->current_rate = 0;
    ctx->enabled = false;
    
    return FWK_SUCCESS;
}

static int your_platform_clock_process_bind_request(fwk_id_t source_id,
                                                   fwk_id_t target_id,
                                                   fwk_id_t api_id,
                                                   const void **api)
{
    *api = &clock_driver_api;
    return FWK_SUCCESS;
}

/* 模組描述符 */
const struct fwk_module module_your_platform_clock = {
    .name = "Your Platform Clock Driver",
    .type = FWK_MODULE_TYPE_DRIVER,
    .api_count = MOD_YOUR_PLATFORM_CLOCK_API_IDX_COUNT,
    .init = your_platform_clock_init,
    .element_init = your_platform_clock_element_init,
    .process_bind_request = your_platform_clock_process_bind_request,
};
```

### 5. 更新建構配置 (scp_ramfw/CMakeLists.txt)

```cmake
# 新增你的平台時鐘模組
target_sources(scp_ramfw PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../module/your_platform_clock/src/mod_your_platform_clock.c"
)

target_include_directories(scp_ramfw PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../module/your_platform_clock/include"
)

# 確保包含必要的模組
list(APPEND SCP_MODULES "clock")
list(APPEND SCP_MODULES "scmi")
list(APPEND SCP_MODULES "scmi-clock")
list(APPEND SCP_MODULES "your-platform-clock")
```

## 🔍 驗證和測試

### 1. 編譯驗證
```bash
cd SCP-firmware
make -f Makefile.cmake PRODUCT=your_platform MODE=debug
```

### 2. 功能測試
在 Linux 端測試：
```bash
# 檢查 SCMI 時鐘
ls /sys/class/clk/
cat /sys/kernel/debug/clk/clk_summary

# 測試時鐘設定
echo 1000000000 > /sys/class/clk/cpu0_clk/clk_rate
```

### 3. 除錯技巧
在 SCP firmware 中加入日誌：
```c
FWK_LOG_INFO("[Clock] Rate set request: Clock %u, Rate %llu Hz", 
             clock_id, rate);
```

## 📝 常見問題

### 1. 時鐘 ID 不匹配
確保 Linux DT 中的時鐘 ID 與 SCP firmware 中的索引一致。

### 2. 權限問題
檢查 SCMI 代理配置，確保 OSPM 代理有正確的時鐘存取權限。

### 3. PLL 鎖定失敗
檢查 PLL 參數計算和硬體暫存器配置。

### 4. 頻率範圍錯誤
確認時鐘配置中的 min_rate 和 max_rate 設定正確。

這個修改指南涵蓋了在 SCP firmware 中新增時鐘支援的所有必要步驟。根據你的具體硬體平台，可能需要調整暫存器定義和 PLL 計算邏輯。