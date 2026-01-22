# USB 中斷傳輸 (Interrupt Transfer) 完整流程詳解

## 1. 問題總覽

本文件將以 High Speed USB 中斷傳輸為例，回答以下核心問題：

1. **`usb_submit_urb()` 多久呼叫一次？**
2. **呼叫時 xHCI 內部做什麼？**
3. **中斷何時觸發？**
4. **資料如何讀取？**
5. **如何 ACK 給 Device？**
6. **下一筆傳輸如何發生？**

---

## 2. usb_submit_urb() 呼叫時機

### 2.1 上層驅動程式的輪詢機制

對於中斷傳輸，`usb_submit_urb()` **不是由計時器週期性呼叫**，而是由設備驅動程式在完成上一次傳輸後**立即重新提交**。

```
┌─────────────────────────────────────────────────────────────────────┐
│                    中斷傳輸的輪詢循環                                │
├─────────────────────────────────────────────────────────────────────┤
│                                                                     │
│   [設備驅動程式]                                                    │
│         │                                                          │
│         │  1. 第一次呼叫 usb_submit_urb()                           │
│         ↓                                                          │
│   [usb_submit_urb]                                                 │
│         │                                                          │
│         │  2. xHCI 處理並完成傳輸                                   │
│         ↓                                                          │
│   [完成回調函數]                                                    │
│         │                                                          │
│         │  3. 在回調函數中再次呼叫 usb_submit_urb()                  │
│         ↓                                                          │
│   [回到步驟 2]                                                      │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

### 2.2 實際範例：ldusb 驅動程式 ([`drivers/usb/misc/ldusb.c`](drivers/usb/misc/ldusb.c))

```c
static void ld_interrupt_complete(struct urb *urb)
{
    struct ld_device *dev = urb->context;
    int status = urb->status;
    
    switch (status) {
    case 0:  // 傳輸成功完成
        /* 處理接收到的資料 */
        process_data(urb->transfer_buffer, urb->actual_length);
        break;
    }
    
    // 關鍵：傳輸完成後立即重新提交 URB
    if (atomic_read(&dev->probe_count)) {
        usb_submit_urb(urb, GFP_ATOMIC);  // 再次提交，開始下一輪輪詢
    }
}
```

### 2.3 輪詢間隔由誰決定？

輪詢間隔由 **bInterval** 決定，但這是**硬體層面的間隔**，不是軟體呼叫頻率：

| 參數 | 位置 | 意義 |
|------|------|------|
| `bInterval` | USB 端點描述符 | xHC 多久向 Device 發起一次 IN 請求 |
| `usb_submit_urb()` 頻率 | 軟體 | 每次傳輸完成後立即呼叫 |

**重要**：軟體層面 `usb_submit_urb()` 是**連續呼叫**的，每次都在上一次完成後立即進行。真正的「等待」發生在**硬體層面**。

---

## 3. usb_submit_urb() 呼叫時 xHCI 內部流程

### 3.1 流程總覽

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    usb_submit_urb() 內部流程                            │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  1. usb_submit_urb() 被調用                                              │
│         │                                                                │
│         ↓                                                                │
│  2. USB Core 調用 xHCI 的 urb_enqueue()                                  │
│         │                                                                │
│         ↓                                                                │
│  3. 創建 xhci_td (Transfer Descriptor)                                   │
│         │                                                                │
│         ↓                                                                │
│  4. 將 TD 拆分為 TRB (Transfer Request Block)                           │
│         │                                                                │
│         ↓                                                                │
│  5. TRB 入隊到端點傳輸環 (Endpoint Transfer Ring)                        │
│         │                                                                │
│         ↓                                                                │
│  6. 寫入 Doorbell Register                                              │
│         │                                                                │
│         ↓                                                                │
│  7. 返回 (此時傳輸由 xHC 硬體處理)                                       │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 3.2 詳細步驟說明

#### 步驟 1-2: URB 進入 xHCI 驅動

```c
// usb/core/hcd.c
int usb_submit_urb(struct urb *urb, gfp_t mem_flags)
{
    // ... 驗證和初始化 ...
    
    // 調用主機控制器特定的 enqueue 函數
    int ret = hcd->driver->urb_enqueue(hcd, urb, mem_flags);
    return ret;
}
```

#### 步驟 3: 創建 Transfer Descriptor ([`xhci.h:1534`](drivers/usb/host/xhci.h:1534))

```c
struct xhci_td {
    struct list_head td_list;           // TD 列表項
    struct list_head cancelled_td_list; // 取消的 TD 列表
    struct urb *urb;                    // 關聯的 URB
    struct xhci_segment *start_seg;     // 起始段
    union xhci_trb *first_trb;          // 第一個 TRB
    union xhci_trb *last_trb;           // 最後一個 TRB
    struct xhci_segment *bounce_seg;    // 彈跳緩衝區段
    bool urb_length_set;                // URB 長度是否已設置
};
```

#### 步驟 4-5: TRB 入隊 ([`xhci-ring.c:204`](drivers/usb/host/xhci-ring.c:204))

```c
static void inc_enq(struct xhci_hcd *xhci, struct xhci_ring *ring,
                    bool more_trbs_coming)
{
    u32 chain;
    union xhci_trb *next;
    
    // 獲取前一個 TRB 的鏈接位
    chain = le32_to_cpu(ring->enqueue->generic.field[3]) & TRB_CHAIN;
    
    // 減少可用 TRB 數量
    if (!trb_is_link(ring->enqueue))
        ring->num_trbs_free--;
    
    // 移動入隊指標到下一個位置
    next = ++(ring->enqueue);
    
    // 處理連結 TRB (用於環段連結)
    while (trb_is_link(next)) {
        // 如果沒有更多 TRB 入隊，則停止
        if (!chain && !more_trbs_coming)
            break;
        
        // 處理鏈接位的quirk
        if (!(ring->type == TYPE_ISOC &&
              (xhci->quirks & XHCI_AMD_0x96_HOST)) &&
            !xhci_link_trb_quirk(xhci)) {
            next->link.control &= cpu_to_le32(~TRB_CHAIN);
            next->link.control |= cpu_to_le32(chain);
        }
        
        // 記憶體屏障確保寫入完成
        wmb();
        
        // 切換循環位 (Cycle Bit)
        next->link.control ^= cpu_to_le32(TRB_CYCLE);
        
        // 切換到下一個環段
        ring->enq_seg = ring->enq_seg->next;
        ring->enqueue = ring->enq_seg->trbs;
        next = ring->enqueue;
    }
}
```

#### 步驟 6: Doorbell 通知 ([`xhci-ring.c:386`](drivers/usb/host/xhci-ring.c:386))

```c
void xhci_ring_ep_doorbell(struct xhci_hcd *xhci,
        unsigned int slot_id,
        unsigned int ep_index,
        unsigned int stream_id)
{
    __le32 __iomem *db_addr = &xhci->dba->doorbell[slot_id];
    struct xhci_virt_ep *ep = &xhci->devs[slot_id]->eps[ep_index];
    unsigned int ep_state = ep->ep_state;
    
    // 檢查端點狀態
    if ((ep_state & EP_STOP_CMD_PENDING) ||
        (ep_state & SET_DEQ_PENDING) ||
        (ep_state & EP_HALTED) ||
        (ep_state & EP_CLEARING_TT))
        return;
    
    // 寫入 Doorbell 值，通知 xHC 有新工作
    writel(DB_VALUE(ep_index, stream_id), db_addr);
}
```

**Doorbell 值的意義**：
- 告訴 xHC：「端點 X 有新的 TRB 在傳輸環上，請開始處理」
- xHC 收到 Doorbell 後，會從傳輸環讀取 TRB 並執行 DMA 傳輸

---

## 4. 中斷何時觸發？

### 4.1 觸發時機

中斷觸發的時機有兩種：

#### 4.1.1 傳輸完成中斷 (IOC - Interrupt on Completion)

如果 TRB 設置了 `TRB_IOC` 位，傳輸完成時會觸發中斷：

```c
// 設置 IOC 位
trb->generic.field[3] |= TRB_IOC;  // Interrupt on Completion
```

#### 4.1.2 短封包中斷 (ISP - Interrupt on Short Packet)

如果設置了 `TRB_ISP` 位，當收到比預期短的封包時觸發中斷：

```c
// 設置 ISP 位
trb->generic.field[3] |= TRB_ISP;  // Interrupt on Short Packet
```

### 4.2 硬體層面的中斷觸發流程

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    中斷觸發流程                                          │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  [xHC 硬體]                                                             │
│       │                                                                │
│       │  1. 從傳輸環讀取 TRB                                            │
│       │       │                                                        │
│       │       ↓                                                        │
│       │  2. 執行 DMA 傳輸 (與 Device 通訊)                              │
│       │       │                                                        │
│       │       ↓                                                        │
│       │  3. 產生 Transfer Event                                        │
│       │       │                                                        │
│       │       ↓                                                        │
│       │  4. 將 Event 寫入 Event Ring                                   │
│       │       │                                                        │
│       │       ↓                                                        │
│       │  5. 如果 TRB 設置了 IOC/ISP，觸發 MSI-X 中斷                    │
│       │       │                                                        │
│       ↓       ↓                                                        │
│  [CPU]  [PCIe/系統匯流排]                                               │
│       │                                                                │
│       ↓                                                                │
│  [中斷處理常式]                                                         │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 4.3 事件環 (Event Ring) 處理

```c
// 傳輸事件結構 ([xhci.h:1070](drivers/usb/host/xhci.h:1070))
struct xhci_transfer_event {
    __le64 buffer;           // 緩衝區地址
    __le32 transfer_len;     // 傳輸長度
    __le32 flags;            // 標誌 (包含完成碼)
};

// 完成碼定義 ([xhci.h:1088](drivers/usb/host/xhci.h:1088))
#define COMP_SUCCESS         1   // 成功
#define COMP_SHORT_PACKET   13   // 短封包
#define COMP_STALL_ERROR     6   // 端點停滯
```

---

## 5. 資料如何讀取？

### 5.1 DMA 傳輸過程

**重要觀念**：資料不是由 CPU 讀取的，而是由 **xHC 硬體通過 DMA 直接讀取/寫入記憶體**。

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    DMA 資料傳輸                                          │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  [USB Device]                                                           │
│       │    DATA IN                                                      │
│       │    (設備發送資料)                                               │
│       ↓                                                                 │
│  [xHC 控制器]                                                           │
│       │    │                                                            │
│       │    │  DMA 引擎直接將資料寫入記憶體                               │
│       │    ↓                                                            │
│  [系統記憶體]  ◄────────────────────────────────────────┐               │
│       │                     DMA 傳輸               │                    │
│       ↓                                              │                    │
│  [CPU]                                               │                    │
│       │                                              │                    │
│       │  中斷處理常式                               │                    │
│       │  (只讀取狀態，不讀取資料)                   │                    │
│       ↓                                              ↓                    │
│  [驅動程式]        ◄──────────────────────────────────┘               │
│       │                                                                │
│       │  直接訪問 transfer_buffer                                       │
│       ↓                                                                │
│  [應用程式]                                                            │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 5.2 資料訪問

```c
static void ld_interrupt_complete(struct urb *urb)
{
    struct ld_device *dev = urb->context;
    
    if (urb->status == 0) {
        // urb->transfer_buffer 已經包含 DMA 傳輸的資料
        // CPU 可以直接訪問這個緩衝區
        unsigned char *data = urb->transfer_buffer;
        int length = urb->actual_length;
        
        // 處理資料
        for (int i = 0; i < length; i++) {
            process_byte(data[i]);
        }
    }
}
```

---

## 6. 如何 ACK 給 Device？

### 6.1 USB 協定的 ACK 機制

**ACK 是由 xHC 硬體自動處理的**，不需要軟體干預：

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    USB 協定層面的 ACK                                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  [Device]          [xHC]                                                │
│     │                │                                                   │
│     │  --- DATA ---> │  設備發送資料                                     │
│     │                │                                                   │
│     │  <-- ACK ---   │  xHC 自動發送 ACK                                │
│     │                │                                                   │
│     │  (設備收到 ACK，知道資料被成功接收)                                │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 6.2 軟體層面的「ACK」

軟體層面的「ACK」是指**處理完成並歸還 URB**：

```c
// URB 歸還 ([xhci-ring.c:638](drivers/usb/host/xhci-ring.c:638))
static void xhci_giveback_urb_in_irq(struct xhci_hcd *xhci,
                                     struct xhci_td *cur_td, int status)
{
    struct urb *urb = cur_td->urb;
    struct usb_hcd *hcd = bus_to_hcd(urb->dev->bus);
    
    // 釋放 URB 私有資料
    xhci_urb_free_priv(urb->hcpriv);
    
    // 從端點移除 URB
    usb_hcd_unlink_urb_from_ep(hcd, urb);
    
    // 解鎖，避免回調中死鎖
    spin_unlock(&xhci->lock);
    
    // 歸還 URB，觸發上層驅動的完成回調
    usb_hcd_giveback_urb(hcd, urb, status);
    
    // 重新獲取鎖
    spin_lock(&xhci->lock);
}
```

---

## 7. 下一筆中斷傳輸如何發生？

### 7.1 完整循環流程

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    中斷傳輸的完整循環                                    │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  [1] usb_submit_urb()                                                   │
│       │                                                                │
│       ↓                                                                │
│  [2] TRB 入隊 + Doorbell                                                │
│       │                                                                │
│       ↓                                                                │
│  [3] xHC 硬體執行 DMA 傳輸                                              │
│       │                                                                │
│       │  (bInterval 決定硬體多久發起一次輪詢)                            │
│       │                                                                │
│       ↓                                                                │
│  [4] 傳輸完成，產生 Event                                                │
│       │                                                                │
│       ↓                                                                │
│  [5] 觸發中斷 (如果設置了 IOC)                                           │
│       │                                                                │
│       ↓                                                                │
│  [6] 中斷處理常式處理 Event                                              │
│       │                                                                │
│       ↓                                                                │
│  [7] 歸還 URB，調用完成回調                                              │
│       │                                                                │
│       ↓                                                                │
│  [8] 完成回調中再次呼叫 usb_submit_urb()  ← 回到步驟 1                  │
│       │                                                                │
│       ↓                                                                │
│  [9] 循環繼續                                                            │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 7.2 bInterval 的實際作用

對於 **High Speed** 設備，`bInterval` 的計算方式：

```
輪詢間隔 (微秒) = 2^(bInterval-1) × 125 µs
```

**範例**：
- `bInterval = 4` → 間隔 = 2^(4-1) × 125 µs = 8 × 125 µs = 1000 µs = 1ms
- `bInterval = 6` → 間隔 = 2^(6-1) × 125 µs = 32 × 125 µs = 4000 µs = 4ms

**重要**：`bInterval` 是 xHC 硬體向設備發起輪詢的間隔，但軟體層面的 `usb_submit_urb()` 是**連續呼叫**的，每次都在上一次完成後立即進行。

### 7.3 時序圖

```
時間軸 ──────────────────────────────────────────────────────────────────→

[Device]     │ 資料0  │      │ 資料1  │      │ 資料2  │
             ▼        │      ▼        │      ▼        │
[xHC 硬體]   │ 輪詢   │ 等待 │ 輪詢   │ 等待 │ 輪詢   │
             │        │ bInt │        │ bInt │        │
             ▼        │      ▼        │      ▼        │
[Event Ring] │ Event0 │      │ Event1 │      │ Event2 │
             ▼        │      ▼        │      ▼        │
[中斷]       │ IRQ    │      │ IRQ    │      │ IRQ    │
             ▼        │      ▼        │      ▼        │
[軟體]       │ 處理   │ 提交 │ 處理   │ 提交 │ 處理   │
             │ 完成   │ 新URB│ 完成   │ 新URB│ 完成   │
             ▼        ▼      ▼        ▼      ▼        ▼
[URB 狀態]   完成 ──→ 提交 ──→ 完成 ──→ 提交 ──→ 完成
```

---

## 8. 總結：完整問答

### Q1: usb_submit_urb() 多久呼叫一次？

**答**：對於中斷傳輸，`usb_submit_urb()` **不是按固定時間間隔呼叫**的。它是在**每次傳輸完成後**，由完成回調函數**立即再次呼叫**。所以軟體層面的呼叫是連續的，沒有「等待」。

### Q2: 呼叫時 xHCI 內部做什麼？

**答**：
1. 創建 Transfer Descriptor (TD)
2. 將 TD 拆分為一個或多個 TRB
3. 將 TRB 入隊到端點傳輸環
4. 寫入 Doorbell Register 通知 xHC

### Q3: 中斷何時觸發？

**答**：當 TRB 設置了 `TRB_IOC` (Interrupt on Completion) 或 `TRB_ISP` (Interrupt on Short Packet) 位時，傳輸完成會觸發 MSI-X 中斷。

### Q4: 資料如何讀取？

**答**：資料由 **xHC 硬體通過 DMA 直接傳輸到記憶體**。CPU 不需要「讀取」資料，只需直接訪問 `urb->transfer_buffer` 即可。

### Q5: 如何 ACK 給 Device？

**答**：ACK 是 **xHC 硬體自動處理**的。軟體層面的「ACK」是指處理完成並歸還 URB。

### Q6: 下一筆傳輸如何發生？

**答**：在完成回調函數中**再次呼叫 `usb_submit_urb()`**，將新的 TRB 入隊並敲 Doorbell，xHC 會根據 `bInterval` 設定的間隔進行下一次輪詢。

---

## 9. 參考資料

- Linux 核心源碼: [`drivers/usb/host/xhci-ring.c`](drivers/usb/host/xhci-ring.c)
- Linux 核心源碼: [`drivers/usb/host/xhci.h`](drivers/usb/host/xhci.h)
- Linux 核心源碼: [`drivers/usb/misc/ldusb.c`](drivers/usb/misc/ldusb.c)
- xHCI 規範: [Intel xHCI Specification](https://www.intel.com/content/www/us/en/io/universal-serial-bus/extensible-host-controler-interface-usb-xhci.html)
- USB 規範: [USB 3.2 Specification](https://usb.org/documents)

---

## 10. XHCI Bulk/Interrupt 定期傳輸機制

### 10.1 問題背景

在 USB 傳輸中，interrupt 傳輸是**定期（periodic）**的，而 bulk 傳輸則不是。本節將詳細解釋 XHCI 如何實現定期傳輸。

### 10.2 核心元件

#### 10.2.1 Interval 欄位

端點 context 中的 `EP_INTERVAL` 欄位控制定期傳輸的頻率：

```c
// xhci.h:732-735
/* Interval - period between requests to an endpoint - 125u increments. */
#define EP_INTERVAL(p)          (((p) & 0xff) << 16)
#define EP_INTERVAL_TO_UFRAMES(p)   (1 << (((p) >> 16) & 0xff))
#define CTX_TO_EP_INTERVAL(p)       (((p) >> 16) & 0xff)
```

**關鍵點**：
- Interval 以 **125μs 為單位**（一個 USB microframe）
- 值範圍從 2^0 到 2^16 microframes，取決於端點類型
- 公式：`輪詢間隔 = 2^(bInterval-1) × 125 µs`

#### 10.2.2 Microframe Index (MFINDEX)

硬體暫存器追蹤當前的 microframe 編號：

```c
// xhci.h:531
struct xhci_run_regs {
    __le32          microframe_index;  /* MFINDEX - current microframe number */
    __le32          rsvd[7];
    struct xhci_intr_reg   ir_set[128];
};
```

**MFINDEX** 是 XHCI 定期傳輸排程的核心，HC 在每個 microframe 邊界檢查是否需要執行傳輸。

#### 10.2.3 傳輸環 (Transfer Ring)

環形緩衝區儲存待處理的 TRB：

```c
// xhci.h:1594-1617
struct xhci_ring {
    struct xhci_segment    *first_seg;
    struct xhci_segment    *last_seg;
    union  xhci_trb        *enqueue;
    struct xhci_segment    *enq_seg;
    union  xhci_trb        *dequeue;
    struct xhci_segment    *deq_seg;
    struct list_head       td_list;
    u32            cycle_state;
    enum xhci_ring_type    type;
    /* ... */
};
```

#### 10.2.4 Doorbell 機制

軟體透過 ring doorbell 通知 HC 有待處理的傳輸：

```c
// xhci-ring.c:386-408
void xhci_ring_ep_doorbell(struct xhci_hcd *xhci,
        unsigned int slot_id,
        unsigned int ep_index,
        unsigned int stream_id)
{
    __le32 __iomem *db_addr = &xhci->dba->doorbell[slot_id];
    struct xhci_virt_ep *ep = &xhci->devs[slot_id]->eps[ep_index];
    unsigned int ep_state = ep->ep_state;

    /* 檢查端點狀態 */
    if ((ep_state & EP_STOP_CMD_PENDING) ||
        (ep_state & SET_DEQ_PENDING) ||
        (ep_state & EP_HALTED) ||
        (ep_state & EP_CLEARING_TT))
        return;
    
    /* 寫入 Doorbell 值，通知 xHC 有新工作 */
    writel(DB_VALUE(ep_index, stream_id), db_addr);
}
```

### 10.3 定期傳輸排程流程

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    XHCI 定期傳輸排程流程                                 │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  [1] 軟體層面                                                           │
│       │  usb_submit_urb()                                               │
│       │  將 TRB 加入傳輸環                                               │
│       │  Ring doorbell 通知 HC                                          │
│       │       │                                                         │
│       │       ↓                                                         │
│  [2] HC 硬體層面                                                        │
│       │  監控 MFINDEX (microframe 計數器)                               │
│       │  在每個 microframe 邊界檢查：                                    │
│       │    - 端點的 interval 欄位                                        │
│       │    - 傳輸環中是否有待處理的 TRB                                  │
│       │    - 當前 microframe 是否符合排程                                │
│       │       │                                                         │
│       │       ↓                                                         │
│  [3] 傳輸執行                                                           │
│       │  HC 在排程時間執行傳輸                                           │
│       │  產生 transfer event                                            │
│       │       │                                                         │
│       │       ↓                                                         │
│  [4] 中斷處理                                                           │
│       │  軟體處理 event                                                  │
│       │  歸還 URB                                                        │
│       │       │                                                         │
│       │       ↓                                                         │
│  [5] 回到步驟 1                                                         │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 10.4 Interrupt vs Bulk 傳輸

| 特性 | Interrupt 傳輸 | Bulk 傳輸 |
|------|----------------|-----------|
| **定期性** | 是，定期排程 | 否 |
| **Interval 欄位** | 使用，不為 0 | 忽略或為 0 |
| **排程方式** | HC 根據 interval 自動排程 | 軟體 ring doorbell 時盡快服務 |
| **頻寬保證** | 有，保證延遲上限 | 無，先到先服務 |
| **適用場景** | 鍵盤、滑鼠、遊戲手把 | 隨身碟、印表機 |

### 10.5 端點類型定義

```c
// xhci.h:753-759
#define ISOC_OUT_EP     1   // 等時輸出
#define BULK_OUT_EP     2   // 大量輸出
#define INT_OUT_EP      3   // 中斷輸出
#define CTRL_EP         4   // 控制
#define ISOC_IN_EP      5   // 等時輸入
#define BULK_IN_EP      6   // 大量輸入
#define INT_IN_EP       7   // 中斷輸入
```

### 10.6 頻寬管理

XHCI 維護頻寬資訊以確保定期傳輸可以在匯流排頻寬內排程：

```c
// xhci.h:865-874
struct xhci_bw_info {
    unsigned int        ep_interval;   /* ep_interval 是零基準 */
    unsigned int        mult;          /* mult 和 num_packets 是一基準 */
    unsigned int        num_packets;
    unsigned int        max_packet_size;
    unsigned int        max_esit_payload;
    unsigned int        type;
};
```

### 10.7 總結

**Interrupt 傳輸的定期機制**：
1. 端點 context 中的 `interval` 欄位定義服務間隔（125μs 為單位）
2. HC 監控 `MFINDEX` 硬體暫存器
3. 在每個符合 interval 的 microframe，HC 檢查傳輸環
4. 如果有待處理的 TRB，則執行傳輸
5. 傳輸完成後產生 event，軟體處理並重新提交 URB

**Bulk 傳輸**：
- 無定期排程
- 軟體 ring doorbell 時，HC 在可用頻寬內盡快服務
- 適合大量資料傳輸，但不保證延遲

---

## 11. XHCI 定期傳輸程式流程詳解

### 11.1 軟體層面流程

#### 步驟 1.1: [`usb_submit_urb()`](drivers/usb/core/hcd.c:1234) 提交 URB

```c
// usb/core/hcd.c
int usb_submit_urb(struct urb *urb, gfp_t mem_flags)
{
    struct usb_hcd *hcd = bus_to_hcd(urb->dev->bus);
    int ret;

    // 驗證 URB 參數
    if (!urb || !urb->dev || !urb->ep)
        return -EINVAL;

    // 獲取端點資訊
    urb->ep = usb_get_ep(urb->dev, urb->ep->desc.bEndpointAddress);

    // 調用主機控制器特定的 enqueue 函數
    ret = hcd->driver->urb_enqueue(hcd, urb, mem_flags);
    return ret;
}
```

#### 步驟 1.2: 建立 TRB 並加入傳輸環

```c
// drivers/usb/host/xhci-ring.c:3107
int xhci_queue_intr_tx(struct xhci_hcd *xhci, gfp_t mem_flags,
        struct urb *urb, int slot_id, unsigned int ep_index)
{
    struct xhci_ep_ctx *ep_ctx;
    unsigned int num_tds;

    // 驗證 interval 是否匹配
    ep_ctx = xhci_get_ep_ctx(xhci, urb->dev->slot_id, ep_index);
    if (check_interval(xhci, urb, ep_ctx) < 0)
        return -EINVAL;

    // 呼叫 bulk_tx 函數處理實際的 TRB 建立
    return xhci_queue_bulk_tx(xhci, mem_flags, urb, slot_id, ep_index);
}
```

#### 步驟 1.3: 將 TRB 加入傳輸環

```c
// drivers/usb/host/xhci-ring.c:3231
int xhci_queue_bulk_tx(struct xhci_hcd *xhci, gfp_t mem_flags,
        struct urb *urb, int slot_id, unsigned int ep_index)
{
    struct xhci_ring *ring;
    struct xhci_td *td;
    unsigned int num_tds;
    int ret;

    ring = xhci_urb_to_transfer_ring(xhci, urb);
    if (!ring)
        return -EINVAL;

    // 為每個 URB 建立 TD
    num_tds = DIV_ROUND_UP(urb->transfer_buffer_length,
                           urb->maxpacket);

    // 分配 TD 結構
    td = xhci_alloc_td(xhci, urb, GFP_ATOMIC);

    // 將 TD 拆分為 TRB 並加入傳輸環
    ret = queue_bulk_tx(xhci, ring, num_tds, td, urb, mem_flags);

    return ret;
}
```

#### 步驟 1.4: Ring Doorbell 通知 HC

```c
// drivers/usb/host/xhci-ring.c:386-408
void xhci_ring_ep_doorbell(struct xhci_hcd *xhci,
        unsigned int slot_id, unsigned int ep_index,
        unsigned int stream_id)
{
    __le32 __iomem *db_addr = &xhci->dba->doorbell[slot_id];
    struct xhci_virt_ep *ep = &xhci->devs[slot_id]->eps[ep_index];
    unsigned int ep_state = ep->ep_state;

    // 檢查端點狀態
    if ((ep_state & EP_STOP_CMD_PENDING) ||
        (ep_state & SET_DEQ_PENDING) ||
        (ep_state & EP_HALTED) ||
        (ep_state & EP_CLEARING_TT))
        return;

    // 寫入 Doorbell 值，通知 xHC 有新工作
    // DB_VALUE 格式: (ep_index << 8) | stream_id
    writel(DB_VALUE(ep_index, stream_id), db_addr);
}
```

**Doorbell 機制說明**：
- Doorbell Register 位於 `xhci->dba->doorbell[slot_id]`
- 寫入 Doorbell 告訴 HC：「端點 X 有新的 TRB 在傳輸環上」
- HC 收到 Doorbell 後，會從傳輸環讀取 TRB 並執行 DMA 傳輸

### 11.2 HC 硬體層面流程

#### 步驟 2.1: 監控 MFINDEX 暫存器

```c
// xhci.h:531-558
struct xhci_run_regs {
    __le32  microframe_index;  /* MFINDEX - current microframe number */
    __le32  rsvd[7];
    struct xhci_intr_reg   ir_set[128];
};

struct xhci_intr_reg {
    __le32  imod;      /* Interrupt Moderation Register */
    __le32  erst_size; /* Event Ring Segment Table Size */
    __le64  erst_base; /* Event Ring Segment Table Base Address */
    __le64  erdp;      /* Event Ring Dequeue Pointer */
};
```

**MFINDEX 運作原理**：
- HC 內部有一個 14 位元的計數器，每 125μs 遞增一次
- 計數器值對應 USB microframe 編號 (0-7999，對應 1 秒)
- HC 在每個 microframe 邊界檢查是否需要執行定期傳輸

#### 步驟 2.2: 定期傳輸排程判斷

HC 硬體在每個 microframe 執行以下檢查：

```
┌─────────────────────────────────────────────────────────────────┐
│                    HC Microframe 邊界檢查                        │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. 讀取 MFINDEX 暫存器                                         │
│         │                                                       │
│         ↓                                                       │
│  2. 檢查所有已配置的端點：                                       │
│     - 讀取端點 context 中的 EP_INTERVAL 欄位                     │
│     - 計算: schedule_frame = MFINDEX % (2^(interval-1))         │
│     - 如果 schedule_frame == 0，則需要服務此端點                 │
│         │                                                       │
│         ↓                                                       │
│  3. 檢查傳輸環：                                                 │
│     - 讀取 enqueue/dequeue 指標                                 │
│     - 如果 enqueue != dequeue，表示有待處理的 TRB               │
│         │                                                       │
│         ↓                                                       │
│  4. 執行傳輸（如果符合條件）                                     │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

#### 步驟 2.3: Interval 欄位解讀

```c
// xhci.h:732-740
/* Interval - period between requests to an endpoint - 125u increments. */
#define EP_INTERVAL(p)          (((p) & 0xff) << 16)
#define EP_INTERVAL_TO_UFRAMES(p)   (1 << (((p) >> 16) & 0xff))
#define CTX_TO_EP_INTERVAL(p)       (((p) >> 16) & 0xff)

/* 端點 context 結構 (xHCI 1.0) */
struct xhci_ep_ctx {
    __le32  ep_info;        /* Endpoint State */
    __le32  ep_info2;       /* Endpoint Type, Max Packet Size, etc. */
    __le64  tr_dequeue_ptr; /* Transfer Ring Dequeue Pointer */
    __le32  length;         /* Average TRB Length */
    __le32  interval;       /* bInterval (125us units, stored in bits 16-23) */
    __le32  maxp;           /* Max Packet Size */
};
```

**Interval 計算範例**：
- `bInterval = 4` → `EP_INTERVAL = 0x40000` → `interval = 4`
- 服務間隔 = `2^(4-1) × 125μs = 8 × 125μs = 1000μs = 1ms`
- HC 每 8 個 microframe 檢查一次此端點

### 11.3 傳輸執行流程

#### 步驟 3.1: HC 讀取 TRB 並執行 DMA

```c
// TRB 結構定義 (xhci.h:978-1002)
struct xhci_trb {
    __le64  ptr;        /* Data Buffer Pointer */
    __le32  status;     /* Status Field (Transfer Length, Completion Code) */
    __le32  control;    /* Control Field (Cycle Bit, Chain, IOC, ISP, etc.) */
};

// Normal TRB (用於資料傳輸)
struct xhci_normal_trb {
    __le64  buf_ptr;        /* Buffer Pointer */
    __le32  buf_status;     /* Transfer Length, Interrupter Target */
    __le32  control;        /* Cycle Bit, Chain, Interrupt on Short Packet */
};
```

**TRB 處理流程**：
1. HC 讀取傳輸環指標，獲取第一個 TRB
2. 檢查 Cycle Bit 是否匹配 (確保軟體沒有覆蓋)
3. 從 `buf_ptr` 指向的記憶體位置讀取資料
4. 通過 USB 匯流排與 Device 通訊
5. 根據 `control` 欄位決定是否產生中斷

#### 步驟 3.2: 產生 Transfer Event

```c
// xhci.h:1070-1090
struct xhci_transfer_event {
    __le64  buffer;       /* Data Buffer Pointer */
    __le32  transfer_len; /* Transfer Length */
    __le32  flags;        /* Completion Code, Endpoint ID, etc. */
};

// 完成碼定義
#define COMP_SUCCESS          1   /* 成功 */
#define COMP_SHORT_PACKET    13   /* 短封包 */
#define COMP_STALL_ERROR      6   /* 端點停滯 */
#define COMP_BABBLE_ERROR     4   /* 溢位錯誤 */
```

**Event 產生條件**：
- TRB 設置了 `TRB_IOC` (Interrupt on Completion) 位
- 傳輸完成（成功、短封包、錯誤）
- HC 將 Event 寫入 Event Ring

### 11.4 中斷處理流程

#### 步驟 4.1: 中斷觸發與處理常式

```c
// drivers/usb/host/xhci-ring.c:2673
int xhci_handle_event(struct xhci_hcd *xhci)
{
    union xhci_trb *event;
    int ret = 0;

    // 迴圈處理所有待處理的 event
    while (xhci->event_ring->dequeue != xhci->event_ring->enqueue) {
        event = xhci->event_ring->dequeue;

        // 讀取 event 類型
        trb_type = GET_TRB_TYPE(event->generic.field[3]);

        switch (trb_type) {
        case TRB_TRANSFER:
            ret = handle_tx_event(xhci, event);
            break;
        case TRB_PORT_STATUS_CHANGE:
            handle_port_status_change(xhci, event);
            break;
        // ... 其他 event 類型
        }

        // 移動 dequeue 指標
        inc_deq(xhci, xhci->event_ring);
    }

    return ret;
}
```

#### 步驟 4.2: 處理 Transfer Event

```c
// drivers/usb/host/xhci-ring.c:2303
static int handle_tx_event(struct xhci_hcd *xhci,
        union xhci_trb *event)
{
    struct xhci_transfer_event *trb_ev;
    struct xhci_td *cur_td;
    struct xhci_ep *ep;
    unsigned int slot_id;
    int ret;

    trb_ev = &event->transfer_event;
    slot_id = TRB_TO_SLOT_ID(event->generic.field[3]);

    // 根據 TRB 指標找到對應的 TD
    cur_td = xhci_find_td(xhci, ep, event->generic.field[0]);

    // 處理 TD 完成狀態
    ret = process_bulk_intr_td(xhci, cur_td, trb_ev, event);

    return ret;
}
```

#### 步驟 4.3: 處理 Bulk/Interrupt TD 完成

```c
// drivers/usb/host/xhci-ring.c:2224
static int process_bulk_intr_td(struct xhci_hcd *xhci,
        struct xhci_td *td, struct xhci_transfer_event *event,
        union xhci_trb *trb)
{
    struct urb *urb = td->urb;
    struct xhci_ring *ep_ring;
    int status;
    u32 comp_code;

    // 獲取完成碼
    comp_code = GET_COMP_CODE(event->flags);

    // 根據完成碼設置狀態
    switch (comp_code) {
    case COMP_SUCCESS:
        status = 0;
        // 計算實際傳輸長度
        urb->actual_length = event->transfer_len;
        break;
    case COMP_SHORT_PACKET:
        status = 0;
        urb->actual_length = event->transfer_len;
        break;
    case COMP_STALL_ERROR:
        status = -EPIPE;
        break;
    default:
        status = -ENODEV;
        break;
    }

    // 歸還 URB
    xhci_giveback_urb_in_irq(xhci, td, status);

    return 0;
}
```

#### 步驟 4.4: 歸還 URB 並呼叫完成回調

```c
// drivers/usb/host/xhci-ring.c:638
static void xhci_giveback_urb_in_irq(struct xhci_hcd *xhci,
        struct xhci_td *cur_td, int status)
{
    struct urb *urb = cur_td->urb;
    struct usb_hcd *hcd = bus_to_hcd(urb->dev->bus);

    // 釋放 URB 私有資料
    xhci_urb_free_priv(urb->hcpriv);

    // 從端點移除 URB
    usb_hcd_unlink_urb_from_ep(hcd, urb);

    // 解鎖，避免回調中死鎖
    spin_unlock(&xhci->lock);

    // 歸還 URB，觸發上層驅動的完成回調
    usb_hcd_giveback_urb(hcd, urb, status);

    // 重新獲取鎖
    spin_lock(&xhci->lock);
}
```

### 11.5 回到步驟 1：完成回調中重新提交 URB

```c
// 驅動程式完成回調範例 (drivers/usb/misc/ldusb.c)
static void ld_interrupt_complete(struct urb *urb)
{
    struct ld_device *dev = urb->context;
    int status = urb->status;

    switch (status) {
    case 0:  // 傳輸成功完成
        /* 處理接收到的資料 */
        process_data(urb->transfer_buffer, urb->actual_length);
        break;
    case -ENOENT:  // URB 被取消
        break;
    default:  // 其他錯誤
        dev_err(&dev->intf->dev, "URB error: %d\n", status);
        break;
    }

    // 關鍵：傳輸完成後立即重新提交 URB
    if (atomic_read(&dev->probe_count)) {
        usb_submit_urb(urb, GFP_ATOMIC);  // 再次提交，開始下一輪輪詢
    }
}
```

### 11.6 完整時序圖

```
時間軸 ──────────────────────────────────────────────────────────────────→

[軟體]       usb_submit_urb() ──> TRB 入隊 ──> Doorbell ──> 等待
                  │                    │            │
                  ▼                    ▼            ▼
[傳輸環]      [TRB]              [TRB]        [Doorbell]
                  │                    │            │
                  └────────────────────┴────────────┘
                                           │
[HC 硬體]                                  │
                  ┌────────────────────────┘
                  │
                  ▼
[MFINDEX]    0   1   2   3   4   5   6   7   8   ...
                  │       │               │
                  │       └─ 檢查 interval=4 ──> 執行傳輸
                  │
                  └─ 檢查 interval=4 ──> 執行傳輸
                           (bInterval=4, 每 8 個 microframe)
                           │
                           ▼
[Event Ring]                    [Event]
                           (傳輸完成)
                           │
                           ▼
[中斷]                         IRQ
                           │
                           ▼
[軟體]                  handle_tx_event() ──> process_bulk_intr_td()
                           │
                           ▼
                  xhci_giveback_urb_in_irq() ──> 完成回調
                           │
                           ▼
                  usb_submit_urb() ──> 回到步驟 1
```

### 11.7 關鍵函數呼叫鏈

```
usb_submit_urb()
    │
    └─> hcd->driver->urb_enqueue()
            │
            └─> xhci_urb_enqueue()
                    │
                    ├─> xhci_queue_intr_tx()
                    │       │
                    │       ├─> check_interval()
                    │       │
                    │       └─> xhci_queue_bulk_tx()
                    │               │
                    │               └─> queue_bulk_tx()
                    │                       │
                    │                       └─> 將 TRB 加入傳輸環
                    │
                    └─> xhci_ring_ep_doorbell()
                            │
                            └─> writel(DB_VALUE(...), doorbell)

[中斷發生]

xhci_irq()
    │
    └─> xhci_handle_event()
            │
            └─> handle_tx_event()
                    │
                    └─> process_bulk_intr_td()
                            │
                            └─> xhci_giveback_urb_in_irq()
                                    │
                                    └─> usb_hcd_giveback_urb()
                                            │
                                            └─> 完成回調函數
                                                    │
                                                    └─> usb_submit_urb()
                                                            (回到步驟 1)
```

### 11.8 xhci_irq 中斷類型說明

#### 11.8.1 中斷觸發源

`xhci_irq()` 是由 **Transfer Event** (`TRB_TRANSFER`, type=32) 觸發的。當軟體放置在傳輸環上的 TRB 執行完成後，HC 會產生一個 Transfer Event 寫入 Event Ring，如果該 TRB 設置了 `TRB_IOC` (Interrupt on Completion) 位，就會觸發中斷。

**重要區分**：

| 類型 | 編號 | 說明 |
|------|------|------|
| **TRB types** | 1-31 | 軟體放置在**傳輸環 (Transfer Ring)**上的請求區塊 |
| **Event types** | 32+ | HC 放置在**事件環 (Event Ring)**上的事件通知 |

對於 **Interrupt 傳輸**，軟體在傳輸環上放置的是 **`TRB_NORMAL`** (type=1) 區塊。當這個 TRB 執行完成後，HC 產生 **`TRB_TRANSFER`** (type=32) 事件。

**中斷觸發與事件處理的關係**：

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    TRB_IOC 與 TRB_TRANSFER 的關係                        │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  [軟體]                         [HC 硬體]                               │
│       │                              │                                  │
│       │  1. 放置 TRB_NORMAL           │                                  │
│       │     (設置 IOC 位)             │                                  │
│       │       │                       │                                  │
│       │       ↓                       │                                  │
│       │  2. Ring Doorbell            │                                  │
│       │       │                       │                                  │
│       │       └───────────────────────┤                                  │
│       │                              ↓                                  │
│       │                    3. 執行傳輸完成                               │
│       │                              │                                  │
│       │                              ↓                                  │
│       │                    4. 產生 TRB_TRANSFER 事件                     │
│       │                    5. 寫入 Event Ring                            │
│       │                              │                                  │
│       │                              ↓                                  │
│       │                    6. 觸發 MSI-X 中斷 (因為 IOC=1)               │
│       │                              │                                  │
│       │       ┌──────────────────────┤                                  │
│       │       │                      ↓                                  │
│       │       │              [CPU 接收中斷]                              │
│       │       │                      │                                  │
│       │       │                      ↓                                  │
│       │       │              xhci_irq() 被調用                          │
│       │       │                      │                                  │
│       │       │                      ↓                                  │
│       │       │              處理 TRB_TRANSFER 事件                      │
│       │       │                      │                                  │
│       │       │                      ↓                                  │
│       │       │              歸還 URB，呼叫完成回調                      │
│       │       │                      │                                  │
│       │       ↓                      ↓                                  │
│       │  usb_submit_urb() (下一輪)                                       │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

**關鍵點**：
- **`TRB_IOC`** 是軟體在**傳輸環 TRB** 上設置的標誌位，表示「傳輸完成後請通知我」
- **`TRB_TRANSFER`** 是 HC 在**事件環**上產生的事件類型，表示「某個傳輸已完成」
- 當 `TRB_IOC=1` 的 TRB 執行完成時，HC 會產生 `TRB_TRANSFER` 事件並觸發中斷
- `xhci_irq()` 被調用時，處理的就是這個 `TRB_TRANSFER` 事件

#### 11.8.2 xHCI 支援的中斷類型

xHCI 控制器支援三種中斷類型，按優先順序排列：

| 中斷類型 | 優先順序 | 說明 |
|---------|---------|------|
| **MSI-X** | 最高 | Message Signaled Interrupt - Extended，支援多個中斷向量，每個 CPU 核心可分配獨立中斷 |
| **MSI** | 中 | Message Signaled Interrupt，單一中斷向量 |
| **Legacy IRQ** | 最低 | 傳統 PCI 中斷，需要共享 IRQ |

#### 11.8.3 中斷初始化流程

```c
// drivers/usb/host/xhci.c:403-448
static int xhci_try_enable_msi(struct usb_hcd *hcd)
{
    struct xhci_hcd *xhci = hcd_to_xhci(hcd);
    struct pci_dev *pdev = to_pci_dev(hcd->self.controller);
    int ret;

    // 優先嘗試 MSI-X
    ret = xhci_setup_msix(xhci);
    if (ret)
        // 回退到 MSI
        ret = xhci_setup_msi(xhci);

    if (!ret) {
        hcd->msi_enabled = 1;
        return 0;
    }

    // 最後回退到 legacy IRQ
    if (!pdev->irq) {
        xhci_err(xhci, "No msi-x/msi found and no IRQ in BIOS\n");
        return -EINVAL;
    }

    ret = request_irq(pdev->irq, &usb_hcd_irq, IRQF_SHARED,
            hcd->irq_descr, hcd);
    return ret;
}
```

#### 11.8.4 MSI-X 中斷設定

```c
// drivers/usb/host/xhci.c:322-354
static int xhci_setup_msix(struct xhci_hcd *xhci)
{
    struct usb_hcd *hcd = xhci_to_hcd(xhci);
    struct pci_dev *pdev = to_pci_dev(hcd->self.controller);
    int i, ret;

    // 計算需要的 MSI-X 向量數量
    // 最多支援 num_online_cpus() + 1 個向量
    xhci->msix_count = min(num_online_cpus() + 1,
                HCS_MAX_INTRS(xhci->hcs_params1));

    // 分配 MSI-X 向量
    ret = pci_alloc_irq_vectors(pdev, xhci->msix_count, xhci->msix_count,
            PCI_IRQ_MSIX);
    if (ret < 0) {
        xhci_dbg_trace(xhci, trace_xhci_dbg_init,
                "Failed to enable MSI-X");
        return ret;
    }

    // 為每個向量註冊中斷處理常式
    for (i = 0; i < xhci->msix_count; i++) {
        ret = request_irq(pci_irq_vector(pdev, i), xhci_msi_irq, 0,
                "xhci_hcd", xhci_to_hcd(xhci));
        if (ret)
            goto disable_msix;
    }

    hcd->msix_enabled = 1;
    return ret;

disable_msix:
    // 錯誤處理：釋放已分配的中斷向量
    while (--i >= 0)
        free_irq(pci_irq_vector(pdev, i), xhci_to_hcd(xhci));
    pci_free_irq_vectors(pdev);
    return ret;
}
```

#### 11.8.5 中斷處理函數

```c
// drivers/usb/host/xhci-ring.c:2747-2784
irqreturn_t xhci_irq(struct usb_hcd *hcd)
{
    struct xhci_hcd *xhci = hcd_to_xhci(hcd);
    irqreturn_t ret;

    // 讀取並清除中斷狀態
    xhci->irq = 0;

    // 處理事件環中的所有事件
    ret = xhci_handle_event(xhci);

    // 如果 MSI-X 啟用，清除中斷狀態暫存器
    if (hcd->msi_enabled) {
        u32 irq_pending;
        irq_pending = readl(&xhci->op_regs->status);
        writel(irq_pending, &xhci->op_regs->status);
    }

    return ret;
}

// MSI/MSI-X 中斷處理入口
irqreturn_t xhci_msi_irq(int irq, void *hcd)
{
    return xhci_irq(hcd);
}
```

#### 11.8.6 中斷觸發時序圖

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    xHCI 中斷觸發時序                                      │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  [1] TRB 設置 IOC 位                                                    │
│       │                                                                │
│       ↓                                                                │
│  [2] HC 執行傳輸完成                                                    │
│       │                                                                │
│       ↓                                                                │
│  [3] HC 產生 Transfer Event 寫入 Event Ring                             │
│       │                                                                │
│       ↓                                                                │
│  [4] HC 觸發中斷 (MSI-X/MSI/Legacy)                                     │
│       │                                                                │
│       ↓                                                                │
│  [5] CPU 接收中斷，呼叫 xhci_msi_irq() 或 usb_hcd_irq()                  │
│       │                                                                │
│       ↓                                                                │
│  [6] xhci_irq() 讀取並清除中斷狀態                                       │
│       │                                                                │
│       ↓                                                                │
│  [7] xhci_handle_event() 處理 Event Ring 中的事件                       │
│       │                                                                │
│       ↓                                                                │
│  [8] 歸還 URB，呼叫完成回調                                             │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

#### 11.8.7 MSI-X 優勢

| 特性 | Legacy IRQ | MSI | MSI-X |
|------|-----------|-----|-------|
| **中斷向量數** | 1 | 1 | 最多 2048 |
| **CPU 親和性** | 固定 | 固定 | 可綁定到不同 CPU |
| **延遲** | 較高 | 中等 | 最低 |
| **中斷共享** | 需要 | 不需要 | 不需要 |
| **支援熱拔插** | 有限 | 有限 | 完整 |

**MSI-X 是 xHCI 的首選中斷方式**，因為：
1. 可以為每個 CPU 核心分配獨立的中斷向量，減少中斷衝突
2. 降低中斷延遲，提高傳輸效能
3. 支援更細緻的中斷親和性控制
4. 現代 PCIe xHCI 控制器普遍支援 MSI-X

---

### 11.9 Interrupt 傳輸的輪詢特性

#### 11.9.1 問題描述

在使用 IN token 讀取資料時，可能會遇到以下情況：
- Protocol Analyzer 顯示 Device 已經發送資料
- `usb_hcd_giveback_urb()` 被調用，驅動程式的完成回調執行
- 但驅動程式讀取 `urb->transfer_buffer` 時，資料已經在記憶體中

**這是正常的行為，符合 Spec**。

#### 11.9.2 Interrupt 傳輸是輪詢機制

Interrupt 傳輸的本質是**輪詢 (Polling)**，不是事件驅動：

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    Interrupt 傳輸的輪詢機制                              │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  [Device]     新資料產生          新資料產生          新資料產生         │
│       │            │                    │                    │          │
│       │            ▼                    ▼                    ▼          │
│  [HC 硬體]   輪詢(IN token)      輪詢(IN token)      輪詢(IN token)     │
│       │            │                    │                    │          │
│       │            ▼                    ▼                    ▼          │
│       │       收到資料             收到資料             收到資料          │
│       │            │                    │                    │          │
│       │            ▼                    ▼                    ▼          │
│  [中斷]       IRQ                  IRQ                  IRQ             │
│                                                                         │
│  問題：Device 產生新資料的時間點與 HC 輪詢的時間點不一定吻合             │
│  結果：最多可能延遲一個 bInterval 間隔                                    │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

#### 11.9.3 為什麼這是正常的？

| 特性 | 說明 |
|------|------|
| **輪詢機制** | HC 定期發送 IN token 詢問 Device 是否有資料 |
| **無主動通知** | Device 沒有能力主動通知 Host（不像 PCIe 有 MSI） |
| **資料產生時機** | 資料可能隨時產生，但 HC 只在固定的 bInterval 輪詢 |
| **最大延遲** | 最多一個 bInterval 間隔，這是 interrupt 傳輸的設計特性 |

#### 11.9.4 時序圖

```
時間軸 ──────────────────────────────────────────────────────────────────→

[Device]     資料A產生          資料B產生          資料C產生
     │           │                  │                  │
     │           ▼                  ▼                  ▼
[HC 輪詢]        IN token           IN token           IN token
     │           │                  │                  │
     │           ▼                  ▼                  ▼
     │      收到資料A           收到資料B           收到資料C
     │           │                  │                  │
     │           ▼                  ▼                  ▼
[中斷]          IRQ                 IRQ                 IRQ
     │           │                  │                  │
     │           ▼                  ▼                  ▼
[軟體]      讀取資料A           讀取資料B           讀取資料C

注意：資料B產生後，必須等到下一次輪詢才能讀取
     這就是為什麼 Protocol Analyzer 看到資料，但程式還沒讀到
```

#### 11.9.5 輪詢間隔計算

對於 **High Speed** 設備，`bInterval` 的計算方式：

```
輪詢間隔 (微秒) = 2^(bInterval-1) × 125 µs
```

**範例**：
- `bInterval = 4` → 間隔 = 2^(4-1) × 125 µs = 8 × 125 µs = 1000 µs = 1ms
- `bInterval = 6` → 間隔 = 2^(6-1) × 125 µs = 32 × 125 µs = 4000 µs = 4ms

#### 11.9.6 與 PCIe MSI 的比較

| 特性 | USB Interrupt 傳輸 | PCIe MSI/MSI-X |
|------|-------------------|----------------|
| **通知機制** | 輪詢 (Host 定期詢問) | 中斷 (Device 主動通知) |
| **延遲** | 最多一個 bInterval | 硬體中斷延遲 (通常 < 10μs) |
| **CPU 負擔** | 較高 (定期輪詢) | 較低 (事件驅動) |
| **資料新舊** | 資料可能已經存在一段時間 | 資料剛到達就被通知 |

#### 11.9.7 結論

**您觀察到的現象是正常的**：

1. Protocol Analyzer 看到資料 → Device 已經發送資料
2. 程式還沒讀到 → HC 還沒輪詢到，所以還沒寫入 DMA 記憶體
3. 這就是 interrupt 傳輸的工作方式

**設計取捨**：
- Interrupt 傳輸犧牲了即時性，換取簡單的硬體設計
- Device 不需要複雜的中斷產生電路
- Host 軟體可以批次處理多個 Device 的輪詢

**軟體層面的最佳實踐**：
1. 在完成回調中檢查 `urb->actual_length`
2. 處理資料後立即重新提交 URB (`usb_submit_urb()`)
3. 不要依賴 interrupt 傳輸處理即時性要求高的資料
4. 如果需要更低延遲，考慮使用 USB 3.0 或其他高速傳輸方式
