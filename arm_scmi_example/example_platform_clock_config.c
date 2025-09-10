/*
 * Example Platform Clock Configuration
 * 
 * 這個範例展示如何為一個新的平台配置時鐘支援
 * 假設平台名稱為 "myplatform"
 */

#include "myplatform_clock.h"
#include "myplatform_mmap.h"
#include "myplatform_scmi.h"

#include <mod_clock.h>
#include <mod_scmi_clock.h>
#include <mod_myplatform_clock.h>

#include <fwk_element.h>
#include <fwk_id.h>
#include <fwk_macros.h>
#include <fwk_module.h>
#include <fwk_module_idx.h>

/*
 * 平台時鐘配置資料
 */

/* CPU 時鐘配置 */
static const struct myplatform_clock_config cpu_clock_config_table[] = {
    [MYPLATFORM_CLOCK_IDX_CPU0] = {
        .base_address = MYPLATFORM_CPU0_PLL_BASE,
        .pll_config = {
            .ref_freq = 24000000,    /* 24MHz 參考時鐘 */
            .multiplier = 50,        /* 預設倍頻 */
            .divider = 1,            /* 預設分頻 */
            .post_div = 1,           /* 後分頻 */
        },
        .min_rate = 200UL * FWK_MHZ,     /* 最小 200MHz */
        .max_rate = 2000UL * FWK_MHZ,    /* 最大 2GHz */
        .step_size = 25UL * FWK_MHZ,     /* 25MHz 步進 */
        .supports_rate_change = true,
    },
    
    [MYPLATFORM_CLOCK_IDX_CPU1] = {
        .base_address = MYPLATFORM_CPU1_PLL_BASE,
        .pll_config = {
            .ref_freq = 24000000,
            .multiplier = 50,
            .divider = 1,
            .post_div = 1,
        },
        .min_rate = 200UL * FWK_MHZ,
        .max_rate = 2000UL * FWK_MHZ,
        .step_size = 25UL * FWK_MHZ,
        .supports_rate_change = true,
    },
    
    [MYPLATFORM_CLOCK_IDX_CPU2] = {
        .base_address = MYPLATFORM_CPU2_PLL_BASE,
        .pll_config = {
            .ref_freq = 24000000,
            .multiplier = 50,
            .divider = 1,
            .post_div = 1,
        },
        .min_rate = 200UL * FWK_MHZ,
        .max_rate = 2000UL * FWK_MHZ,
        .step_size = 25UL * FWK_MHZ,
        .supports_rate_change = true,
    },
    
    [MYPLATFORM_CLOCK_IDX_CPU3] = {
        .base_address = MYPLATFORM_CPU3_PLL_BASE,
        .pll_config = {
            .ref_freq = 24000000,
            .multiplier = 50,
            .divider = 1,
            .post_div = 1,
        },
        .min_rate = 200UL * FWK_MHZ,
        .max_rate = 2000UL * FWK_MHZ,
        .step_size = 25UL * FWK_MHZ,
        .supports_rate_change = true,
    },
};

/* GPU 時鐘配置 */
static const struct myplatform_clock_config gpu_clock_config = {
    .base_address = MYPLATFORM_GPU_PLL_BASE,
    .pll_config = {
        .ref_freq = 24000000,
        .multiplier = 40,        /* 預設 960MHz */
        .divider = 1,
        .post_div = 1,
    },
    .min_rate = 100UL * FWK_MHZ,     /* 最小 100MHz */
    .max_rate = 1200UL * FWK_MHZ,    /* 最大 1.2GHz */
    .step_size = 50UL * FWK_MHZ,     /* 50MHz 步進 */
    .supports_rate_change = true,
};

/* 系統時鐘配置 */
static const struct myplatform_clock_config sys_clock_config = {
    .base_address = MYPLATFORM_SYS_PLL_BASE,
    .pll_config = {
        .ref_freq = 24000000,
        .multiplier = 25,        /* 600MHz */
        .divider = 1,
        .post_div = 6,           /* 分頻到 100MHz */
    },
    .min_rate = 50UL * FWK_MHZ,      /* 最小 50MHz */
    .max_rate = 200UL * FWK_MHZ,     /* 最大 200MHz */
    .step_size = 25UL * FWK_MHZ,     /* 25MHz 步進 */
    .supports_rate_change = false,   /* 系統時鐘通常不動態調整 */
};

/* 周邊時鐘配置 */
static const struct myplatform_clock_config peripheral_clock_config = {
    .base_address = MYPLATFORM_PERIPHERAL_CLK_BASE,
    .pll_config = {
        .ref_freq = 24000000,
        .multiplier = 20,        /* 480MHz */
        .divider = 1,
        .post_div = 10,          /* 分頻到 48MHz */
    },
    .min_rate = 12UL * FWK_MHZ,      /* 最小 12MHz */
    .max_rate = 96UL * FWK_MHZ,      /* 最大 96MHz */
    .step_size = 12UL * FWK_MHZ,     /* 12MHz 步進 */
    .supports_rate_change = true,
};

/*
 * 時鐘設備描述表
 */
static struct fwk_element clock_dev_desc_table[] = {
    /* CPU 時鐘 */
    [MYPLATFORM_CLOCK_IDX_CPU0] = {
        .name = "CPU0_CLK",
        .data = &cpu_clock_config_table[MYPLATFORM_CLOCK_IDX_CPU0],
        .sub_element_count = 0,
    },
    
    [MYPLATFORM_CLOCK_IDX_CPU1] = {
        .name = "CPU1_CLK", 
        .data = &cpu_clock_config_table[MYPLATFORM_CLOCK_IDX_CPU1],
        .sub_element_count = 0,
    },
    
    [MYPLATFORM_CLOCK_IDX_CPU2] = {
        .name = "CPU2_CLK",
        .data = &cpu_clock_config_table[MYPLATFORM_CLOCK_IDX_CPU2],
        .sub_element_count = 0,
    },
    
    [MYPLATFORM_CLOCK_IDX_CPU3] = {
        .name = "CPU3_CLK",
        .data = &cpu_clock_config_table[MYPLATFORM_CLOCK_IDX_CPU3],
        .sub_element_count = 0,
    },
    
    /* GPU 時鐘 */
    [MYPLATFORM_CLOCK_IDX_GPU_CORE] = {
        .name = "GPU_CORE_CLK",
        .data = &gpu_clock_config,
        .sub_element_count = 0,
    },
    
    /* 系統時鐘 */
    [MYPLATFORM_CLOCK_IDX_SYS_CLK] = {
        .name = "SYS_CLK",
        .data = &sys_clock_config,
        .sub_element_count = 0,
    },
    
    [MYPLATFORM_CLOCK_IDX_AHB_CLK] = {
        .name = "AHB_CLK",
        .data = &sys_clock_config,  /* 共用系統時鐘配置 */
        .sub_element_count = 0,
    },
    
    [MYPLATFORM_CLOCK_IDX_APB_CLK] = {
        .name = "APB_CLK",
        .data = &sys_clock_config,
        .sub_element_count = 0,
    },
    
    /* 周邊時鐘 */
    [MYPLATFORM_CLOCK_IDX_UART0] = {
        .name = "UART0_CLK",
        .data = &peripheral_clock_config,
        .sub_element_count = 0,
    },
    
    [MYPLATFORM_CLOCK_IDX_UART1] = {
        .name = "UART1_CLK",
        .data = &peripheral_clock_config,
        .sub_element_count = 0,
    },
    
    [MYPLATFORM_CLOCK_IDX_I2C0] = {
        .name = "I2C0_CLK",
        .data = &peripheral_clock_config,
        .sub_element_count = 0,
    },
    
    [MYPLATFORM_CLOCK_IDX_I2C1] = {
        .name = "I2C1_CLK",
        .data = &peripheral_clock_config,
        .sub_element_count = 0,
    },
    
    [MYPLATFORM_CLOCK_IDX_SPI0] = {
        .name = "SPI0_CLK",
        .data = &peripheral_clock_config,
        .sub_element_count = 0,
    },
    
    [MYPLATFORM_CLOCK_IDX_SPI1] = {
        .name = "SPI1_CLK",
        .data = &peripheral_clock_config,
        .sub_element_count = 0,
    },
    
    /* 顯示時鐘 */
    [MYPLATFORM_CLOCK_IDX_DISPLAY_PIXEL] = {
        .name = "DISPLAY_PIXEL_CLK",
        .data = &((struct myplatform_clock_config) {
            .base_address = MYPLATFORM_DISPLAY_PLL_BASE,
            .pll_config = {
                .ref_freq = 24000000,
                .multiplier = 30,    /* 720MHz */
                .divider = 1,
                .post_div = 10,      /* 72MHz 像素時鐘 */
            },
            .min_rate = 25UL * FWK_MHZ,      /* 最小 25MHz */
            .max_rate = 200UL * FWK_MHZ,     /* 最大 200MHz */
            .step_size = 1UL * FWK_MHZ,      /* 1MHz 步進 */
            .supports_rate_change = true,
        }),
        .sub_element_count = 0,
    },
    
    [MYPLATFORM_CLOCK_IDX_DISPLAY_AXI] = {
        .name = "DISPLAY_AXI_CLK",
        .data = &sys_clock_config,  /* 使用系統時鐘 */
        .sub_element_count = 0,
    },
    
    /* 結束標記 */
    [MYPLATFORM_CLOCK_IDX_COUNT] = { 0 },
};

/*
 * Clock 模組配置
 */
static const struct fwk_element *clock_get_dev_desc_table(fwk_id_t module_id)
{
    return clock_dev_desc_table;
}

struct fwk_module_config config_clock = {
    .data = &(struct mod_clock_config) {
        .pd_transition_notification_id = FWK_ID_NOTIFICATION_INIT(
            FWK_MODULE_IDX_POWER_DOMAIN,
            MOD_PD_NOTIFICATION_IDX_POWER_STATE_TRANSITION),
        .pd_pre_transition_notification_id = FWK_ID_NOTIFICATION_INIT(
            FWK_MODULE_IDX_POWER_DOMAIN,
            MOD_PD_NOTIFICATION_IDX_POWER_STATE_PRE_TRANSITION),
    },
    .elements = FWK_MODULE_DYNAMIC_ELEMENTS(clock_get_dev_desc_table),
};

/*
 * SCMI Clock 協議配置
 */

/* OSPM 代理可存取的時鐘 */
static const struct mod_scmi_clock_device agent_device_table_ospm[] = {
    /* CPU 時鐘 - 允許 Linux CPUFreq 控制 */
    {
        .element_id = FWK_ID_ELEMENT_INIT(
            FWK_MODULE_IDX_CLOCK, 
            MYPLATFORM_CLOCK_IDX_CPU0),
        .starts_enabled = true,
    },
    {
        .element_id = FWK_ID_ELEMENT_INIT(
            FWK_MODULE_IDX_CLOCK, 
            MYPLATFORM_CLOCK_IDX_CPU1),
        .starts_enabled = true,
    },
    {
        .element_id = FWK_ID_ELEMENT_INIT(
            FWK_MODULE_IDX_CLOCK, 
            MYPLATFORM_CLOCK_IDX_CPU2),
        .starts_enabled = true,
    },
    {
        .element_id = FWK_ID_ELEMENT_INIT(
            FWK_MODULE_IDX_CLOCK, 
            MYPLATFORM_CLOCK_IDX_CPU3),
        .starts_enabled = true,
    },
    
    /* GPU 時鐘 - 允許 Linux GPU driver 控制 */
    {
        .element_id = FWK_ID_ELEMENT_INIT(
            FWK_MODULE_IDX_CLOCK, 
            MYPLATFORM_CLOCK_IDX_GPU_CORE),
        .starts_enabled = true,
    },
    
    /* 顯示時鐘 - 允許 Linux Display driver 控制 */
    {
        .element_id = FWK_ID_ELEMENT_INIT(
            FWK_MODULE_IDX_CLOCK, 
            MYPLATFORM_CLOCK_IDX_DISPLAY_PIXEL),
        .starts_enabled = false,  /* 預設關閉，由 driver 控制 */
    },
    
    /* 注意：系統時鐘和周邊時鐘通常不暴露給 Linux，
     * 由 SCP firmware 內部管理 */
};

/* 受信任代理可存取的時鐘 (可選) */
static const struct mod_scmi_clock_device agent_device_table_trusted[] = {
    /* 系統關鍵時鐘 */
    {
        .element_id = FWK_ID_ELEMENT_INIT(
            FWK_MODULE_IDX_CLOCK, 
            MYPLATFORM_CLOCK_IDX_SYS_CLK),
        .starts_enabled = true,
    },
    {
        .element_id = FWK_ID_ELEMENT_INIT(
            FWK_MODULE_IDX_CLOCK, 
            MYPLATFORM_CLOCK_IDX_AHB_CLK),
        .starts_enabled = true,
    },
    {
        .element_id = FWK_ID_ELEMENT_INIT(
            FWK_MODULE_IDX_CLOCK, 
            MYPLATFORM_CLOCK_IDX_APB_CLK),
        .starts_enabled = true,
    },
};

/* SCMI 代理表 */
static const struct mod_scmi_clock_agent 
    agent_table[MYPLATFORM_SCMI_AGENT_IDX_COUNT] = {
    
    /* PSCI 代理 - 無時鐘存取權限 */
    [MYPLATFORM_SCMI_AGENT_IDX_PSCI] = { 
        .device_table = NULL,
        .device_count = 0,
    },
    
    /* OSPM 代理 - Linux kernel */
    [MYPLATFORM_SCMI_AGENT_IDX_OSPM] = {
        .device_table = agent_device_table_ospm,
        .device_count = FWK_ARRAY_SIZE(agent_device_table_ospm),
    },
    
    /* 受信任代理 (可選) */
    [MYPLATFORM_SCMI_AGENT_IDX_TRUSTED] = {
        .device_table = agent_device_table_trusted,
        .device_count = FWK_ARRAY_SIZE(agent_device_table_trusted),
    },
};

/* SCMI Clock 模組配置 */
struct fwk_module_config config_scmi_clock = {
    .data = &((struct mod_scmi_clock_config) {
        .max_pending_transactions = 0,  /* 使用預設值 */
        .agent_table = agent_table,
        .agent_count = FWK_ARRAY_SIZE(agent_table),
    }),
};

/*
 * 平台特定時鐘驅動配置
 */

/* 平台時鐘驅動元素配置 */
static struct fwk_element myplatform_clock_element_table[] = {
    /* CPU 時鐘元素 */
    [MYPLATFORM_CLOCK_IDX_CPU0] = {
        .name = "CPU0 PLL",
        .data = &cpu_clock_config_table[MYPLATFORM_CLOCK_IDX_CPU0],
    },
    
    [MYPLATFORM_CLOCK_IDX_CPU1] = {
        .name = "CPU1 PLL",
        .data = &cpu_clock_config_table[MYPLATFORM_CLOCK_IDX_CPU1],
    },
    
    [MYPLATFORM_CLOCK_IDX_CPU2] = {
        .name = "CPU2 PLL",
        .data = &cpu_clock_config_table[MYPLATFORM_CLOCK_IDX_CPU2],
    },
    
    [MYPLATFORM_CLOCK_IDX_CPU3] = {
        .name = "CPU3 PLL",
        .data = &cpu_clock_config_table[MYPLATFORM_CLOCK_IDX_CPU3],
    },
    
    /* GPU 時鐘元素 */
    [MYPLATFORM_CLOCK_IDX_GPU_CORE] = {
        .name = "GPU Core PLL",
        .data = &gpu_clock_config,
    },
    
    /* 其他時鐘元素... */
    
    /* 結束標記 */
    [MYPLATFORM_CLOCK_IDX_COUNT] = { 0 },
};

static const struct fwk_element *myplatform_clock_get_element_table(fwk_id_t module_id)
{
    return myplatform_clock_element_table;
}

/* 平台時鐘驅動模組配置 */
struct fwk_module_config config_myplatform_clock = {
    .elements = FWK_MODULE_DYNAMIC_ELEMENTS(myplatform_clock_get_element_table),
};

/*
 * 使用說明：
 * 
 * 1. 將此檔案放在 product/myplatform/scp_ramfw/ 目錄下
 * 2. 在 CMakeLists.txt 中包含此配置
 * 3. 確保對應的標頭檔案定義了所有常數
 * 4. 實作平台特定的時鐘驅動模組
 * 
 * Linux 端對應的 Device Tree 配置：
 * 
 * scmi {
 *     compatible = "arm,scmi";
 *     // ... 其他配置 ...
 *     
 *     scmi_clk: protocol@14 {
 *         reg = <0x14>;
 *         #clock-cells = <1>;
 *     };
 * };
 * 
 * cpus {
 *     cpu0 {
 *         clocks = <&scmi_clk 0>;  // MYPLATFORM_CLOCK_IDX_CPU0
 *         clock-names = "cpu";
 *     };
 *     
 *     cpu1 {
 *         clocks = <&scmi_clk 1>;  // MYPLATFORM_CLOCK_IDX_CPU1
 *         clock-names = "cpu";
 *     };
 *     
 *     // ... 其他 CPU ...
 * };
 * 
 * gpu {
 *     clocks = <&scmi_clk 4>;      // MYPLATFORM_CLOCK_IDX_GPU_CORE
 *     clock-names = "core";
 * };
 * 
 * display {
 *     clocks = <&scmi_clk 9>;      // MYPLATFORM_CLOCK_IDX_DISPLAY_PIXEL
 *     clock-names = "pixel";
 * };
 */