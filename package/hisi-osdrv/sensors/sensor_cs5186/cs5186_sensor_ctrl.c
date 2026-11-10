#include "sensor_common.h"
#include "cs5186_cfg.h"

static void cs5186_default_reg_init(cis_info *cis)
{
    td_u32 i;
    td_s32 ret = TD_SUCCESS;
    ot_isp_sns_state *past_sensor = TD_NULL;

    past_sensor = cis->sns_state;
    for (i = 0; i < past_sensor->regs_info[0].reg_num; i++) {
        ret += cis_write_reg(&cis->i2c,
            past_sensor->regs_info[0].i2c_data[i].reg_addr,
            past_sensor->regs_info[0].i2c_data[i].data);
    }

    if (ret != TD_SUCCESS) {
        isp_err_trace("write register failed!\n");
    }
    return;
}

static td_s32 cs5186_reg_init(cis_info *cis, cis_reg_cfg_with_32bit *cfg, td_u32 len)
{
    td_u32 i;
    usleep(200);
    for (i = 0; i < len; i++) {
        if (cfg->addr == 0xFFFFFFFF) {
            cis_delay_ms(cfg->data);
        } else {
            sns_check_return(cis_write_reg(&cis->i2c, cfg->addr, cfg->data));
        }
        cfg++;
    }

    cs5186_default_reg_init(cis);

    cis_delay_ms(20);

    return TD_SUCCESS;
}

td_s32 cs5186_linear_8m30_10bit_init(cis_info *cis)
{
    td_s32 ret;
    td_u32 len;
    cis_reg_cfg_with_32bit *cfg = cs5186_linear_8m30_10bit;

    sns_check_pointer_return(cis);

    len = (td_u32)(sizeof(cs5186_linear_8m30_10bit) / sizeof(cs5186_linear_8m30_10bit[0]));
    ret = cs5186_reg_init(cis, cfg, len);
    if (ret != TD_SUCCESS) {
        isp_err_trace("cs5186_reg_init failed!\n");
        return ret;
    }

    printf("===================================================================================\n");
    printf("vi_pipe:%d,== CS5186_MIPI_27Minput_4lane_10bit_630Mbps_3840x2160_30fps Init OK! ==\n",
            cis->pipe);
    printf("===================================================================================\n");

    return TD_SUCCESS;
}
