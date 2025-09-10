# clk_set_rate 數值流程分析

## 概述

`clk_set_rate` 設定的是**目標時鐘頻率**（以 Hz 為單位），這個值會經過多層轉換最終寫入硬體暫存器。

## 🔄 完整數據流程

```
User Space Application
        │
        ▼ (頻率值，例如：1500000000 Hz = 1.5GHz)
Linux Clock Framework (clk_set_rate)
        │
        ▼ (相同頻率值)
SCMI Clock Driver (scmi_clk_set_rate)
        │
        ▼ (SCMI 訊息：頻率值分成高低 32 位元)
SCMI Transport Layer (Mailbox/SMC)
        │
        ▼ (透過共享記憶體傳輸)
SCP Firmware SCMI Handler
        │
        ▼ (解析頻率值)
SCP Clock Module (計算 PLL 參數)
        │
        ▼ (PLL 倍頻/分頻參數)
Hardware Register Write (實際硬體控制)
```

## 📊 具體數值範例

### 1. Linux 端呼叫
```c
// 使用者想要設定 CPU 頻率為 1.5GHz
unsigned long target_freq = 1500000000;  // 1,500,000,000 Hz
int ret = clk_set_rate(cpu_clk, target_freq);
```

### 2. SCMI 訊息封裝
```c
// drivers/firmware/arm_scmi/clock.c
static int scmi_clock_rate_set(const struct scmi_protocol_handle *ph,
                              u32 clk_id, u64 rate)
{
    struct scmi_clock_set_rate *cfg;
    
    cfg->flags = cpu_to_le32(0);
    cfg->id = cpu_to_le32(clk_id);                    // 時鐘 ID = 0 (CPU0)
    cfg->value_low = cpu_to_le32(rate & 0xffffffff);  // 低 32 位元 = 0x59682F00
    cfg->value_high = cpu_to_le32(rate >> 32);        // 高 32 位元 = 0x00000000
    
    // 實際傳送的 SCMI 訊息：
    // Header: 0x00XX5014 (Protocol=0x14, Cmd=0x5)
    // Payload: [0x00000000][0x00000000][0x59682F00][0x00000000]
    //          [  flags  ][clock_id=0][rate_low ][rate_high]
}
```

### 3. SCP Firmware 解析
```c
// SCP firmware 端
static int scmi_clock_rate_set_handler(fwk_id_t service_id, 
                                      const uint32_t *payload)
{
    const struct scmi_clock_rate_set_a2p *params = 
        (const struct scmi_clock_rate_set_a2p *)payload;
    
    uint32_t clock_id = le32_to_cpu(params->id);           // = 0
    uint64_t rate = ((uint64_t)le32_to_cpu(params->rate_high) << 32) | 
                    le32_to_cpu(params->rate_low);         // = 1500000000
    
    // 呼叫底層時鐘模組
    return clock_api->set_rate(clock_element_id, rate, MOD_CLOCK_ROUND_MODE_NEAREST);
}
```### 4. 
PLL 參數計算
```c
// SCP firmware 時鐘驅動
static int calculate_pll_params(uint64_t target_rate, uint32_t ref_rate,
                               uint32_t *multiplier, uint32_t *divider)
{
    // 目標：1,500,000,000 Hz
    // 參考時鐘：24,000,000 Hz (24MHz 晶振)
    
    // PLL 公式：Output = (Reference × Multiplier) / Divider
    // 1,500,000,000 = (24,000,000 × M) / D
    
    // 計算結果：
    // Multiplier = 125, Divider = 2
    // 驗證：(24,000,000 × 125) / 2 = 3,000,000,000 / 2 = 1,500,000,000 ✓
    
    *multiplier = 125;
    *divider = 2;
    
    return FWK_SUCCESS;
}

static int set_pll_rate(struct clock_dev_ctx *ctx, uint64_t rate)
{
    uint32_t multiplier, divider;
    uint32_t reg_val;
    
    // 1. 計算 PLL 參數
    calculate_pll_params(rate, 24000000, &multiplier, &divider);
    
    // 2. 停用 PLL
    ctx->reg->PLL_CTRL &= ~PLL_CTRL_ENABLE;
    
    // 3. 設定新的 PLL 參數到暫存器
    reg_val = ctx->reg->PLL_CTRL;
    reg_val &= ~(PLL_CTRL_MULTIPLIER_MSK | PLL_CTRL_DIVIDER_MSK);
    
    // 將計算出的參數寫入暫存器
    reg_val |= (multiplier << PLL_CTRL_MULTIPLIER_POS) & PLL_CTRL_MULTIPLIER_MSK;  // 125 << 8
    reg_val |= (divider << PLL_CTRL_DIVIDER_POS) & PLL_CTRL_DIVIDER_MSK;          // 2 << 20
    
    ctx->reg->PLL_CTRL = reg_val;  // 實際寫入硬體暫存器！
    
    // 4. 啟用 PLL
    ctx->reg->PLL_CTRL |= PLL_CTRL_ENABLE;
    
    // 5. 等待 PLL 鎖定
    while (!(ctx->reg->PLL_STATUS & PLL_STATUS_LOCKED)) {
        // 等待硬體 PLL 穩定
    }
    
    return FWK_SUCCESS;
}
```

## 🔧 硬體暫存器實際數值

### PLL 控制暫存器 (PLL_CTRL) 範例
```
暫存器地址：0x50000000 (假設)
暫存器位元配置：

Bits [31:29] - Reserved
Bits [28:20] - DIVIDER    (9 bits) = 2      = 0b000000010
Bits [19:8]  - MULTIPLIER (12 bits) = 125   = 0b001111101
Bits [7:2]   - Reserved  
Bit  [1]     - BYPASS    = 0 (不旁路)
Bit  [0]     - ENABLE    = 1 (啟用)

實際寫入的 32 位元值：
0x00207D01 = 0b00000000001000000111110100000001
                    │      │        │
                    │      │        └─ ENABLE=1
                    │      └─ MULTIPLIER=125
                    └─ DIVIDER=2
```

### 暫存器寫入順序
```c
// 實際的硬體暫存器操作
void write_pll_registers(uint32_t base_addr, uint32_t multiplier, uint32_t divider)
{
    volatile uint32_t *pll_ctrl = (volatile uint32_t *)(base_addr + 0x00);
    volatile uint32_t *pll_status = (volatile uint32_t *)(base_addr + 0x04);
    
    // 1. 停用 PLL
    *pll_ctrl &= ~0x1;  // 清除 ENABLE 位元
    
    // 2. 設定倍頻和分頻參數
    uint32_t reg_val = *pll_ctrl;
    reg_val &= ~0x1FFFF00;  // 清除 MULTIPLIER 和 DIVIDER 位元
    reg_val |= (multiplier << 8) & 0xFFF00;    // 設定 MULTIPLIER
    reg_val |= (divider << 20) & 0x1FF00000;   // 設定 DIVIDER
    *pll_ctrl = reg_val;
    
    // 3. 啟用 PLL
    *pll_ctrl |= 0x1;  // 設定 ENABLE 位元
    
    // 4. 等待 PLL 鎖定
    while ((*pll_status & 0x1) == 0) {
        // 等待 LOCKED 位元變為 1
    }
}
```

## 📈 頻率驗證

### 實際輸出頻率計算
```c
// 驗證實際輸出頻率
uint64_t calculate_actual_frequency(uint32_t ref_freq, uint32_t multiplier, uint32_t divider)
{
    // PLL 公式：Output = (Reference × Multiplier) / Divider
    uint64_t output_freq = ((uint64_t)ref_freq * multiplier) / divider;
    
    // 範例計算：
    // ref_freq = 24,000,000 Hz
    // multiplier = 125
    // divider = 2
    // output_freq = (24,000,000 × 125) / 2 = 1,500,000,000 Hz
    
    return output_freq;
}

// 讀取實際硬體頻率
uint64_t read_actual_pll_frequency(uint32_t base_addr)
{
    volatile uint32_t *pll_ctrl = (volatile uint32_t *)(base_addr + 0x00);
    
    uint32_t reg_val = *pll_ctrl;
    uint32_t multiplier = (reg_val >> 8) & 0xFFF;    // 提取 MULTIPLIER
    uint32_t divider = (reg_val >> 20) & 0x1FF;      // 提取 DIVIDER
    
    return calculate_actual_frequency(24000000, multiplier, divider);
}
```## 🔍 實際範
例：完整追蹤

### 使用者操作
```bash
# 使用者設定 CPU 頻率為 1.5GHz
echo 1500000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_setspeed
```

### 1. CPUFreq 子系統
```c
// drivers/cpufreq/cpufreq.c
static int cpufreq_set_policy(struct cpufreq_policy *policy, 
                             struct cpufreq_policy *new_policy)
{
    // 轉換 kHz 到 Hz
    unsigned long rate_hz = new_policy->max * 1000;  // 1500000 * 1000 = 1500000000
    
    return clk_set_rate(policy->clk, rate_hz);  // 呼叫 Clock Framework
}
```

### 2. Clock Framework
```c
// drivers/clk/clk.c
int clk_set_rate(struct clk *clk, unsigned long rate)
{
    // rate = 1500000000 Hz
    return clk_core_set_rate_nolock(clk->core, rate);
}

static int clk_core_set_rate_nolock(struct clk_core *core, unsigned long rate)
{
    // 呼叫 SCMI clock driver 的 set_rate 操作
    return core->ops->set_rate(core->hw, rate, core->parent->rate);
}
```

### 3. SCMI Clock Driver
```c
// 我們的 scmi_clock_example.c
static int scmi_clk_set_rate(struct clk_hw *hw, unsigned long rate,
                            unsigned long parent_rate)
{
    struct scmi_clk_data *clk = to_scmi_clk(hw);
    
    // rate = 1500000000 Hz
    // 透過 SCMI 協議傳送給 SCP
    return clk->ops->rate_set(clk->ph, clk->id, (u64)rate);
}
```

### 4. SCMI 協議層
```c
// drivers/firmware/arm_scmi/clock.c
static int scmi_clock_rate_set(const struct scmi_protocol_handle *ph,
                              u32 clk_id, u64 rate)
{
    struct scmi_clock_set_rate *cfg;
    
    // 準備 SCMI 訊息
    cfg->flags = cpu_to_le32(0);
    cfg->id = cpu_to_le32(clk_id);                    // 0 (CPU0)
    cfg->value_low = cpu_to_le32(rate & 0xffffffff);  // 0x59682F00
    cfg->value_high = cpu_to_le32(rate >> 32);        // 0x00000000
    
    // 透過 mailbox 傳送到 SCP
    return ph->xops->do_xfer(ph, t);
}
```

### 5. SCP Firmware 接收
```c
// SCP firmware SCMI clock handler
static int scmi_clock_rate_set_handler(fwk_id_t service_id, 
                                      const uint32_t *payload)
{
    // 解析 SCMI 訊息
    uint32_t clock_id = le32_to_cpu(params->id);           // 0
    uint64_t rate = ((uint64_t)le32_to_cpu(params->rate_high) << 32) | 
                    le32_to_cpu(params->rate_low);         // 1500000000
    
    // 呼叫平台時鐘驅動
    return clock_api->set_rate(clock_element_id, rate, MOD_CLOCK_ROUND_MODE_NEAREST);
}
```

### 6. 平台時鐘驅動
```c
// 我們的 mod_myplatform_clock.c
static int myplatform_clock_set_rate(fwk_id_t clock_id, uint64_t rate, 
                                    enum mod_clock_round_mode round_mode)
{
    // rate = 1500000000 Hz
    
    // 計算 PLL 參數
    uint32_t multiplier = 125;  // 從 calculate_pll_params 得到
    uint32_t divider = 2;       // 從 calculate_pll_params 得到
    
    // 寫入硬體暫存器
    return set_pll_registers(ctx->reg_base, multiplier, divider);
}
```

### 7. 硬體暫存器寫入
```c
static int set_pll_registers(uint32_t base_addr, uint32_t multiplier, uint32_t divider)
{
    volatile uint32_t *pll_ctrl = (volatile uint32_t *)base_addr;
    
    // 實際的硬體操作
    *pll_ctrl = 0x00000000;        // 停用 PLL
    *pll_ctrl = 0x00207D00;        // 設定參數 (M=125, D=2, ENABLE=0)
    *pll_ctrl = 0x00207D01;        // 啟用 PLL (ENABLE=1)
    
    // 等待 PLL 穩定...
    
    return FWK_SUCCESS;
}
```

## 📊 數值轉換總結

| 階段 | 數值格式 | 實際數值 | 說明 |
|------|----------|----------|------|
| 使用者輸入 | kHz | 1500000 | CPUFreq 使用 kHz 單位 |
| Linux Clock | Hz | 1500000000 | Clock Framework 使用 Hz |
| SCMI 訊息 | 64-bit split | Low:0x59682F00, High:0x00000000 | 分成兩個 32-bit 傳輸 |
| SCP 解析 | Hz | 1500000000 | 重組 64-bit 頻率值 |
| PLL 計算 | 倍頻/分頻 | M=125, D=2 | 硬體參數 |
| 暫存器值 | 32-bit hex | 0x00207D01 | 實際寫入硬體的值 |

## ✅ 驗證方法

### 1. Linux 端驗證
```bash
# 檢查設定的頻率
cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq
# 輸出：1500000 (kHz)

# 檢查實際時鐘頻率
cat /sys/kernel/debug/clk/scmi_clk_0/clk_rate  
# 輸出：1500000000 (Hz)
```

### 2. SCP 端驗證
```c
// 在 SCP firmware 中讀取暫存器驗證
uint32_t reg_val = *pll_ctrl_reg;
uint32_t multiplier = (reg_val >> 8) & 0xFFF;    // 應該是 125
uint32_t divider = (reg_val >> 20) & 0x1FF;      // 應該是 2
uint64_t actual_freq = (24000000ULL * multiplier) / divider;  // 應該是 1500000000
```

### 3. 硬體測量
```bash
# 使用示波器或頻率計測量實際輸出
# 應該測得接近 1.5GHz 的時鐘信號
```

## 🎯 關鍵要點

1. **數值保真度**：從使用者輸入到硬體暫存器，頻率數值經過多次轉換但保持一致性
2. **單位轉換**：注意 kHz (CPUFreq) 和 Hz (Clock Framework) 的轉換
3. **硬體抽象**：SCMI 協議隱藏了底層 PLL 計算的複雜性
4. **參數計算**：最終的暫存器值是經過 PLL 公式計算得出的硬體參數
5. **實際控制**：`clk_set_rate` 確實會寫入硬體暫存器來控制實際的時鐘頻率

所以回答你的問題：**是的，`clk_set_rate` 設定的頻率值最終會轉換成 PLL 參數並寫入硬體暫存器，實際控制硬體時鐘頻率**。