# 完整 SCMI Clock 設定指南

## 概述

本指南提供從零開始設定 ARM SCMI Clock 通訊的完整步驟，包括：
- SCP Firmware 端配置
- Linux Kernel 端配置  
- Device Tree 設定
- 驅動程式整合
- 測試和驗證

## 🏗️ 系統架構

```
┌─────────────────────────────────────────────────────────────────┐
│                    Application Processor (Linux)                │
├─────────────────────────────────────────────────────────────────┤
│ User Space: cpufreq-set, devfreq tools                         │
│ Kernel:     CPUFreq → Clock Framework → SCMI Clock Driver      │
│ Transport:  Mailbox/SMC Driver                                 │
└─────────────────┬───────────────────────────────────────────────┘
                  │ SCMI Protocol Messages
                  │ (Mailbox/SMC/Shared Memory)
┌─────────────────▼───────────────────────────────────────────────┐
│                System Control Processor (SCP)                   │
├─────────────────────────────────────────────────────────────────┤
│ SCMI Framework: Message parsing and routing                     │
│ Clock Protocol: SCMI Clock command handlers                     │
│ Clock Module:   Hardware abstraction layer                     │
│ Hardware:       PLL/Clock controllers                          │
└─────────────────────────────────────────────────────────────────┘
```

## 📋 完整步驟清單

### Phase 1: SCP Firmware 配置
### Phase 2: Linux Kernel 配置
### Phase 3: Device Tree 設定
### Phase 4: 驅動程式整合
### Phase 5: 測試和驗證

---# Pha
se 1: SCP Firmware 配置

## 1.1 建立平台目錄結構

```bash
cd SCP-firmware/product/
mkdir -p myplatform/{include,scp_ramfw,module/myplatform_clock/{src,include},src}
```

## 1.2 定義時鐘索引 (include/myplatform_clock.h)

```c
#ifndef MYPLATFORM_CLOCK_H
#define MYPLATFORM_CLOCK_H

enum myplatform_clock_idx {
    MYPLATFORM_CLOCK_IDX_CPU0,      /* 0 - CPU0 時鐘 */
    MYPLATFORM_CLOCK_IDX_CPU1,      /* 1 - CPU1 時鐘 */
    MYPLATFORM_CLOCK_IDX_GPU,       /* 2 - GPU 時鐘 */
    MYPLATFORM_CLOCK_IDX_SYS,       /* 3 - 系統時鐘 */
    MYPLATFORM_CLOCK_IDX_COUNT
};

#define MYPLATFORM_CLOCK_RATE_CPU_MIN    (200UL * 1000000)  /* 200MHz */
#define MYPLATFORM_CLOCK_RATE_CPU_MAX    (2000UL * 1000000) /* 2GHz */
#define MYPLATFORM_CLOCK_RATE_GPU_MIN    (100UL * 1000000)  /* 100MHz */
#define MYPLATFORM_CLOCK_RATE_GPU_MAX    (1000UL * 1000000) /* 1GHz */

#endif
```

## 1.3 定義 SCMI 代理 (include/myplatform_scmi.h)

```c
#ifndef MYPLATFORM_SCMI_H
#define MYPLATFORM_SCMI_H

enum myplatform_scmi_agent_idx {
    MYPLATFORM_SCMI_AGENT_IDX_PSCI,     /* 0 - PSCI 代理 */
    MYPLATFORM_SCMI_AGENT_IDX_OSPM,     /* 1 - Linux 代理 */
    MYPLATFORM_SCMI_AGENT_IDX_COUNT
};

#endif
```## 1.4 
配置時鐘模組 (scp_ramfw/config_clock.c)

```c
#include "myplatform_clock.h"
#include <mod_clock.h>
#include <fwk_element.h>
#include <fwk_module.h>

static struct fwk_element clock_dev_desc_table[] = {
    [MYPLATFORM_CLOCK_IDX_CPU0] = {
        .name = "CPU0_CLK",
        .data = &((struct mod_clock_dev_config) {
            .driver_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 0),
            .api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 0),
        }),
    },
    [MYPLATFORM_CLOCK_IDX_CPU1] = {
        .name = "CPU1_CLK",
        .data = &((struct mod_clock_dev_config) {
            .driver_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 1),
            .api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 0),
        }),
    },
    [MYPLATFORM_CLOCK_IDX_GPU] = {
        .name = "GPU_CLK",
        .data = &((struct mod_clock_dev_config) {
            .driver_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 2),
            .api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 0),
        }),
    },
    [MYPLATFORM_CLOCK_IDX_COUNT] = { 0 },
};

struct fwk_module_config config_clock = {
    .elements = FWK_MODULE_STATIC_ELEMENTS_PTR(clock_dev_desc_table),
};
```

## 1.5 配置 SCMI 時鐘協議 (scp_ramfw/config_scmi_clock.c)

```c
#include "myplatform_clock.h"
#include "myplatform_scmi.h"
#include <mod_scmi_clock.h>

/* Linux 可存取的時鐘 */
static const struct mod_scmi_clock_device agent_device_table_ospm[] = {
    {
        .element_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_CLOCK, 
                                         MYPLATFORM_CLOCK_IDX_CPU0),
        .starts_enabled = true,
    },
    {
        .element_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_CLOCK, 
                                         MYPLATFORM_CLOCK_IDX_CPU1),
        .starts_enabled = true,
    },
    {
        .element_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_CLOCK, 
                                         MYPLATFORM_CLOCK_IDX_GPU),
        .starts_enabled = true,
    },
};

static const struct mod_scmi_clock_agent 
    agent_table[MYPLATFORM_SCMI_AGENT_IDX_COUNT] = {
    [MYPLATFORM_SCMI_AGENT_IDX_PSCI] = { 0 },
    [MYPLATFORM_SCMI_AGENT_IDX_OSPM] = {
        .device_table = agent_device_table_ospm,
        .device_count = FWK_ARRAY_SIZE(agent_device_table_ospm),
    },
};

struct fwk_module_config config_scmi_clock = {
    .data = &((struct mod_scmi_clock_config) {
        .agent_table = agent_table,
        .agent_count = FWK_ARRAY_SIZE(agent_table),
    }),
};
```## 1.
6 實作平台時鐘驅動 (module/myplatform_clock/src/mod_myplatform_clock.c)

```c
#include <mod_clock.h>
#include <fwk_module.h>
#include <fwk_log.h>

struct clock_dev_ctx {
    uint64_t current_rate;
    bool enabled;
};

static struct clock_dev_ctx dev_ctx_table[MYPLATFORM_CLOCK_IDX_COUNT];

static int myplatform_clock_set_rate(fwk_id_t clock_id, uint64_t rate, 
                                    enum mod_clock_round_mode round_mode)
{
    unsigned int idx = fwk_id_get_element_idx(clock_id);
    
    FWK_LOG_INFO("[Clock] Setting clock %u to %llu Hz", idx, rate);
    
    /* 這裡實作實際的硬體控制 */
    /* 例如：設定 PLL 暫存器 */
    
    dev_ctx_table[idx].current_rate = rate;
    return FWK_SUCCESS;
}

static int myplatform_clock_get_rate(fwk_id_t clock_id, uint64_t *rate)
{
    unsigned int idx = fwk_id_get_element_idx(clock_id);
    *rate = dev_ctx_table[idx].current_rate;
    return FWK_SUCCESS;
}

static int myplatform_clock_set_state(fwk_id_t clock_id, 
                                     enum mod_clock_state state)
{
    unsigned int idx = fwk_id_get_element_idx(clock_id);
    
    dev_ctx_table[idx].enabled = (state == MOD_CLOCK_STATE_RUNNING);
    
    FWK_LOG_INFO("[Clock] Clock %u %s", idx, 
                 dev_ctx_table[idx].enabled ? "enabled" : "disabled");
    
    return FWK_SUCCESS;
}

static const struct mod_clock_drv_api clock_driver_api = {
    .set_rate = myplatform_clock_set_rate,
    .get_rate = myplatform_clock_get_rate,
    .set_state = myplatform_clock_set_state,
};

static int myplatform_clock_init(fwk_id_t module_id, unsigned int element_count,
                                const void *data)
{
    /* 初始化預設頻率 */
    dev_ctx_table[MYPLATFORM_CLOCK_IDX_CPU0].current_rate = 1000000000; /* 1GHz */
    dev_ctx_table[MYPLATFORM_CLOCK_IDX_CPU1].current_rate = 1000000000; /* 1GHz */
    dev_ctx_table[MYPLATFORM_CLOCK_IDX_GPU].current_rate = 500000000;   /* 500MHz */
    
    return FWK_SUCCESS;
}

static int myplatform_clock_process_bind_request(fwk_id_t source_id,
                                               fwk_id_t target_id,
                                               fwk_id_t api_id,
                                               const void **api)
{
    *api = &clock_driver_api;
    return FWK_SUCCESS;
}

const struct fwk_module module_myplatform_clock = {
    .name = "MyPlatform Clock Driver",
    .type = FWK_MODULE_TYPE_DRIVER,
    .api_count = 1,
    .init = myplatform_clock_init,
    .process_bind_request = myplatform_clock_process_bind_request,
};
```##
 1.7 更新建構配置 (scp_ramfw/CMakeLists.txt)

```cmake
# 設定目標名稱
set(SCP_FIRMWARE_TARGET scp_ramfw)

# 新增模組
list(APPEND SCP_MODULES "clock")
list(APPEND SCP_MODULES "scmi")
list(APPEND SCP_MODULES "scmi-clock")
list(APPEND SCP_MODULES "myplatform-clock")

# 新增源檔案
target_sources(${SCP_FIRMWARE_TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/config_clock.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/config_scmi_clock.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../module/myplatform_clock/src/mod_myplatform_clock.c"
)

# 新增標頭檔案路徑
target_include_directories(${SCP_FIRMWARE_TARGET} PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/../include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../module/myplatform_clock/include"
)
```

## 1.8 編譯 SCP Firmware

```bash
cd SCP-firmware
make -f Makefile.cmake PRODUCT=myplatform MODE=debug
```

---

# Phase 2: Linux Kernel 配置

## 2.1 確認 SCMI 支援已啟用

檢查 Linux kernel 配置：

```bash
# 檢查 .config 檔案
grep -E "CONFIG_ARM_SCMI|CONFIG_COMMON_CLK_SCMI" .config

# 應該看到：
# CONFIG_ARM_SCMI_PROTOCOL=y
# CONFIG_ARM_SCMI_POWER_DOMAIN=y  
# CONFIG_COMMON_CLK_SCMI=y
# CONFIG_MAILBOX=y
```

如果沒有啟用，需要在 kernel 配置中啟用：

```bash
make menuconfig
# 導航到：
# Device Drivers → Firmware Drivers → ARM System Control and Management Interface Protocol
# Device Drivers → Common Clock Framework → Clock driver for ARM System Control and Management Interface
```## 2.
2 實作 SCMI Clock Consumer Driver

建立一個使用 SCMI clock 的驅動程式範例：

```c
/* drivers/cpufreq/myplatform-cpufreq.c */
#include <linux/clk.h>
#include <linux/cpufreq.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>

struct myplatform_cpufreq_data {
    struct clk *cpu_clk;
    struct device *cpu_dev;
};

static struct myplatform_cpufreq_data *cpufreq_data;

static int myplatform_cpufreq_set_target(struct cpufreq_policy *policy,
                                        unsigned int target_freq,
                                        unsigned int relation)
{
    struct clk *cpu_clk = cpufreq_data->cpu_clk;
    unsigned long freq_hz = target_freq * 1000;
    int ret;

    pr_debug("Setting CPU frequency to %u kHz\n", target_freq);

    /* 透過 SCMI 設定 CPU 時鐘頻率 */
    ret = clk_set_rate(cpu_clk, freq_hz);
    if (ret) {
        pr_err("Failed to set CPU frequency to %lu Hz: %d\n", freq_hz, ret);
        return ret;
    }

    pr_info("CPU frequency set to %lu Hz\n", clk_get_rate(cpu_clk));
    return 0;
}

static unsigned int myplatform_cpufreq_get(unsigned int cpu)
{
    return clk_get_rate(cpufreq_data->cpu_clk) / 1000;
}

static int myplatform_cpufreq_init(struct cpufreq_policy *policy)
{
    struct device_node *np;
    struct clk *cpu_clk;
    int ret;

    /* 取得 CPU device node */
    np = of_cpu_device_node_get(policy->cpu);
    if (!np) {
        pr_err("Failed to get CPU device node\n");
        return -ENODEV;
    }

    /* 取得 SCMI clock */
    cpu_clk = of_clk_get_by_name(np, "cpu");
    if (IS_ERR(cpu_clk)) {
        ret = PTR_ERR(cpu_clk);
        pr_err("Failed to get CPU clock: %d\n", ret);
        of_node_put(np);
        return ret;
    }

    cpufreq_data->cpu_clk = cpu_clk;
    
    /* 設定 CPUFreq 政策 */
    policy->clk = cpu_clk;
    policy->min = MYPLATFORM_CLOCK_RATE_CPU_MIN / 1000;  /* kHz */
    policy->max = MYPLATFORM_CLOCK_RATE_CPU_MAX / 1000;  /* kHz */
    policy->cur = clk_get_rate(cpu_clk) / 1000;          /* kHz */
    policy->cpuinfo.transition_latency = 300000;         /* 300us */

    of_node_put(np);
    return 0;
}

static struct cpufreq_driver myplatform_cpufreq_driver = {
    .name = "myplatform-cpufreq",
    .flags = CPUFREQ_STICKY | CPUFREQ_NEED_INITIAL_FREQ_CHECK,
    .init = myplatform_cpufreq_init,
    .target = myplatform_cpufreq_set_target,
    .get = myplatform_cpufreq_get,
    .attr = cpufreq_generic_attr,
};

static int myplatform_cpufreq_probe(struct platform_device *pdev)
{
    int ret;

    cpufreq_data = devm_kzalloc(&pdev->dev, sizeof(*cpufreq_data), GFP_KERNEL);
    if (!cpufreq_data)
        return -ENOMEM;

    cpufreq_data->cpu_dev = &pdev->dev;

    ret = cpufreq_register_driver(&myplatform_cpufreq_driver);
    if (ret) {
        dev_err(&pdev->dev, "Failed to register CPUFreq driver: %d\n", ret);
        return ret;
    }

    dev_info(&pdev->dev, "MyPlatform CPUFreq driver registered\n");
    return 0;
}

static int myplatform_cpufreq_remove(struct platform_device *pdev)
{
    cpufreq_unregister_driver(&myplatform_cpufreq_driver);
    if (cpufreq_data->cpu_clk)
        clk_put(cpufreq_data->cpu_clk);
    return 0;
}

static const struct of_device_id myplatform_cpufreq_match[] = {
    { .compatible = "myplatform,cpufreq" },
    {}
};

static struct platform_driver myplatform_cpufreq_platdrv = {
    .driver = {
        .name = "myplatform-cpufreq",
        .of_match_table = myplatform_cpufreq_match,
    },
    .probe = myplatform_cpufreq_probe,
    .remove = myplatform_cpufreq_remove,
};
module_platform_driver(myplatform_cpufreq_platdrv);

MODULE_DESCRIPTION("MyPlatform CPUFreq driver using SCMI");
MODULE_LICENSE("GPL v2");
```--
-

# Phase 3: Device Tree 設定

## 3.1 基本 SCMI 節點配置

```dts
/* arch/arm64/boot/dts/myplatform/myplatform.dts */

/ {
    /* 共享記憶體區域 */
    reserved-memory {
        #address-cells = <2>;
        #size-cells = <2>;
        ranges;

        scp_shmem: scp-shmem@4e000000 {
            compatible = "arm,scmi-shmem";
            reg = <0x0 0x4e000000 0x0 0x80>;
        };
    };

    /* Mailbox 控制器 */
    mailbox: mailbox@2e000000 {
        compatible = "arm,mhu-doorbell";
        reg = <0x0 0x2e000000 0x0 0x1000>;
        interrupts = <GIC_SPI 36 IRQ_TYPE_LEVEL_HIGH>;
        #mbox-cells = <2>;
    };

    /* SCMI 協議節點 */
    firmware {
        scmi {
            compatible = "arm,scmi";
            mboxes = <&mailbox 0 0>, <&mailbox 0 1>;
            mbox-names = "tx", "rx";
            shmem = <&scp_shmem>;
            #address-cells = <1>;
            #size-cells = <0>;

            /* SCMI Clock 協議 */
            scmi_clk: protocol@14 {
                reg = <0x14>;
                #clock-cells = <1>;
            };

            /* SCMI Performance 協議 */
            scmi_perf: protocol@13 {
                reg = <0x13>;
                #performance-domain-cells = <1>;
            };

            /* SCMI Power Domain 協議 */
            scmi_pd: protocol@11 {
                reg = <0x11>;
                #power-domain-cells = <1>;
            };
        };
    };
};
```

## 3.2 CPU 節點配置

```dts
/ {
    cpus {
        #address-cells = <1>;
        #size-cells = <0>;

        cpu0: cpu@0 {
            device_type = "cpu";
            compatible = "arm,cortex-a78";
            reg = <0x0>;
            enable-method = "scmi";
            
            /* 使用 SCMI Clock ID 0 (MYPLATFORM_CLOCK_IDX_CPU0) */
            clocks = <&scmi_clk 0>;
            clock-names = "cpu";
            
            /* CPUFreq OPP 表 */
            operating-points-v2 = <&cpu_opp_table>;
            
            /* 效能域 */
            performance-domains = <&scmi_perf 0>;
        };

        cpu1: cpu@1 {
            device_type = "cpu";
            compatible = "arm,cortex-a78";
            reg = <0x1>;
            enable-method = "scmi";
            
            /* 使用 SCMI Clock ID 1 (MYPLATFORM_CLOCK_IDX_CPU1) */
            clocks = <&scmi_clk 1>;
            clock-names = "cpu";
            
            operating-points-v2 = <&cpu_opp_table>;
            performance-domains = <&scmi_perf 1>;
        };
    };

    /* CPU OPP 表 */
    cpu_opp_table: opp-table-cpu {
        compatible = "operating-points-v2";
        opp-shared;

        opp-200000000 {
            opp-hz = /bits/ 64 <200000000>;    /* 200MHz */
            opp-microvolt = <800000>;          /* 0.8V */
        };

        opp-500000000 {
            opp-hz = /bits/ 64 <500000000>;    /* 500MHz */
            opp-microvolt = <850000>;          /* 0.85V */
        };

        opp-1000000000 {
            opp-hz = /bits/ 64 <1000000000>;   /* 1GHz */
            opp-microvolt = <900000>;          /* 0.9V */
        };

        opp-1500000000 {
            opp-hz = /bits/ 64 <1500000000>;   /* 1.5GHz */
            opp-microvolt = <950000>;          /* 0.95V */
        };

        opp-2000000000 {
            opp-hz = /bits/ 64 <2000000000>;   /* 2GHz */
            opp-microvolt = <1000000>;         /* 1.0V */
        };
    };
};
```##
 3.3 GPU 和其他周邊設備配置

```dts
/ {
    soc {
        /* GPU 節點 */
        gpu: gpu@2d000000 {
            compatible = "arm,mali-g78";
            reg = <0x0 0x2d000000 0x0 0x200000>;
            interrupts = <GIC_SPI 64 IRQ_TYPE_LEVEL_HIGH>,
                        <GIC_SPI 65 IRQ_TYPE_LEVEL_HIGH>,
                        <GIC_SPI 66 IRQ_TYPE_LEVEL_HIGH>;
            interrupt-names = "job", "mmu", "gpu";
            
            /* 使用 SCMI Clock ID 2 (MYPLATFORM_CLOCK_IDX_GPU) */
            clocks = <&scmi_clk 2>;
            clock-names = "core";
            
            /* GPU OPP 表 */
            operating-points-v2 = <&gpu_opp_table>;
        };

        /* Display 控制器 */
        display: display@2c000000 {
            compatible = "arm,hdlcd";
            reg = <0x0 0x2c000000 0x0 0x1000>;
            interrupts = <GIC_SPI 85 IRQ_TYPE_LEVEL_HIGH>;
            
            /* 可以使用系統時鐘或專用顯示時鐘 */
            clocks = <&scmi_clk 3>;
            clock-names = "pxlclk";
        };

        /* CPUFreq 平台設備 */
        cpufreq {
            compatible = "myplatform,cpufreq";
        };
    };

    /* GPU OPP 表 */
    gpu_opp_table: opp-table-gpu {
        compatible = "operating-points-v2";

        opp-100000000 {
            opp-hz = /bits/ 64 <100000000>;    /* 100MHz */
        };

        opp-200000000 {
            opp-hz = /bits/ 64 <200000000>;    /* 200MHz */
        };

        opp-400000000 {
            opp-hz = /bits/ 64 <400000000>;    /* 400MHz */
        };

        opp-600000000 {
            opp-hz = /bits/ 64 <600000000>;    /* 600MHz */
        };

        opp-800000000 {
            opp-hz = /bits/ 64 <800000000>;    /* 800MHz */
        };

        opp-1000000000 {
            opp-hz = /bits/ 64 <1000000000>;   /* 1GHz */
        };
    };
};
```

---

# Phase 4: 驅動程式整合

## 4.1 更新 Kernel 配置

在 `drivers/cpufreq/Kconfig` 中新增：

```kconfig
config ARM_MYPLATFORM_CPUFREQ
    tristate "MyPlatform CPUFreq driver"
    depends on ARM_SCMI_PROTOCOL && COMMON_CLK_SCMI
    select PM_OPP
    help
      This adds the CPUFreq driver for MyPlatform SoC using SCMI protocol.
```

在 `drivers/cpufreq/Makefile` 中新增：

```makefile
obj-$(CONFIG_ARM_MYPLATFORM_CPUFREQ) += myplatform-cpufreq.o
```

## 4.2 GPU Driver 整合範例

```c
/* drivers/gpu/drm/myplatform/myplatform_gpu.c */
#include <linux/clk.h>
#include <linux/pm_runtime.h>
#include <linux/devfreq.h>

struct myplatform_gpu {
    struct device *dev;
    struct clk *gpu_clk;
    struct devfreq *devfreq;
};

static int myplatform_gpu_set_freq(struct device *dev, unsigned long *freq,
                                  u32 flags)
{
    struct myplatform_gpu *gpu = dev_get_drvdata(dev);
    int ret;

    /* 透過 SCMI 設定 GPU 頻率 */
    ret = clk_set_rate(gpu->gpu_clk, *freq);
    if (ret) {
        dev_err(dev, "Failed to set GPU frequency to %lu Hz: %d\n", 
                *freq, ret);
        return ret;
    }

    *freq = clk_get_rate(gpu->gpu_clk);
    dev_dbg(dev, "GPU frequency set to %lu Hz\n", *freq);
    
    return 0;
}

static int myplatform_gpu_get_cur_freq(struct device *dev, unsigned long *freq)
{
    struct myplatform_gpu *gpu = dev_get_drvdata(dev);
    
    *freq = clk_get_rate(gpu->gpu_clk);
    return 0;
}

static struct devfreq_dev_profile myplatform_gpu_devfreq_profile = {
    .target = myplatform_gpu_set_freq,
    .get_cur_freq = myplatform_gpu_get_cur_freq,
    .polling_ms = 100,
};

static int myplatform_gpu_probe(struct platform_device *pdev)
{
    struct myplatform_gpu *gpu;
    int ret;

    gpu = devm_kzalloc(&pdev->dev, sizeof(*gpu), GFP_KERNEL);
    if (!gpu)
        return -ENOMEM;

    gpu->dev = &pdev->dev;

    /* 取得 SCMI GPU 時鐘 */
    gpu->gpu_clk = devm_clk_get(&pdev->dev, "core");
    if (IS_ERR(gpu->gpu_clk)) {
        ret = PTR_ERR(gpu->gpu_clk);
        dev_err(&pdev->dev, "Failed to get GPU clock: %d\n", ret);
        return ret;
    }

    /* 啟用時鐘 */
    ret = clk_prepare_enable(gpu->gpu_clk);
    if (ret) {
        dev_err(&pdev->dev, "Failed to enable GPU clock: %d\n", ret);
        return ret;
    }

    /* 註冊 devfreq */
    gpu->devfreq = devm_devfreq_add_device(&pdev->dev, 
                                          &myplatform_gpu_devfreq_profile,
                                          DEVFREQ_GOV_SIMPLE_ONDEMAND, 
                                          NULL);
    if (IS_ERR(gpu->devfreq)) {
        ret = PTR_ERR(gpu->devfreq);
        dev_err(&pdev->dev, "Failed to add devfreq device: %d\n", ret);
        clk_disable_unprepare(gpu->gpu_clk);
        return ret;
    }

    platform_set_drvdata(pdev, gpu);
    dev_info(&pdev->dev, "MyPlatform GPU driver probed successfully\n");
    
    return 0;
}
```--
-

# Phase 5: 測試和驗證

## 5.1 編譯和部署

### 編譯 SCP Firmware
```bash
cd SCP-firmware
make -f Makefile.cmake PRODUCT=myplatform MODE=debug
# 產生的檔案：build/myplatform/GNU/debug/firmware-scp_ramfw/bin/scp_ramfw.bin
```

### 編譯 Linux Kernel
```bash
cd linux
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- myplatform_defconfig
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- -j$(nproc)
# 產生的檔案：arch/arm64/boot/Image, arch/arm64/boot/dts/myplatform/myplatform.dtb
```

## 5.2 系統啟動驗證

### 檢查 SCMI 初始化
```bash
# 檢查 SCMI 設備
ls -la /sys/bus/scmi/devices/
# 應該看到：scmi_dev.0, scmi_dev.1 等

# 檢查 SCMI 協議
cat /sys/bus/scmi/devices/scmi_dev.0/modalias
# 應該看到：scmi:protocol-14 (Clock protocol)

# 檢查 dmesg 日誌
dmesg | grep -i scmi
# 應該看到 SCMI 初始化訊息
```

### 檢查時鐘註冊
```bash
# 檢查時鐘樹
cat /sys/kernel/debug/clk/clk_summary | grep scmi
# 應該看到 SCMI 時鐘

# 檢查特定時鐘
ls /sys/class/clk/ | grep scmi
```

## 5.3 功能測試

### CPU 頻率控制測試
```bash
# 檢查 CPUFreq 驅動
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_driver
# 應該顯示：myplatform-cpufreq

# 檢查可用頻率
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies
# 應該顯示：200000 500000 1000000 1500000 2000000

# 設定 CPU 頻率
echo 1500000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed

# 驗證頻率設定
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq
# 應該顯示：1500000

# 檢查實際硬體頻率 (透過 SCMI)
cat /sys/kernel/debug/clk/scmi_clk_0/clk_rate
```

### GPU 頻率控制測試
```bash
# 檢查 GPU devfreq
ls /sys/class/devfreq/
# 應該看到 GPU 設備

# 檢查 GPU 頻率
cat /sys/class/devfreq/*/cur_freq

# 設定 GPU governor
echo performance > /sys/class/devfreq/*/governor

# 檢查頻率變化
watch -n 1 cat /sys/class/devfreq/*/cur_freq
```

## 5.4 除錯技巧

### 啟用 SCMI 除錯
```bash
# 啟用動態除錯
echo 'file drivers/firmware/arm_scmi/* +p' > /sys/kernel/debug/dynamic_debug/control

# 啟用 SCMI trace
echo 1 > /sys/kernel/debug/tracing/events/scmi/enable
cat /sys/kernel/debug/tracing/trace_pipe
```

### SCP Firmware 除錯
在 SCP firmware 中加入更多日誌：
```c
FWK_LOG_INFO("[SCMI Clock] Rate set: Clock %u, Rate %llu Hz", clock_id, rate);
FWK_LOG_INFO("[SCMI Clock] Current rates: CPU0=%llu, CPU1=%llu, GPU=%llu", 
             cpu0_rate, cpu1_rate, gpu_rate);
```

### 常見問題排除

1. **SCMI 通訊失敗**
   ```bash
   # 檢查 mailbox 狀態
   cat /proc/interrupts | grep mhu
   
   # 檢查共享記憶體
   cat /proc/iomem | grep scmi
   ```

2. **時鐘設定失敗**
   ```bash
   # 檢查時鐘權限
   dmesg | grep "SCMI.*denied"
   
   # 檢查頻率範圍
   cat /sys/kernel/debug/clk/scmi_clk_*/clk_min_rate
   cat /sys/kernel/debug/clk/scmi_clk_*/clk_max_rate
   ```

3. **效能問題**
   ```bash
   # 監控 SCMI 訊息頻率
   cat /sys/kernel/debug/tracing/trace | grep scmi_xfer
   
   # 檢查 CPU 使用率
   top -p $(pgrep scmi)
   ```

## 5.5 效能測試

### CPU 效能測試
```bash
# 安裝測試工具
apt-get install stress-ng cpufrequtils

# CPU 壓力測試
stress-ng --cpu 4 --timeout 60s &

# 監控頻率變化
watch -n 0.5 'cat /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq'

# 測試頻率切換延遲
cpufreq-bench -l 1000 -s 1 -x 2000000 -y 200000
```

### GPU 效能測試
```bash
# GPU 壓力測試 (如果有 GPU benchmark)
glmark2-es2 &

# 監控 GPU 頻率
watch -n 0.5 'cat /sys/class/devfreq/*/cur_freq'
```

## 5.6 自動化測試腳本

```bash
#!/bin/bash
# test_scmi_clock.sh

echo "=== SCMI Clock 功能測試 ==="

# 檢查 SCMI 設備
echo "1. 檢查 SCMI 設備..."
if [ -d "/sys/bus/scmi/devices/scmi_dev.0" ]; then
    echo "✓ SCMI 設備存在"
else
    echo "✗ SCMI 設備不存在"
    exit 1
fi

# 檢查時鐘驅動
echo "2. 檢查 CPUFreq 驅動..."
driver=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_driver 2>/dev/null)
if [ "$driver" = "myplatform-cpufreq" ]; then
    echo "✓ CPUFreq 驅動正確: $driver"
else
    echo "✗ CPUFreq 驅動錯誤: $driver"
fi

# 測試頻率設定
echo "3. 測試 CPU 頻率設定..."
frequencies=(200000 500000 1000000 1500000 2000000)

for freq in "${frequencies[@]}"; do
    echo $freq > /sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed
    sleep 1
    current=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq)
    
    if [ "$current" = "$freq" ]; then
        echo "✓ 頻率設定成功: $freq kHz"
    else
        echo "✗ 頻率設定失敗: 期望 $freq, 實際 $current"
    fi
done

echo "=== 測試完成 ==="
```

---

# 總結

這個完整指南涵蓋了從 SCP firmware 到 Linux kernel 的所有配置步驟：

1. **SCP Firmware**: 實作時鐘驅動和 SCMI 協議支援
2. **Linux Kernel**: 配置 SCMI 支援和實作 consumer driver  
3. **Device Tree**: 正確配置 SCMI 節點和時鐘綁定
4. **整合測試**: 驗證端到端功能

關鍵要點：
- 確保時鐘 ID 在兩端保持一致
- 正確配置 SCMI 代理權限
- 實作適當的錯誤處理和日誌
- 進行充分的功能和效能測試

透過這個指南，你可以建立一個完整的 SCMI-based 時鐘管理系統。