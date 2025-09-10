# 系統全面 SCMI Clock 遷移指南

## 🎯 目標
讓系統所有 clock 相關設定都透過 SCMI 協議呼叫到 SCP firmware，而不是直接操作硬體暫存器。

## 📋 需要修改的部分

### 1. Device Tree 修改
### 2. Clock Provider 替換
### 3. 移除原有 Clock Driver
### 4. 更新 SCP Firmware
### 5. 驅動程式適配

---

## 🔧 Phase 1: Device Tree 全面改造

### 1.1 移除原有 Clock 節點
```dts
/* 移除或註解掉原有的 clock 節點 */
/*
clocks {
    compatible = "simple-bus";
    
    // 移除所有原有的 clock 定義
    osc24m: osc24m {
        compatible = "fixed-clock";
        clock-frequency = <24000000>;
    };
    
    pll1: pll1 {
        compatible = "myplatform,pll-clock";
        reg = <0x50000000 0x100>;
    };
    
    // ... 其他原有 clock 節點
};
*/
```

### 1.2 建立完整的 SCMI Clock 架構
```dts
/ {
    /* 共享記憶體 */
    reserved-memory {
        #address-cells = <2>;
        #size-cells = <2>;
        ranges;

        scp_shmem: scp-shmem@4e000000 {
            compatible = "arm,scmi-shmem";
            reg = <0x0 0x4e000000 0x0 0x80>;
        };
    };

    /* Mailbox */
    mailbox: mailbox@2e000000 {
        compatible = "arm,mhu-doorbell";
        reg = <0x0 0x2e000000 0x0 0x1000>;
        interrupts = <GIC_SPI 36 IRQ_TYPE_LEVEL_HIGH>;
        #mbox-cells = <2>;
    };

    /* SCMI 協議 */
    firmware {
        scmi {
            compatible = "arm,scmi";
            mboxes = <&mailbox 0 0>, <&mailbox 0 1>;
            mbox-names = "tx", "rx";
            shmem = <&scp_shmem>;
            #address-cells = <1>;
            #size-cells = <0>;

            /* SCMI Clock 協議 - 這將成為系統唯一的 clock provider */
            scmi_clk: protocol@14 {
                reg = <0x14>;
                #clock-cells = <1>;
            };

            /* 其他 SCMI 協議 */
            scmi_perf: protocol@13 {
                reg = <0x13>;
                #performance-domain-cells = <1>;
            };

            scmi_pd: protocol@11 {
                reg = <0x11>;
                #power-domain-cells = <1>;
            };
        };
    };
};
```

### 1.3 更新所有設備節點使用 SCMI Clock
```dts
/ {
    cpus {
        #address-cells = <1>;
        #size-cells = <0>;

        cpu0: cpu@0 {
            device_type = "cpu";
            compatible = "arm,cortex-a78";
            reg = <0x0>;
            
            /* 原來可能是：clocks = <&pll1>; */
            /* 改為 SCMI：*/
            clocks = <&scmi_clk 0>;  /* SCMI Clock ID 0 */
            clock-names = "cpu";
            
            operating-points-v2 = <&cpu_opp_table>;
        };

        cpu1: cpu@1 {
            device_type = "cpu";
            compatible = "arm,cortex-a78";
            reg = <0x1>;
            
            clocks = <&scmi_clk 1>;  /* SCMI Clock ID 1 */
            clock-names = "cpu";
            
            operating-points-v2 = <&cpu_opp_table>;
        };
    };

    soc {
        /* GPU */
        gpu: gpu@2d000000 {
            compatible = "arm,mali-g78";
            reg = <0x0 0x2d000000 0x0 0x200000>;
            
            /* 原來：clocks = <&gpu_pll>; */
            /* 改為：*/
            clocks = <&scmi_clk 2>;  /* SCMI Clock ID 2 */
            clock-names = "core";
        };

        /* UART */
        uart0: serial@2a000000 {
            compatible = "arm,pl011", "arm,primecell";
            reg = <0x0 0x2a000000 0x0 0x1000>;
            
            /* 原來：clocks = <&uart_clk>, <&apb_clk>; */
            /* 改為：*/
            clocks = <&scmi_clk 3>, <&scmi_clk 4>;
            clock-names = "uartclk", "apb_pclk";
        };

        /* I2C */
        i2c0: i2c@2b000000 {
            compatible = "arm,versatile-i2c";
            reg = <0x0 0x2b000000 0x0 0x1000>;
            
            /* 原來：clocks = <&i2c_clk>; */
            /* 改為：*/
            clocks = <&scmi_clk 5>;
            clock-names = "i2c";
        };

        /* SPI */
        spi0: spi@2c000000 {
            compatible = "arm,pl022", "arm,primecell";
            reg = <0x0 0x2c000000 0x0 0x1000>;
            
            /* 原來：clocks = <&spi_clk>, <&apb_clk>; */
            /* 改為：*/
            clocks = <&scmi_clk 6>, <&scmi_clk 4>;
            clock-names = "SSPCLK", "apb_pclk";
        };

        /* MMC/SD */
        mmc0: mmc@2d000000 {
            compatible = "arm,pl180", "arm,primecell";
            reg = <0x0 0x2d000000 0x0 0x1000>;
            
            /* 原來：clocks = <&mmc_clk>, <&apb_clk>; */
            /* 改為：*/
            clocks = <&scmi_clk 7>, <&scmi_clk 4>;
            clock-names = "mclk", "apb_pclk";
        };

        /* Display */
        display: display@2e000000 {
            compatible = "arm,hdlcd";
            reg = <0x0 0x2e000000 0x0 0x1000>;
            
            /* 原來：clocks = <&pixel_clk>; */
            /* 改為：*/
            clocks = <&scmi_clk 8>;
            clock-names = "pxlclk";
        };

        /* Ethernet */
        ethernet: ethernet@2f000000 {
            compatible = "smsc,lan91c111";
            reg = <0x0 0x2f000000 0x0 0x10000>;
            
            /* 原來：clocks = <&eth_clk>; */
            /* 改為：*/
            clocks = <&scmi_clk 9>;
            clock-names = "ethclk";
        };
    };
};
```---

## 🔧
 Phase 2: Kernel 配置修改

### 2.1 移除原有 Clock Driver
```kconfig
# arch/arm64/configs/myplatform_defconfig

# 移除原有的 clock driver 配置
# CONFIG_COMMON_CLK_MYPLATFORM=n
# CONFIG_CLK_MYPLATFORM_PLL=n

# 確保 SCMI 相關配置啟用
CONFIG_ARM_SCMI_PROTOCOL=y
CONFIG_COMMON_CLK_SCMI=y
CONFIG_ARM_SCMI_POWER_DOMAIN=y
CONFIG_MAILBOX=y
CONFIG_ARM_MHU=y
```

### 2.2 更新 Makefile 移除原有 Clock Driver
```makefile
# drivers/clk/Makefile

# 註解或移除原有的 clock driver
# obj-$(CONFIG_COMMON_CLK_MYPLATFORM) += clk-myplatform.o
# obj-$(CONFIG_CLK_MYPLATFORM_PLL) += clk-myplatform-pll.o

# 確保 SCMI clock 已包含
obj-$(CONFIG_COMMON_CLK_SCMI) += clk-scmi.o
```

### 2.3 建立 SCMI Clock 對應表
```c
/* arch/arm64/boot/dts/myplatform/myplatform-clocks.h */
#ifndef _DT_BINDINGS_MYPLATFORM_CLOCKS_H
#define _DT_BINDINGS_MYPLATFORM_CLOCKS_H

/* SCMI Clock IDs - 必須與 SCP firmware 一致 */
#define SCMI_CLK_CPU0           0
#define SCMI_CLK_CPU1           1
#define SCMI_CLK_GPU            2
#define SCMI_CLK_UART           3
#define SCMI_CLK_APB            4
#define SCMI_CLK_I2C            5
#define SCMI_CLK_SPI            6
#define SCMI_CLK_MMC            7
#define SCMI_CLK_DISPLAY        8
#define SCMI_CLK_ETHERNET       9
#define SCMI_CLK_USB            10
#define SCMI_CLK_AUDIO          11
#define SCMI_CLK_CAMERA         12

#define SCMI_CLK_COUNT          13

#endif
```

---

## 🔧 Phase 3: SCP Firmware 全面更新

### 3.1 擴展時鐘定義 (include/myplatform_clock.h)
```c
#ifndef MYPLATFORM_CLOCK_H
#define MYPLATFORM_CLOCK_H

/* 完整的系統時鐘索引 - 對應 Linux DT 中的 SCMI Clock ID */
enum myplatform_clock_idx {
    /* CPU 時鐘 */
    MYPLATFORM_CLOCK_IDX_CPU0 = 0,     /* 對應 SCMI_CLK_CPU0 */
    MYPLATFORM_CLOCK_IDX_CPU1 = 1,     /* 對應 SCMI_CLK_CPU1 */
    
    /* GPU 時鐘 */
    MYPLATFORM_CLOCK_IDX_GPU = 2,      /* 對應 SCMI_CLK_GPU */
    
    /* 系統匯流排時鐘 */
    MYPLATFORM_CLOCK_IDX_UART = 3,     /* 對應 SCMI_CLK_UART */
    MYPLATFORM_CLOCK_IDX_APB = 4,      /* 對應 SCMI_CLK_APB */
    
    /* 周邊時鐘 */
    MYPLATFORM_CLOCK_IDX_I2C = 5,      /* 對應 SCMI_CLK_I2C */
    MYPLATFORM_CLOCK_IDX_SPI = 6,      /* 對應 SCMI_CLK_SPI */
    MYPLATFORM_CLOCK_IDX_MMC = 7,      /* 對應 SCMI_CLK_MMC */
    MYPLATFORM_CLOCK_IDX_DISPLAY = 8,  /* 對應 SCMI_CLK_DISPLAY */
    MYPLATFORM_CLOCK_IDX_ETHERNET = 9, /* 對應 SCMI_CLK_ETHERNET */
    MYPLATFORM_CLOCK_IDX_USB = 10,     /* 對應 SCMI_CLK_USB */
    MYPLATFORM_CLOCK_IDX_AUDIO = 11,   /* 對應 SCMI_CLK_AUDIO */
    MYPLATFORM_CLOCK_IDX_CAMERA = 12,  /* 對應 SCMI_CLK_CAMERA */
    
    MYPLATFORM_CLOCK_IDX_COUNT
};

/* 時鐘頻率定義 */
#define MYPLATFORM_CLOCK_RATE_CPU_MIN        (200UL * 1000000)
#define MYPLATFORM_CLOCK_RATE_CPU_MAX        (2000UL * 1000000)
#define MYPLATFORM_CLOCK_RATE_GPU_MIN        (100UL * 1000000)
#define MYPLATFORM_CLOCK_RATE_GPU_MAX        (1000UL * 1000000)
#define MYPLATFORM_CLOCK_RATE_UART_DEFAULT   (48UL * 1000000)
#define MYPLATFORM_CLOCK_RATE_APB_DEFAULT    (100UL * 1000000)
#define MYPLATFORM_CLOCK_RATE_I2C_DEFAULT    (24UL * 1000000)
#define MYPLATFORM_CLOCK_RATE_SPI_DEFAULT    (50UL * 1000000)
#define MYPLATFORM_CLOCK_RATE_MMC_DEFAULT    (200UL * 1000000)

#endif
```

### 3.2 完整的 SCMI Clock 配置 (scp_ramfw/config_scmi_clock.c)
```c
#include "myplatform_clock.h"
#include "myplatform_scmi.h"
#include <mod_scmi_clock.h>

/* Linux 可存取的所有時鐘 */
static const struct mod_scmi_clock_device agent_device_table_ospm[] = {
    /* CPU 時鐘 - 高頻動態調整 */
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
    
    /* GPU 時鐘 - 動態調頻 */
    {
        .element_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_CLOCK, 
                                         MYPLATFORM_CLOCK_IDX_GPU),
        .starts_enabled = true,
    },
    
    /* 周邊時鐘 - Linux 驅動程式控制 */
    {
        .element_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_CLOCK, 
                                         MYPLATFORM_CLOCK_IDX_UART),
        .starts_enabled = true,
    },
    {
        .element_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_CLOCK, 
                                         MYPLATFORM_CLOCK_IDX_APB),
        .starts_enabled = true,
    },
    {
        .element_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_CLOCK, 
                                         MYPLATFORM_CLOCK_IDX_I2C),
        .starts_enabled = false,  /* 由驅動程式控制 */
    },
    {
        .element_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_CLOCK, 
                                         MYPLATFORM_CLOCK_IDX_SPI),
        .starts_enabled = false,
    },
    {
        .element_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_CLOCK, 
                                         MYPLATFORM_CLOCK_IDX_MMC),
        .starts_enabled = false,
    },
    {
        .element_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_CLOCK, 
                                         MYPLATFORM_CLOCK_IDX_DISPLAY),
        .starts_enabled = false,
    },
    {
        .element_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_CLOCK, 
                                         MYPLATFORM_CLOCK_IDX_ETHERNET),
        .starts_enabled = false,
    },
    {
        .element_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_CLOCK, 
                                         MYPLATFORM_CLOCK_IDX_USB),
        .starts_enabled = false,
    },
    {
        .element_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_CLOCK, 
                                         MYPLATFORM_CLOCK_IDX_AUDIO),
        .starts_enabled = false,
    },
    {
        .element_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_CLOCK, 
                                         MYPLATFORM_CLOCK_IDX_CAMERA),
        .starts_enabled = false,
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
        .max_pending_transactions = 10,  /* 支援更多並發請求 */
        .agent_table = agent_table,
        .agent_count = FWK_ARRAY_SIZE(agent_table),
    }),
};
```#
## 3.3 完整的時鐘模組配置 (scp_ramfw/config_clock.c)
```c
#include "myplatform_clock.h"
#include <mod_clock.h>
#include <mod_myplatform_clock.h>

static struct fwk_element clock_dev_desc_table[] = {
    /* CPU 時鐘 */
    [MYPLATFORM_CLOCK_IDX_CPU0] = {
        .name = "CPU0_CLK",
        .data = &((struct mod_clock_dev_config) {
            .driver_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 
                                           MYPLATFORM_CLOCK_IDX_CPU0),
            .api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 0),
        }),
    },
    [MYPLATFORM_CLOCK_IDX_CPU1] = {
        .name = "CPU1_CLK",
        .data = &((struct mod_clock_dev_config) {
            .driver_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 
                                           MYPLATFORM_CLOCK_IDX_CPU1),
            .api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 0),
        }),
    },
    
    /* GPU 時鐘 */
    [MYPLATFORM_CLOCK_IDX_GPU] = {
        .name = "GPU_CLK",
        .data = &((struct mod_clock_dev_config) {
            .driver_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 
                                           MYPLATFORM_CLOCK_IDX_GPU),
            .api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 0),
        }),
    },
    
    /* 周邊時鐘 */
    [MYPLATFORM_CLOCK_IDX_UART] = {
        .name = "UART_CLK",
        .data = &((struct mod_clock_dev_config) {
            .driver_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 
                                           MYPLATFORM_CLOCK_IDX_UART),
            .api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 0),
        }),
    },
    [MYPLATFORM_CLOCK_IDX_APB] = {
        .name = "APB_CLK",
        .data = &((struct mod_clock_dev_config) {
            .driver_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 
                                           MYPLATFORM_CLOCK_IDX_APB),
            .api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 0),
        }),
    },
    [MYPLATFORM_CLOCK_IDX_I2C] = {
        .name = "I2C_CLK",
        .data = &((struct mod_clock_dev_config) {
            .driver_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 
                                           MYPLATFORM_CLOCK_IDX_I2C),
            .api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 0),
        }),
    },
    [MYPLATFORM_CLOCK_IDX_SPI] = {
        .name = "SPI_CLK",
        .data = &((struct mod_clock_dev_config) {
            .driver_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 
                                           MYPLATFORM_CLOCK_IDX_SPI),
            .api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 0),
        }),
    },
    [MYPLATFORM_CLOCK_IDX_MMC] = {
        .name = "MMC_CLK",
        .data = &((struct mod_clock_dev_config) {
            .driver_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 
                                           MYPLATFORM_CLOCK_IDX_MMC),
            .api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 0),
        }),
    },
    [MYPLATFORM_CLOCK_IDX_DISPLAY] = {
        .name = "DISPLAY_CLK",
        .data = &((struct mod_clock_dev_config) {
            .driver_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 
                                           MYPLATFORM_CLOCK_IDX_DISPLAY),
            .api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 0),
        }),
    },
    [MYPLATFORM_CLOCK_IDX_ETHERNET] = {
        .name = "ETHERNET_CLK",
        .data = &((struct mod_clock_dev_config) {
            .driver_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 
                                           MYPLATFORM_CLOCK_IDX_ETHERNET),
            .api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 0),
        }),
    },
    [MYPLATFORM_CLOCK_IDX_USB] = {
        .name = "USB_CLK",
        .data = &((struct mod_clock_dev_config) {
            .driver_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 
                                           MYPLATFORM_CLOCK_IDX_USB),
            .api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 0),
        }),
    },
    [MYPLATFORM_CLOCK_IDX_AUDIO] = {
        .name = "AUDIO_CLK",
        .data = &((struct mod_clock_dev_config) {
            .driver_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 
                                           MYPLATFORM_CLOCK_IDX_AUDIO),
            .api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 0),
        }),
    },
    [MYPLATFORM_CLOCK_IDX_CAMERA] = {
        .name = "CAMERA_CLK",
        .data = &((struct mod_clock_dev_config) {
            .driver_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 
                                           MYPLATFORM_CLOCK_IDX_CAMERA),
            .api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_MYPLATFORM_CLOCK, 0),
        }),
    },
    
    [MYPLATFORM_CLOCK_IDX_COUNT] = { 0 },
};

struct fwk_module_config config_clock = {
    .elements = FWK_MODULE_STATIC_ELEMENTS_PTR(clock_dev_desc_table),
};
```

---

## 🔧 Phase 4: 驅動程式適配

### 4.1 確保所有驅動程式使用 Clock Framework
大部分標準 Linux 驅動程式已經使用 Clock Framework，但需要檢查：

```c
/* 檢查驅動程式是否正確使用 clk API */

/* 正確的做法 - 使用 Clock Framework */
static int mydriver_probe(struct platform_device *pdev)
{
    struct clk *clk;
    
    /* 透過 Clock Framework 取得時鐘 */
    clk = devm_clk_get(&pdev->dev, "mydriver-clk");
    if (IS_ERR(clk)) {
        dev_err(&pdev->dev, "Failed to get clock\n");
        return PTR_ERR(clk);
    }
    
    /* 透過 Clock Framework 操作時鐘 */
    clk_prepare_enable(clk);
    clk_set_rate(clk, target_rate);
    
    return 0;
}

/* 錯誤的做法 - 直接操作暫存器 (需要移除) */
static int bad_driver_probe(struct platform_device *pdev)
{
    void __iomem *clk_reg;
    
    /* 不要這樣做！直接操作時鐘暫存器 */
    clk_reg = ioremap(CLK_REG_BASE, CLK_REG_SIZE);
    writel(CLK_ENABLE_BIT, clk_reg + CLK_CTRL_OFFSET);  /* ← 移除這種做法 */
    
    return 0;
}
```

### 4.2 建立驅動程式檢查清單
```bash
#!/bin/bash
# check_drivers_clock_usage.sh

echo "檢查驅動程式時鐘使用方式..."

# 搜尋直接操作時鐘暫存器的驅動程式
echo "=== 搜尋可能直接操作時鐘暫存器的驅動程式 ==="
grep -r "ioremap.*CLK\|writel.*CLK\|readl.*CLK" drivers/ --include="*.c"

# 搜尋使用 Clock Framework 的驅動程式
echo "=== 搜尋使用 Clock Framework 的驅動程式 ==="
grep -r "clk_get\|clk_enable\|clk_set_rate" drivers/ --include="*.c" | head -10

# 檢查 Device Tree 中的時鐘引用
echo "=== 檢查 Device Tree 時鐘引用 ==="
grep -r "clocks.*=" arch/arm64/boot/dts/ --include="*.dts*"
```

---

## 🔧 Phase 5: 系統整合和測試

### 5.1 建立測試腳本
```bash
#!/bin/bash
# test_scmi_clock_migration.sh

echo "=== SCMI Clock 遷移測試 ==="

# 1. 檢查 SCMI 設備
echo "1. 檢查 SCMI 設備..."
if [ -d "/sys/bus/scmi/devices/scmi_dev.0" ]; then
    echo "✓ SCMI 設備存在"
else
    echo "✗ SCMI 設備不存在"
    exit 1
fi

# 2. 檢查所有 SCMI 時鐘
echo "2. 檢查 SCMI 時鐘..."
scmi_clocks=$(ls /sys/kernel/debug/clk/ | grep scmi | wc -l)
echo "發現 $scmi_clocks 個 SCMI 時鐘"

if [ $scmi_clocks -lt 10 ]; then
    echo "⚠ SCMI 時鐘數量可能不足"
fi

# 3. 檢查是否還有非 SCMI 時鐘
echo "3. 檢查非 SCMI 時鐘..."
non_scmi_clocks=$(ls /sys/kernel/debug/clk/ | grep -v scmi | grep -v "clk_summary\|clk_orphan_summary" | wc -l)
echo "發現 $non_scmi_clocks 個非 SCMI 時鐘"

if [ $non_scmi_clocks -gt 5 ]; then
    echo "⚠ 可能還有未遷移的時鐘"
    ls /sys/kernel/debug/clk/ | grep -v scmi | grep -v "clk_summary\|clk_orphan_summary"
fi

# 4. 測試時鐘設定
echo "4. 測試時鐘設定..."
test_clocks=("cpu0" "cpu1" "gpu")

for clk in "${test_clocks[@]}"; do
    clk_path="/sys/kernel/debug/clk/scmi_clk_${clk}"
    if [ -d "$clk_path" ]; then
        rate=$(cat "$clk_path/clk_rate" 2>/dev/null)
        echo "✓ $clk 時鐘頻率: $rate Hz"
    else
        echo "✗ $clk 時鐘不存在"
    fi
done

# 5. 檢查驅動程式狀態
echo "5. 檢查關鍵驅動程式..."
drivers=("uart" "i2c" "spi" "mmc")

for driver in "${drivers[@]}"; do
    if lsmod | grep -q "$driver"; then
        echo "✓ $driver 驅動程式已載入"
    else
        echo "⚠ $driver 驅動程式未載入"
    fi
done

echo "=== 測試完成 ==="
```

### 5.2 效能監控腳本
```bash
#!/bin/bash
# monitor_scmi_performance.sh

echo "=== SCMI 效能監控 ==="

# 監控 SCMI 訊息頻率
echo "監控 SCMI 訊息..."
echo 1 > /sys/kernel/debug/tracing/events/scmi/enable
timeout 10s cat /sys/kernel/debug/tracing/trace_pipe | grep scmi | wc -l

# 監控時鐘切換延遲
echo "測試時鐘切換延遲..."
start_time=$(date +%s%N)
echo 1000000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed
end_time=$(date +%s%N)
latency=$(( (end_time - start_time) / 1000000 ))
echo "時鐘切換延遲: ${latency} ms"

# 檢查系統穩定性
echo "檢查系統穩定性..."
uptime
free -h
```

---

## 📋 遷移檢查清單

### ✅ Device Tree 修改
- [ ] 移除所有原有 clock 節點
- [ ] 建立完整 SCMI 架構
- [ ] 更新所有設備節點使用 SCMI clock
- [ ] 建立 SCMI Clock ID 對應表

### ✅ Kernel 配置
- [ ] 移除原有 clock driver 配置
- [ ] 啟用所有 SCMI 相關配置
- [ ] 更新 Makefile 移除原有 driver

### ✅ SCP Firmware
- [ ] 擴展時鐘定義涵蓋所有系統時鐘
- [ ] 更新 SCMI clock 配置支援所有時鐘
- [ ] 實作完整的平台時鐘驅動
- [ ] 測試所有時鐘功能

### ✅ 驅動程式適配
- [ ] 檢查所有驅動程式使用 Clock Framework
- [ ] 移除直接操作時鐘暫存器的程式碼
- [ ] 確保所有設備正常工作

### ✅ 測試驗證
- [ ] 系統正常啟動
- [ ] 所有設備功能正常
- [ ] 時鐘設定正確
- [ ] 效能符合預期

## 🎯 預期結果

完成遷移後，系統將：

1. **統一時鐘管理**：所有時鐘都透過 SCMI 協議管理
2. **集中控制**：SCP firmware 統一控制所有硬體時鐘
3. **標準介面**：Linux 驅動程式透過標準 Clock Framework 操作
4. **更好的電源管理**：SCP 可以更好地協調時鐘和電源狀態
5. **簡化維護**：減少平台特定的時鐘驅動程式

這樣的架構讓系統更加模組化和可維護，同時提供更好的電源效率和系統穩定性。