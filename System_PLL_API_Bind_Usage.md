# System PLL API Bind 和使用詳解

## 概述

`api_system_pll` 中的 `set_rate` 函數通過 SCP 框架的綁定機制被其他模組使用。本文檔詳細說明綁定過程、配置方式和實際使用流程。

## 1. API 結構定義

### 1.1 system_pll API 結構

**文件**: `module/system_pll/src/mod_system_pll.c`

```c
static const struct mod_clock_drv_api api_system_pll = {
    .set_rate = system_pll_set_rate,                    // ← 設定頻率函數
    .get_rate = system_pll_get_rate,                    // 獲取頻率函數
    .get_rate_from_index = system_pll_get_rate_from_index,
    .set_state = system_pll_set_state,                  // 設定狀態函數
    .get_state = system_pll_get_state,                  // 獲取狀態函數
    .get_range = system_pll_get_range,                  // 獲取頻率範圍
    .process_power_transition = system_pll_power_state_change,
    .process_pending_power_transition = system_pll_power_state_pending_change,
};
```

### 1.2 綁定處理函數

```c
static int system_pll_process_bind_request(fwk_id_t requester_id, fwk_id_t id,
                                        fwk_id_t api_type, const void **api)
{
    // 返回 API 結構指針
    *api = &api_system_pll;
    return FWK_SUCCESS;
}
```

## 2. 綁定配置

### 2.1 Clock 模組配置

**文件**: `product/totalcompute/tc2/scp_ramfw/config_clock.c`

```c
static const struct fwk_element clock_element_table[] = {
    [CLOCK_IDX_PIXEL_0] = {
        .name = "PIXEL_0",
        .data = &((struct mod_clock_dev_config){
            // 指定要綁定的 system_pll 元素
            .driver_id = FWK_ID_ELEMENT_INIT(
                FWK_MODULE_IDX_SYSTEM_PLL,    // 模組 ID
                CLOCK_PLL_IDX_PIX0),          // 元素索引
            
            // 指定要綁定的 API
            .api_id = FWK_ID_API_INIT(
                FWK_MODULE_IDX_SYSTEM_PLL,    // 模組 ID
                MOD_SYSTEM_PLL_API_TYPE_DEFAULT), // API 類型
        }),
    },
    
    [CLOCK_IDX_GPU] = {
        .name = "GPU",
        .data = &((struct mod_clock_dev_config){
            .driver_id = FWK_ID_ELEMENT_INIT(
                FWK_MODULE_IDX_SYSTEM_PLL,
                CLOCK_PLL_IDX_GPU),
            .api_id = FWK_ID_API_INIT(
                FWK_MODULE_IDX_SYSTEM_PLL,
                MOD_SYSTEM_PLL_API_TYPE_DEFAULT),
        }),
    },
};
```

### 2.2 CSS Clock 模組配置

**文件**: `product/totalcompute/tc2/scp_ramfw/config_css_clock.c`

```c
static const struct fwk_element css_clock_element_table[] = {
    [CLOCK_CSS_IDX_CPU_GROUP_CORTEX_A520] = {
        .name = "CPU_GROUP_CORTEX_A520",
        .data = &((struct mod_css_clock_dev_config){
            // ... 其他配置
            
            // 綁定到 system_pll
            .pll_id = FWK_ID_ELEMENT_INIT(
                FWK_MODULE_IDX_SYSTEM_PLL,
                CLOCK_PLL_IDX_CPU_CORTEX_A520),
            .pll_api_id = FWK_ID_API_INIT(
                FWK_MODULE_IDX_SYSTEM_PLL,
                MOD_SYSTEM_PLL_API_TYPE_DEFAULT),
        }),
    },
};
```

## 3. 綁定過程

### 3.1 框架綁定流程

```
1. 框架啟動
    ↓
2. 模組初始化完成
    ↓
3. 開始綁定階段 (fwk_module_bind_modules)
    ↓
4. Clock 模組的 bind 函數被調用
    ↓
5. clock_bind() 調用 fwk_module_bind()
    ↓
6. 框架調用 system_pll_process_bind_request()
    ↓
7. 返回 api_system_pll 指針
    ↓
8. Clock 模組保存 API 指針到 ctx->api
```

### 3.2 Clock 模組綁定實現

**文件**: `module/clock/src/mod_clock.c`

```c
static int clock_bind(fwk_id_t id, unsigned int round)
{
    struct clock_dev_ctx *ctx;
    int status;

    if (round == 1) {
        return FWK_SUCCESS;
    }

    if (!fwk_id_is_type(id, FWK_ID_TYPE_ELEMENT)) {
        return FWK_SUCCESS;
    }

    ctx = &mod_clock_ctx.dev_ctx_table[fwk_id_get_element_idx(id)];

    // 關鍵：綁定到 system_pll API
    status = fwk_module_bind(
        ctx->config->driver_id,    // system_pll 元素 ID
        ctx->config->api_id,       // system_pll API ID
        &ctx->api);                // 保存 API 指針
    
    if (status != FWK_SUCCESS) {
        return status;
    }

    return FWK_SUCCESS;
}
```

### 3.3 綁定後的結果

```c
// 綁定完成後，ctx->api 指向 api_system_pll
struct clock_dev_ctx {
    const struct mod_clock_drv_api *api;  // ← 指向 api_system_pll
    // ...
};

// ctx->api->set_rate 實際上是 system_pll_set_rate
```

## 4. API 使用

### 4.1 通過 Clock 模組使用

**文件**: `module/clock/src/mod_clock.c`

```c
static int clock_set_rate(fwk_id_t clock_id, uint64_t rate,
                          enum mod_clock_round_mode round_mode)
{
    struct clock_dev_ctx *ctx;
    int status;

    clock_get_ctx(clock_id, &ctx);

    // 併發檢查
    if (ctx->request.is_ongoing) {
        return FWK_E_BUSY;
    }

    // 關鍵：調用綁定的 API
    status = ctx->api->set_rate(ctx->config->driver_id, rate, round_mode);
    //              ↑
    //       實際調用 system_pll_set_rate
    
    if (status == FWK_PENDING) {
        return create_async_request(ctx, clock_id, 
                                   mod_clock_event_id_set_rate_request);
    }
    
    return status;
}
```

### 4.2 通過 CSS Clock 模組使用

**文件**: `module/css_clock/src/mod_css_clock.c` (類似實現)

```c
static int css_clock_set_rate(fwk_id_t clock_id, uint64_t rate,
                              enum mod_clock_round_mode round_mode)
{
    struct css_clock_dev_ctx *ctx;
    int status;
    
    ctx = &css_clock_ctx.dev_ctx_table[fwk_id_get_element_idx(clock_id)];
    
    // 調用綁定的 system_pll API
    status = ctx->pll_api->set_rate(ctx->config->pll_id, rate, round_mode);
    //                    ↑
    //            實際調用 system_pll_set_rate
    
    return status;
}
```

## 5. 完整的調用鏈

### 5.1 應用層到硬體的完整流程

```
應用層調用
    ↓
Clock HAL API (clock_set_rate)
    ↓
ctx->api->set_rate()                    ← 通過函數指針
    ↓
system_pll_set_rate()                   ← 實際的驅動函數
    ↓
*ctx->config->control_reg = picoseconds ← 硬體寄存器操作
```

### 5.2 實際範例

```c
// 1. 應用層調用
clock_api->set_rate(CLOCK_IDX_GPU, 800000000, MOD_CLOCK_ROUND_MODE_NEAREST);

// 2. Clock 模組處理
clock_set_rate(CLOCK_IDX_GPU, 800000000, MOD_CLOCK_ROUND_MODE_NEAREST)
{
    // ctx->api 指向 api_system_pll
    status = ctx->api->set_rate(GPU_PLL_ELEMENT_ID, 800000000, NEAREST);
}

// 3. System PLL 驅動處理
system_pll_set_rate(GPU_PLL_ELEMENT_ID, 800000000, NEAREST)
{
    // 計算時序參數
    picoseconds = freq_to_half_cycle_ps(800000000);
    
    // 寫入硬體寄存器
    *ctx->config->control_reg = picoseconds;  // 實際硬體操作
    
    // 等待鎖定
    while ((*ctx->config->status_reg & ctx->config->lock_flag_mask) == 0)
        continue;
}
```

## 6. 不同模組的綁定方式

### 6.1 Clock 模組綁定

```c
// 配置
.driver_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_SYSTEM_PLL, pll_index),
.api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_SYSTEM_PLL, MOD_SYSTEM_PLL_API_TYPE_DEFAULT),

// 綁定
fwk_module_bind(ctx->config->driver_id, ctx->config->api_id, &ctx->api);

// 使用
ctx->api->set_rate(ctx->config->driver_id, rate, round_mode);
```

### 6.2 CSS Clock 模組綁定

```c
// 配置
.pll_id = FWK_ID_ELEMENT_INIT(FWK_MODULE_IDX_SYSTEM_PLL, pll_index),
.pll_api_id = FWK_ID_API_INIT(FWK_MODULE_IDX_SYSTEM_PLL, MOD_SYSTEM_PLL_API_TYPE_DEFAULT),

// 綁定
fwk_module_bind(ctx->config->pll_id, ctx->config->pll_api_id, &ctx->pll_api);

// 使用
ctx->pll_api->set_rate(ctx->config->pll_id, rate, round_mode);
```

## 7. 調試和驗證

### 7.1 檢查綁定是否成功

```c
static int clock_bind(fwk_id_t id, unsigned int round)
{
    // ... 綁定代碼 ...
    
    status = fwk_module_bind(ctx->config->driver_id, ctx->config->api_id, &ctx->api);
    if (status != FWK_SUCCESS) {
        FWK_LOG_ERR("[CLOCK] Failed to bind to system_pll: %d", status);
        return status;
    }
    
    // 驗證 API 指針
    if (ctx->api == NULL || ctx->api->set_rate == NULL) {
        FWK_LOG_ERR("[CLOCK] Invalid system_pll API");
        return FWK_E_PANIC;
    }
    
    FWK_LOG_INFO("[CLOCK] Successfully bound to system_pll");
    return FWK_SUCCESS;
}
```

### 7.2 添加調用日誌

```c
static int clock_set_rate(fwk_id_t clock_id, uint64_t rate,
                          enum mod_clock_round_mode round_mode)
{
    struct clock_dev_ctx *ctx;
    int status;

    clock_get_ctx(clock_id, &ctx);
    
    FWK_LOG_INFO("[CLOCK] Calling system_pll set_rate: %llu Hz", rate);
    
    status = ctx->api->set_rate(ctx->config->driver_id, rate, round_mode);
    
    FWK_LOG_INFO("[CLOCK] system_pll set_rate returned: %d", status);
    
    return status;
}
```

## 8. 常見問題和解決方案

### 8.1 綁定失敗

**問題**: `fwk_module_bind` 返回錯誤

**可能原因**:
- 配置中的 `driver_id` 或 `api_id` 不正確
- system_pll 模組未正確初始化
- API 類型不匹配

**解決方案**:
```c
// 檢查配置
FWK_LOG_INFO("Binding to driver_id: 0x%x, api_id: 0x%x", 
             ctx->config->driver_id.value, ctx->config->api_id.value);

// 檢查模組是否存在
if (!fwk_module_is_valid_module_id(FWK_ID_MODULE(FWK_MODULE_IDX_SYSTEM_PLL))) {
    FWK_LOG_ERR("system_pll module not available");
}
```

### 8.2 API 調用失敗

**問題**: `ctx->api->set_rate` 調用失敗

**可能原因**:
- API 指針為 NULL
- 傳遞的參數不正確
- PLL 硬體問題

**解決方案**:
```c
// 檢查 API 指針
if (ctx->api == NULL || ctx->api->set_rate == NULL) {
    FWK_LOG_ERR("Invalid API pointer");
    return FWK_E_PANIC;
}

// 添加參數檢查
FWK_LOG_INFO("Calling set_rate with driver_id: 0x%x, rate: %llu", 
             ctx->config->driver_id.value, rate);
```

## 9. 總結

### 9.1 綁定過程總結

1. **配置階段**: 在配置文件中指定 `driver_id` 和 `api_id`
2. **綁定階段**: 框架調用 `fwk_module_bind()` 進行綁定
3. **使用階段**: 通過 `ctx->api->set_rate()` 調用實際函數

### 9.2 關鍵點

- **函數指針**: `ctx->api->set_rate` 實際指向 `system_pll_set_rate`
- **間接調用**: 通過 API 結構體實現模組間的解耦
- **配置驅動**: 綁定關係由配置文件決定
- **框架管理**: 整個過程由 SCP 框架自動處理

### 9.3 使用優勢

- **模組化**: 不同模組可以獨立開發和測試
- **可配置**: 可以通過配置改變綁定關係
- **統一接口**: 所有時鐘驅動使用相同的 API 接口
- **錯誤處理**: 框架提供統一的錯誤處理機制

這種綁定機制是 SCP 固件架構的核心特性，實現了模組間的松耦合和高度可配置性。