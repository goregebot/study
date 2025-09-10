/*
 * SCMI Clock Driver Example
 * 
 * 這個範例展示如何實作一個 SCMI clock driver，
 * 並與 SCP firmware 進行通訊來設定時鐘頻率
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/scmi_protocol.h>
#include <linux/clk-provider.h>
#include <linux/of.h>
#include <linux/slab.h>

/* SCMI Clock Driver 資料結構 */
struct scmi_clk_data {
    const struct scmi_protocol_handle *ph;
    const struct scmi_clk_proto_ops *ops;
    struct clk_hw hw;
    u32 id;
    const char *name;
};

struct scmi_clk_provider {
    const struct scmi_protocol_handle *ph;
    const struct scmi_clk_proto_ops *ops;
    struct scmi_clk_data **clks;
    int num_clocks;
    struct device *dev;
};

#define to_scmi_clk(hw) container_of(hw, struct scmi_clk_data, hw)

/*
 * SCMI Clock 操作函數實作
 * 這些函數會透過 SCMI 協議與 SCP firmware 通訊
 */

static int scmi_clk_enable(struct clk_hw *hw)
{
    struct scmi_clk_data *clk = to_scmi_clk(hw);
    int ret;
    
    dev_dbg(clk->ph->dev, "Enabling clock %s (ID: %u)\n", 
            clk->name, clk->id);
    
    /* 透過 SCMI 協議啟用時鐘 */
    ret = clk->ops->enable(clk->ph, clk->id);
    if (ret)
        dev_err(clk->ph->dev, "Failed to enable clock %s: %d\n",
                clk->name, ret);
    else
        dev_info(clk->ph->dev, "Clock %s enabled successfully\n", 
                 clk->name);
    
    return ret;
}

static void scmi_clk_disable(struct clk_hw *hw)
{
    struct scmi_clk_data *clk = to_scmi_clk(hw);
    int ret;
    
    dev_dbg(clk->ph->dev, "Disabling clock %s (ID: %u)\n", 
            clk->name, clk->id);
    
    /* 透過 SCMI 協議停用時鐘 */
    ret = clk->ops->disable(clk->ph, clk->id);
    if (ret)
        dev_err(clk->ph->dev, "Failed to disable clock %s: %d\n",
                clk->name, ret);
    else
        dev_info(clk->ph->dev, "Clock %s disabled successfully\n", 
                 clk->name);
}

static unsigned long scmi_clk_recalc_rate(struct clk_hw *hw,
                                         unsigned long parent_rate)
{
    struct scmi_clk_data *clk = to_scmi_clk(hw);
    u64 rate;
    int ret;
    
    /* 從 SCP firmware 取得目前時鐘頻率 */
    ret = clk->ops->rate_get(clk->ph, clk->id, &rate);
    if (ret) {
        dev_err(clk->ph->dev, "Failed to get rate for clock %s: %d\n",
                clk->name, ret);
        return 0;
    }
    
    dev_dbg(clk->ph->dev, "Clock %s current rate: %llu Hz\n", 
            clk->name, rate);
    
    return (unsigned long)rate;
}

static int scmi_clk_set_rate(struct clk_hw *hw, unsigned long rate,
                            unsigned long parent_rate)
{
    struct scmi_clk_data *clk = to_scmi_clk(hw);
    int ret;
    
    dev_info(clk->ph->dev, "Setting clock %s rate to %lu Hz\n", 
             clk->name, rate);
    
    /* 
     * 關鍵函數：透過 SCMI 協議設定時鐘頻率
     * 這會觸發與 SCP firmware 的通訊
     */
    ret = clk->ops->rate_set(clk->ph, clk->id, (u64)rate);
    if (ret) {
        dev_err(clk->ph->dev, "Failed to set rate %lu for clock %s: %d\n",
                rate, clk->name, ret);
        return ret;
    }
    
    dev_info(clk->ph->dev, "Clock %s rate set to %lu Hz successfully\n", 
             clk->name, rate);
    
    return 0;
}

static long scmi_clk_round_rate(struct clk_hw *hw, unsigned long rate,
                              unsigned long *parent_rate)
{
    struct scmi_clk_data *clk = to_scmi_clk(hw);
    const struct scmi_clock_info *info;
    
    /* 取得時鐘資訊以驗證頻率範圍 */
    info = clk->ops->info_get(clk->ph, clk->id);
    if (!info)
        return rate;
    
    /* 如果是離散頻率，找到最接近的支援頻率 */
    if (info->rate_discrete) {
        int i;
        unsigned long best_rate = info->list.rates[0];
        unsigned long diff = abs((long)rate - (long)best_rate);
        
        for (i = 1; i < info->list.num_rates; i++) {
            unsigned long new_diff = abs((long)rate - (long)info->list.rates[i]);
            if (new_diff < diff) {
                diff = new_diff;
                best_rate = info->list.rates[i];
            }
        }
        
        dev_dbg(clk->ph->dev, "Rounded rate %lu to %lu for clock %s\n",
                rate, best_rate, clk->name);
        return best_rate;
    }
    
    /* 如果是連續頻率範圍，限制在支援範圍內 */
    if (rate < info->range.min_rate)
        rate = info->range.min_rate;
    else if (rate > info->range.max_rate)
        rate = info->range.max_rate;
    
    dev_dbg(clk->ph->dev, "Clock %s rate %lu within range [%llu, %llu]\n",
            clk->name, rate, info->range.min_rate, info->range.max_rate);
    
    return rate;
}

/* Clock 操作函數表 */
static const struct clk_ops scmi_clk_ops = {
    .enable = scmi_clk_enable,
    .disable = scmi_clk_disable,
    .recalc_rate = scmi_clk_recalc_rate,
    .set_rate = scmi_clk_set_rate,
    .round_rate = scmi_clk_round_rate,
};

/*
 * 註冊單一時鐘到 Linux Clock Framework
 */
static int scmi_clk_register_single(struct scmi_clk_provider *provider, 
                                   int clk_id)
{
    struct scmi_clk_data *sclk;
    const struct scmi_clock_info *info;
    struct clk_init_data init = {};
    struct clk *clk;
    int ret;
    
    /* 取得時鐘資訊 */
    info = provider->ops->info_get(provider->ph, clk_id);
    if (!info) {
        dev_warn(provider->dev, "Clock ID %d not found\n", clk_id);
        return -ENODEV;
    }
    
    /* 分配時鐘資料結構 */
    sclk = devm_kzalloc(provider->dev, sizeof(*sclk), GFP_KERNEL);
    if (!sclk)
        return -ENOMEM;
    
    /* 初始化時鐘資料 */
    sclk->ph = provider->ph;
    sclk->ops = provider->ops;
    sclk->id = clk_id;
    sclk->name = info->name;
    sclk->hw.init = &init;
    
    /* 設定 clock init 資料 */
    init.name = info->name;
    init.ops = &scmi_clk_ops;
    init.num_parents = 0;
    init.flags = CLK_GET_RATE_NOCACHE; /* 總是從 SCP 取得最新頻率 */
    
    /* 註冊時鐘到 Linux Clock Framework */
    clk = devm_clk_register(provider->dev, &sclk->hw);
    if (IS_ERR(clk)) {
        ret = PTR_ERR(clk);
        dev_err(provider->dev, "Failed to register clock %s: %d\n",
                info->name, ret);
        return ret;
    }
    
    /* 儲存時鐘資料 */
    provider->clks[clk_id] = sclk;
    
    dev_info(provider->dev, "Registered SCMI clock: %s (ID: %d)\n", 
             info->name, clk_id);
    
    /* 顯示時鐘詳細資訊 */
    if (info->rate_discrete) {
        dev_info(provider->dev, "  Discrete rates: %d rates available\n",
                 info->list.num_rates);
    } else {
        dev_info(provider->dev, "  Range: %llu - %llu Hz (step: %llu)\n",
                 info->range.min_rate, info->range.max_rate, 
                 info->range.step_size);
    }
    
    return 0;
}

/*
 * Clock Provider 的 of_xlate 函數
 * 用於從 device tree 解析時鐘請求
 */
static struct clk_hw *scmi_clk_of_xlate(struct of_phandle_args *clkspec,
                                       void *data)
{
    struct scmi_clk_provider *provider = data;
    u32 clk_id;
    
    if (clkspec->args_count != 1)
        return ERR_PTR(-EINVAL);
    
    clk_id = clkspec->args[0];
    
    if (clk_id >= provider->num_clocks || !provider->clks[clk_id])
        return ERR_PTR(-EINVAL);
    
    return &provider->clks[clk_id]->hw;
}

/*
 * SCMI Clock Driver 主要 probe 函數
 */
static int scmi_clocks_probe(struct scmi_device *sdev)
{
    int ret, i, num_clocks;
    struct device *dev = &sdev->dev;
    struct scmi_clk_provider *provider;
    const struct scmi_protocol_handle *ph;
    const struct scmi_clk_proto_ops *clk_ops;
    
    dev_info(dev, "SCMI Clock Driver probing...\n");
    
    /* 取得 SCMI Clock Protocol Handle */
    clk_ops = sdev->handle->devm_protocol_get(sdev, SCMI_PROTOCOL_CLOCK, &ph);
    if (IS_ERR(clk_ops)) {
        ret = PTR_ERR(clk_ops);
        dev_err(dev, "Failed to get SCMI clock protocol: %d\n", ret);
        return ret;
    }
    
    /* 取得時鐘數量 */
    num_clocks = clk_ops->count_get(ph);
    if (num_clocks <= 0) {
        dev_err(dev, "No SCMI clocks available: %d\n", num_clocks);
        return num_clocks ?: -ENODEV;
    }
    
    dev_info(dev, "Found %d SCMI clocks\n", num_clocks);
    
    /* 分配 provider 結構 */
    provider = devm_kzalloc(dev, sizeof(*provider), GFP_KERNEL);
    if (!provider)
        return -ENOMEM;
    
    /* 分配時鐘陣列 */
    provider->clks = devm_kcalloc(dev, num_clocks, sizeof(*provider->clks),
                                 GFP_KERNEL);
    if (!provider->clks)
        return -ENOMEM;
    
    /* 初始化 provider */
    provider->ph = ph;
    provider->ops = clk_ops;
    provider->num_clocks = num_clocks;
    provider->dev = dev;
    
    /* 註冊所有時鐘 */
    for (i = 0; i < num_clocks; i++) {
        ret = scmi_clk_register_single(provider, i);
        if (ret)
            dev_warn(dev, "Failed to register clock ID %d: %d\n", i, ret);
    }
    
    /* 註冊 Clock Provider */
    ret = devm_of_clk_add_hw_provider(dev, scmi_clk_of_xlate, provider);
    if (ret) {
        dev_err(dev, "Failed to add clock provider: %d\n", ret);
        return ret;
    }
    
    /* 儲存 provider 到 device data */
    dev_set_drvdata(dev, provider);
    
    dev_info(dev, "SCMI Clock Driver probe completed successfully\n");
    
    return 0;
}

static void scmi_clocks_remove(struct scmi_device *sdev)
{
    dev_info(&sdev->dev, "SCMI Clock Driver removed\n");
}

/* SCMI Device ID Table */
static const struct scmi_device_id scmi_id_table[] = {
    { SCMI_PROTOCOL_CLOCK, "scmi-clocks" },
    { },
};
MODULE_DEVICE_TABLE(scmi, scmi_id_table);

/* SCMI Driver 結構 */
static struct scmi_driver scmi_clocks_driver = {
    .name = "scmi-clocks",
    .probe = scmi_clocks_probe,
    .remove = scmi_clocks_remove,
    .id_table = scmi_id_table,
};

/*
 * 模組初始化和清理
 */
static int __init scmi_clocks_init(void)
{
    pr_info("SCMI Clock Driver initializing...\n");
    return scmi_driver_register(&scmi_clocks_driver, THIS_MODULE, 
                               KBUILD_MODNAME);
}

static void __exit scmi_clocks_exit(void)
{
    pr_info("SCMI Clock Driver exiting...\n");
    scmi_driver_unregister(&scmi_clocks_driver);
}

module_init(scmi_clocks_init);
module_exit(scmi_clocks_exit);

MODULE_AUTHOR("Your Name <your.email@example.com>");
MODULE_DESCRIPTION("SCMI Clock Driver Example");
MODULE_LICENSE("GPL v2");
MODULE_VERSION("1.0");

/*
 * 使用範例：
 * 
 * 1. Device Tree 配置：
 *    clocks = <&scmi_clk 0>; // 使用 SCMI clock ID 0
 * 
 * 2. 在其他 driver 中使用：
 *    struct clk *clk = devm_clk_get(dev, "scmi-clock");
 *    clk_set_rate(clk, 100000000); // 設定為 100MHz
 *    clk_enable(clk);
 * 
 * 3. 通訊流程：
 *    clk_set_rate() -> scmi_clk_set_rate() -> 
 *    clk_ops->rate_set() -> SCMI protocol -> 
 *    SCP firmware -> 實際硬體設定
 */