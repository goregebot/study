# SCMI Clock 通訊流程詳解

## 完整通訊架構

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           Application Processor (AP)                        │
├─────────────────────────────────────────────────────────────────────────────┤
│  User Space Application                                                     │
│  ┌─────────────────┐                                                        │
│  │   cpufreq-set   │ (設定 CPU 頻率到 1.2GHz)                               │
│  └─────────┬───────┘                                                        │
│            │                                                                │
│  ┌─────────▼───────┐                                                        │
│  │  CPUFreq Driver │                                                        │
│  └─────────┬───────┘                                                        │
│            │ clk_set_rate(clk, 1200000000)                                  │
│  ┌─────────▼───────┐                                                        │
│  │ SCMI Clock      │                                                        │
│  │ Driver          │ scmi_clk_set_rate()                                    │
│  └─────────┬───────┘                                                        │
│            │                                                                │
│  ┌─────────▼───────┐                                                        │
│  │ SCMI Framework  │ 準備 SCMI 訊息                                         │
│  └─────────┬───────┘                                                        │
│            │                                                                │
│  ┌─────────▼───────┐                                                        │
│  │ Mailbox/SMC     │ 硬體傳輸層                                             │
│  │ Transport       │                                                        │
│  └─────────┬───────┘                                                        │
└────────────┼─────────────────────────────────────────────────────────────────┘
             │ SCMI Message
             │ ┌─────────────────────────────────────────────────────────────┐
             │ │ Header: Protocol=0x14, Cmd=0x5, Seq=123                    │
             │ │ Payload: [Clock_ID=2][Rate_Low=0x47868C00][Rate_High=0x0]  │
             │ └─────────────────────────────────────────────────────────────┘
             ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│                      System Control Processor (SCP)                        │
├─────────────────────────────────────────────────────────────────────────────┤
│  ┌─────────────────┐                                                        │
│  │ Mailbox/SMC     │ 接收 SCMI 訊息                                         │
│  │ Handler         │                                                        │
│  └─────────┬───────┘                                                        │
│            │                                                                │
│  ┌─────────▼───────┐                                                        │
│  │ SCMI Framework  │ 解析訊息標頭                                           │
│  └─────────┬───────┘                                                        │
│            │                                                                │
│  ┌─────────▼───────┐                                                        │
│  │ SCMI Clock      │ scmi_clock_rate_set_handler()                          │
│  │ Protocol        │                                                        │
│  └─────────┬───────┘                                                        │
│            │ clock_api->set_rate(clock_id=2, rate=1200000000)               │
│  ┌─────────▼───────┐                                                        │
│  │ Clock Module    │                                                        │
│  └─────────┬───────┘                                                        │
│            │                                                                │
│  ┌─────────▼───────┐                                                        │
│  │ Hardware        │ 實際設定 PLL/時鐘分頻器                                │
│  │ Abstraction     │                                                        │
│  └─────────┬───────┘                                                        │
│            │                                                                │
│            │ 回應: Status = SUCCESS                                         │
│            ▼                                                                │
└─────────────────────────────────────────────────────────────────────────────┘
```

## 詳細通訊步驟

### 1. Linux Kernel 端 (AP)

#### 步驟 1: 應用程式請求
```bash
# 使用者空間應用程式
echo 1200000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed
```

#### 步驟 2: CPUFreq Driver 處理
```c
// drivers/cpufreq/cpufreq.c
static int cpufreq_set_policy(struct cpufreq_policy *policy, 
                             struct cpufreq_policy *new_policy)
{
    // ...
    clk_set_rate(policy->clk, new_policy->max * 1000);
    // ...
}
```

#### 步驟 3: SCMI Clock Driver 處理
```c
// 我們的 scmi_clock_example.c
static int scmi_clk_set_rate(struct clk_hw *hw, unsigned long rate,
                            unsigned long parent_rate)
{
    struct scmi_clk_data *clk = to_scmi_clk(hw);
    
    // 呼叫 SCMI 協議
    return clk->ops->rate_set(clk->ph, clk->id, (u64)rate);
}
```

#### 步驟 4: SCMI Framework 準備訊息
```c
// drivers/firmware/arm_scmi/clock.c
static int scmi_clock_rate_set(const struct scmi_protocol_handle *ph,
                              u32 clk_id, u64 rate)
{
    struct scmi_xfer *t;
    struct scmi_clock_set_rate *cfg;
    
    // 取得傳輸結構
    ph->xops->xfer_get_init(ph, CLOCK_RATE_SET, sizeof(*cfg), 0, &t);
    
    // 準備訊息內容
    cfg = t->tx.buf;
    cfg->flags = cpu_to_le32(0);
    cfg->id = cpu_to_le32(clk_id);
    cfg->value_low = cpu_to_le32(rate & 0xffffffff);
    cfg->value_high = cpu_to_le32(rate >> 32);
    
    // 傳送訊息
    return ph->xops->do_xfer(ph, t);
}
```

#### 步驟 5: 傳輸層處理
```c
// drivers/firmware/arm_scmi/mailbox.c
static int mailbox_send_message(struct scmi_chan_info *cinfo,
                               struct scmi_xfer *xfer)
{
    // 寫入共享記憶體
    shmem_tx_prepare(smbox->shmem, xfer, cinfo);
    
    // 觸發 mailbox 中斷
    mbox_send_message(smbox->chan, NULL);
    
    return 0;
}
```

### 2. SCP Firmware 端

#### 步驟 1: 中斷處理
```c
// SCP firmware mailbox handler
void mailbox_interrupt_handler(void)
{
    // 讀取共享記憶體
    struct scmi_message *msg = read_shared_memory();
    
    // 分派給 SCMI framework
    scmi_message_dispatch(msg);
}
```

#### 步驟 2: SCMI Framework 解析
```c
// SCP firmware SCMI framework
int scmi_message_dispatch(struct scmi_message *msg)
{
    uint8_t protocol_id = SCMI_MSG_PROTOCOL_ID(msg->header);
    uint8_t message_id = SCMI_MSG_MESSAGE_ID(msg->header);
    
    // 找到對應的協議處理器
    if (protocol_id == SCMI_PROTOCOL_CLOCK) {
        return scmi_clock_message_handler(msg->service_id, 
                                        msg->payload, 
                                        msg->payload_size,
                                        message_id);
    }
}
```

#### 步驟 3: Clock Protocol 處理
```c
// 我們的 scp_firmware_clock_handler.c
static int scmi_clock_rate_set_handler(fwk_id_t service_id, 
                                      const uint32_t *payload)
{
    const struct scmi_clock_rate_set_a2p *params = 
        (const struct scmi_clock_rate_set_a2p *)payload;
    
    uint32_t clock_id = params->clock_id;
    uint64_t rate = ((uint64_t)params->rate_high << 32) | params->rate_low;
    
    // 呼叫底層 Clock 模組
    int status = scmi_clock_ctx.clock_api->set_rate(clock_element_id, 
                                                   rate, 
                                                   MOD_CLOCK_ROUND_MODE_NEAREST);
    
    // 準備回應
    struct scmi_clock_rate_set_p2a response = {
        .status = (status == FWK_SUCCESS) ? SCMI_SUCCESS : SCMI_GENERIC_ERROR
    };
    
    // 傳送回應
    scmi_clock_ctx.scmi_api->respond(service_id, &response, sizeof(response));
    
    return FWK_SUCCESS;
}
```

#### 步驟 4: 硬體控制
```c
// SCP firmware clock module
int clock_set_rate(fwk_id_t clock_id, uint64_t rate)
{
    struct clock_dev_ctx *ctx = &clock_dev_ctx_table[fwk_id_get_element_idx(clock_id)];
    
    // 計算 PLL 參數
    uint32_t multiplier = rate / ctx->config->ref_rate;
    uint32_t divider = 1;
    
    // 設定 PLL 暫存器
    ctx->reg->PLL_CTRL = (multiplier << 8) | divider;
    
    // 等待 PLL 鎖定
    while (!(ctx->reg->PLL_STATUS & PLL_LOCK_BIT)) {
        // 等待
    }
    
    return FWK_SUCCESS;
}
```

## 訊息格式詳解

### SCMI 訊息標頭 (32-bit)
```
Bits [31:28] - Reserved
Bits [27:18] - Token (序列號)
Bits [17:10] - Protocol ID (0x14 for Clock)
Bits [9:8]   - Message Type (0=Command, 2=Response)
Bits [7:0]   - Message ID (0x5 for CLOCK_RATE_SET)
```

### Clock Rate Set 命令 Payload
```c
struct scmi_clock_rate_set_payload {
    uint32_t flags;      // Bit 0: Async, Bit 1: Ignore Response
    uint32_t clock_id;   // 時鐘 ID
    uint32_t rate_low;   // 頻率低 32 位元
    uint32_t rate_high;  // 頻率高 32 位元
};
```

### 實際範例訊息
設定 Clock ID 2 為 1.2GHz：
```
Header:    0x00F05014  (Token=123, Protocol=0x14, Type=0, Cmd=0x5)
Payload:   0x00000000  (flags = 0)
           0x00000002  (clock_id = 2)  
           0x47868C00  (rate_low = 1200000000 & 0xFFFFFFFF)
           0x00000000  (rate_high = 1200000000 >> 32)
```

## 錯誤處理

### Linux Kernel 端錯誤處理
```c
static int scmi_clk_set_rate(struct clk_hw *hw, unsigned long rate,
                            unsigned long parent_rate)
{
    int ret = clk->ops->rate_set(clk->ph, clk->id, (u64)rate);
    
    switch (ret) {
    case 0:
        return 0;
    case -EINVAL:
        dev_err(clk->ph->dev, "Invalid rate %lu for clock %s\n", 
                rate, clk->name);
        return -EINVAL;
    case -ERANGE:
        dev_err(clk->ph->dev, "Rate %lu out of range for clock %s\n", 
                rate, clk->name);
        return -ERANGE;
    default:
        dev_err(clk->ph->dev, "Failed to set rate for clock %s: %d\n", 
                clk->name, ret);
        return ret;
    }
}
```

### SCP Firmware 端錯誤處理
```c
static int scmi_clock_rate_set_handler(fwk_id_t service_id, 
                                      const uint32_t *payload)
{
    // ... 參數解析 ...
    
    int status = scmi_clock_ctx.clock_api->set_rate(clock_element_id, rate, 
                                                   MOD_CLOCK_ROUND_MODE_NEAREST);
    
    struct scmi_clock_rate_set_p2a response;
    
    switch (status) {
    case FWK_SUCCESS:
        response.status = SCMI_SUCCESS;
        break;
    case FWK_E_RANGE:
        response.status = SCMI_OUT_OF_RANGE;
        fwk_log_error("Rate %llu out of range for clock %u", rate, clock_id);
        break;
    case FWK_E_BUSY:
        response.status = SCMI_BUSY;
        fwk_log_error("Clock %u is busy", clock_id);
        break;
    default:
        response.status = SCMI_GENERIC_ERROR;
        fwk_log_error("Unknown error %d for clock %u", status, clock_id);
        break;
    }
    
    scmi_clock_ctx.scmi_api->respond(service_id, &response, sizeof(response));
    return FWK_SUCCESS;
}
```

## 效能最佳化

### 1. 非同步操作
```c
// Linux 端支援非同步設定
cfg->flags = cpu_to_le32(CLOCK_SET_ASYNC);
ret = ph->xops->do_xfer_with_response(ph, t);
```

### 2. 批次操作
```c
// 批次設定多個時鐘
for (i = 0; i < num_clocks; i++) {
    prepare_clock_set_message(i, rates[i]);
}
send_batch_messages();
```

### 3. 快取機制
```c
// 快取時鐘狀態避免重複查詢
static struct {
    uint64_t cached_rate;
    bool valid;
} clock_cache[MAX_CLOCKS];
```

## 除錯技巧

### 1. 啟用 SCMI Trace
```bash
echo 1 > /sys/kernel/debug/tracing/events/scmi/enable
cat /sys/kernel/debug/tracing/trace_pipe
```

### 2. 檢查共享記憶體
```bash
# 檢查 SCMI 裝置
ls -la /sys/bus/scmi/devices/
cat /sys/bus/scmi/devices/scmi_dev.0/modalias
```

### 3. SCP Firmware 日誌
```c
// 在 SCP firmware 中加入詳細日誌
fwk_log_info("[SCMI] Clock %u rate set: %llu -> %llu Hz", 
             clock_id, old_rate, new_rate);
```

這個完整的通訊流程展示了從使用者空間應用程式到實際硬體控制的每一個步驟，包括錯誤處理、效能最佳化和除錯技巧。