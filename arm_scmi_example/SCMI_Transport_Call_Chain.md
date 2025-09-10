# SCMI 傳輸層呼叫鏈分析

## 🔍 關鍵問題：Linux 最後從哪裡傳送給 SCP？

答案是：**透過 SCMI Transport Layer**，具體來說是 **Mailbox 或 SMC 驅動程式**。

## 📋 完整呼叫鏈追蹤

### 1. SCMI Clock Driver 呼叫
```c
// drivers/firmware/arm_scmi/clock.c
static int scmi_clock_rate_set(const struct scmi_protocol_handle *ph,
                              u32 clk_id, u64 rate)
{
    struct scmi_xfer *t;
    
    // 準備 SCMI 訊息
    ret = ph->xops->xfer_get_init(ph, CLOCK_RATE_SET, sizeof(*cfg), 0, &t);
    
    // 填入訊息內容
    cfg = t->tx.buf;
    cfg->flags = cpu_to_le32(0);
    cfg->id = cpu_to_le32(clk_id);
    cfg->value_low = cpu_to_le32(rate & 0xffffffff);
    cfg->value_high = cpu_to_le32(rate >> 32);
    
    // 🔑 關鍵呼叫：透過 xops 傳送訊息
    ret = ph->xops->do_xfer(ph, t);  // ← 這裡開始進入傳輸層
    
    ph->xops->xfer_put(ph, t);
    return ret;
}
```

### 2. SCMI Core Framework
```c
// drivers/firmware/arm_scmi/driver.c
static int do_xfer(const struct scmi_protocol_handle *ph,
                   struct scmi_xfer *xfer)
{
    struct scmi_info *info = handle_to_scmi_info(ph->parent);
    struct device *dev = info->dev;
    struct scmi_chan_info *cinfo;
    
    // 取得通道資訊
    cinfo = idr_find(&info->tx_idr, pi->proto->id);
    
    // 🔑 關鍵呼叫：透過傳輸層傳送
    ret = info->desc->ops->send_message(cinfo, xfer);  // ← 進入具體傳輸層
    
    if (!ret && !xfer->hdr.poll_completion) {
        // 等待回應
        ret = scmi_wait_for_message_response(cinfo, xfer);
    }
    
    return ret;
}
```

### 3. 傳輸層選擇 (Mailbox 範例)
```c
// drivers/firmware/arm_scmi/mailbox.c
static int mailbox_send_message(struct scmi_chan_info *cinfo,
                               struct scmi_xfer *xfer)
{
    struct scmi_mailbox *smbox = cinfo->transport_info;
    int ret;
    
    // 🔑 關鍵步驟 1：寫入共享記憶體
    shmem_tx_prepare(smbox->shmem, xfer, cinfo);
    
    // 🔑 關鍵步驟 2：觸發 mailbox 中斷通知 SCP
    ret = mbox_send_message(smbox->chan, NULL);  // ← 實際硬體操作！
    
    if (ret < 0)
        shmem_clear_channel(smbox->shmem);
    
    return ret;
}
```

### 4. 共享記憶體寫入
```c
// drivers/firmware/arm_scmi/shmem.c
void shmem_tx_prepare(struct scmi_shared_mem __iomem *shmem,
                     struct scmi_xfer *xfer,
                     struct scmi_chan_info *cinfo)
{
    // 🔑 實際寫入共享記憶體
    iowrite32(pack_scmi_header(&xfer->hdr), &shmem->msg_header);
    iowrite32(xfer->tx.len, &shmem->length);
    
    if (xfer->tx.buf && xfer->tx.len) {
        // 將 SCMI 訊息內容寫入共享記憶體
        memcpy_toio(shmem->msg_payload, xfer->tx.buf, xfer->tx.len);
    }
}
```

### 5. Mailbox 硬體操作
```c
// drivers/mailbox/arm_mhu_doorbell.c (或類似的 mailbox driver)
static int mhu_send_data(struct mbox_chan *chan, void *data)
{
    struct mhu_doorbell_chan *mhu_chan = chan->con_priv;
    struct mhu_doorbell *mhu = mhu_chan->mhu;
    
    // 🔑 最終硬體操作：寫入 mailbox 暫存器觸發中斷
    writel_relaxed(BIT(mhu_chan->doorbell), 
                   mhu->tx_reg + DOORBELL_SET_OFFSET);  // ← 實際硬體寫入！
    
    return 0;
}
```

## 🔧 具體硬體操作

### Mailbox 暫存器寫入
```c
// 實際的硬體暫存器操作
static void trigger_scp_interrupt(void)
{
    volatile uint32_t *mailbox_reg = (uint32_t *)MAILBOX_BASE_ADDR;
    
    // 寫入 doorbell 暫存器，觸發 SCP 中斷
    *mailbox_reg = 0x1;  // 設定 doorbell bit
    
    // 這個寫入會：
    // 1. 在硬體層面觸發中斷線
    // 2. SCP 收到中斷
    // 3. SCP 讀取共享記憶體中的 SCMI 訊息
}
```

### 共享記憶體佈局
```c
// 共享記憶體中的實際資料
struct scmi_shared_mem {
    uint32_t reserved1;
    uint32_t channel_status;    // 通道狀態
    uint32_t reserved2[2];
    uint32_t flags;            // 旗標
    uint32_t length;           // 訊息長度
    uint32_t msg_header;       // SCMI 訊息標頭
    uint32_t msg_payload[];    // SCMI 訊息內容
};

// 實際寫入的資料 (1.5GHz 範例)
// 地址 0x4e000000: 共享記憶體基址
// +0x00: 0x00000000 (reserved1)
// +0x04: 0x00000000 (channel_status)  
// +0x08: 0x00000000 (reserved2[0])
// +0x0C: 0x00000000 (reserved2[1])
// +0x10: 0x00000000 (flags)
// +0x14: 0x00000010 (length = 16 bytes)
// +0x18: 0x00XX5014 (msg_header: Protocol=0x14, Cmd=0x5)
// +0x1C: 0x00000000 (payload[0]: flags)
// +0x20: 0x00000000 (payload[1]: clock_id)
// +0x24: 0x59682F00 (payload[2]: rate_low)
// +0x28: 0x00000000 (payload[3]: rate_high)
```## 🚀
 傳輸方式比較

### 1. Mailbox 傳輸 (最常見)
```c
// drivers/firmware/arm_scmi/mailbox.c
static const struct scmi_transport_ops scmi_mailbox_ops = {
    .chan_available = mailbox_chan_available,
    .chan_setup = mailbox_chan_setup,
    .chan_free = mailbox_chan_free,
    .send_message = mailbox_send_message,  // ← 最終呼叫點
    .mark_txdone = mailbox_mark_txdone,
    .fetch_response = mailbox_fetch_response,
    .clear_channel = mailbox_clear_channel,
};

// 實際硬體操作
static int mailbox_send_message(struct scmi_chan_info *cinfo,
                               struct scmi_xfer *xfer)
{
    // 1. 寫入共享記憶體
    shmem_tx_prepare(smbox->shmem, xfer, cinfo);
    
    // 2. 觸發 mailbox 中斷 (實際硬體操作)
    return mbox_send_message(smbox->chan, NULL);
}
```

### 2. SMC 傳輸 (Secure Monitor Call)
```c
// drivers/firmware/arm_scmi/smc.c
static int smc_send_message(struct scmi_chan_info *cinfo,
                           struct scmi_xfer *xfer)
{
    struct scmi_smc *scmi_info = cinfo->transport_info;
    struct arm_smccc_res res;
    
    // 1. 寫入共享記憶體
    shmem_tx_prepare(scmi_info->shmem, xfer, cinfo);
    
    // 2. 透過 SMC 呼叫通知 SCP (實際硬體操作)
    arm_smccc_1_1_invoke(scmi_info->func_id, 0, 0, 0, 0, 0, 0, 0, &res);
    
    return 0;
}
```

### 3. Virtio 傳輸 (虛擬化環境)
```c
// drivers/firmware/arm_scmi/virtio.c
static int virtio_send_message(struct scmi_chan_info *cinfo,
                              struct scmi_xfer *xfer)
{
    struct scmi_vio_channel *vioch = cinfo->transport_info;
    struct scatterlist sg_out;
    struct scatterlist sg_in;
    
    // 準備 virtqueue 緩衝區
    sg_init_one(&sg_out, &vioch->pending_cmds_list, 
                sizeof(vioch->pending_cmds_list));
    sg_init_one(&sg_in, &vioch->input, VIRTIO_SCMI_MAX_PDU_SIZE);
    
    // 透過 virtqueue 傳送 (實際虛擬化操作)
    return virtqueue_add_sgs(vioch->vqueue, sgs, 1, 1, xfer, GFP_ATOMIC);
}
```

## 🎯 最終傳輸點總結

### Mailbox 方式 (最常見)
```
Linux SCMI Driver
        ↓
scmi_clock_rate_set()
        ↓  
do_xfer()
        ↓
mailbox_send_message()
        ↓
shmem_tx_prepare()          ← 寫入共享記憶體
        ↓
mbox_send_message()         ← 觸發 mailbox 中斷
        ↓
writel_relaxed()            ← 實際硬體暫存器寫入
        ↓
🔔 SCP 收到中斷
```

### 實際硬體操作位置
```c
// drivers/mailbox/arm_mhu_doorbell.c (或平台特定的 mailbox driver)
static int mhu_send_data(struct mbox_chan *chan, void *data)
{
    // 這裡是最終的硬體操作點！
    writel_relaxed(BIT(doorbell_bit), mhu_base + DOORBELL_SET);
    
    // 這個 writel_relaxed 會：
    // 1. 寫入 mailbox 控制器暫存器
    // 2. 觸發硬體中斷線
    // 3. SCP 處理器收到中斷
    // 4. SCP 讀取共享記憶體中的 SCMI 訊息
}
```

## 📊 Device Tree 配置對應

### Mailbox 配置
```dts
mailbox: mailbox@2e000000 {
    compatible = "arm,mhu-doorbell";
    reg = <0x0 0x2e000000 0x0 0x1000>;  // ← 這個暫存器地址
    interrupts = <GIC_SPI 36 IRQ_TYPE_LEVEL_HIGH>;
    #mbox-cells = <2>;
};

firmware {
    scmi {
        compatible = "arm,scmi";
        mboxes = <&mailbox 0 0>, <&mailbox 0 1>;  // ← 使用 mailbox
        mbox-names = "tx", "rx";
        shmem = <&scp_shmem>;  // ← 共享記憶體
    };
};
```

### SMC 配置
```dts
firmware {
    scmi {
        compatible = "arm,scmi-smc";
        arm,smc-id = <0x82000010>;  // ← SMC 功能 ID
        shmem = <&scp_shmem>;
    };
};
```

## 🔍 除錯方法

### 1. 追蹤 Mailbox 操作
```bash
# 啟用 mailbox 除錯
echo 'file drivers/mailbox/* +p' > /sys/kernel/debug/dynamic_debug/control

# 監控中斷
cat /proc/interrupts | grep mhu
```

### 2. 檢查共享記憶體
```bash
# 檢查共享記憶體映射
cat /proc/iomem | grep scmi

# 使用 devmem 讀取共享記憶體內容 (需要 root)
devmem 0x4e000000 32  # 讀取共享記憶體標頭
```

### 3. SCMI 傳輸 Trace
```bash
# 啟用 SCMI 傳輸 trace
echo 1 > /sys/kernel/debug/tracing/events/scmi/scmi_xfer_begin/enable
echo 1 > /sys/kernel/debug/tracing/events/scmi/scmi_xfer_end/enable
cat /sys/kernel/debug/tracing/trace_pipe
```

## ✅ 答案總結

**Linux 最後從哪裡傳送給 SCP？**

1. **函數層面**：`mailbox_send_message()` 或 `smc_send_message()`
2. **檔案位置**：`drivers/firmware/arm_scmi/mailbox.c` 或 `smc.c`
3. **硬體操作**：透過 mailbox driver 的 `writel_relaxed()` 寫入硬體暫存器
4. **實際機制**：
   - 寫入共享記憶體 (SCMI 訊息內容)
   - 觸發硬體中斷 (通知 SCP)
   - SCP 收到中斷後讀取共享記憶體

所以答案是：**SCMI Transport Layer 中的 `mailbox_send_message()` 函數，透過 `mbox_send_message()` 最終呼叫 mailbox 硬體驅動程式寫入暫存器觸發中斷給 SCP**。