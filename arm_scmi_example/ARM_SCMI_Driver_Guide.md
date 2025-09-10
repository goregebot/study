# ARM SCMI Driver 使用指南

## 概述

ARM SCMI (System Control and Management Interface) 是一個標準化的協議，用於應用處理器 (AP) 與系統控制處理器 (SCP) 之間的通訊。Linux kernel 中的 ARM SCMI driver 提供了一個框架，讓其他 driver 可以透過標準化的介面與 SCP firmware 進行通訊。

## 架構概覽

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Client Driver │    │   SCMI Driver   │    │  SCP Firmware   │
│   (Clock/DVFS)  │◄──►│   Framework     │◄──►│   (ARM M-class) │
└─────────────────┘    └─────────────────┘    └─────────────────┘
                              │
                       ┌─────────────────┐
                       │   Transport     │
                       │ (Mailbox/SMC)   │
                       └─────────────────┘
```

## 主要組件

### 1. SCMI 協議支援
- **Clock Protocol**: 時鐘管理
- **Performance Protocol**: DVFS (動態電壓頻率調整)
- **Power Domain Protocol**: 電源域管理
- **Sensor Protocol**: 感測器管理
- **Reset Protocol**: 重置管理
- **Voltage Protocol**: 電壓管理

### 2. 傳輸層
- **Mailbox**: 使用 mailbox 子系統
- **SMC**: 使用 Secure Monitor Call
- **Virtio**: 虛擬化環境
- **OP-TEE**: 透過 OP-TEE 安全世界

### 3. 核心檔案結構
```
drivers/firmware/arm_scmi/
├── driver.c          # 主要驅動程式框架
├── bus.c             # SCMI 匯流排實作
├── clock.c           # 時鐘協議實作
├── perf.c            # 效能協議實作
├── power.c           # 電源協議實作
├── mailbox.c         # Mailbox 傳輸層
├── smc.c             # SMC 傳輸層
└── protocols.h       # 協議定義
```

## Device Tree 配置

### 基本配置範例
```dts
firmware {
    scmi {
        compatible = "arm,scmi";
        mboxes = <&mailbox 0>, <&mailbox 1>;
        mbox-names = "tx", "rx";
        shmem = <&cpu_scp_lpri0>, <&cpu_scp_lpri1>;
        #address-cells = <1>;
        #size-cells = <0>;

        scmi_clk: protocol@14 {
            reg = <0x14>;
            #clock-cells = <1>;
        };

        scmi_dvfs: protocol@13 {
            reg = <0x13>;
            #performance-domain-cells = <1>;
        };

        scmi_powerdomain: protocol@11 {
            reg = <0x11>;
            #power-domain-cells = <1>;
        };
    };
};

/* 共享記憶體定義 */
reserved-memory {
    cpu_scp_lpri0: scp-shmem@0x4f000000 {
        compatible = "arm,scmi-shmem";
        reg = <0x0 0x4f000000 0x0 0x80>;
    };

    cpu_scp_lpri1: scp-shmem@0x4f000080 {
        compatible = "arm,scmi-shmem";
        reg = <0x0 0x4f000080 0x0 0x80>;
    };
};
```

## 如何使用 SCMI Driver

### 1. 取得 SCMI Protocol Handle

```c
#include <linux/scmi_protocol.h>

static const struct scmi_handle *scmi_handle;
static const struct scmi_clk_proto_ops *clk_ops;

static int my_driver_probe(struct scmi_device *sdev)
{
    scmi_handle = sdev->handle;
    
    /* 取得時鐘協議操作介面 */
    clk_ops = scmi_handle->devm_protocol_get(sdev, 
                                           SCMI_PROTOCOL_CLOCK, 
                                           &ph);
    if (IS_ERR(clk_ops))
        return PTR_ERR(clk_ops);
        
    return 0;
}
```

### 2. SCMI Device Driver 註冊

```c
static const struct scmi_device_id my_scmi_id_table[] = {
    { SCMI_PROTOCOL_CLOCK, "my-clock-driver" },
    { },
};
MODULE_DEVICE_TABLE(scmi, my_scmi_id_table);

static struct scmi_driver my_scmi_driver = {
    .name = "my-scmi-driver",
    .probe = my_driver_probe,
    .remove = my_driver_remove,
    .id_table = my_scmi_id_table,
};
module_scmi_driver(my_scmi_driver);
```

## Clock Protocol 使用範例

### 完整的 Clock Driver 範例

```c
// my_scmi_clock_driver.c
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/scmi_protocol.h>
#include <linux/clk-provider.h>

struct scmi_clk_data {
    const struct scmi_protocol_handle *ph;
    const struct scmi_clk_proto_ops *ops;
    struct clk_hw hw;
    u32 id;
};

#define to_scmi_clk(hw) container_of(hw, struct scmi_clk_data, hw)

/* Clock 操作函數 */
static int scmi_clk_enable(struct clk_hw *hw)
{
    struct scmi_clk_data *clk = to_scmi_clk(hw);
    
    return clk->ops->enable(clk->ph, clk->id);
}

static void scmi_clk_disable(struct clk_hw *hw)
{
    struct scmi_clk_data *clk = to_scmi_clk(hw);
    
    clk->ops->disable(clk->ph, clk->id);
}

static unsigned long scmi_clk_recalc_rate(struct clk_hw *hw,
                                         unsigned long parent_rate)
{
    struct scmi_clk_data *clk = to_scmi_clk(hw);
    u64 rate;
    int ret;
    
    ret = clk->ops->rate_get(clk->ph, clk->id, &rate);
    if (ret)
        return 0;
        
    return rate;
}

static int scmi_clk_set_rate(struct clk_hw *hw, unsigned long rate,
                            unsigned long parent_rate)
{
    struct scmi_clk_data *clk = to_scmi_clk(hw);
    
    return clk->ops->rate_set(clk->ph, clk->id, rate);
}

static long scmi_clk_round_rate(struct clk_hw *hw, unsigned long rate,
                              unsigned long *parent_rate)
{
    /* 實作 rate rounding 邏輯 */
    return rate;
}

static const struct clk_ops scmi_clk_ops = {
    .enable = scmi_clk_enable,
    .disable = scmi_clk_disable,
    .recalc_rate = scmi_clk_recalc_rate,
    .set_rate = scmi_clk_set_rate,
    .round_rate = scmi_clk_round_rate,
};

/* SCMI Clock Driver 主要函數 */
static int scmi_clocks_probe(struct scmi_device *sdev)
{
    int ret, i, num_clocks;
    struct device *dev = &sdev->dev;
    const struct scmi_protocol_handle *ph;
    const struct scmi_clk_proto_ops *clk_ops;
    
    /* 取得 SCMI protocol handle */
    clk_ops = sdev->handle->devm_protocol_get(sdev, SCMI_PROTOCOL_CLOCK, &ph);
    if (IS_ERR(clk_ops))
        return PTR_ERR(clk_ops);
    
    /* 取得時鐘數量 */
    num_clocks = clk_ops->count_get(ph);
    if (num_clocks < 0)
        return num_clocks;
    
    dev_info(dev, "Found %d SCMI clocks\n", num_clocks);
    
    /* 註冊每個時鐘 */
    for (i = 0; i < num_clocks; i++) {
        struct scmi_clk_data *sclk;
        const struct scmi_clock_info *info;
        struct clk_init_data init = {};
        struct clk *clk;
        
        /* 取得時鐘資訊 */
        info = clk_ops->info_get(ph, i);
        if (!info)
            continue;
            
        sclk = devm_kzalloc(dev, sizeof(*sclk), GFP_KERNEL);
        if (!sclk)
            return -ENOMEM;
            
        sclk->ph = ph;
        sclk->ops = clk_ops;
        sclk->id = i;
        sclk->hw.init = &init;
        
        init.name = info->name;
        init.ops = &scmi_clk_ops;
        init.num_parents = 0;
        
        /* 註冊時鐘到 Linux clock framework */
        clk = devm_clk_register(dev, &sclk->hw);
        if (IS_ERR(clk)) {
            dev_err(dev, "Failed to register clock %s: %ld\n",
                   info->name, PTR_ERR(clk));
            continue;
        }
        
        /* 註冊 clock provider */
        ret = of_clk_add_hw_provider(dev->of_node, of_clk_hw_simple_get,
                                   &sclk->hw);
        if (ret)
            dev_warn(dev, "Failed to add clock provider for %s\n",
                    info->name);
                    
        dev_info(dev, "Registered SCMI clock: %s\n", info->name);
    }
    
    return 0;
}

static void scmi_clocks_remove(struct scmi_device *sdev)
{
    of_clk_del_provider(sdev->dev.of_node);
}

static const struct scmi_device_id scmi_id_table[] = {
    { SCMI_PROTOCOL_CLOCK, "scmi-clocks" },
    { },
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

static struct scmi_driver scmi_clocks_driver = {
    .name = "scmi-clocks",
    .probe = scmi_clocks_probe,
    .remove = scmi_clocks_remove,
    .id_table = scmi_id_table,
};
module_scmi_driver(scmi_clocks_driver);

MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("SCMI Clock Driver");
MODULE_LICENSE("GPL v2");
```

## SCMI 與 SCP Firmware 通訊流程

### 1. 訊息格式
```c
/* SCMI 訊息標頭 */
struct scmi_msg_hdr {
    u8 id;           /* 命令 ID */
    u8 protocol_id;  /* 協議 ID */
    u8 type;         /* 訊息類型 */
    u16 seq;         /* 序列號 */
    u32 status;      /* 狀態 */
};

/* Clock Rate Set 命令 */
struct scmi_clock_set_rate {
    __le32 flags;      /* 設定旗標 */
    __le32 id;         /* 時鐘 ID */
    __le32 value_low;  /* 頻率低 32 位元 */
    __le32 value_high; /* 頻率高 32 位元 */
};
```

### 2. 通訊步驟

1. **準備訊息**: Driver 準備 SCMI 命令
2. **傳送請求**: 透過 mailbox/SMC 傳送到 SCP
3. **SCP 處理**: SCP firmware 處理請求
4. **回應**: SCP 傳送回應給 AP
5. **完成**: Driver 處理回應並完成操作

### 3. 實際通訊範例 (Clock Rate Set)

```c
static int scmi_clock_rate_set_example(const struct scmi_protocol_handle *ph,
                                     u32 clk_id, u64 rate)
{
    int ret;
    struct scmi_xfer *t;
    struct scmi_clock_set_rate *cfg;
    
    /* 1. 初始化傳輸結構 */
    ret = ph->xops->xfer_get_init(ph, CLOCK_RATE_SET, 
                                 sizeof(*cfg), 0, &t);
    if (ret)
        return ret;
    
    /* 2. 準備命令資料 */
    cfg = t->tx.buf;
    cfg->flags = cpu_to_le32(0);           /* 同步設定 */
    cfg->id = cpu_to_le32(clk_id);         /* 時鐘 ID */
    cfg->value_low = cpu_to_le32(rate & 0xffffffff);
    cfg->value_high = cpu_to_le32(rate >> 32);
    
    /* 3. 傳送命令到 SCP 並等待回應 */
    ret = ph->xops->do_xfer(ph, t);
    
    /* 4. 釋放傳輸資源 */
    ph->xops->xfer_put(ph, t);
    
    return ret;
}
```

## SCP Firmware 端處理

在 SCP firmware 端，對應的處理流程：

```c
/* SCP firmware 中的 clock rate set 處理 */
static int scp_clock_rate_set_handler(fwk_id_t service_id, 
                                     const uint32_t *payload)
{
    struct scmi_clock_set_rate *parameters;
    uint64_t rate;
    uint32_t clock_id;
    
    parameters = (struct scmi_clock_set_rate *)payload;
    
    /* 解析參數 */
    clock_id = le32_to_cpu(parameters->id);
    rate = ((uint64_t)le32_to_cpu(parameters->value_high) << 32) |
           le32_to_cpu(parameters->value_low);
    
    /* 呼叫 clock module 設定頻率 */
    return clock_module_set_rate(clock_id, rate);
}
```

## 除錯和監控

### 1. 啟用 SCMI 除錯
```bash
# 啟用 SCMI 除錯訊息
echo 8 > /proc/sys/kernel/printk
echo 'file drivers/firmware/arm_scmi/* +p' > /sys/kernel/debug/dynamic_debug/control
```

### 2. 檢查 SCMI 狀態
```bash
# 檢查 SCMI 裝置
ls /sys/bus/scmi/devices/

# 檢查時鐘資訊
cat /sys/kernel/debug/clk/clk_summary
```

### 3. Trace 支援
```bash
# 啟用 SCMI trace
echo 1 > /sys/kernel/debug/tracing/events/scmi/enable
cat /sys/kernel/debug/tracing/trace
```

## 常見問題和解決方案

### 1. 通訊失敗
- 檢查 mailbox 配置
- 確認共享記憶體設定
- 驗證 SCP firmware 是否正常運行

### 2. 時鐘設定失敗
- 確認時鐘 ID 正確
- 檢查頻率範圍是否支援
- 驗證電源域狀態

### 3. 效能問題
- 使用非同步操作
- 批次處理多個請求
- 最佳化共享記憶體存取

## 總結

ARM SCMI driver 提供了一個標準化且強大的框架，讓 Linux kernel 能夠與 SCP firmware 進行有效的通訊。透過理解其架構和 API，開發者可以輕鬆地實作各種系統管理功能，如時鐘控制、電源管理和效能調整。

關鍵要點：
- 使用標準化的 SCMI 協議
- 透過 device tree 進行配置
- 實作適當的 driver 介面
- 處理非同步操作和錯誤情況
- 充分利用除錯和監控工具