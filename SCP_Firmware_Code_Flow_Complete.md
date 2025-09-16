# SCP Firmware Code Flow 完整分析

## 概述

本文檔詳細分析 SCP (System Control Processor) firmware 的代碼執行流程，從 SCP-RAM 啟動開始，經過關鍵模組的初始化過程，包括 PPU v1、System PLL、PIK Clock 等模組的初始化和配置流程。

## 1. SCP-RAM 啟動流程

### 1.1 系統啟動入口

SCP firmware 的啟動流程從 `fwk_arch_init()` 開始：

```c
// framework/src/fwk_arch.c
int fwk_arch_init(const struct fwk_arch_init_driver *driver)
{
    // 1. 模組系統初始化
    fwk_module_init();
    
    // 2. I/O 系統初始化
    status = fwk_io_init();
    
    // 3. 日誌系統初始化
    status = fwk_log_init();
    
    // 4. 中斷管理初始化
    status = fwk_arch_interrupt_init(driver->interrupt);
    
    // 5. 啟動所有模組
    status = fwk_module_start();
    
    // 6. 進入主事件循環
    #if defined(BUILD_HAS_SUB_SYSTEM_MODE)
        fwk_process_event_queue();
        fwk_log_flush();
    #else
        __fwk_run_main_loop();
    #endif
}
```

### 1.2 Framework 初始化階段

Framework 按照以下順序初始化各個模組：

1. **Module Initialization** - 模組初始化
2. **Element Initialization** - 元素初始化  
3. **Post-Initialization** - 後初始化
4. **Bind** - 模組間綁定
5. **Start** - 啟動階段

## 2. PPU v1 模組初始化流程

### 2.1 PPU v1 初始化入口

```c
// module/ppu_v1/src/mod_ppu_v1.c
static int ppu_v1_init(fwk_id_t module_id, unsigned int pd_count, 
                       const void *data)
{
    // 分配模組上下文記憶體
    ppu_v1_ctx.pd_ctx_table = fwk_mm_calloc(pd_count, 
                                            sizeof(struct ppu_v1_pd_ctx));
    
    // 設定電源域數量
    ppu_v1_ctx.pd_ctx_table_size = pd_count;
    
    return FWK_SUCCESS;
}
```

### 2.2 PPU v1 元素初始化

```c
static int ppu_v1_element_init(fwk_id_t pd_id, unsigned int unused,
                               const void *data)
{
    struct ppu_v1_pd_ctx *pd_ctx;
    const struct mod_ppu_v1_pd_config *config = data;
    
    // 獲取電源域上下文
    pd_ctx = ppu_v1_ctx.pd_ctx_table + fwk_id_get_element_idx(pd_id);
    
    // 設定配置和 PPU 暫存器基址
    pd_ctx->config = config;
    pd_ctx->ppu = (struct ppu_v1_reg *)config->ppu.reg_base;
    
    // 如果需要預設開機，則設定電源狀態
    if (config->default_power_on) {
        return ppu_v1_set_power_mode(pd_ctx->ppu, PPU_V1_MODE_ON, NULL);
    }
    
    return FWK_SUCCESS;
}
```

### 2.3 PPU v1 電源狀態設定

```c
static int ppu_v1_pd_set_state(fwk_id_t pd_id, unsigned int state)
{
    struct ppu_v1_pd_ctx *pd_ctx;
    
    pd_ctx = ppu_v1_ctx.pd_ctx_table + fwk_id_get_element_idx(pd_id);
    
    switch (state) {
    case MOD_PD_STATE_ON:
        // 設定 PPU 為 ON 模式
        status = ppu_v1_set_power_mode(pd_ctx->ppu, PPU_V1_MODE_ON, 
                                       pd_ctx->timer_ctx);
        break;
        
    case MOD_PD_STATE_OFF:
        // 設定 PPU 為 OFF 模式
        status = ppu_v1_set_power_mode(pd_ctx->ppu, PPU_V1_MODE_OFF, 
                                       pd_ctx->timer_ctx);
        break;
    }
    
    // 回報電源狀態轉換
    return pd_ctx->pd_driver_input_api->report_power_state_transition(
        pd_ctx->bound_id, state);
}
```

## 3. System PLL 模組初始化流程

### 3.1 System PLL 模組初始化

```c
// module/system_pll/src/mod_system_pll.c
static int system_pll_init(fwk_id_t module_id, unsigned int element_count,
                           const void *data)
{
    // 分配設備上下文表
    module_ctx.dev_ctx_table = fwk_mm_calloc(element_count, 
                                             sizeof(struct system_pll_dev_ctx));
    module_ctx.dev_count = element_count;
    
    return FWK_SUCCESS;
}
```

### 3.2 System PLL 元素初始化

```c
static int system_pll_element_init(fwk_id_t element_id, unsigned int unused,
                                   const void *data)
{
    struct system_pll_dev_ctx *ctx;
    const struct mod_system_pll_dev_config *config = data;
    
    // 獲取設備上下文
    ctx = module_ctx.dev_ctx_table + fwk_id_get_element_idx(element_id);
    
    // 設定配置和初始狀態
    ctx->config = config;
    ctx->initialized = true;
    ctx->current_state = MOD_CLOCK_STATE_RUNNING;
    
    // 設定初始頻率
    return system_pll_set_rate(element_id, ctx->config->initial_rate,
                               MOD_CLOCK_ROUND_MODE_NONE);
}
```

### 3.3 System PLL 頻率設定

```c
static int system_pll_set_rate(fwk_id_t dev_id, uint64_t rate,
                               enum mod_clock_round_mode round_mode)
{
    struct system_pll_dev_ctx *ctx;
    uint64_t rounded_rate;
    unsigned int picoseconds;
    
    ctx = module_ctx.dev_ctx_table + fwk_id_get_element_idx(dev_id);
    
    // 檢查電源狀態
    if (ctx->current_state == MOD_CLOCK_STATE_STOPPED)
        return FWK_E_PWRSTATE;
    
    // 頻率對齊處理
    if ((rate % ctx->config->min_step) > 0) {
        switch (round_mode) {
        case MOD_CLOCK_ROUND_MODE_NEAREST:
            // 選擇最接近的頻率
            rounded_rate = FWK_ALIGN_PREVIOUS(rate, ctx->config->min_step);
            break;
        case MOD_CLOCK_ROUND_MODE_DOWN:
            rounded_rate = FWK_ALIGN_PREVIOUS(rate, ctx->config->min_step);
            break;
        case MOD_CLOCK_ROUND_MODE_UP:
            rounded_rate = FWK_ALIGN_NEXT(rate, ctx->config->min_step);
            break;
        }
    } else {
        rounded_rate = rate;
    }
    
    // 頻率範圍檢查
    if (rounded_rate < ctx->config->min_rate || 
        rounded_rate > ctx->config->max_rate)
        return FWK_E_RANGE;
    
    // 轉換為半週期皮秒值
    picoseconds = freq_to_half_cycle_ps(rounded_rate);
    
    // 寫入控制暫存器
    *ctx->config->control_reg = picoseconds;
    
    // 等待 PLL 鎖定
    if (ctx->config->status_reg != NULL) {
        while ((*ctx->config->status_reg & ctx->config->lock_flag_mask) == 0)
            continue;
    }
    
    // 更新當前頻率
    ctx->current_rate = rounded_rate;
    
    return FWK_SUCCESS;
}
```

## 4. CSS Clock 模組初始化流程

### 4.1 CSS Clock 模組初始化

```c
// module/css_clock/src/mod_css_clock.c
static int css_clock_init(fwk_id_t module_id, unsigned int element_count,
                          const void *data)
{
    // 分配設備上下文表
    module_ctx.dev_ctx_table = fwk_mm_calloc(element_count, 
                                             sizeof(struct css_clock_dev_ctx));
    module_ctx.dev_count = element_count;
    
    return FWK_SUCCESS;
}
```

### 4.2 CSS Clock 元素初始化

```c
static int css_clock_element_init(fwk_id_t element_id, unsigned int unused,
                                  const void *data)
{
    struct css_clock_dev_ctx *ctx;
    const struct mod_css_clock_dev_config *config = data;
    
    ctx = module_ctx.dev_ctx_table + fwk_id_get_element_idx(element_id);
    
    // 設定配置
    ctx->config = config;
    ctx->initialized = true;
    ctx->current_rate = config->initial_rate;
    
    return FWK_SUCCESS;
}
```

### 4.3 CSS Clock 頻率設定機制

CSS Clock 支援兩種類型的時鐘設定：

#### 4.3.1 索引型時鐘 (Indexed Clock)

```c
static int set_rate_indexed(struct css_clock_dev_ctx *ctx, uint64_t rate,
                            enum mod_clock_round_mode round_mode)
{
    int status;
    unsigned int i;
    struct mod_css_clock_rate *rate_entry;
    
    // 1. 查找頻率表項目
    status = get_rate_entry(ctx, rate, &rate_entry);
    if (status != FWK_SUCCESS)
        goto exit;
    
    // 2. 將成員時鐘切換到備用時鐘源
    for (i = 0; i < ctx->config->member_count; i++) {
        status = ctx->clock_api->set_source(ctx->config->member_table[i],
            ctx->config->clock_switching_source);
        if (status != FWK_SUCCESS)
            goto exit;
        
        // 3. 設定分頻器
        status = ctx->clock_api->set_div(ctx->config->member_table[i],
                                         rate_entry->clock_div_type,
                                         rate_entry->clock_div);
        if (status != FWK_SUCCESS)
            goto exit;
        
        // 4. 設定調變器（如果支援）
        if (ctx->config->modulation_supported) {
            status = ctx->clock_api->set_mod(ctx->config->member_table[i],
                                             rate_entry->clock_mod_numerator,
                                             rate_entry->clock_mod_denominator);
        }
    }
    
    // 5. 改變 PLL 到目標頻率
    status = ctx->pll_api->set_rate(ctx->config->pll_id, rate_entry->pll_rate,
                                    MOD_CLOCK_ROUND_MODE_NONE);
    
    // 6. 將成員時鐘切回 PLL 時鐘源
    for (i = 0; i < ctx->config->member_count; i++) {
        status = ctx->clock_api->set_source(ctx->config->member_table[i],
                                            rate_entry->clock_source);
    }
    
exit:
    if (status == FWK_SUCCESS)
        ctx->current_rate = rate;
    return status;
}
```

#### 4.3.2 非索引型時鐘 (Non-Indexed Clock)

```c
static int set_rate_non_indexed(struct css_clock_dev_ctx *ctx, uint64_t rate,
                                enum mod_clock_round_mode round_mode)
{
    int status;
    unsigned int i;
    
    // 1. 將成員時鐘切換到備用時鐘源
    for (i = 0; i < ctx->config->member_count; i++) {
        status = ctx->clock_api->set_source(ctx->config->member_table[i],
            ctx->config->clock_switching_source);
    }
    
    // 2. 直接改變 PLL 頻率
    status = ctx->pll_api->set_rate(ctx->config->pll_id, rate, round_mode);
    
    // 3. 將成員時鐘切回預設時鐘源
    for (i = 0; i < ctx->config->member_count; i++) {
        status = ctx->clock_api->set_source(ctx->config->member_table[i],
                                            ctx->config->clock_default_source);
    }
    
    if (status == FWK_SUCCESS)
        ctx->current_rate = rate;
    return status;
}
```

## 5. Clock HAL 模組初始化流程

### 5.1 Clock HAL 模組初始化

```c
// module/clock/src/mod_clock.c
static int clock_init(fwk_id_t module_id, unsigned int element_count,
                      const void *data)
{
    // 分配設備上下文表
    mod_clock_ctx.dev_ctx_table = fwk_mm_calloc(element_count, 
                                                sizeof(struct clock_dev_ctx));
    mod_clock_ctx.dev_count = element_count;
    
    return FWK_SUCCESS;
}
```

### 5.2 Clock HAL 元素初始化

```c
static int clock_element_init(fwk_id_t element_id, unsigned int unused,
                              const void *data)
{
    struct clock_dev_ctx *ctx;
    const struct mod_clock_dev_config *config = data;
    
    ctx = &mod_clock_ctx.dev_ctx_table[fwk_id_get_element_idx(element_id)];
    
    // 設定配置
    ctx->config = config;
    
    return FWK_SUCCESS;
}
```

### 5.3 Clock HAL API 實現

```c
static int clock_set_rate(fwk_id_t clock_id, uint64_t rate,
                          enum mod_clock_round_mode round_mode)
{
    int status;
    struct clock_dev_ctx *ctx;
    
    clock_get_ctx(clock_id, &ctx);
    
    // 檢查並發性
    if (ctx->request.is_ongoing) {
        return FWK_E_BUSY;
    }
    
    // 調用底層驅動 API
    status = ctx->api->set_rate(ctx->config->driver_id, rate, round_mode);
    
    // 處理異步請求
    if (status == FWK_PENDING) {
        return create_async_request(ctx, clock_id, 
                                   mod_clock_event_id_set_rate_request);
    }
    
    return status;
}
```

## 6. PIK Clock 模組初始化流程

### 6.1 PIK Clock 初始化

```c
// module/pik_clock/src/mod_pik_clock.c
static int pik_clock_init(fwk_id_t module_id, unsigned int element_count,
                          const void *data)
{
    // 分配設備上下文表
    module_ctx.dev_ctx_table = fwk_mm_calloc(element_count, 
                                             sizeof(struct pik_clock_dev_ctx));
    module_ctx.dev_count = element_count;
    
    // 設定最大分頻器值
    module_ctx.divider_max = PIK_CLOCK_DIVIDER_MAX;
    
    return FWK_SUCCESS;
}
```

### 6.2 PIK Clock 元素初始化

```c
static int pik_clock_element_init(fwk_id_t element_id, unsigned int unused,
                                  const void *data)
{
    struct pik_clock_dev_ctx *ctx;
    const struct mod_pik_clock_dev_config *config = data;
    
    ctx = module_ctx.dev_ctx_table + fwk_id_get_element_idx(element_id);
    
    // 設定配置
    ctx->config = config;
    ctx->initialized = true;
    ctx->current_state = MOD_CLOCK_STATE_STOPPED;
    
    // 根據時鐘類型進行初始化
    switch (config->type) {
    case MOD_PIK_CLOCK_TYPE_SINGLE_SOURCE:
        // 單一來源時鐘初始化
        break;
    case MOD_PIK_CLOCK_TYPE_MULTI_SOURCE:
        // 多來源時鐘初始化
        break;
    case MOD_PIK_CLOCK_TYPE_CLUSTER:
        // 叢集時鐘初始化
        break;
    }
    
    return FWK_SUCCESS;
}
```

### 4.3 CSS Clock 頻率設定

### 6.3 PIK Clock 頻率設定

當需要設定 PIK Clock 頻率時，會調用：

```c
static int css_clock_set_rate(fwk_id_t clock_id, uint64_t rate,
                              enum mod_clock_round_mode round_mode)
{
    struct pik_clock_dev_ctx *ctx;
    struct mod_pik_clock_rate *rate_entry;
    
    ctx = module_ctx.dev_ctx_table + fwk_id_get_element_idx(clock_id);
    
    // 查找頻率表項目
    status = get_rate_entry(ctx, rate, &rate_entry);
    if (status != FWK_SUCCESS)
        return status;
    
    // 根據時鐘類型設定頻率
    switch (ctx->config->type) {
    case MOD_PIK_CLOCK_TYPE_SINGLE_SOURCE:
        return ssclock_set_div(ctx, rate_entry->divider, true);
        
    case MOD_PIK_CLOCK_TYPE_MULTI_SOURCE:
        // 設定多來源時鐘分頻器和來源
        status = msclock_set_source(ctx, rate_entry->source);
        if (status == FWK_SUCCESS) {
            status = msclock_set_div(ctx, rate_entry->divider_reg, 
                                    rate_entry->divider, true);
        }
        break;
    }
    
    if (status == FWK_SUCCESS) {
        ctx->current_rate = rate_entry->rate;
        ctx->current_source = rate_entry->source;
    }
    
    return status;
}
```

## 7. 模組間互動流程與時鐘層次結構

### 7.1 時鐘系統架構層次

SCP firmware 的時鐘系統採用分層架構：

```
┌─────────────────────────────────────────────────────────────┐
│                    Clock HAL (統一介面)                      │
├─────────────────────────────────────────────────────────────┤
│  CSS Clock (CPU群組時鐘)  │  PIK Clock (系統時鐘)  │  其他時鐘  │
├─────────────────────────────────────────────────────────────┤
│                    System PLL (時鐘源)                      │
├─────────────────────────────────────────────────────────────┤
│                    PPU v1 (電源域管理)                      │
└─────────────────────────────────────────────────────────────┘
```

### 7.2 時鐘配置示例 (TC2 平台)

#### 7.2.1 CSS Clock 配置

```c
// CPU Cortex-A520 群組時鐘配置
static const struct mod_css_clock_rate rate_table_cpu_group_cortex_a520[5] = {
    {
        .rate = 768 * FWK_MHZ,          // 目標頻率
        .pll_rate = 768 * FWK_MHZ,      // PLL 頻率
        .clock_source = MOD_PIK_CLOCK_CLUSCLK_SOURCE_TC2_PLL0,  // 時鐘源
        .clock_div_type = MOD_PIK_CLOCK_MSCLOCK_DIVIDER_DIV_EXT, // 分頻器類型
        .clock_div = 1,                 // 分頻比
        .clock_mod_numerator = 1,       // 調變器分子
        .clock_mod_denominator = 1,     // 調變器分母
    },
    // ... 其他頻率點
};
```

#### 7.2.2 Clock HAL 配置

```c
// Clock HAL 設備描述表
static const struct fwk_element clock_dev_desc_table[CLOCK_IDX_COUNT + 1] = {
    [CLOCK_IDX_CPU_GROUP_CORTEX_A520] = {
        .name = "CPU_GROUP_CORTEX_A520",
        .data = &((struct mod_clock_dev_config){
            .driver_id = FWK_ID_ELEMENT_INIT(
                FWK_MODULE_IDX_CSS_CLOCK,
                CLOCK_CSS_IDX_CPU_GROUP_CORTEX_A520),  // 指向 CSS Clock
            .api_id = FWK_ID_API_INIT(
                FWK_MODULE_IDX_CSS_CLOCK,
                MOD_CSS_CLOCK_API_TYPE_CLOCK),
        }),
    },
    [CLOCK_IDX_PIXEL_0] = {
        .name = "PIXEL_0",
        .data = &((struct mod_clock_dev_config){
            .driver_id = FWK_ID_ELEMENT_INIT(
                FWK_MODULE_IDX_SYSTEM_PLL,
                CLOCK_PLL_IDX_PIX0),                   // 直接指向 System PLL
            .api_id = FWK_ID_API_INIT(
                FWK_MODULE_IDX_SYSTEM_PLL,
                MOD_SYSTEM_PLL_API_TYPE_DEFAULT),
        }),
    },
    // ...
};
```

## 8. 模組間互動流程

### 8.1 電源域與時鐘的協調

```
Clock HAL (統一介面)
    ↓
CSS Clock / PIK Clock (時鐘控制) ←→ System PLL (時鐘源)
    ↓                                    ↓
PIK Clock (硬體控制) ←→ PPU v1 (電源域管理)
    ↓                        ↓
硬體暫存器              電源狀態控制
```

### 8.2 完整的時鐘設定流程

#### 8.2.1 CPU 時鐘頻率調整流程

```
SCMI 請求 → Clock HAL → CSS Clock → PIK Clock → System PLL
    ↓           ↓           ↓           ↓           ↓
  協議解析   統一介面   群組管理   硬體控制   頻率產生
```

具體步驟：
1. **SCMI 協議接收**頻率調整請求
2. **Clock HAL** 提供統一的時鐘介面
3. **CSS Clock** 管理 CPU 群組時鐘，執行安全的頻率切換
4. **PIK Clock** 控制硬體暫存器，設定分頻器和時鐘源
5. **System PLL** 產生所需的基礎頻率

#### 8.2.2 頻率切換安全機制

```c
// CSS Clock 安全頻率切換流程
1. 切換到備用時鐘源 (REFCLK)
   └─ 確保 CPU 在頻率切換期間仍有穩定時鐘
   
2. 設定分頻器和調變器
   └─ 預先配置好目標頻率的分頻比
   
3. 調整 PLL 頻率
   └─ System PLL 產生新的目標頻率
   
4. 等待 PLL 鎖定
   └─ 確保新頻率穩定
   
5. 切換回 PLL 時鐘源
   └─ CPU 開始使用新頻率運行
```

### 8.3 初始化順序

1. **Framework 初始化**
   - 記憶體管理初始化
   - 中斷系統初始化
   - 日誌系統初始化

2. **模組初始化階段**
   - PPU v1 模組初始化
   - System PLL 模組初始化  
   - PIK Clock 模組初始化
   - CSS Clock 模組初始化
   - Clock HAL 模組初始化

3. **元素初始化階段**
   - 各電源域 PPU 初始化
   - 各 PLL 設定初始頻率
   - 各 PIK Clock 配置
   - 各 CSS Clock 群組配置
   - Clock HAL 設備註冊

4. **綁定階段**
   - 模組間 API 綁定
   - 建立依賴關係

5. **啟動階段**
   - 啟動各模組服務
   - 進入運行時事件處理

## 9. 關鍵資料結構

### 9.1 CSS Clock 配置結構

```c
struct mod_css_clock_dev_config {
    enum mod_css_clock_type clock_type;          // 時鐘類型 (索引/非索引)
    uint8_t clock_default_source;                // 預設時鐘源
    uint8_t clock_switching_source;              // 切換時使用的時鐘源
    fwk_id_t pll_id;                            // 關聯的 PLL ID
    fwk_id_t pll_api_id;                        // PLL API ID
    fwk_id_t const *member_table;               // 成員時鐘表
    uint32_t member_count;                      // 成員時鐘數量
    fwk_id_t member_api_id;                     // 成員時鐘 API ID
    uint64_t initial_rate;                      // 初始頻率
    bool modulation_supported;                  // 是否支援調變
    struct mod_css_clock_rate const *rate_table; // 頻率表
    uint32_t rate_count;                        // 頻率表項目數
};
```

### 9.2 CSS Clock 頻率表結構

```c
struct mod_css_clock_rate {
    uint64_t rate;                    // 目標頻率 (Hz)
    uint64_t pll_rate;               // PLL 頻率 (Hz)
    uint8_t clock_source;            // 時鐘源選擇
    uint8_t clock_div_type;          // 分頻器類型
    uint32_t clock_div;              // 分頻比
    uint32_t clock_mod_numerator;    // 調變器分子
    uint32_t clock_mod_denominator;  // 調變器分母
};
```

### 9.3 Clock HAL 配置結構

```c
struct mod_clock_dev_config {
    fwk_id_t driver_id;              // 底層驅動 ID
    fwk_id_t api_id;                 // 驅動 API ID
    fwk_id_t pd_source_id;           // 電源域源 ID
};
```

## 10. 關鍵資料結構

### 10.1 PPU v1 配置結構

```c
struct mod_ppu_v1_pd_config {
    enum mod_pd_type pd_type;           // 電源域類型
    struct mod_ppu_v1 ppu;              // PPU 硬體描述
    fwk_id_t cluster_id;                // 所屬叢集 ID
    enum ppu_v1_opmode opmode;          // 操作模式
    bool default_power_on;              // 預設是否開機
    fwk_id_t observer_id;               // 觀察者 ID
    // ...
};
```

### 10.2 System PLL 配置結構

```c
struct mod_system_pll_dev_config {
    volatile uint32_t * const control_reg;    // 控制暫存器
    volatile uint32_t * const status_reg;     // 狀態暫存器
    const uint32_t lock_flag_mask;            // 鎖定旗標遮罩
    const uint64_t initial_rate;              // 初始頻率
    const uint64_t min_rate;                  // 最小頻率
    const uint64_t max_rate;                  // 最大頻率
    const uint64_t min_step;                  // 最小步進
    const bool defer_initialization;          // 延遲初始化
};
```

### 10.3 PIK Clock 配置結構

```c
struct mod_pik_clock_dev_config {
    enum mod_pik_clock_type type;             // 時鐘類型
    volatile uint32_t *control_reg;           // 控制暫存器
    volatile uint32_t *divsys_reg;            // 系統分頻暫存器
    volatile uint32_t *divext_reg;            // 外部分頻暫存器
    volatile uint32_t *modulator_reg;         // 調變器暫存器
    struct mod_pik_clock_rate *rate_table;    // 頻率表
    size_t rate_count;                        // 頻率表項目數
    uint64_t initial_rate;                    // 初始頻率
    bool defer_initialization;                // 延遲初始化
};
```

## 11. 完整執行流程圖

```
系統啟動
    ↓
fwk_arch_init()
    ↓
fwk_module_init() → 初始化模組管理系統
    ↓
fwk_io_init() → 初始化 I/O 系統
    ↓
fwk_log_init() → 初始化日誌系統
    ↓
fwk_arch_interrupt_init() → 初始化中斷管理
    ↓
fwk_module_start() → 啟動所有模組
    ↓
┌─────────────────────────────────────────┐
│           模組初始化階段                  │
├─────────────────────────────────────────┤
│ 1. ppu_v1_init()                       │
│    - 分配電源域上下文表                  │
│                                        │
│ 2. system_pll_init()                   │
│    - 分配 PLL 設備上下文表              │
│                                        │
│ 3. pik_clock_init()                    │
│    - 分配 PIK 時鐘設備上下文表          │
│                                        │
│ 4. css_clock_init()                    │
│    - 分配 CSS 時鐘設備上下文表          │
│                                        │
│ 5. clock_init()                        │
│    - 分配 Clock HAL 設備上下文表        │
└─────────────────────────────────────────┘
    ↓
┌─────────────────────────────────────────┐
│           元素初始化階段                  │
├─────────────────────────────────────────┤
│ 1. ppu_v1_element_init()               │
│    - 設定各電源域 PPU 配置              │
│    - 如需要則預設開機                   │
│                                        │
│ 2. system_pll_element_init()           │
│    - 設定 PLL 初始配置                  │
│    - 調用 system_pll_set_rate()        │
│      設定初始頻率                       │
│                                        │
│ 3. pik_clock_element_init()            │
│    - 設定 PIK 時鐘源配置                │
│    - 根據類型初始化                     │
│                                        │
│ 4. css_clock_element_init()            │
│    - 設定 CSS 時鐘群組配置              │
│    - 設定初始頻率                       │
│                                        │
│ 5. clock_element_init()                │
│    - 註冊 Clock HAL 設備                │
│    - 建立驅動程式綁定                   │
└─────────────────────────────────────────┘
    ↓
┌─────────────────────────────────────────┐
│             綁定階段                     │
├─────────────────────────────────────────┤
│ - 模組間 API 綁定                       │
│ - Clock HAL 綁定到 CSS/PIK/PLL 驅動     │
│ - CSS Clock 綁定到 PIK Clock 和 PLL     │
│ - 建立電源域與時鐘依賴關係               │
│ - 設定觀察者和通知機制                   │
└─────────────────────────────────────────┘
    ↓
┌─────────────────────────────────────────┐
│             啟動階段                     │
├─────────────────────────────────────────┤
│ - 啟動各模組服務                        │
│ - 註冊事件處理器                        │
│ - 啟用中斷服務                          │
└─────────────────────────────────────────┘
    ↓
┌─────────────────────────────────────────┐
│           運行時階段                     │
├─────────────────────────────────────────┤
│ __fwk_run_main_loop() 或               │
│ fwk_process_event_queue()              │
│                                        │
│ - 處理 SCMI 協議請求                    │
│ - 電源狀態管理 (PPU v1)                 │
│ - 時鐘頻率調整 (Clock HAL → CSS/PIK)    │
│ - PLL 頻率控制 (System PLL)             │
│ - 系統事件處理                          │
└─────────────────────────────────────────┘
```

## 12. 關鍵函數調用序列

### 12.1 PPU v1 初始化序列

```
ppu_v1_init()
    ↓
ppu_v1_element_init() (針對每個電源域)
    ↓
ppu_v1_set_power_mode() (如果 default_power_on = true)
    ↓
ppu_v1_pd_set_state() (運行時調用)
```

### 12.2 System PLL 初始化序列

```
system_pll_init()
    ↓
system_pll_element_init() (針對每個 PLL)
    ↓
system_pll_set_rate() (設定初始頻率)
    ↓
freq_to_half_cycle_ps() (頻率轉換)
    ↓
寫入控制暫存器並等待鎖定
```

### 12.3 CSS Clock 初始化序列

```
css_clock_init()
    ↓
css_clock_element_init() (針對每個 CSS 時鐘群組)
    ↓
css_clock_bind() (綁定到 PIK Clock 和 System PLL)
    ↓
css_clock_set_rate() (運行時調用)
    ↓
set_rate_indexed() 或 set_rate_non_indexed()
    ↓
安全頻率切換流程 (切換時鐘源 → 調整 PLL → 切回)
```

### 12.4 Clock HAL 初始化序列

```
clock_init()
    ↓
clock_element_init() (針對每個時鐘設備)
    ↓
clock_bind() (綁定到底層驅動)
    ↓
clock_set_rate() (運行時調用)
    ↓
調用底層驅動 API (CSS Clock, PIK Clock, 或 System PLL)
```

### 12.5 PIK Clock 初始化序列

```
pik_clock_init()
    ↓
pik_clock_element_init() (針對每個時鐘)
    ↓
css_clock_set_rate() (運行時調用)
    ↓
get_rate_entry() (查找頻率表)
    ↓
ssclock_set_div() 或 msclock_set_div() (設定分頻)

### 12.6 完整的時鐘調整調用鏈

```
SCMI 時鐘頻率請求
    ↓
Clock HAL: clock_set_rate()
    ↓
CSS Clock: css_clock_set_rate()
    ↓
set_rate_indexed() (如果是索引型時鐘)
    ↓
1. PIK Clock API: set_source() (切換到備用時鐘源)
2. PIK Clock API: set_div() (設定分頻器)
3. PIK Clock API: set_mod() (設定調變器，如果支援)
4. System PLL API: set_rate() (調整 PLL 頻率)
5. PIK Clock API: set_source() (切回 PLL 時鐘源)
```
```

## 13. config_system_pll.c 中 system_pll_element_table 的調用機制

### 13.1 配置結構定義

在 `config_system_pll.c` 中，`system_pll_element_table` 的調用遵循 SCP framework 的動態元素生成機制：

```c
// product/totalcompute/tc2/scp_ramfw/config_system_pll.c

// 1. 定義靜態元素表
static const struct fwk_element system_pll_element_table[
    CLOCK_PLL_IDX_COUNT + 1] = {
        [CLOCK_PLL_IDX_CPU_CORTEX_A520] = {
            .name = "CPU_PLL_CORTEX_A520",
            .data = &((struct mod_system_pll_dev_config){
                .control_reg = (void *)SCP_PLL_CPU0,
                .status_reg = (void *)&SCP_PIK_PTR->PLL_STATUS[1],
                .lock_flag_mask = PLL_STATUS_CPUPLL_LOCK(0),
                .initial_rate = 1537 * FWK_MHZ,
                .min_rate = MOD_SYSTEM_PLL_MIN_RATE,
                .max_rate = MOD_SYSTEM_PLL_MAX_RATE,
                .min_step = MOD_SYSTEM_PLL_MIN_INTERVAL,
            }),
        },
        // ... 其他 PLL 配置
        [CLOCK_PLL_IDX_COUNT] = { 0 }, /* 終止描述符 */
    };

// 2. 定義元素表生成函數
static const struct fwk_element *system_pll_get_element_table(
    fwk_id_t module_id)
{
    return system_pll_element_table;
}

// 3. 模組配置結構
const struct fwk_module_config config_system_pll = {
    .elements = FWK_MODULE_DYNAMIC_ELEMENTS(system_pll_get_element_table),
};
```

### 13.2 調用流程分析

#### 步驟 1: 宏展開
`FWK_MODULE_DYNAMIC_ELEMENTS` 宏會展開為：

```c
// framework/include/fwk_module.h
#define FWK_MODULE_DYNAMIC_ELEMENTS(GENERATOR) \
    { \
        .type = FWK_MODULE_ELEMENTS_TYPE_DYNAMIC, \
        .generator = (GENERATOR), \
    }
```

因此 `config_system_pll` 實際上變成：

```c
const struct fwk_module_config config_system_pll = {
    .elements = {
        .type = FWK_MODULE_ELEMENTS_TYPE_DYNAMIC,
        .generator = system_pll_get_element_table,
    },
};
```

#### 步驟 2: Framework 調用序列

```
Framework 初始化
    ↓
fwk_module_init()
    ↓
讀取模組配置 (config_system_pll)
    ↓
檢查 elements.type == FWK_MODULE_ELEMENTS_TYPE_DYNAMIC
    ↓
調用 elements.generator(module_id)
    ↓
system_pll_get_element_table(FWK_MODULE_IDX_SYSTEM_PLL)
    ↓
返回 system_pll_element_table 指標
    ↓
Framework 遍歷元素表進行初始化
    ↓
針對每個元素調用 system_pll_element_init()
```

### 13.3 元素初始化過程

當 Framework 獲得元素表後，會針對每個元素進行初始化：

```c
// 針對 system_pll_element_table 中的每個元素
for (element_idx = 0; element_table[element_idx].name != NULL; element_idx++) {
    
    // 獲取元素配置
    const struct fwk_element *element = &element_table[element_idx];
    const struct mod_system_pll_dev_config *config = element->data;
    
    // 調用模組的元素初始化函數
    status = system_pll_element_init(
        FWK_ID_ELEMENT(FWK_MODULE_IDX_SYSTEM_PLL, element_idx),
        0,  // unused
        config  // 元素配置資料
    );
    
    // 在 system_pll_element_init() 中會調用
    system_pll_set_rate(element_id, config->initial_rate, 
                        MOD_CLOCK_ROUND_MODE_NONE);
}
```

### 13.4 具體的 PLL 配置示例

以 TC2 平台的 CPU PLL 為例：

```c
[CLOCK_PLL_IDX_CPU_CORTEX_A520] = {
    .name = "CPU_PLL_CORTEX_A520",
    .data = &((struct mod_system_pll_dev_config){
        .control_reg = (void *)SCP_PLL_CPU0,        // 控制暫存器地址
        .status_reg = (void *)&SCP_PIK_PTR->PLL_STATUS[1], // 狀態暫存器
        .lock_flag_mask = PLL_STATUS_CPUPLL_LOCK(0), // 鎖定旗標
        .initial_rate = 1537 * FWK_MHZ,             // 初始頻率 1537MHz
        .min_rate = MOD_SYSTEM_PLL_MIN_RATE,        // 最小頻率 50MHz
        .max_rate = MOD_SYSTEM_PLL_MAX_RATE,        // 最大頻率 4GHz
        .min_step = MOD_SYSTEM_PLL_MIN_INTERVAL,    // 最小步進 1KHz
    }),
},
```

### 13.5 調用時序圖

```
系統啟動
    ↓
fwk_module_start()
    ↓
system_pll 模組初始化
    ↓
Framework 讀取 config_system_pll
    ↓
發現 elements.type = FWK_MODULE_ELEMENTS_TYPE_DYNAMIC
    ↓
調用 system_pll_get_element_table(FWK_MODULE_IDX_SYSTEM_PLL)
    ↓
返回 system_pll_element_table 指標
    ↓
Framework 遍歷元素表：
    ├─ CPU_PLL_CORTEX_A520 → system_pll_element_init() → system_pll_set_rate(1537MHz)
    ├─ CPU_PLL_CORTEX_A720 → system_pll_element_init() → system_pll_set_rate(1893MHz)  
    ├─ CPU_PLL_CORTEX_X4   → system_pll_element_init() → system_pll_set_rate(2176MHz)
    ├─ SYS_PLL             → system_pll_element_init() → system_pll_set_rate(2000MHz)
    ├─ DPU_PLL             → system_pll_element_init() → system_pll_set_rate(600MHz)
    ├─ PIX0_PLL            → system_pll_element_init() → system_pll_set_rate(594MHz)
    ├─ PIX1_PLL            → system_pll_element_init() → system_pll_set_rate(594MHz)
    └─ GPU_PLL             → system_pll_element_init() → system_pll_set_rate(800MHz)
    ↓
所有 PLL 初始化完成，系統進入運行狀態
```

### 13.6 關鍵優勢

這種動態元素生成機制的優勢：

1. **靈活性** - 可以根據平台動態生成不同數量的元素
2. **可重用性** - 同一個模組可以支援不同平台配置
3. **記憶體效率** - 只分配實際需要的元素
4. **類型安全** - 編譯時檢查配置結構的正確性
5. **可維護性** - 配置與實現分離，易於維護

## 14. 總結

SCP firmware 的啟動流程是一個高度模組化和階段化的過程：

1. **系統啟動** - 從 `fwk_arch_init()` 開始，初始化基礎框架
2. **模組初始化** - 按順序初始化各功能模組
3. **電源管理** - PPU v1 負責各電源域的控制
4. **時鐘管理** - 分層架構：Clock HAL → CSS/PIK Clock → System PLL
5. **運行時服務** - 進入事件驅動的運行時階段，支援動態頻率調整

整個流程確保了系統的有序啟動和各模組間的正確協調，為後續的 SCMI 協議服務和系統管理功能提供了穩固的基礎。

### 關鍵特點：

- **階段化初始化**：嚴格按照 Framework 定義的五個階段進行
- **分層架構**：Clock HAL 提供統一介面，CSS/PIK Clock 處理具體控制，System PLL 提供時鐘源
- **模組化設計**：每個功能模組獨立初始化和管理
- **安全頻率切換**：CSS Clock 實現無縫的 CPU 頻率調整機制
- **事件驅動**：運行時採用事件驅動架構處理各種請求
- **硬體抽象**：通過配置結構和 API 抽象硬體差異
- **動態配置**：支援運行時動態頻率和電壓調整 (DVFS)
- **錯誤處理**：完整的錯誤檢查和狀態管理機制

### 時鐘系統優勢：

- **統一介面**：Clock HAL 為上層提供一致的時鐘控制介面
- **群組管理**：CSS Clock 可以同時管理多個相關的時鐘
- **安全切換**：頻率調整過程中確保系統穩定運行
- **精確控制**：支援精確的頻率設定和調變功能
- **電源協調**：與 PPU v1 協調，確保電源和時鐘狀態一致