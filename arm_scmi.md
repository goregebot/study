# ARM SCMI 驅動程式使用說明文件

## 概述

ARM SCMI (System Control and Management Interface) 是一個標準化的介面協議，用於應用處理器 (AP) 與系統控制處理器 (SCP) 之間的通信。它提供了電源管理、時鐘控制、性能調節、感測器監控等功能。

### 架構圖

```
應用處理器 AP
    ↓
SCMI 驅動程式
    ↓
Mailbox 傳輸層
    ↓
共享記憶體 SRAM
    ↓
系統控制處理器 SCP
```

### 支援的協議

- **Performance Protocol (0x13)**: CPU 頻率和電壓調節 (DVFS)
- **Clock Protocol (0x14)**: 系統時鐘管理
- **Power Domain Protocol (0x11)**: 電源域控制
- **Sensor Protocol (0x15)**: 溫度和其他感測器監控
- **Reset Protocol (0x16)**: 設備重置控制
- **Base Protocol (0x10)**: 基本協議管理

## 核心組件

### 主要資料結構

#### 訊息標頭
```c
struct scmi_msg_hdr {
    u8 id;              // 訊息識別碼
    u8 protocol_id;     // 協議識別碼
    u16 seq;            // 序列號
    u32 status;         // 傳輸狀態
    bool poll_completion; // 輪詢完成標誌
};
```

#### 訊息傳輸結構
```c
struct scmi_xfer {
    struct scmi_msg_hdr hdr;    // 傳輸訊息標頭
    struct scmi_msg tx;         // 傳輸訊息
    struct scmi_msg rx;         // 接收訊息
    struct completion done;     // 完成事件
    struct completion *async_done; // 異步完成事件
};
```

### 核心 API 函數

```c
// 取得 SCMI 控制代碼
struct scmi_handle *scmi_handle_get(struct device *dev);

// 初始化訊息傳輸
int scmi_xfer_get_init(const struct scmi_handle *h, u8 msg_id, u8 prot_id,
                       size_t tx_size, size_t rx_size, struct scmi_xfer **p);

// 執行訊息傳輸
int scmi_do_xfer(const struct scmi_handle *h, struct scmi_xfer *xfer);

// 釋放訊息傳輸資源
void scmi_xfer_put(const struct scmi_handle *h, struct scmi_xfer *xfer);
```

## Device Tree 配置

### 基本配置範例

```dts
sram@50000000 {
    compatible = "mmio-sram";
    reg = <0x0 0x50000000 0x0 0x10000>;
    #address-cells = <1>;
    #size-cells = <1>;
    ranges = <0 0x0 0x50000000 0x10000>;

    cpu_scp_lpri: scp-shmem@0 {
        compatible = "arm,scmi-shmem";
        reg = <0x0 0x200>;
    };

    cpu_scp_hpri: scp-shmem@200 {
        compatible = "arm,scmi-shmem";
        reg = <0x200 0x200>;
    };
};

mailbox@40000000 {
    #mbox-cells = <1>;
    reg = <0x0 0x40000000 0x0 0x10000>;
};

firmware {
    scmi {
        compatible = "arm,scmi";
        mboxes = <&mailbox 0 &mailbox 1>;
        mbox-names = "tx", "rx";
        shmem = <&cpu_scp_lpri &cpu_scp_hpri>;
        #address-cells = <1>;
        #size-cells = <0>;

        scmi_devpd: protocol@11 {
            reg = <0x11>;
            #power-domain-cells = <1>;
        };

        scmi_dvfs: protocol@13 {
            reg = <0x13>;
            #clock-cells = <1>;
        };

        scmi_clk: protocol@14 {
            reg = <0x14>;
            #clock-cells = <1>;
        };

        scmi_sensors0: protocol@15 {
            reg = <0x15>;
            #thermal-sensor-cells = <1>;
        };

        scmi_reset: protocol@16 {
            reg = <0x16>;
            #reset-cells = <1>;
        };
    };
};
```

### CPU 節點配置

```dts
cpu@0 {
    compatible = "arm,cortex-a53";
    reg = <0 0>;
    clocks = <&scmi_dvfs 0>;
    power-domains = <&scmi_devpd 0>;
    #cooling-cells = <2>;
};
```

## 驅動程式使用方法

### 1. CPUFreq SCMI 驅動程式

#### 初始化流程
```c
static int scmi_cpufreq_init(struct cpufreq_policy *policy)
{
    int ret, nr_opp;
    struct device *cpu_dev;
    struct scmi_data *priv;
    struct cpufreq_frequency_table *freq_table;

    // 取得 CPU 設備
    cpu_dev = get_cpu_device(policy->cpu);
    if (!cpu_dev) {
        pr_err("failed to get cpu%d device\n", policy->cpu);
        return -ENODEV;
    }

    // 添加 OPP 到設備
    ret = handle->perf_ops->device_opps_add(handle, cpu_dev);
    if (ret) {
        dev_warn(cpu_dev, "failed to add opps to the device\n");
        return ret;
    }

    // 設定共享 CPU
    ret = scmi_get_sharing_cpus(cpu_dev, policy->cpus);
    
    // 初始化頻率表
    ret = dev_pm_opp_init_cpufreq_table(cpu_dev, &freq_table);
    
    policy->freq_table = freq_table;
    policy->dvfs_possible_from_any_cpu = true;
    policy->fast_switch_possible = true;

    return 0;
}
```

#### 頻率設定
```c
static int scmi_cpufreq_set_target(struct cpufreq_policy *policy, 
                                   unsigned int index)
{
    int ret;
    struct scmi_data *priv = policy->driver_data;
    struct scmi_perf_ops *perf_ops = handle->perf_ops;
    u64 freq = policy->freq_table[index].frequency;

    ret = perf_ops->freq_set(handle, priv->domain_id, freq * 1000, false);
    if (!ret)
        arch_set_freq_scale(policy->related_cpus, freq,
                            policy->cpuinfo.max_freq);
    return ret;
}
```

#### 快速頻率切換
```c
static unsigned int scmi_cpufreq_fast_switch(struct cpufreq_policy *policy,
                                             unsigned int target_freq)
{
    struct scmi_data *priv = policy->driver_data;
    struct scmi_perf_ops *perf_ops = handle->perf_ops;

    if (!perf_ops->freq_set(handle, priv->domain_id,
                            target_freq * 1000, true)) {
        arch_set_freq_scale(policy->related_cpus, target_freq,
                            policy->cpuinfo.max_freq);
        return target_freq;
    }

    return 0;
}
```

### 2. Reset 控制器

#### Reset 操作實現
```c
static int scmi_reset_assert(struct reset_controller_dev *rcdev, 
                             unsigned long id)
{
    const struct scmi_handle *handle = to_scmi_handle(rcdev);
    return handle->reset_ops->assert(handle, id);
}

static int scmi_reset_deassert(struct reset_controller_dev *rcdev, 
                               unsigned long id)
{
    const struct scmi_handle *handle = to_scmi_handle(rcdev);
    return handle->reset_ops->deassert(handle, id);
}

static int scmi_reset_reset(struct reset_controller_dev *rcdev, 
                            unsigned long id)
{
    const struct scmi_handle *handle = to_scmi_handle(rcdev);
    return handle->reset_ops->reset(handle, id);
}
```

#### Reset 控制器註冊
```c
static const struct reset_control_ops scmi_reset_ops = {
    .assert     = scmi_reset_assert,
    .deassert   = scmi_reset_deassert,
    .reset      = scmi_reset_reset,
};
```

### 3. 時鐘管理

#### 時鐘操作
```c
// 啟用時鐘
static int scmi_clk_enable(struct clk_hw *hw)
{
    struct scmi_clk *clk = to_scmi_clk(hw);
    return clk->handle->clk_ops->enable(clk->handle, clk->id);
}

// 停用時鐘
static void scmi_clk_disable(struct clk_hw *hw)
{
    struct scmi_clk *clk = to_scmi_clk(hw);
    clk->handle->clk_ops->disable(clk->handle, clk->id);
}

// 設定時鐘頻率
static int scmi_clk_set_rate(struct clk_hw *hw, unsigned long rate,
                             unsigned long parent_rate)
{
    struct scmi_clk *clk = to_scmi_clk(hw);
    return clk->handle->clk_ops->rate_set(clk->handle, clk->id, rate);
}
```

## 使用步驟

### 步驟 1: 內核配置

在內核配置中啟用以下選項：

```bash
CONFIG_ARM_SCMI_PROTOCOL=y
CONFIG_ARM_SCMI_CPUFREQ=y
CONFIG_RESET_SCMI=y
CONFIG_CLK_SCMI=y
CONFIG_ARM_SCMI_POWER_DOMAIN=y
CONFIG_MAILBOX=y
CONFIG_ARM_MHU=y
```

### 步驟 2: Device Tree 設定

1. 配置共享記憶體區域 (SRAM)
2. 設定 mailbox 通道
3. 定義 SCMI 協議節點
4. 將設備連接到 SCMI 服務

### 步驟 3: 驅動程式註冊

```c
// SCMI 設備驅動程式註冊
static struct scmi_driver scmi_cpufreq_drv = {
    .name       = "scmi-cpufreq",
    .probe      = scmi_cpufreq_probe,
    .remove     = scmi_cpufreq_remove,
    .id_table   = scmi_id_table,
};
module_scmi_driver(scmi_cpufreq_drv);
```

### 步驟 4: 運行時使用

#### CPUFreq 控制
```bash
# 查看可用頻率
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_available_frequencies

# 設定 governor
echo "performance" > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor

# 查看當前頻率
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq

# 設定最大頻率
echo 1800000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq
```

#### Reset 控制
```c
// 在驅動程式中使用 reset
struct reset_control *rst;

rst = devm_reset_control_get(&pdev->dev, "scmi-reset");
if (IS_ERR(rst))
    return PTR_ERR(rst);

// Assert reset
reset_control_assert(rst);

// Deassert reset
reset_control_deassert(rst);

// 執行完整重置週期
reset_control_reset(rst);
```

#### 時鐘控制
```c
// 取得時鐘
struct clk *clk = devm_clk_get(&pdev->dev, "scmi-clk");

// 設定時鐘頻率
clk_set_rate(clk, 100000000); // 100MHz

// 啟用時鐘
clk_prepare_enable(clk);

// 停用時鐘
clk_disable_unprepare(clk);
```

## 協議支援詳細

| 協議 ID | 名稱 | 功能 | 驅動程式檔案 |
|---------|------|------|-------------|
| 0x10 | Base | 基本協議管理、版本查詢 | base.c |
| 0x11 | Power Domain | 電源域開關控制 | scmi_pm_domain.c |
| 0x13 | Performance | 性能/DVFS 控制、OPP 管理 | perf.c |
| 0x14 | Clock | 時鐘頻率設定、啟用/停用 | clock.c |
| 0x15 | Sensor | 溫度、電壓等感測器讀取 | sensors.c |
| 0x16 | Reset | 設備重置控制 | reset.c |

## 除錯與監控

### 除錯介面

```bash
# 查看 SCMI 狀態
cat /sys/kernel/debug/scmi/

# 啟用 SCMI 追蹤
echo 1 > /sys/kernel/debug/tracing/events/scmi/enable

# 查看追蹤記錄
cat /sys/kernel/debug/tracing/trace

# 監控特定協議
echo 1 > /sys/kernel/debug/tracing/events/scmi/scmi_xfer_begin/enable
echo 1 > /sys/kernel/debug/tracing/events/scmi/scmi_xfer_end/enable
```

### 常見問題排除

#### 1. 通信失敗
- **症狀**: SCMI 訊息傳輸超時
- **檢查項目**:
  - Mailbox 配置是否正確
  - 共享記憶體地址是否正確
  - SCP 韌體是否正常運行

#### 2. 協議不支援
- **症狀**: 特定協議功能無法使用
- **檢查項目**:
  - SCP 韌體版本是否支援該協議
  - Device Tree 中協議節點是否正確配置
  - 驅動程式是否已載入

#### 3. 性能問題
- **症狀**: 頻率切換延遲過高
- **解決方案**:
  - 啟用 fast_switch 功能
  - 調整訊息超時設定
  - 優化共享記憶體大小

### 錯誤碼對照

| 錯誤碼 | 名稱 | 描述 |
|--------|------|------|
| 0 | SCMI_SUCCESS | 成功 |
| -1 | SCMI_ERR_SUPPORT | 不支援 |
| -2 | SCMI_ERR_PARAMS | 參數錯誤 |
| -3 | SCMI_ERR_ACCESS | 存取被拒 |
| -4 | SCMI_ERR_ENTRY | 找不到項目 |
| -5 | SCMI_ERR_RANGE | 數值超出範圍 |
| -6 | SCMI_ERR_BUSY | 設備忙碌 |
| -7 | SCMI_ERR_COMMS | 通信錯誤 |
| -8 | SCMI_ERR_GENERIC | 一般錯誤 |
| -9 | SCMI_ERR_HARDWARE | 硬體錯誤 |
| -10 | SCMI_ERR_PROTOCOL | 協議錯誤 |

## 性能優化建議

### 1. 使用 Fast Switch
```c
// 在 CPUFreq 驅動程式中啟用
policy->fast_switch_possible = true;
```

### 2. 批次操作
- 合併多個相關操作
- 減少 mailbox 通信開銷
- 使用異步訊息傳輸

### 3. 記憶體優化
- 適當調整共享記憶體大小
- 使用專用通道處理高優先級訊息

### 4. 監控與調優
```bash
# 監控頻率切換延遲
cat /sys/kernel/debug/tracing/trace | grep scmi

# 調整 governor 參數
echo 50000 > /sys/devices/system/cpu/cpufreq/ondemand/sampling_rate
```

## 開發指南

### 新增自定義協議

1. **定義協議結構**
```c
struct scmi_custom_ops {
    int (*custom_func)(const struct scmi_handle *handle, u32 param);
};
```

2. **實現協議處理函數**
```c
static int scmi_custom_protocol_init(struct scmi_handle *handle)
{
    // 協議初始化邏輯
    return 0;
}
```

3. **註冊協議**
```c
static const struct scmi_protocol scmi_custom = {
    .id = SCMI_PROTOCOL_CUSTOM,
    .init = &scmi_custom_protocol_init,
    .ops = &scmi_custom_ops,
};
```

### 最佳實踐

1. **錯誤處理**: 總是檢查 SCMI 操作的返回值
2. **資源管理**: 正確釋放 xfer 資源
3. **同步處理**: 適當使用 completion 機制
4. **效能考量**: 避免在中斷上下文中進行 SCMI 操作

## 參考資料

- [ARM SCMI 規範文件](http://infocenter.arm.com/help/topic/com.arm.doc.den0056a/index.html)
- [Linux 內核 SCMI 文檔](Documentation/devicetree/bindings/arm/arm,scmi.txt)
- [CPUFreq 子系統文檔](Documentation/cpu-freq/)
- [Reset 框架文檔](Documentation/devicetree/bindings/reset/reset.txt)

## SCP Firmware 控制流程詳解

### 完整通信流程圖

```
[AP 應用處理器]                    [SCP 系統控制處理器]
       │                                    │
   ┌───▼───┐                          ┌────▼────┐
   │ SCMI  │                          │  SCP    │
   │Driver │                          │Firmware │
   └───┬───┘                          └────▲────┘
       │                                   │
   ┌───▼───┐                          ┌────┴────┐
   │Mailbox│                          │Mailbox  │
   │Client │                          │Handler  │
   └───┬───┘                          └────▲────┘
       │                                   │
   ┌───▼───┐    Doorbell Signal       ┌────┴────┐
   │  MHU  │◄─────────────────────────►│   MHU   │
   │Hardware│                          │Hardware │
   └───┬───┘                          └────▲────┘
       │                                   │
   ┌───▼───┐    Shared Memory          ┌────┴────┐
   │ SRAM  │◄─────────────────────────►│  SRAM   │
   │Access │                          │ Access  │
   └───────┘                          └─────────┘
```

### 1. AP 端發送流程

#### 步驟 1: 訊息準備
```c
// 在 drivers/firmware/arm_scmi/driver.c
static void scmi_tx_prepare(struct mbox_client *cl, void *m)
{
    struct scmi_xfer *t = m;
    struct scmi_chan_info *cinfo = client_to_scmi_chan_info(cl);
    struct scmi_shared_mem __iomem *mem = cinfo->payload;

    // 等待共享記憶體通道空閒
    spin_until_cond(ioread32(&mem->channel_status) &
                    SCMI_SHMEM_CHAN_STAT_CHANNEL_FREE);

    // 標記通道忙碌並清除錯誤
    iowrite32(0x0, &mem->channel_status);

    // 設定中斷標誌
    iowrite32(t->hdr.poll_completion ? 0 : SCMI_SHMEM_FLAG_INTR_ENABLED,
              &mem->flags);

    // 寫入訊息長度
    iowrite32(sizeof(mem->msg_header) + t->tx.len, &mem->length);

    // 寫入訊息標頭
    iowrite32(pack_scmi_header(&t->hdr), &mem->msg_header);

    // 複製訊息內容到共享記憶體
    if (t->tx.buf)
        memcpy_toio(mem->msg_payload, t->tx.buf, t->tx.len);
}
```

#### 步驟 2: 發送訊息
```c
int scmi_do_xfer(const struct scmi_handle *handle, struct scmi_xfer *xfer)
{
    int ret;
    struct scmi_info *info = handle_to_scmi_info(handle);
    struct scmi_chan_info *cinfo;

    // 找到對應的通道
    cinfo = idr_find(&info->tx_idr, xfer->hdr.protocol_id);
    if (unlikely(!cinfo))
        return -EINVAL;

    // 透過 mailbox 發送訊息 (觸發 doorbell)
    ret = mbox_send_message(cinfo->chan, xfer);
    if (ret < 0) {
        dev_dbg(dev, "mbox send fail %d\n", ret);
        return ret;
    }

    // 等待回應
    if (xfer->hdr.poll_completion) {
        // 輪詢模式
        ktime_t stop = ktime_add_ns(ktime_get(), SCMI_MAX_POLL_TO_NS);
        spin_until_cond(scmi_xfer_done_no_timeout(cinfo, xfer, stop));

        if (ktime_before(ktime_get(), stop))
            scmi_fetch_response(xfer, cinfo->payload);
        else
            ret = -ETIMEDOUT;
    } else {
        // 中斷模式
        timeout = msecs_to_jiffies(info->desc->max_rx_timeout_ms);
        if (!wait_for_completion_timeout(&xfer->done, timeout)) {
            ret = -ETIMEDOUT;
        }
    }

    // 通知 mailbox 傳輸完成
    mbox_client_txdone(cinfo->chan, ret);
    return ret;
}
```

### 2. 共享記憶體結構

```c
// SCMI 共享記憶體佈局
struct scmi_shared_mem {
    __le32 reserved;           // 保留欄位
    __le32 channel_status;     // 通道狀態
    #define SCMI_SHMEM_CHAN_STAT_CHANNEL_ERROR  BIT(1)
    #define SCMI_SHMEM_CHAN_STAT_CHANNEL_FREE   BIT(0)
    __le32 reserved1[2];       // 保留欄位
    __le32 flags;              // 標誌位
    #define SCMI_SHMEM_FLAG_INTR_ENABLED        BIT(0)
    __le32 length;             // 訊息長度
    __le32 msg_header;         // 訊息標頭
    u8 msg_payload[0];         // 訊息內容
};
```

### 3. SCP Firmware 端處理流程

#### SCP 端接收處理 (偽代碼)
```c
// SCP Firmware 中的處理流程
void scp_scmi_mailbox_handler(void)
{
    // 1. 接收到 MHU doorbell 中斷
    if (mhu_interrupt_received()) {

        // 2. 讀取共享記憶體
        struct scmi_shared_mem *shmem = get_shared_memory();

        // 3. 檢查通道狀態
        if (!(shmem->channel_status & SCMI_SHMEM_CHAN_STAT_CHANNEL_FREE)) {

            // 4. 解析訊息標頭
            uint32_t header = shmem->msg_header;
            uint8_t protocol_id = MSG_XTRACT_PROT_ID(header);
            uint8_t message_id = MSG_XTRACT_ID(header);
            uint16_t token = MSG_XTRACT_TOKEN(header);

            // 5. 根據協議分發處理
            switch (protocol_id) {
                case SCMI_PROTOCOL_PERF:
                    scp_handle_performance_protocol(message_id,
                                                   shmem->msg_payload,
                                                   shmem->length);
                    break;

                case SCMI_PROTOCOL_CLOCK:
                    scp_handle_clock_protocol(message_id,
                                            shmem->msg_payload,
                                            shmem->length);
                    break;

                case SCMI_PROTOCOL_POWER:
                    scp_handle_power_protocol(message_id,
                                            shmem->msg_payload,
                                            shmem->length);
                    break;

                default:
                    scp_send_error_response(SCMI_ERR_PROTOCOL);
                    return;
            }

            // 6. 準備回應訊息
            scp_prepare_response(token, response_data);

            // 7. 標記通道空閒
            shmem->channel_status |= SCMI_SHMEM_CHAN_STAT_CHANNEL_FREE;

            // 8. 如果需要，發送中斷通知 AP
            if (shmem->flags & SCMI_SHMEM_FLAG_INTR_ENABLED) {
                mhu_send_interrupt_to_ap();
            }
        }
    }
}
```

#### SCP 協議處理範例 - Performance Protocol
```c
void scp_handle_performance_protocol(uint8_t msg_id, uint8_t *payload, uint32_t length)
{
    switch (msg_id) {
        case SCMI_PERF_DOMAIN_ATTRIBUTES_GET:
            {
                struct scmi_perf_domain_attr_req *req =
                    (struct scmi_perf_domain_attr_req *)payload;

                // 取得效能域屬性
                struct perf_domain_info *domain =
                    scp_get_perf_domain(req->domain_id);

                if (domain) {
                    // 準備回應
                    struct scmi_perf_domain_attr_resp resp = {
                        .status = SCMI_SUCCESS,
                        .attributes = domain->attributes,
                        .rate_limit_us = domain->rate_limit,
                        .sustained_freq_khz = domain->sustained_freq,
                        .sustained_perf_level = domain->sustained_perf
                    };

                    // 複製回應到共享記憶體
                    scp_write_response(&resp, sizeof(resp));
                } else {
                    scp_send_error_response(SCMI_ERR_ENTRY);
                }
            }
            break;

        case SCMI_PERF_LEVEL_SET:
            {
                struct scmi_perf_level_set_req *req =
                    (struct scmi_perf_level_set_req *)payload;

                // 設定效能等級
                int result = scp_set_performance_level(req->domain_id,
                                                      req->performance_level);

                if (result == 0) {
                    // 實際執行 DVFS 操作
                    scp_dvfs_set_frequency(req->domain_id, req->performance_level);
                    scp_dvfs_set_voltage(req->domain_id, req->performance_level);

                    scp_send_success_response();
                } else {
                    scp_send_error_response(SCMI_ERR_RANGE);
                }
            }
            break;

        case SCMI_PERF_LEVEL_GET:
            {
                struct scmi_perf_level_get_req *req =
                    (struct scmi_perf_level_get_req *)payload;

                uint32_t current_level = scp_get_current_perf_level(req->domain_id);

                struct scmi_perf_level_get_resp resp = {
                    .status = SCMI_SUCCESS,
                    .performance_level = current_level
                };

                scp_write_response(&resp, sizeof(resp));
            }
            break;
    }
}
```

### 4. 硬體層面的通信機制

#### MHU (Message Handling Unit) 操作
```c
// AP 端觸發 doorbell
static int mhu_send_signal(struct mbox_chan *chan, void *data)
{
    struct arm_mhu_chan *mhu_chan = chan->con_priv;

    // 寫入 doorbell 暫存器觸發中斷
    writel_relaxed(BIT(mhu_chan->index), mhu_chan->tx_reg);

    return 0;
}

// SCP 端接收中斷
void scp_mhu_interrupt_handler(void)
{
    uint32_t status = readl(MHU_STATUS_REG);

    if (status & MHU_CHANNEL_MASK) {
        // 清除中斷狀態
        writel(status, MHU_CLEAR_REG);

        // 處理 SCMI 訊息
        scp_scmi_mailbox_handler();
    }
}
```

### 5. 完整的呼叫時序圖

```
AP (Linux)                    Hardware                    SCP Firmware
    │                            │                            │
    │ 1. scmi_do_xfer()          │                            │
    │ ─────────────────────────► │                            │
    │                            │                            │
    │ 2. scmi_tx_prepare()       │                            │
    │ (寫入共享記憶體)            │                            │
    │ ─────────────────────────► │                            │
    │                            │                            │
    │ 3. mbox_send_message()     │                            │
    │ ─────────────────────────► │                            │
    │                            │                            │
    │                            │ 4. MHU doorbell           │
    │                            │ ─────────────────────────► │
    │                            │                            │
    │                            │                            │ 5. 中斷處理
    │                            │                            │ scp_mhu_interrupt_handler()
    │                            │                            │
    │                            │                            │ 6. 讀取共享記憶體
    │                            │                            │ 解析 SCMI 訊息
    │                            │                            │
    │                            │                            │ 7. 協議處理
    │                            │                            │ (DVFS/Clock/Power)
    │                            │                            │
    │                            │                            │ 8. 硬體操作
    │                            │                            │ (設定頻率/電壓)
    │                            │                            │
    │                            │                            │ 9. 準備回應
    │                            │                            │ 寫入共享記憶體
    │                            │                            │
    │                            │ 10. 回應中斷               │
    │                            │ ◄───────────────────────── │
    │                            │                            │
    │ 11. 中斷處理               │                            │
    │ scmi_rx_callback()         │                            │
    │ ◄───────────────────────── │                            │
    │                            │                            │
    │ 12. 讀取回應               │                            │
    │ scmi_fetch_response()      │                            │
    │ ─────────────────────────► │                            │
    │                            │                            │
    │ 13. 完成處理               │                            │
    │ complete(&xfer->done)      │                            │
```

### 6. SCP Firmware 中的實際硬體控制

#### DVFS 控制範例
```c
// SCP 中的 DVFS 實作
int scp_dvfs_set_frequency(uint32_t domain_id, uint32_t perf_level)
{
    struct perf_domain *domain = &perf_domains[domain_id];
    struct opp_entry *opp = &domain->opp_table[perf_level];

    // 1. 頻率提升時：先調電壓，再調頻率
    if (opp->frequency > domain->current_freq) {
        // 設定電壓
        pmic_set_voltage(domain->voltage_rail, opp->voltage);

        // 等待電壓穩定
        scp_delay_us(domain->voltage_settle_time);

        // 設定頻率
        pll_set_frequency(domain->clock_source, opp->frequency);
    }
    // 2. 頻率降低時：先調頻率，再調電壓
    else {
        // 設定頻率
        pll_set_frequency(domain->clock_source, opp->frequency);

        // 設定電壓
        pmic_set_voltage(domain->voltage_rail, opp->voltage);
    }

    // 更新當前狀態
    domain->current_freq = opp->frequency;
    domain->current_voltage = opp->voltage;

    return 0;
}
```

### 7. 關鍵特點

1. **異步通信**: AP 發送請求後可以等待中斷或輪詢
2. **共享記憶體**: 所有資料透過 SRAM 交換
3. **Doorbell 機制**: 使用 MHU 硬體中斷通知
4. **協議分層**: 不同功能使用不同協議 ID
5. **錯誤處理**: 完整的錯誤碼回報機制
6. **原子操作**: 確保訊息傳輸的完整性

這個流程確保了 AP 和 SCP 之間可靠、高效的通信，讓 Linux 能夠透過標準化的 SCMI 協議控制系統的各種硬體資源。

---

**版本**: 1.0
**更新日期**: 2024年
**作者**: Linux 內核開發團隊
