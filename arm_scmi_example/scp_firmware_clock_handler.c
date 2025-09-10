/*
 * SCP Firmware Clock Handler Example
 * 
 * 這個範例展示 SCP firmware 端如何處理來自 Linux SCMI driver 的
 * 時鐘設定請求，並實際控制硬體時鐘
 */

#include <fwk_module.h>
#include <fwk_element.h>
#include <fwk_id.h>
#include <fwk_macros.h>
#include <fwk_status.h>
#include <mod_scmi.h>
#include <mod_scmi_clock.h>
#include <mod_clock.h>

/* SCMI Clock 協議命令定義 */
enum scmi_clock_command_id {
    SCMI_CLOCK_ATTRIBUTES = 0x3,
    SCMI_CLOCK_DESCRIBE_RATES = 0x4,
    SCMI_CLOCK_RATE_SET = 0x5,
    SCMI_CLOCK_RATE_GET = 0x6,
    SCMI_CLOCK_CONFIG_SET = 0x7,
};

/* SCMI Clock Rate Set 命令結構 */
struct scmi_clock_rate_set_a2p {
    uint32_t flags;
    uint32_t clock_id;
    uint32_t rate_low;
    uint32_t rate_high;
};

/* SCMI Clock Rate Set 回應結構 */
struct scmi_clock_rate_set_p2a {
    int32_t status;
};

/* SCMI Clock Config Set 命令結構 */
struct scmi_clock_config_set_a2p {
    uint32_t clock_id;
    uint32_t attributes;
};

/* Clock 模組上下文 */
struct scmi_clock_ctx {
    /* SCMI 服務 ID */
    fwk_id_t scmi_service_id;
    
    /* Clock 模組 API */
    const struct mod_clock_api *clock_api;
    
    /* 支援的時鐘數量 */
    unsigned int clock_count;
    
    /* 時鐘設定表 */
    const struct mod_scmi_clock_device *clock_devices;
};

static struct scmi_clock_ctx scmi_clock_ctx;

/*
 * 處理 SCMI Clock Rate Set 命令
 * 這是核心函數，處理來自 Linux kernel 的時鐘頻率設定請求
 */
static int scmi_clock_rate_set_handler(fwk_id_t service_id, 
                                      const uint32_t *payload)
{
    int status;
    const struct scmi_clock_rate_set_a2p *parameters;
    struct scmi_clock_rate_set_p2a return_values;
    uint64_t rate;
    uint32_t clock_id;
    fwk_id_t clock_element_id;
    
    parameters = (const struct scmi_clock_rate_set_a2p *)payload;
    
    /* 解析參數 */
    clock_id = parameters->clock_id;
    rate = ((uint64_t)parameters->rate_high << 32) | parameters->rate_low;
    
    fwk_log_info("[SCMI Clock] Rate set request: Clock ID %u, Rate %llu Hz", 
                 clock_id, rate);
    
    /* 驗證時鐘 ID */
    if (clock_id >= scmi_clock_ctx.clock_count) {
        fwk_log_error("[SCMI Clock] Invalid clock ID: %u", clock_id);
        return_values.status = SCMI_INVALID_PARAMETERS;
        goto exit;
    }
    
    /* 取得對應的時鐘元素 ID */
    clock_element_id = scmi_clock_ctx.clock_devices[clock_id].element_id;
    
    /* 檢查時鐘是否存在 */
    if (fwk_id_is_equal(clock_element_id, FWK_ID_NONE)) {
        fwk_log_error("[SCMI Clock] Clock ID %u not configured", clock_id);
        return_values.status = SCMI_NOT_FOUND;
        goto exit;
    }
    
    /* 
     * 呼叫 Clock 模組 API 設定實際硬體頻率
     * 這裡會與底層硬體抽象層互動
     */
    status = scmi_clock_ctx.clock_api->set_rate(clock_element_id, 
                                               rate, 
                                               MOD_CLOCK_ROUND_MODE_NEAREST);
    
    if (status != FWK_SUCCESS) {
        fwk_log_error("[SCMI Clock] Failed to set rate for clock %u: %d", 
                      clock_id, status);
        
        /* 轉換錯誤碼 */
        switch (status) {
        case FWK_E_RANGE:
            return_values.status = SCMI_OUT_OF_RANGE;
            break;
        case FWK_E_BUSY:
            return_values.status = SCMI_BUSY;
            break;
        case FWK_E_SUPPORT:
            return_values.status = SCMI_NOT_SUPPORTED;
            break;
        default:
            return_values.status = SCMI_GENERIC_ERROR;
            break;
        }
        goto exit;
    }
    
    /* 成功設定 */
    return_values.status = SCMI_SUCCESS;
    fwk_log_info("[SCMI Clock] Clock %u rate set to %llu Hz successfully", 
                 clock_id, rate);

exit:
    /* 傳送回應給 AP */
    scmi_clock_ctx.scmi_api->respond(service_id, &return_values, 
                                    sizeof(return_values));
    
    return FWK_SUCCESS;
}

/*
 * 處理 SCMI Clock Rate Get 命令
 */
static int scmi_clock_rate_get_handler(fwk_id_t service_id, 
                                      const uint32_t *payload)
{
    int status;
    uint32_t clock_id;
    uint64_t rate;
    fwk_id_t clock_element_id;
    
    struct {
        int32_t status;
        uint32_t rate_low;
        uint32_t rate_high;
    } return_values;
    
    clock_id = *(const uint32_t *)payload;
    
    fwk_log_debug("[SCMI Clock] Rate get request: Clock ID %u", clock_id);
    
    /* 驗證時鐘 ID */
    if (clock_id >= scmi_clock_ctx.clock_count) {
        return_values.status = SCMI_INVALID_PARAMETERS;
        goto exit;
    }
    
    clock_element_id = scmi_clock_ctx.clock_devices[clock_id].element_id;
    
    if (fwk_id_is_equal(clock_element_id, FWK_ID_NONE)) {
        return_values.status = SCMI_NOT_FOUND;
        goto exit;
    }
    
    /* 從硬體取得目前頻率 */
    status = scmi_clock_ctx.clock_api->get_rate(clock_element_id, &rate);
    if (status != FWK_SUCCESS) {
        return_values.status = SCMI_HARDWARE_ERROR;
        goto exit;
    }
    
    /* 準備回應資料 */
    return_values.status = SCMI_SUCCESS;
    return_values.rate_low = (uint32_t)(rate & 0xFFFFFFFF);
    return_values.rate_high = (uint32_t)(rate >> 32);
    
    fwk_log_debug("[SCMI Clock] Clock %u current rate: %llu Hz", 
                  clock_id, rate);

exit:
    scmi_clock_ctx.scmi_api->respond(service_id, &return_values, 
                                    sizeof(return_values));
    
    return FWK_SUCCESS;
}

/*
 * 處理 SCMI Clock Config Set 命令 (啟用/停用時鐘)
 */
static int scmi_clock_config_set_handler(fwk_id_t service_id, 
                                        const uint32_t *payload)
{
    int status;
    const struct scmi_clock_config_set_a2p *parameters;
    uint32_t clock_id;
    bool enable;
    fwk_id_t clock_element_id;
    
    struct {
        int32_t status;
    } return_values;
    
    parameters = (const struct scmi_clock_config_set_a2p *)payload;
    clock_id = parameters->clock_id;
    enable = (parameters->attributes & 0x1) != 0;
    
    fwk_log_info("[SCMI Clock] Config set request: Clock ID %u, Enable %s", 
                 clock_id, enable ? "true" : "false");
    
    /* 驗證時鐘 ID */
    if (clock_id >= scmi_clock_ctx.clock_count) {
        return_values.status = SCMI_INVALID_PARAMETERS;
        goto exit;
    }
    
    clock_element_id = scmi_clock_ctx.clock_devices[clock_id].element_id;
    
    if (fwk_id_is_equal(clock_element_id, FWK_ID_NONE)) {
        return_values.status = SCMI_NOT_FOUND;
        goto exit;
    }
    
    /* 啟用或停用時鐘 */
    if (enable) {
        status = scmi_clock_ctx.clock_api->set_state(clock_element_id, 
                                                    MOD_CLOCK_STATE_RUNNING);
    } else {
        status = scmi_clock_ctx.clock_api->set_state(clock_element_id, 
                                                    MOD_CLOCK_STATE_STOPPED);
    }
    
    if (status != FWK_SUCCESS) {
        fwk_log_error("[SCMI Clock] Failed to %s clock %u: %d", 
                      enable ? "enable" : "disable", clock_id, status);
        return_values.status = SCMI_HARDWARE_ERROR;
        goto exit;
    }
    
    return_values.status = SCMI_SUCCESS;
    fwk_log_info("[SCMI Clock] Clock %u %s successfully", 
                 clock_id, enable ? "enabled" : "disabled");

exit:
    scmi_clock_ctx.scmi_api->respond(service_id, &return_values, 
                                    sizeof(return_values));
    
    return FWK_SUCCESS;
}

/*
 * SCMI Clock 協議訊息處理器
 * 根據命令 ID 分派到對應的處理函數
 */
static int scmi_clock_message_handler(fwk_id_t protocol_id, 
                                     fwk_id_t service_id,
                                     const uint32_t *payload, 
                                     size_t payload_size,
                                     unsigned int message_id)
{
    int status = FWK_SUCCESS;
    
    fwk_log_debug("[SCMI Clock] Received message ID: 0x%x", message_id);
    
    switch (message_id) {
    case SCMI_CLOCK_RATE_SET:
        status = scmi_clock_rate_set_handler(service_id, payload);
        break;
        
    case SCMI_CLOCK_RATE_GET:
        status = scmi_clock_rate_get_handler(service_id, payload);
        break;
        
    case SCMI_CLOCK_CONFIG_SET:
        status = scmi_clock_config_set_handler(service_id, payload);
        break;
        
    case SCMI_CLOCK_ATTRIBUTES:
        /* 處理時鐘屬性查詢 */
        status = scmi_clock_attributes_handler(service_id, payload);
        break;
        
    case SCMI_CLOCK_DESCRIBE_RATES:
        /* 處理時鐘頻率範圍查詢 */
        status = scmi_clock_describe_rates_handler(service_id, payload);
        break;
        
    default:
        fwk_log_error("[SCMI Clock] Unsupported message ID: 0x%x", message_id);
        
        /* 傳送不支援的回應 */
        struct {
            int32_t status;
        } error_response = { SCMI_NOT_SUPPORTED };
        
        scmi_clock_ctx.scmi_api->respond(service_id, &error_response, 
                                        sizeof(error_response));
        break;
    }
    
    return status;
}

/*
 * 模組初始化
 */
static int scmi_clock_init(fwk_id_t module_id, 
                          unsigned int element_count,
                          const void *data)
{
    const struct mod_scmi_clock_config *config = data;
    
    if (config == NULL) {
        return FWK_E_PARAM;
    }
    
    scmi_clock_ctx.clock_count = config->clock_count;
    scmi_clock_ctx.clock_devices = config->clock_devices;
    
    fwk_log_info("[SCMI Clock] Module initialized with %u clocks", 
                 scmi_clock_ctx.clock_count);
    
    return FWK_SUCCESS;
}

/*
 * 綁定其他模組的 API
 */
static int scmi_clock_bind(fwk_id_t id, unsigned int round)
{
    int status;
    
    if (round == 1) {
        return FWK_SUCCESS;
    }
    
    /* 綁定 Clock 模組 API */
    status = fwk_module_bind(FWK_ID_MODULE(FWK_MODULE_IDX_CLOCK),
                            FWK_ID_API(FWK_MODULE_IDX_CLOCK, 0),
                            &scmi_clock_ctx.clock_api);
    if (status != FWK_SUCCESS) {
        return status;
    }
    
    /* 綁定 SCMI 模組 API */
    status = fwk_module_bind(FWK_ID_MODULE(FWK_MODULE_IDX_SCMI),
                            FWK_ID_API(FWK_MODULE_IDX_SCMI, 
                                      MOD_SCMI_API_IDX_PROTOCOL),
                            &scmi_clock_ctx.scmi_api);
    
    return status;
}

/*
 * 處理程序啟動
 */
static int scmi_clock_process_bind_request(fwk_id_t source_id,
                                          fwk_id_t target_id,
                                          fwk_id_t api_id,
                                          const void **api)
{
    /* 提供 SCMI Clock 協議 API 給 SCMI 模組 */
    static const struct mod_scmi_to_protocol_api scmi_clock_protocol_api = {
        .get_scmi_protocol_id = scmi_clock_get_scmi_protocol_id,
        .message_handler = scmi_clock_message_handler,
    };
    
    *api = &scmi_clock_protocol_api;
    
    return FWK_SUCCESS;
}

/* 模組描述符 */
const struct fwk_module module_scmi_clock = {
    .name = "SCMI Clock Management Protocol",
    .api_count = 1,
    .type = FWK_MODULE_TYPE_PROTOCOL,
    .init = scmi_clock_init,
    .bind = scmi_clock_bind,
    .process_bind_request = scmi_clock_process_bind_request,
};

/*
 * 通訊流程說明：
 * 
 * 1. Linux SCMI Driver 傳送 SCMI Clock Rate Set 命令
 * 2. SCP firmware 接收命令並解析參數
 * 3. 呼叫 scmi_clock_rate_set_handler()
 * 4. 透過 Clock 模組 API 設定實際硬體
 * 5. 傳送回應給 Linux kernel
 * 
 * 訊息格式：
 * - 命令: [Header][Clock ID][Rate Low][Rate High]
 * - 回應: [Header][Status]
 * 
 * 錯誤處理：
 * - 參數驗證
 * - 硬體錯誤轉換為 SCMI 錯誤碼
 * - 適當的日誌記錄
 */