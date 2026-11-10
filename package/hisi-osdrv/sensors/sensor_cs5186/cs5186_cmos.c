#include <stdio.h>
#include "sensor_common.h"
#include "ot_mpi_isp.h"
#include "ot_mpi_ae.h"
#include "ot_mpi_awb.h"
#include "ot_math.h"

#include "cs5186_cmos.h"
#include "cs5186_cmos_param.h"
#include "cs5186_sensor_ctrl.h"

/****************************************************************************
 * global variables                                                         *
 ****************************************************************************/
#define cs5186_sensor_set_ctx(pipe, ctx)   ((g_cs5186_sns_state[pipe]) = (ctx))
#define cs5186_sensor_reset_ctx(pipe)      (g_cs5186_sns_state[pipe] = TD_NULL)
#define cs5186_sensor_get_ctx(pipe, ctx)   ((ctx) = g_cs5186_sns_state[pipe])

typedef struct {
    td_u8       hcg;
    td_u32      brl;
    td_u32      rhs1_max;
    td_u32      rhs2_max;
} cs5186_sensor_state;

cs5186_sensor_state g_cs5186_sensor_state[OT_ISP_MAX_PIPE_NUM] = {{0}};

ot_isp_sns_commbus g_cs5186_bus_info[OT_ISP_MAX_PIPE_NUM] = {
    [0] = { .i2c_dev = 0},
    [1 ... OT_ISP_MAX_PIPE_NUM - 1] = { .i2c_dev = -1}
};

ot_isp_sns_state *g_cs5186_sns_state[OT_ISP_MAX_PIPE_NUM] = {TD_NULL};

static ot_isp_fswdr_mode g_fswdr_mode[OT_ISP_MAX_PIPE_NUM] = {
    [0 ... OT_ISP_MAX_PIPE_NUM - 1] = OT_ISP_FSWDR_NORMAL_MODE
};
static td_u8 g_ae_stat_pos[OT_ISP_MAX_PIPE_NUM] = {0};
static td_u32 g_max_time_get_cnt[OT_ISP_MAX_PIPE_NUM] = {0};
static td_u32 g_init_exposure[OT_ISP_MAX_PIPE_NUM]  = {0};
static td_u32 g_lines_per500ms[OT_ISP_MAX_PIPE_NUM] = {0};

static td_u16 g_init_wb_gain[OT_ISP_MAX_PIPE_NUM][OT_ISP_RGB_CHN_NUM] = {{0}};
static td_u16 g_sample_r_gain[OT_ISP_MAX_PIPE_NUM] = {0};
static td_u16 g_sample_b_gain[OT_ISP_MAX_PIPE_NUM] = {0};
static td_bool g_quick_start_en[OT_ISP_MAX_PIPE_NUM] = {TD_FALSE};

static td_bool g_ae_route_ex_valid[OT_ISP_MAX_PIPE_NUM] = {0};
static ot_isp_ae_route g_init_ae_route[OT_ISP_MAX_PIPE_NUM] = {{0}};
static ot_isp_ae_route_ex g_init_ae_route_ex[OT_ISP_MAX_PIPE_NUM] = {{0}};
static ot_isp_ae_route g_init_ae_route_sf[OT_ISP_MAX_PIPE_NUM] = {{0}};
static ot_isp_ae_route_ex g_init_ae_route_sf_ex[OT_ISP_MAX_PIPE_NUM] = {{0}};

static td_bool blc_clamp_info[OT_ISP_MAX_PIPE_NUM] = {[0 ...(OT_ISP_MAX_PIPE_NUM - 1)] = TD_TRUE};

#define SEXP_OFFSET_WDR                     11
#define CS5186_SEXP_MAX_DEFAULT            0x178
static td_u32 g_max_short_exp[OT_ISP_MAX_PIPE_NUM] = {
    [0 ...(OT_ISP_MAX_PIPE_NUM - 1)] = CS5186_SEXP_MAX_DEFAULT * 2
};

const cs5186_video_mode_tbl g_cs5186_mode_tbl[CS5186_MODE_BUTT] = {
    {CS5186_VMAX_640_480_LINEAR,   CS5186_FULL_LINES_MAX_LINEAR,          240, 0.07,
     640, 480, 0, OT_WDR_MODE_NONE,       "CS5186_SENSOR_640_480_15FPS_LINEAR_MODE"},
    {CS5186_VMAX_4K_15FPS_WDR,   CS5186_FULL_LINES_MAX_LINEAR,          15, 0.07,
     3840, 2160, 0, OT_WDR_MODE_2To1_LINE,       "CS5186_SENSOR_4K_15FPS_LINEAR_WDR_MODE"},
    {CS5186_VMAX_1K_LINEAR,   CS5186_FULL_LINES_MAX_LINEAR,          90, 0.07,
     1920, 1080, 0, OT_WDR_MODE_NONE,       "CS5186_SENSOR_24M_60FPS_LINEAR_MODE"},
    {CS5186_VMAX_1K_LINEAR,   CS5186_FULL_LINES_MAX_LINEAR,          90, 0.07,
     1920, 1080, 0, OT_WDR_MODE_NONE,       "CS5186_SENSOR_1K_60FPS_LINEAR_MODE"},
    {CS5186_VMAX_2K_LINEAR,   CS5186_FULL_LINES_MAX_LINEAR,          60, 0.07,
     2560,1440, 0, OT_WDR_MODE_NONE,        "CS5186_SENSOR_2K_60FPS_LINEAR_MODE"},
    {CS5186_VMAX_4K_LINEAR,   CS5186_FULL_LINES_MAX_LINEAR,          15, 0.07,
     3840, 2160, 0, OT_WDR_MODE_NONE,       "CS5186_SENSOR_4K_60FPS_LINEAR_MODE"},
    {CS5186_VMAX_1K_LINEAR,   CS5186_FULL_LINES_MAX_LINEAR,          90, 0.07,
     1920, 1080, 0, OT_WDR_MODE_2To1_LINE,       "CS5186_SENSOR_1K_90FPS_DOL2_MODE"},
    {CS5186_VMAX_1K_LINEAR,   CS5186_FULL_LINES_MAX_LINEAR,          90, 0.07,
     1920, 1080, 0, OT_WDR_MODE_2To1_FRAME,       "CS5186_SENSOR_1K_60FPS_WDR_MODE"},
    {CS5186_VMAX_2K_LINEAR,   CS5186_FULL_LINES_MAX_LINEAR,          60, 0.07,
     2560,1440, 0, OT_WDR_MODE_2To1_LINE,       "CS5186_SENSOR_2K_90FPS_DOL2_MODE"},
    {CS5186_VMAX_2K_LINEAR,   CS5186_FULL_LINES_MAX_LINEAR,          30, 0.07,
     2560,1440, 0, OT_WDR_MODE_2To1_FRAME,       "CS5186_SENSOR_2K_60FPS_WDR_MODE"},
    {CS5186_VMAX_4K_LINEAR,   CS5186_FULL_LINES_MAX_LINEAR,          30, 0.07,
     3840, 2160, 0, OT_WDR_MODE_2To1_LINE,       "CS5186_SENSOR_4K_90FPS_DOL2_MODE"},
    {CS5186_VMAX_4K_LINEAR,   CS5186_FULL_LINES_MAX_LINEAR,          30, 0.07,
     3840, 2160, 0, OT_WDR_MODE_2To1_FRAME,       "CS5186_SENSOR_4K_60FPS_WDR_MODE"},
    {CS5186_VMAX_4K_LINEAR,   CS5186_FULL_LINES_MAX_LINEAR,          30, 0.07,
     3840, 2176, 0, OT_WDR_MODE_NONE,       "CS5186_SENSOR_4K_60FPS_LINEAR_OB_MODE"},
};

/****************************************************************************
  Again & Dgain table for TABLE Mode                                        *
 ****************************************************************************/
static td_u32 g_gain_table[CS5186_AD_GAIN_TBL_RANGE] = {
    1024,   1059,   1097,   1135,   1175,   1217,   1259,   1304,   1349,   1397,   1446,   1497,   1549,   1604,
    1660,   1719,   1779,   1842,   1906,   1973,   2043,   2114,   2189,   2266,   2345,   2428,   2513,   2601,
    2693,   2788,   2886,   2987,   3092,   3201,   3313,   3430,   3550,   3675,   3804,   3938,   4076,   4219,
    4368,   4521,   4680,   4845,   5015,   5191,   5374,   5562,   5758,   5960,   6170,   6387,   6611,   6843,
    7084,   7333,   7591,   7857,   8133,   8419,   8715,   9021,   9338,   9667,  10006,  10358,  10722,  11099,
    11489,  11893,  12311,  12743,  13191,  13655,  14135,  14631,  15146,  15678,  16229,  16799,  17390,  18001,
    18633,  19288,  19966,  20668,  21394,  22146,  22924,  23730,  24564,  25427,  26320,  27245,  28203,  29194,
    30220,  31282,  32381,  41519,  34697,  35917,  37179,  38485,  39838,  41238,  42687,  44187,  45740,  47347,
    49011,  50734,  52517,  54362,  56273,  58250,  60297,  62416,  64610,  66880,  69230,  71663,  74182,  76789,
    79487,  82281,  85172,  88165,  91264,  94471,  97791, 101228, 104785, 108468, 112279, 116225, 120310, 124537,
    128914, 133444, 138134, 142988, 148013, 153215, 158599, 164172, 169942, 175914, 182096, 188495, 195119, 201976,
    209074, 216421, 224027, 231900, 240049, 248485, 257217, 266256, 275613, 285299, 295325, 305703, 316446, 327567,
    339078, 350994, 363329, 376097, 389314, 402995, 417157, 431817, 446992, 462700, 478961, 495793, 513216, 531251,
    549921, 569246, 589250, 609958, 631393, 653582, 676550, 700326, 724936, 750412, 776783, 804081, 832338, 861589,
    891867, 923209, 955652, 989236, 1024000, 1059985, 1097236, 1135795, 1175709, 1217026, 1259795, 1304067, 1349895,
    1397333, 1446438, 1497269, 1549887, 1604353, 1660734, 1719095, 1779508, 1842044, 1906777, 1973786, 2043149,
    2114949, 2189273, 2266209,  2345848, 2428287, 2513622, 2601956, 2693394, 2788046, 2886024, 2987445, 3092431,
    3201105, 3313599, 3430046, 3550585, 3675361, 3804521, 3938220, 4076617
};

static td_u32 dgain_fix = 1;

/****************************************************************************
 * common functions                                                         *
 ****************************************************************************/
static cis_info g_cs5186_info[OT_ISP_MAX_PIPE_NUM] = {
    [0 ...(OT_ISP_MAX_PIPE_NUM - 1)] = {
        .mutex = PTHREAD_MUTEX_INITIALIZER,
        .sns_id = CS5186_ID,
        .fswdr_mode = OT_ISP_FSWDR_NORMAL_MODE,
        .quick_start_en = TD_FALSE,
        .fast_ae_en = TD_FALSE,
        .bus_info = { .i2c_dev = -1 },
        .sns_state = TD_NULL,
        .blc_clamp_info = TD_TRUE,
        .i2c.fd = -1,
    }
};

static cis_info *cmos_get_info(ot_vi_pipe vi_pipe)
{
    if (vi_pipe < 0 || vi_pipe >= OT_ISP_MAX_PIPE_NUM) {
        return TD_NULL;
    }
    return &g_cs5186_info[vi_pipe];
}

static ot_isp_sns_state *cmos_get_state(ot_vi_pipe vi_pipe)
{
    if (vi_pipe < 0 || vi_pipe >= OT_ISP_MAX_PIPE_NUM) {
        return TD_NULL;
    }
    return cmos_get_info(vi_pipe)->sns_state;
}

ot_isp_sns_commbus *cs5186_get_bus_info(ot_vi_pipe vi_pipe)
{
    return &g_cs5186_bus_info[vi_pipe];
}

ot_isp_sns_state *cs5186_get_ctx(ot_vi_pipe vi_pipe)
{
    return g_cs5186_sns_state[vi_pipe];
}

static void cmos_err_mode_print(const ot_isp_cmos_sns_image_mode *sns_image_mode, const ot_isp_sns_state *sns_state)
{
    isp_err_trace("Not support! Width:%u, Height:%u, Fps:%f, WDRMode:%d\n",
        (sns_image_mode)->width, (sns_image_mode)->height, (sns_image_mode)->fps, (sns_state)->wdr_mode);
}

/****************************************************************************
 * AE functions                                                             *
 ****************************************************************************/
static td_void cmos_get_ae_comm_default(ot_vi_pipe vi_pipe, ot_isp_ae_sensor_default *ae_sns_dft,
    const ot_isp_sns_state *sns_state)
{
    ae_sns_dft->full_lines_std = sns_state->fl_std;
    ae_sns_dft->flicker_freq = 50 * 256; /* light flicker freq: 50Hz, accuracy: 256 */
    ae_sns_dft->full_lines_max = CS5186_FULL_LINES_MAX_LINEAR;
    ae_sns_dft->hmax_times = (1000000000) / (sns_state->fl_std * 30); /* 1000000000ns, 4k 15fps */

    ae_sns_dft->int_time_accu.accu_type = OT_ISP_AE_ACCURACY_LINEAR;
    ae_sns_dft->int_time_accu.accuracy = 1;
    ae_sns_dft->int_time_accu.offset = 0;

    ae_sns_dft->again_accu.accu_type = OT_ISP_AE_ACCURACY_TABLE;
    ae_sns_dft->again_accu.accuracy  = 1;

    ae_sns_dft->dgain_accu.accu_type = OT_ISP_AE_ACCURACY_TABLE;
    ae_sns_dft->dgain_accu.accuracy = 1;

    ae_sns_dft->isp_dgain_shift = 8; /* accuracy: 8 */
    ae_sns_dft->min_isp_dgain_target = 1 << ae_sns_dft->isp_dgain_shift;
    ae_sns_dft->max_isp_dgain_target = 8192;
    if (g_lines_per500ms[vi_pipe] == 0) {
        ae_sns_dft->lines_per500ms = sns_state->fl_std * ae_sns_dft->fps / 2; /* 30fps, div 2 */
    } else {
        ae_sns_dft->lines_per500ms = g_lines_per500ms[vi_pipe];
    }

    ae_sns_dft->max_iris_fno = OT_ISP_IRIS_F_NO_1_0;
    ae_sns_dft->min_iris_fno = OT_ISP_IRIS_F_NO_32_0;

    ae_sns_dft->ae_route_ex_valid = TD_FALSE;
    ae_sns_dft->ae_route_attr.total_num = 0;
    ae_sns_dft->ae_route_attr_ex.total_num = 0;
    ae_sns_dft->quick_start.quick_start_enable = g_quick_start_en[vi_pipe];
    ae_sns_dft->quick_start.black_frame_num = 0;
    ae_sns_dft->ae_stat_pos = g_ae_stat_pos[vi_pipe]; /* 1 use be stat to AE */
    return;
}

static td_void cmos_get_ae_linear_default(ot_vi_pipe vi_pipe, ot_isp_ae_sensor_default *ae_sns_dft,
    const ot_isp_sns_state *sns_state)
{
    ae_sns_dft->max_again = CS5186_AGAIN_MAX;
    ae_sns_dft->min_again = CS5186_AGAIN_MIN;
    ae_sns_dft->max_again_target = ae_sns_dft->max_again;
    ae_sns_dft->min_again_target = ae_sns_dft->min_again;

    ae_sns_dft->max_dgain = CS5186_DGAIN_MAX;
    ae_sns_dft->min_dgain = CS5186_DGAIN_MIN;
    ae_sns_dft->max_dgain_target = ae_sns_dft->max_dgain;
    ae_sns_dft->min_dgain_target = ae_sns_dft->min_dgain;

    ae_sns_dft->ae_compensation = 0x38;
    ae_sns_dft->ae_exp_mode = OT_ISP_AE_EXP_HIGHLIGHT_PRIOR;
    ae_sns_dft->init_exposure = g_init_exposure[vi_pipe] ? g_init_exposure[vi_pipe] : 76151; /* init 76151 */

    ae_sns_dft->max_int_time = sns_state->fl_std;
    ae_sns_dft->min_int_time = 9; /* min 9 */
    ae_sns_dft->max_int_time_target = ae_sns_dft->max_int_time;  /* for exp_time_range.max */
    ae_sns_dft->min_int_time_target = 9; /* min 9 */
    ae_sns_dft->ae_route_ex_valid = g_ae_route_ex_valid[vi_pipe];
    (td_void)memcpy_s(&ae_sns_dft->ae_route_attr, sizeof(ot_isp_ae_route),
                      &g_init_ae_route[vi_pipe],  sizeof(ot_isp_ae_route));
    (td_void)memcpy_s(&ae_sns_dft->ae_route_attr_ex, sizeof(ot_isp_ae_route_ex),
                      &g_init_ae_route_ex[vi_pipe], sizeof(ot_isp_ae_route_ex));
    return;
}

static td_void cmos_get_ae_2to1_line_wdr_default(ot_vi_pipe vi_pipe, ot_isp_ae_sensor_default *ae_sns_dft,
    const ot_isp_sns_state *sns_state)
{
    ae_sns_dft->max_again = CS5186_AGAIN_MAX;
    ae_sns_dft->min_again = CS5186_AGAIN_MIN;
    ae_sns_dft->max_again_target = ae_sns_dft->max_again;
    ae_sns_dft->min_again_target = ae_sns_dft->min_again;

    ae_sns_dft->max_dgain = CS5186_DGAIN_MAX;
    ae_sns_dft->min_dgain = CS5186_DGAIN_MIN;
    ae_sns_dft->max_dgain_target = ae_sns_dft->max_dgain;
    ae_sns_dft->min_dgain_target = ae_sns_dft->min_dgain;

    ae_sns_dft->max_isp_dgain_target = 4 << ae_sns_dft->isp_dgain_shift; /* max 4 << shift */
    ae_sns_dft->diff_gain_support = TD_TRUE;

    ae_sns_dft->ae_compensation = 0x38;
    ae_sns_dft->ae_exp_mode = OT_ISP_AE_EXP_HIGHLIGHT_PRIOR;
    ae_sns_dft->init_exposure = g_init_exposure[vi_pipe] ? g_init_exposure[vi_pipe] : 76151; /* init 76151 */

    ae_sns_dft->max_int_time = sns_state->fl_std;
    ae_sns_dft->min_int_time = 9; /* min 9 */
    ae_sns_dft->max_int_time_target = sns_state->fl_std;  /* max 33333 */
    ae_sns_dft->min_int_time_target = 9; /* min 9 */

    ae_sns_dft->int_time_accu.offset = 0;

    ae_sns_dft->ae_route_ex_valid = g_ae_route_ex_valid[vi_pipe];
    (td_void)memcpy_s(&ae_sns_dft->ae_route_attr, sizeof(ot_isp_ae_route),
                      &g_init_ae_route[vi_pipe],  sizeof(ot_isp_ae_route));
    (td_void)memcpy_s(&ae_sns_dft->ae_route_attr_ex, sizeof(ot_isp_ae_route_ex),
                      &g_init_ae_route_ex[vi_pipe], sizeof(ot_isp_ae_route_ex));
    (td_void)memcpy_s(&ae_sns_dft->ae_route_sf_attr, sizeof(ot_isp_ae_route),
                      &g_init_ae_route_sf[vi_pipe], sizeof(ot_isp_ae_route));
    (td_void)memcpy_s(&ae_sns_dft->ae_route_sf_attr_ex, sizeof(ot_isp_ae_route_ex),
                      &g_init_ae_route_sf_ex[vi_pipe],  sizeof(ot_isp_ae_route_ex));
    return;
}

static td_s32 cmos_get_ae_default(ot_vi_pipe vi_pipe, ot_isp_ae_sensor_default *ae_sns_dft)
{
    cis_info *cis = TD_NULL;
    ot_isp_sns_state *sns_state = TD_NULL;

    sns_check_pipe_return(vi_pipe);
    sns_check_pointer_return(ae_sns_dft);

    cis = cmos_get_info(vi_pipe);
    sns_check_pointer_return(cis);

    sns_state = cis->sns_state;
    sns_check_pointer_return(sns_state);

    (td_void)memset_s(&ae_sns_dft->ae_route_attr, sizeof(ot_isp_ae_route), 0, sizeof(ot_isp_ae_route));

    cmos_get_ae_comm_default(vi_pipe, ae_sns_dft, sns_state);

    switch (sns_state->wdr_mode) {
        case OT_WDR_MODE_NONE:   /* linear mode */
            cmos_get_ae_linear_default(vi_pipe, ae_sns_dft, sns_state);
            break;
        case OT_WDR_MODE_2To1_LINE:
        case OT_WDR_MODE_2To1_FRAME:
            cmos_get_ae_2to1_line_wdr_default(vi_pipe, ae_sns_dft, sns_state);
            break;
        default:
            break;
    }

    return TD_SUCCESS;
}

static td_void cmos_config_vmax(ot_isp_sns_state *sns_state, td_u32 vmax)
{
    if (sns_state->wdr_mode == OT_WDR_MODE_NONE) {
        /* index 5/6/7 are dgain-max/short-inttime/short-again regs (0x40020930/0x40020b48/0x40020b44),
           NOT VMAX. This smart sensor has no per-frame VMAX reg; frame length is fixed by the blob.
           Writing vmax here corrupts gain/short-exposure. Disabled to match working mis80w1. */
        /* sns_state->regs_info[0].i2c_data[5].data = low_8bits(vmax); */          /* array index 5 */
        /* sns_state->regs_info[0].i2c_data[6].data = high_8bits(vmax); */         /* array index 6 */
        /* sns_state->regs_info[0].i2c_data[7].data = higher_4bits_from_20(vmax); *//* array index 7 */
    }
    return;
}

/* the function of sensor set fps */
static td_void cmos_fps_set(ot_vi_pipe vi_pipe, td_float fps, ot_isp_ae_sensor_default *ae_sns_dft)
{
    td_u32 lines, lines_max, vmax;
    td_float max_fps, min_fps;
    ot_isp_sns_state *sns_state = TD_NULL;

    sns_check_pointer_void_return(ae_sns_dft);
    cs5186_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);

    lines = g_cs5186_mode_tbl[sns_state->img_mode].ver_lines;
    lines_max = g_cs5186_mode_tbl[sns_state->img_mode].max_ver_lines;
    max_fps = g_cs5186_mode_tbl[sns_state->img_mode].max_fps;
    min_fps = g_cs5186_mode_tbl[sns_state->img_mode].min_fps;

    if ((fps > max_fps) || (fps < min_fps)) {
        isp_err_trace("Not support Fps: %f\n", fps);
        return;
    }

    vmax = (td_u32)(lines * max_fps / div_0_to_1_float(fps));
    vmax = (vmax > lines_max) ? lines_max : vmax;
    cmos_config_vmax(sns_state, vmax);

    sns_state->fl_std = vmax;
    ae_sns_dft->lines_per500ms = (td_u32)(lines * max_fps / 2); /* div 2 */

    ae_sns_dft->fps = fps;
    ae_sns_dft->full_lines_std = sns_state->fl_std;
    ae_sns_dft->max_int_time = sns_state->fl_std - CS5186_EXP_TIME_LMT; /* sensor value */
    sns_state->fl[0] = sns_state->fl_std;
    ae_sns_dft->full_lines = sns_state->fl[0];
    ae_sns_dft->hmax_times =
        (td_u32)((1000000000) / (sns_state->fl_std * div_0_to_1_float(fps))); /* 1000000000ns */

    return;
}

static td_void cmos_slow_framerate_set(ot_vi_pipe vi_pipe, td_u32 full_lines, ot_isp_ae_sensor_default *ae_sns_dft)
{
    td_u32 lines_max;
    td_u32 vmax;
    ot_isp_sns_state *sns_state = TD_NULL;

    sns_check_pointer_void_return(ae_sns_dft);
    cs5186_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);

    lines_max = g_cs5186_mode_tbl[sns_state->img_mode].max_ver_lines;

    if (sns_state->wdr_mode == OT_WDR_MODE_NONE) {
        vmax = full_lines;
        vmax = (vmax > lines_max) ? lines_max : vmax;
        sns_state->fl[0] = vmax;
    }

    switch (sns_state->wdr_mode) {
        case OT_WDR_MODE_NONE:
            /* index 5/6/7 are gain/short-exposure regs, not VMAX; see cmos_config_vmax. Disabled. */
            /* sns_state->regs_info[0].i2c_data[5].data = low_8bits(vmax); */          /* index 5 */
            /* sns_state->regs_info[0].i2c_data[6].data = high_8bits(vmax); */         /* index 6 */
            /* sns_state->regs_info[0].i2c_data[7].data = higher_4bits_from_20(vmax); *//* index 7 */
            break;
        default:
            break;
    }

    ae_sns_dft->full_lines = sns_state->fl[0];
    ae_sns_dft->max_int_time = sns_state->fl[0] - CS5186_EXP_TIME_LMT;

    return;
}

static td_void cmos_inttime_update_linear(ot_vi_pipe vi_pipe, td_u32 int_time)
{
    ot_isp_sns_state *sns_state = TD_NULL;

    cs5186_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);

    td_u32 tmp_value = int_time % 2 != 0 ? int_time + 1 : int_time; /* Even numbers are supported */
    sns_state->regs_info[0].i2c_data[AE_INTTIME_IDX].data = tmp_value; /* inttime 32 bits */
    sns_state->regs_info[0].i2c_data[AE_W_INTTIME_MAX_MIN_IDX].data = 0XFFFF0004;
    sns_state->regs_info[0].i2c_data[AE_W_SHORT_INTTIME_MAX_MIN_IDX].data = (tmp_value & 0xffff) | ((tmp_value & 0xffff) << 16);
    sns_state->regs_info[0].i2c_data[AE_E0_IDX].data = 0x1; /* inttime 32 bits */
    sns_state->regs_info[0].i2c_data[AE_E4_IDX].data = 0x1; /* inttime 32 bits */
    return;
}

static td_void cmos_inttime_update_2to1_frame(ot_vi_pipe vi_pipe, td_u32 int_time)
{
    ot_isp_sns_state *sns_state = TD_NULL;
    static td_bool is_first[OT_ISP_MAX_PIPE_NUM] = {[0 ...(OT_ISP_MAX_PIPE_NUM - 1)] = 1};

    static td_u32 short_int_time[OT_ISP_MAX_PIPE_NUM] = {0};
    static td_u32 long_int_time[OT_ISP_MAX_PIPE_NUM] = {0};

    cs5186_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);

    td_u32 tmp_value = int_time % 2 != 0 ? int_time + 1 : int_time; /* Even numbers are supported */
    if (is_first[vi_pipe]) { /* short exposure */
        sns_state->wdr_int_time[0] = tmp_value;
        short_int_time[vi_pipe] = tmp_value;
        is_first[vi_pipe] = TD_FALSE;
    } else { /* long exposure */
        sns_state->wdr_int_time[1] = tmp_value;
        long_int_time[vi_pipe] = tmp_value;
        sns_state->regs_info[0].i2c_data[AE_INTTIME_IDX].data = long_int_time[vi_pipe];
        sns_state->regs_info[0].i2c_data[AE_W_INTTIME_MAX_MIN_IDX].data = 0XFFFF0004;
        sns_state->regs_info[0].i2c_data[AE_INTTIME_SHORT_IDX].data = short_int_time[vi_pipe];
        sns_state->regs_info[0].i2c_data[AE_W_SHORT_INTTIME_MAX_MIN_IDX].data = 0XFFFF0004;
        is_first[vi_pipe] = TD_TRUE;
    }
    sns_state->regs_info[0].i2c_data[AE_E0_IDX].data = 0x1; /* inttime 32 bits */
    sns_state->regs_info[0].i2c_data[AE_E4_IDX].data = 0x1; /* inttime 32 bits */

    return;
}

/* while isp notify ae to update sensor regs, ae call these funcs. */
static td_void cmos_inttime_update(ot_vi_pipe vi_pipe, td_u32 int_time)
{
    ot_isp_sns_state *sns_state = TD_NULL;

    cs5186_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);

    if (sns_state->wdr_mode == OT_WDR_MODE_2To1_LINE) {
        cmos_inttime_update_2to1_frame(vi_pipe, int_time);
    } else if (sns_state->wdr_mode == OT_WDR_MODE_2To1_FRAME) {
        cmos_inttime_update_2to1_frame(vi_pipe, int_time);
    } else {
        cmos_inttime_update_linear(vi_pipe, int_time);
    }
    return;
}

static td_void cmos_again_calc_table(ot_vi_pipe vi_pipe, td_u32 *again_lin, td_u32 *again_db)
{
    sns_check_pointer_void_return(again_lin);
    sns_check_pointer_void_return(again_db);
    ot_unused(vi_pipe);

    unsigned int again_shift = 10;

    td_u32 again_input = *again_lin;

    (*again_lin) = (*again_lin) >> again_shift;
    (*again_lin) = clip3(*again_lin, 1, 32);
    (*again_db) = 1024 - 1024 /  (td_s32)(*again_lin);
    (*again_db) = clip3(*again_db, 0, 992);
    (*again_lin) = (1024 / (1024 - (*again_db))) << again_shift;

    dgain_fix = again_input * 256 / (*again_lin);
    *again_lin = again_input;
    return;
}

static td_void cmos_dgain_calc_table(ot_vi_pipe vi_pipe, td_u32 *dgain_lin, td_u32 *dgain_db)
{
    sns_check_pointer_void_return(dgain_lin);
    sns_check_pointer_void_return(dgain_db);
    ot_unused(vi_pipe);

    *dgain_db = *dgain_lin;
    return;
}

static td_void cmos_gains_update(ot_vi_pipe vi_pipe, td_u32 again, td_u32 dgain)
{
    ot_isp_sns_state *sns_state = TD_NULL;

    cs5186_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);

    td_u32 tmp_again = again, tmp_dgain = dgain * dgain_fix / 256;
    tmp_dgain &= 0xffff; /* 16bit */
    tmp_again &= 0x3ff; /* 10bit */

    tmp_dgain = (tmp_dgain / 4);
    td_u32 tmp_value = (tmp_dgain << 10) + tmp_again;

    tmp_again = (1024 << 10) / (1024 - (tmp_again > 992 ? 992 : tmp_again));

    sns_state->regs_info[0].i2c_data[AE_AGAIN_DGAIN_IDX].data = tmp_value;
    sns_state->regs_info[0].i2c_data[AE_W_AGAIN_MAX_MIN_IDX].data = 0x80000400;
    sns_state->regs_info[0].i2c_data[AE_W_DGAIN_MAX_MIN_IDX].data = 0xFFFF0100;
    if (sns_state->wdr_mode == OT_WDR_MODE_2To1_FRAME || sns_state->wdr_mode == OT_WDR_MODE_2To1_LINE) {
        sns_state->regs_info[0].i2c_data[AE_AGAIN_DGAIN_SHORT_IDX].data = tmp_value;
    }
    sns_state->regs_info[0].i2c_data[AE_E0_IDX].data = 0x1;
    sns_state->regs_info[0].i2c_data[AE_E4_IDX].data = 0x1;

    return;
}

static td_void cmos_get_inttime_max_2to1_line(ot_vi_pipe vi_pipe, td_u32 *ratio,
    ot_isp_ae_int_time_range *int_time, td_u32 *lf_max_int_time)
{
    td_u32 short_max0;
    td_u32 short_max;
    td_u32 short_time_min_limit;
    ot_isp_sns_state *sns_state = TD_NULL;

    cs5186_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);

    short_time_min_limit = 4; /* short_time_min_limit 4 */
    short_max = sns_state->fl_std;
    int_time->int_time_max[0] = short_max;  /* long inttime */
    int_time->int_time_min[0] = short_time_min_limit;
    int_time->int_time_max[1] = short_max; /* short inttime */
    int_time->int_time_min[1] = short_time_min_limit;
    return;

    if (g_fswdr_mode[vi_pipe] == OT_ISP_FSWDR_LONG_FRAME_MODE) {
        short_max0 = sns_state->fl[1] - 20 - sns_state->wdr_int_time[0]; /* sensor limit: sub 20 */
        short_max = sns_state->fl[0] - 20; /* sensor limit: sub 20 */
        short_max = short_max > (g_max_short_exp[vi_pipe] - SEXP_OFFSET_WDR) ?
                    (g_max_short_exp[vi_pipe] - SEXP_OFFSET_WDR) : short_max;
        short_max = (short_max0 < short_max) ? short_max0 : short_max;
        int_time->int_time_max[0] = short_time_min_limit;
        int_time->int_time_min[0] = short_time_min_limit;
        int_time->int_time_max[1] = short_max;
        int_time->int_time_min[1] = short_time_min_limit;
        return;
    } else {
        short_max0 = ((sns_state->fl[1] - 20 - sns_state->wdr_int_time[0]) * 0x40) / div_0_to_1(ratio[0]); /* sub 20 */
        short_max = ((sns_state->fl[0] - 20) * 0x40) / (ratio[0] + 0x40); /* sub 20 */
        short_max = short_max > (g_max_short_exp[vi_pipe] - SEXP_OFFSET_WDR) ?
                    (g_max_short_exp[vi_pipe] - SEXP_OFFSET_WDR) : short_max;
        short_max = (short_max0 < short_max) ? short_max0 : short_max;
        short_max = (short_max == 0) ? 1 : short_max;
    }
    *lf_max_int_time = sns_state->fl[0] - 20; /* sub 20 */

    if (short_max >= short_time_min_limit) {
        int_time->int_time_max[0] = short_max;
        int_time->int_time_max[1] = (int_time->int_time_max[0] * ratio[0]) >> 6; /* shift 6 */
        int_time->int_time_min[0] = short_time_min_limit;
        int_time->int_time_min[1] = (int_time->int_time_min[0] * ratio[0]) >> 6; /* shift 6 */
    } else {
        short_max = short_time_min_limit;

        int_time->int_time_max[0] = short_max;
        int_time->int_time_max[1] = (int_time->int_time_max[0] * 0xFFF) >> 6; /* shift 6 */
        int_time->int_time_min[0] = int_time->int_time_max[0];
        int_time->int_time_min[1] = int_time->int_time_max[1];
    }

    return;
}

static td_void cmos_get_inttime_max(ot_vi_pipe vi_pipe, td_u16 man_ratio_enable, td_u32 *ratio,
    ot_isp_ae_int_time_range *int_time, td_u32 *lf_max_int_time)
{
    ot_isp_sns_state *sns_state = TD_NULL;

    sns_check_pointer_void_return(ratio);
    sns_check_pointer_void_return(int_time);
    sns_check_pointer_void_return(lf_max_int_time);
    cs5186_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_void_return(sns_state);

    ot_unused(man_ratio_enable);
    switch (sns_state->wdr_mode) {
        case OT_WDR_MODE_2To1_LINE:
        case OT_WDR_MODE_2To1_FRAME:
            cmos_get_inttime_max_2to1_line(vi_pipe, ratio, int_time, lf_max_int_time);
            break;
        default:
            break;
    }

    return;
}

/* Only used in LINE_WDR mode */
static td_void cmos_ae_fswdr_attr_set(ot_vi_pipe vi_pipe, ot_isp_ae_fswdr_attr *ae_fswdr_attr)
{
    sns_check_pointer_void_return(ae_fswdr_attr);

    g_fswdr_mode[vi_pipe] = ae_fswdr_attr->fswdr_mode;
    g_max_time_get_cnt[vi_pipe] = 0;
    return;
}

static td_void cmos_init_ae_exp_function(ot_isp_ae_sensor_exp_func *exp_func)
{
    (td_void)memset_s(exp_func, sizeof(ot_isp_ae_sensor_exp_func), 0, sizeof(ot_isp_ae_sensor_exp_func));

    exp_func->pfn_cmos_get_ae_default    = cmos_get_ae_default;
    exp_func->pfn_cmos_fps_set           = cmos_fps_set;
    exp_func->pfn_cmos_slow_framerate_set = cmos_slow_framerate_set;
    exp_func->pfn_cmos_inttime_update    = cmos_inttime_update;
    exp_func->pfn_cmos_gains_update      = cmos_gains_update;
    exp_func->pfn_cmos_again_calc_table  = cmos_again_calc_table;
    exp_func->pfn_cmos_dgain_calc_table  = cmos_dgain_calc_table;
    exp_func->pfn_cmos_get_inttime_max   = cmos_get_inttime_max;
    exp_func->pfn_cmos_ae_fswdr_attr_set = cmos_ae_fswdr_attr_set;

    return;
}

/****************************************************************************
 * AWB functions                                                            *
 ****************************************************************************/
static td_s32 cmos_awb_get_default(ot_vi_pipe vi_pipe, ot_isp_awb_sensor_default *awb_sns_dft)
{
    cis_info *cis = TD_NULL;
    ot_isp_sns_state *sns_state = TD_NULL;

    sns_check_pipe_return(vi_pipe);
    sns_check_pointer_return(awb_sns_dft);

    cis = cmos_get_info(vi_pipe);
    sns_check_pointer_return(cis);

    sns_state = cis->sns_state;
    sns_check_pointer_return(sns_state);

    (td_void)memset_s(awb_sns_dft, sizeof(ot_isp_awb_sensor_default), 0, sizeof(ot_isp_awb_sensor_default));
    awb_sns_dft->wb_ref_temp = CALIBRATE_STATIC_TEMP; /* wb_ref_temp 4950 */

    awb_sns_dft->gain_offset[0] = CALIBRATE_STATIC_WB_R_GAIN;
    awb_sns_dft->gain_offset[1] = CALIBRATE_STATIC_WB_GR_GAIN;
    awb_sns_dft->gain_offset[2] = CALIBRATE_STATIC_WB_GB_GAIN; /* index 2 */
    awb_sns_dft->gain_offset[3] = CALIBRATE_STATIC_WB_B_GAIN; /* index 3 */

    awb_sns_dft->wb_para[0] = CALIBRATE_AWB_P1;
    awb_sns_dft->wb_para[1] = CALIBRATE_AWB_P2;
    awb_sns_dft->wb_para[2] = CALIBRATE_AWB_Q1; /* index 2 */
    awb_sns_dft->wb_para[3] = CALIBRATE_AWB_A1; /* index 3 */
    awb_sns_dft->wb_para[4] = CALIBRATE_AWB_B1; /* index 4 */
    awb_sns_dft->wb_para[5] = CALIBRATE_AWB_C1; /* index 5 */

    awb_sns_dft->golden_rgain = GOLDEN_RGAIN;
    awb_sns_dft->golden_bgain = GOLDEN_BGAIN;

    switch (sns_state->wdr_mode) {
        case OT_WDR_MODE_NONE:
            (td_void)memcpy_s(&awb_sns_dft->ccm, sizeof(ot_isp_awb_ccm), &g_awb_ccm, sizeof(ot_isp_awb_ccm));
            (td_void)memcpy_s(&awb_sns_dft->agc_tbl, sizeof(ot_isp_awb_agc_table),
                              &g_awb_agc_table, sizeof(ot_isp_awb_agc_table));
            break;
        case OT_WDR_MODE_2To1_FRAME:
        case OT_WDR_MODE_2To1_LINE:
            (td_void)memcpy_s(&awb_sns_dft->ccm, sizeof(ot_isp_awb_ccm), &g_awb_ccm, sizeof(ot_isp_awb_ccm));
            break;
        default:
            break;
    }

    awb_sns_dft->init_rgain = g_init_wb_gain[vi_pipe][0]; /* 0: Rgain */
    awb_sns_dft->init_ggain = g_init_wb_gain[vi_pipe][1]; /* 1: Ggain */
    awb_sns_dft->init_bgain = g_init_wb_gain[vi_pipe][2]; /* 2: Bgain */
    awb_sns_dft->sample_rgain = g_sample_r_gain[vi_pipe];
    awb_sns_dft->sample_bgain = g_sample_b_gain[vi_pipe];

    return TD_SUCCESS;
}

static td_void cmos_init_awb_exp_function(ot_isp_awb_sensor_exp_func *exp_func)
{
    (td_void)memset_s(exp_func, sizeof(ot_isp_awb_sensor_exp_func), 0, sizeof(ot_isp_awb_sensor_exp_func));

    exp_func->pfn_cmos_get_awb_default = cmos_awb_get_default;

    return;
}

/****************************************************************************
 * ISP functions                                                            *
 ****************************************************************************/
static ot_isp_cmos_dng_color_param g_dng_color_param = {
    { 378, 256, 430 }, { 439, 256, 439 },
    { 2810, { 0x01AC, 0x8093, 0x8019, 0x8070, 0x01EA, 0x807A, 0x802A, 0x80F3, 0x021D }},
    { 4940, { 0x01D7, 0x8084, 0x8053, 0x8053, 0x01D9, 0x8086, 0x8010, 0x80B3, 0x01C3 }}};

static td_void cmos_isp_get_dng_default(const ot_isp_sns_state *sns_state, ot_isp_cmos_default *isp_def)
{
    (td_void)memcpy_s(&isp_def->dng_color_param, sizeof(ot_isp_cmos_dng_color_param), &g_dng_color_param,
                      sizeof(ot_isp_cmos_dng_color_param));

    switch (sns_state->img_mode) {
        case CS5186_SENSOR_24M_60FPS_LINEAR_MODE:
            isp_def->sns_mode.dng_raw_format.bits_per_sample = 12; /* 12bit */
            isp_def->sns_mode.dng_raw_format.white_level = 4095; /* max 4095 */
            break;
        case CS5186_SENSOR_1K_60FPS_LINEAR_MODE:
        case CS5186_SENSOR_2K_60FPS_LINEAR_MODE:
        case CS5186_SENSOR_4K_60FPS_LINEAR_MODE:
        case CS5186_SENSOR_4K_60FPS_LINEAR_OB_MODE:
        case CS5186_SENSOR_1K_90FPS_DOL2_MODE:
        case CS5186_SENSOR_1K_60FPS_WDR_MODE:
        case CS5186_SENSOR_2K_90FPS_DOL2_MODE:
        case CS5186_SENSOR_2K_60FPS_WDR_MODE:
        case CS5186_SENSOR_4K_90FPS_DOL2_MODE:
        case CS5186_SENSOR_4K_60FPS_WDR_MODE:
        case CS5186_SENSOR_4K_15FPS_LINEAR_WDR_MODE:
        case CS5186_SENSOR_640_480_15FPS_LINEAR_MODE:
            isp_def->sns_mode.dng_raw_format.bits_per_sample = 10; /* 10bit */
            isp_def->sns_mode.dng_raw_format.white_level = 4095; /* max 4095 */
            break;
        default:
            break;
    }

    isp_def->sns_mode.dng_raw_format.default_scale.default_scale_hor.denominator = 1;
    isp_def->sns_mode.dng_raw_format.default_scale.default_scale_hor.numerator = 1;
    isp_def->sns_mode.dng_raw_format.default_scale.default_scale_ver.denominator = 1;
    isp_def->sns_mode.dng_raw_format.default_scale.default_scale_ver.numerator = 1;
    isp_def->sns_mode.dng_raw_format.cfa_repeat_pattern_dim.repeat_pattern_dim_row = 2; /* pattern 2 */
    isp_def->sns_mode.dng_raw_format.cfa_repeat_pattern_dim.repeat_pattern_dim_col = 2; /* pattern 2 */
    isp_def->sns_mode.dng_raw_format.black_level_repeat_dim.repeat_row = 2; /* pattern 2 */
    isp_def->sns_mode.dng_raw_format.black_level_repeat_dim.repeat_col = 2; /* pattern 2 */
    isp_def->sns_mode.dng_raw_format.cfa_layout = OT_ISP_CFALAYOUT_TYPE_RECTANGULAR;
    isp_def->sns_mode.dng_raw_format.cfa_plane_color[0] = 0;
    isp_def->sns_mode.dng_raw_format.cfa_plane_color[1] = 1;
    isp_def->sns_mode.dng_raw_format.cfa_plane_color[2] = 2; /* index 2, cfa_plane_color 2 */
    isp_def->sns_mode.dng_raw_format.cfa_pattern[0] = 0;
    isp_def->sns_mode.dng_raw_format.cfa_pattern[1] = 1;
    isp_def->sns_mode.dng_raw_format.cfa_pattern[2] = 1; /* index 2, cfa_pattern 1 */
    isp_def->sns_mode.dng_raw_format.cfa_pattern[3] = 2; /* index 3, cfa_pattern 2 */
    isp_def->sns_mode.valid_dng_raw_format = TD_TRUE;

    return;
}

static void cmos_isp_get_linear_default(ot_isp_cmos_default *isp_def)
{
    isp_def->key.bit1_demosaic         = 1;
    isp_def->demosaic                  = &g_cmos_demosaic;
    isp_def->key.bit1_sharpen          = 1;
    isp_def->sharpen                   = &g_cmos_yuv_sharpen;
    isp_def->key.bit1_drc              = 1;
    isp_def->drc                       = &g_cmos_drc;
    isp_def->key.bit1_bayer_nr         = 1;
    isp_def->bayer_nr                  = &g_cmos_bayer_nr;
    isp_def->key.bit1_anti_false_color = 1;
    isp_def->anti_false_color          = &g_cmos_anti_false_color;
    isp_def->key.bit1_ldci             = 1;
    isp_def->ldci                      = &g_cmos_ldci;
    isp_def->key.bit1_gamma            = 1;
    isp_def->gamma                     = &g_cmos_gamma;
#ifdef CONFIG_OT_ISP_CR_SUPPORT
    isp_def->key.bit1_ge               = 1;
    isp_def->ge                        = &g_cmos_ge;
#endif
    isp_def->key.bit1_dehaze = 1;
    isp_def->dehaze = &g_cmos_dehaze;
    (td_void)memcpy_s(&isp_def->noise_calibration, sizeof(ot_isp_noise_calibration),
                      &g_cmos_noise_calibration, sizeof(ot_isp_noise_calibration));
    return;
}

static td_s32 cmos_isp_get_default(ot_vi_pipe vi_pipe, ot_isp_cmos_default *isp_def)
{
    ot_isp_sns_state *sns_state = TD_NULL;

    sns_check_pointer_return(isp_def);
    cs5186_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_return(sns_state);

    (td_void)memset_s(isp_def, sizeof(ot_isp_cmos_default), 0, sizeof(ot_isp_cmos_default));
#ifdef CONFIG_OT_ISP_CA_SUPPORT
    isp_def->key.bit1_ca      = 1;
    isp_def->ca               = &g_cmos_ca;
#endif
    isp_def->key.bit1_clut    = 1;
    isp_def->clut             = &g_cmos_clut;

    isp_def->key.bit1_dpc     = 1;
    isp_def->dpc              = &g_cmos_dpc;

    isp_def->key.bit1_wdr     = 1;
    isp_def->wdr              = &g_cmos_wdr;

    isp_def->key.bit1_lsc      = 0;
    isp_def->lsc               = &g_cmos_lsc;

    isp_def->key.bit1_acs      = 0;
    isp_def->acs               = &g_cmos_acs;

#ifdef CONFIG_OT_ISP_PREGAMMA_SUPPORT
    isp_def->key.bit1_pregamma = 0;
    isp_def->pregamma          = &g_cmos_pregamma;
#endif
    switch (sns_state->wdr_mode) {
        case OT_WDR_MODE_NONE:
            cmos_isp_get_linear_default(isp_def);
            break;
        default:
            break;
    }

    isp_def->wdr_switch_attr.exp_ratio[0] = 0x40;
    isp_def->sns_mode.sns_id = CS5186_ID;
    isp_def->sns_mode.sns_mode = sns_state->img_mode;
    cmos_isp_get_dng_default(sns_state, isp_def);

    return TD_SUCCESS;
}

static td_s32 cmos_isp_get_black_level(ot_vi_pipe vi_pipe, ot_isp_cmos_black_level *black_level)
{
    td_s32  i;
    ot_isp_sns_state *sns_state = TD_NULL;

    sns_check_pointer_return(black_level);
    cs5186_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_return(sns_state);

    (td_void)memcpy_s(black_level, sizeof(ot_isp_cmos_black_level), &g_cmos_blc, sizeof(ot_isp_cmos_black_level));

    /* Don't need to update black level when iso change */
    black_level->auto_attr.update = TD_FALSE;

    /* black level of linear mode */
    if (sns_state->wdr_mode == OT_WDR_MODE_NONE) {
        for (i = 0; i < OT_ISP_BAYER_CHN_NUM; i++) {
            black_level->auto_attr.black_level[0][i] = 0x320;
        }
    }

    return TD_SUCCESS;
}

static td_s32 cmos_isp_get_blc_clamp_info(ot_vi_pipe vi_pipe, td_bool *blc_clamp_en)
{
    sns_check_pointer_return(blc_clamp_en);

    *blc_clamp_en = blc_clamp_info[vi_pipe];

    return TD_SUCCESS;
}

static td_s32 cmos_isp_set_wdr_mode(ot_vi_pipe vi_pipe, td_u8 mode)
{
    ot_isp_sns_state *sns_state = TD_NULL;

    cs5186_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_return(sns_state);

    sns_state->sync_init    = TD_FALSE;

    switch (mode & 0x3F) {
        case OT_WDR_MODE_NONE:
            sns_state->wdr_mode = OT_WDR_MODE_NONE;
            printf("linear mode\n");
            break;

        case OT_WDR_MODE_2To1_LINE:
            sns_state->wdr_mode = OT_WDR_MODE_2To1_LINE;
            printf("vc 2t1 line mode\n");
            break;

        case OT_WDR_MODE_2To1_FRAME:
            sns_state->wdr_mode = OT_WDR_MODE_2To1_FRAME;
            printf("vc 2t1 frame mode\n");
            break;

        default:
            isp_err_trace("NOT support this mode!\n");
            return TD_FAILURE;
    }

    (td_void)memset_s(sns_state->wdr_int_time, sizeof(sns_state->wdr_int_time), 0, sizeof(sns_state->wdr_int_time));

    return TD_SUCCESS;
}

/****************************************************************************
 * sensor register info functions                                           *
 ****************************************************************************/
static td_void cmos_comm_sns_reg_info_init(ot_vi_pipe vi_pipe, ot_isp_sns_state *sns_state)
{
    td_u32 i;

    sns_state->regs_info[0].sns_type = OT_ISP_SNS_TYPE_I2C;
    sns_state->regs_info[0].com_bus.i2c_dev = g_cs5186_bus_info[vi_pipe].i2c_dev;
    sns_state->regs_info[0].cfg2_valid_delay_max = 2; /* delay_max 2 */
    sns_state->regs_info[0].reg_num = REG_MAX_IDX;

    for (i = 0; i < sns_state->regs_info[0].reg_num; i++) {
        sns_state->regs_info[0].i2c_data[i].update = TD_TRUE;
        sns_state->regs_info[0].i2c_data[i].dev_addr = CS5186_I2C_ADDR;
        sns_state->regs_info[0].i2c_data[i].addr_byte_num = CS5186_ADDR_BYTE;
        sns_state->regs_info[0].i2c_data[i].data_byte_num = CS5186_DATA_BYTE;
    }

    sns_state->regs_info[0].i2c_data[AE_INTTIME_IDX].delay_frame_num = 0;
    sns_state->regs_info[0].i2c_data[AE_INTTIME_IDX].reg_addr = 0x40020968;
    sns_state->regs_info[0].i2c_data[AE_AGAIN_DGAIN_IDX].delay_frame_num = 0;
    sns_state->regs_info[0].i2c_data[AE_AGAIN_DGAIN_IDX].reg_addr = 0x40020964;

    sns_state->regs_info[0].i2c_data[AE_W_INTTIME_MAX_MIN_IDX].delay_frame_num = 0;
    sns_state->regs_info[0].i2c_data[AE_W_INTTIME_MAX_MIN_IDX].reg_addr = 0x4002091c;
    sns_state->regs_info[0].i2c_data[AE_W_AGAIN_MAX_MIN_IDX].delay_frame_num = 0;
    sns_state->regs_info[0].i2c_data[AE_W_AGAIN_MAX_MIN_IDX].reg_addr = 0x4002092c;
    sns_state->regs_info[0].i2c_data[AE_W_DGAIN_MAX_MIN_IDX].delay_frame_num = 0;
    sns_state->regs_info[0].i2c_data[AE_W_DGAIN_MAX_MIN_IDX].reg_addr = 0x40020930;

    sns_state->regs_info[0].i2c_data[AE_INTTIME_SHORT_IDX].delay_frame_num = 0;
    sns_state->regs_info[0].i2c_data[AE_INTTIME_SHORT_IDX].reg_addr = 0x40020b48;
    sns_state->regs_info[0].i2c_data[AE_W_SHORT_INTTIME_MAX_MIN_IDX].delay_frame_num = 0;
    sns_state->regs_info[0].i2c_data[AE_W_SHORT_INTTIME_MAX_MIN_IDX].reg_addr = 0x40020b10;
    sns_state->regs_info[0].i2c_data[AE_AGAIN_DGAIN_SHORT_IDX].delay_frame_num = 0;
    sns_state->regs_info[0].i2c_data[AE_AGAIN_DGAIN_SHORT_IDX].reg_addr = 0x40020b44;

    sns_state->regs_info[0].i2c_data[AE_E0_IDX].delay_frame_num = 0;
    sns_state->regs_info[0].i2c_data[AE_E0_IDX].reg_addr = 0x400200e0;
    sns_state->regs_info[0].i2c_data[AE_E4_IDX].delay_frame_num = 0;
    sns_state->regs_info[0].i2c_data[AE_E4_IDX].reg_addr = 0x400200e4;
    return;
}

static td_void cmos_sns_reg_info_update(ot_vi_pipe vi_pipe, ot_isp_sns_state *sns_state)
{
    td_u32 i;
    ot_unused(vi_pipe);

    for (i = 0; i < AE_E0_IDX; i++) {
        if ((sns_state->regs_info[0].i2c_data[i].data ==
            sns_state->regs_info[1].i2c_data[i].data)) {
            sns_state->regs_info[0].i2c_data[i].update = TD_FALSE;
        } else {
            sns_state->regs_info[0].i2c_data[i].update = TD_TRUE;
            sns_state->regs_info[0].i2c_data[AE_E0_IDX].update = TD_TRUE;
            sns_state->regs_info[0].i2c_data[AE_E4_IDX].update = TD_TRUE;
        }
    }

    return;
}

static td_s32 cmos_isp_get_sns_regs_info(ot_vi_pipe vi_pipe, ot_isp_sns_regs_info *sns_regs_info)
{
    ot_isp_sns_state *sns_state = TD_NULL;

    sns_check_pointer_return(sns_regs_info);
    cs5186_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_return(sns_state);

    if ((sns_state->sync_init == TD_FALSE) || (sns_regs_info->config == TD_FALSE)) {
        cmos_comm_sns_reg_info_init(vi_pipe, sns_state);
        sns_state->sync_init = TD_TRUE;
    } else {
        cmos_sns_reg_info_update(vi_pipe, sns_state);
    }

    sns_regs_info->config = TD_FALSE;
    (td_void)memcpy_s(sns_regs_info, sizeof(ot_isp_sns_regs_info),
                      &sns_state->regs_info[0], sizeof(ot_isp_sns_regs_info));
    (td_void)memcpy_s(&sns_state->regs_info[1], sizeof(ot_isp_sns_regs_info),
                      &sns_state->regs_info[0], sizeof(ot_isp_sns_regs_info));
    sns_state->fl[1] = sns_state->fl[0];

    return TD_SUCCESS;
}

/****************************************************************************
 * sensor mode functions                                                    *
 ****************************************************************************/
static td_void cmos_isp_config_image_mode_param(ot_vi_pipe vi_pipe, td_u8 sns_image_mode,
    ot_isp_sns_state *sns_state)
{
    switch (sns_image_mode) {
        case CS5186_SENSOR_24M_60FPS_LINEAR_MODE:
            sns_state->fl_std = CS5186_VMAX_4K_LINEAR;
            break;
        case CS5186_SENSOR_4K_15FPS_LINEAR_WDR_MODE:
            sns_state->fl_std = CS5186_VMAX_4K_15FPS_WDR;
            break;
        case CS5186_SENSOR_640_480_15FPS_LINEAR_MODE:
            sns_state->fl_std = CS5186_VMAX_640_480_LINEAR;
            break;
        default:
            break;
    }
}

static td_s32 cmos_isp_set_image_mode(ot_vi_pipe vi_pipe, const ot_isp_cmos_sns_image_mode *sns_image_mode)
{
    td_u32 i;
    td_u8 image_mode;
    ot_isp_sns_state *sns_state = TD_NULL;

    sns_check_pointer_return(sns_image_mode);
    cs5186_sensor_get_ctx(vi_pipe, sns_state);
    sns_check_pointer_return(sns_state);

    image_mode = sns_state->img_mode;

    for (i = 0; i < CS5186_MODE_BUTT; i++) {
        if (sns_image_mode->fps <= g_cs5186_mode_tbl[i].max_fps &&
            sns_image_mode->width <= g_cs5186_mode_tbl[i].width &&
            sns_image_mode->height <= g_cs5186_mode_tbl[i].height &&
            sns_state->wdr_mode == g_cs5186_mode_tbl[i].wdr_mode) {
            image_mode = (cs5186_res_mode)i;
            break;
        }
    }

    if (i >= CS5186_MODE_BUTT) {
        cmos_err_mode_print(sns_image_mode, sns_state);
        return TD_FAILURE;
    }

    cmos_isp_config_image_mode_param(vi_pipe, image_mode, sns_state);

    if ((sns_state->init == TD_TRUE) && (image_mode == sns_state->img_mode)) {
        return OT_ISP_DO_NOT_NEED_SWITCH_IMAGEMODE; /* Don't need to switch image_mode */
    }

    sns_state->sync_init = TD_FALSE;
    sns_state->img_mode = image_mode;
    sns_state->fl[0] = sns_state->fl_std;
    sns_state->fl[1] = sns_state->fl[0];

    return TD_SUCCESS;
}

/****************************************************************************
 * sensor life cycle functions                                              *
 ****************************************************************************/
static void cmos_isp_init(ot_vi_pipe vi_pipe)
{
    td_s32           ret;
    ot_wdr_mode      wdr_mode;
    td_u8            img_mode;
    cis_info *cis = TD_NULL;
    ot_isp_sns_state *sensor_state = TD_NULL;

    sns_check_pipe_void_return(vi_pipe);

    cis = cmos_get_info(vi_pipe);
    sns_check_pointer_void_return(cis);

    sensor_state = cis->sns_state;
    sns_check_pointer_void_return(sensor_state);

    wdr_mode    = sensor_state->wdr_mode;
    img_mode    = sensor_state->img_mode;

    cis->i2c.addr = CS5186_I2C_ADDR;
    cis->i2c.addr_byte_num = CS5186_ADDR_BYTE;
    cis->i2c.data_byte_num = CS5186_DATA_BYTE;

    ret = cis_i2c_init(cis);
    if (ret != TD_SUCCESS) {
        isp_err_trace("i2c init failed!\n");
        return;
    }

    /* When sensor first init, config all registers */
    if (OT_WDR_MODE_2To1_LINE == wdr_mode) {
    } else {
        cs5186_linear_8m30_10bit_init(cis);
    }

    sensor_state->init = TD_TRUE;

    return;
}

static void cmos_isp_exit(ot_vi_pipe vi_pipe)
{
    td_s32 ret;
    cis_info *cis = TD_NULL;

    sns_check_pipe_void_return(vi_pipe);

    cis = cmos_get_info(vi_pipe);
    sns_check_pointer_void_return(cis);

    ret = cis_i2c_exit(cis);
    if (ret != TD_SUCCESS) {
        isp_err_trace("CS5186 exit failed!\n");
    }

    return;
}

static void cmos_isp_global_init(ot_vi_pipe vi_pipe)
{
    cis_info *cis = TD_NULL;
    ot_isp_sns_state *sns_state = TD_NULL;

    sns_check_pipe_void_return(vi_pipe);

    cis = cmos_get_info(vi_pipe);
    sns_check_pointer_void_return(cis);

    sns_state = cis->sns_state;
    sns_check_pointer_void_return(sns_state);

    sns_state->init = TD_FALSE;
    sns_state->sync_init = TD_FALSE;
    sns_state->img_mode = CS5186_SENSOR_24M_60FPS_LINEAR_MODE;
    sns_state->wdr_mode = OT_WDR_MODE_NONE;
    sns_state->fl_std  = CS5186_VMAX_4K_LINEAR;
    sns_state->fl[0] = CS5186_VMAX_4K_LINEAR;
    sns_state->fl[1] = CS5186_VMAX_4K_LINEAR;

    (td_void)memset_s(&sns_state->regs_info[0], sizeof(ot_isp_sns_regs_info), 0, sizeof(ot_isp_sns_regs_info));
    (td_void)memset_s(&sns_state->regs_info[1], sizeof(ot_isp_sns_regs_info), 0, sizeof(ot_isp_sns_regs_info));
    return;
}

static void cmos_isp_set_pixel_detect(ot_vi_pipe vi_pipe, td_bool enable)
{
    td_u32 full_lines_5fps, max_int_time_5fps;
    cis_info *cis = TD_NULL;
    cis_i2c *i2c = TD_NULL;
    ot_isp_sns_state *sns_state = TD_NULL;

    sns_check_pipe_void_return(vi_pipe);

    cis = cmos_get_info(vi_pipe);
    sns_check_pointer_void_return(cis);

    i2c = &cis->i2c;
    sns_state = cis->sns_state;
    sns_check_pointer_void_return(sns_state);

    if (sns_state->wdr_mode == OT_WDR_MODE_2To1_LINE) {
        return;
    } else {
        if (sns_state->img_mode == CS5186_SENSOR_24M_60FPS_LINEAR_MODE) {
            full_lines_5fps = (CS5186_VMAX_4K_LINEAR * 30) / 5; /* 4k base 15fps, 5fps */
        } else if (sns_state->img_mode == CS5186_SENSOR_640_480_15FPS_LINEAR_MODE) {
            full_lines_5fps = (CS5186_VMAX_4K_LINEAR * 30) / 5; /* 4k base 15fps, 5fps */
        } else {
            return;
        }
    }

    max_int_time_5fps = CS5186_EXP_TIME_LMT;

    if (enable) { /* setup for ISP pixel calibration mode */
        cis_write_reg(i2c, 0x3090, 0x00); /* GAIN_PGC_L */
        cis_write_reg(i2c, 0x3091, 0x00); /* GAIN_PGC_H */

        cis_write_reg(i2c, 0x3024, low_8bits(full_lines_5fps));  /* VMAX_L */
        cis_write_reg(i2c, 0x3025, high_8bits(full_lines_5fps));  /* VMAX_M */
        cis_write_reg(i2c, 0x3026, higher_4bits_from_20(full_lines_5fps)); /* VMAX_H */

        cis_write_reg(i2c, 0x3050, low_8bits(max_int_time_5fps));  /* SHR0_L */
        cis_write_reg(i2c, 0x3051, high_8bits(max_int_time_5fps));  /* SHR0_M */
        cis_write_reg(i2c, 0x3052, higher_4bits_from_20(max_int_time_5fps)); /* SHR0_H */
    } else { /* setup for ISP 'normal mode' */
        sns_state->fl_std = (sns_state->fl_std > CS5186_FULL_LINES_MAX_LINEAR) ?
            CS5186_FULL_LINES_MAX_LINEAR : sns_state->fl_std;
        cis_write_reg(i2c, 0x3024, low_8bits(sns_state->fl_std));
        cis_write_reg(i2c, 0x3025, high_8bits(sns_state->fl_std));
        cis_write_reg(i2c, 0x3026, higher_4bits_from_20(sns_state->fl_std));
        sns_state->sync_init = TD_FALSE;
    }

    return;
}

/****************************************************************************
 * sensor exp/register functions                                            *
 ****************************************************************************/
static td_void cmos_init_sensor_exp_function(ot_isp_sns_exp_func *sensor_exp_func)
{
    (td_void)memset_s(sensor_exp_func, sizeof(ot_isp_sns_exp_func), 0, sizeof(ot_isp_sns_exp_func));

    sensor_exp_func->pfn_cmos_sns_init         = cmos_isp_init;
    sensor_exp_func->pfn_cmos_sns_exit         = cmos_isp_exit;
    sensor_exp_func->pfn_cmos_sns_global_init  = cmos_isp_global_init;
    sensor_exp_func->pfn_cmos_set_image_mode   = cmos_isp_set_image_mode;
    sensor_exp_func->pfn_cmos_set_wdr_mode     = cmos_isp_set_wdr_mode;
    sensor_exp_func->pfn_cmos_get_isp_default  = cmos_isp_get_default;
    sensor_exp_func->pfn_cmos_get_isp_black_level = cmos_isp_get_black_level;
    sensor_exp_func->pfn_cmos_get_blc_clamp_info  = cmos_isp_get_blc_clamp_info;
    sensor_exp_func->pfn_cmos_set_pixel_detect = cmos_isp_set_pixel_detect;
    sensor_exp_func->pfn_cmos_get_sns_reg_info = cmos_isp_get_sns_regs_info;

    return;
}

static td_s32 cmos_register_callback(ot_vi_pipe vi_pipe, ot_isp_3a_alg_lib *ae_lib, ot_isp_3a_alg_lib *awb_lib)
{
    td_s32 ret;
    cis_register reg = {0};
    cis_info *cis = TD_NULL;

    sns_check_pipe_return(vi_pipe);
    sns_check_pointer_return(ae_lib);
    sns_check_pointer_return(awb_lib);

    cis = cmos_get_info(vi_pipe);
    sns_check_pointer_return(cis);

    cis->pipe = vi_pipe;
    cis->sns_id = CS5186_ID;

    reg.ae_lib = ae_lib;
    reg.awb_lib = awb_lib;

    cmos_init_sensor_exp_function(&reg.isp_register.sns_exp);
    cmos_init_ae_exp_function(&reg.ae_register.sns_exp);
    cmos_init_awb_exp_function(&reg.awb_register.sns_exp);

    ret = cis_register_callback(cis, &reg);
    if (ret != TD_SUCCESS) {
        isp_err_trace("cis_register_callback failed!\n");
        return ret;
    }

    /* sync state pointer for legacy get_ctx access */
    g_cs5186_sns_state[vi_pipe] = cis->sns_state;

    return TD_SUCCESS;
}

static td_s32 cmos_unregister_callback(ot_vi_pipe vi_pipe, ot_isp_3a_alg_lib *ae_lib, ot_isp_3a_alg_lib *awb_lib)
{
    td_s32 ret;
    cis_register reg = {0};
    cis_info *cis = TD_NULL;

    sns_check_pipe_return(vi_pipe);
    sns_check_pointer_return(ae_lib);
    sns_check_pointer_return(awb_lib);

    cis = cmos_get_info(vi_pipe);
    sns_check_pointer_return(cis);

    reg.ae_lib = ae_lib;
    reg.awb_lib = awb_lib;

    ret = cis_unregister_callback(cis, &reg);
    if (ret != TD_SUCCESS) {
        isp_err_trace("cis_unregister_callback failed!\n");
        return ret;
    }

    g_cs5186_sns_state[vi_pipe] = TD_NULL;

    return TD_SUCCESS;
}

static void cmos_standby(ot_vi_pipe vi_pipe)
{
    ot_unused(vi_pipe);
    return;
}

static void cmos_restart(ot_vi_pipe vi_pipe)
{
    ot_unused(vi_pipe);
    return;
}

static td_s32 cmos_write_register(ot_vi_pipe vi_pipe, td_u32 addr, td_u32 data)
{
    ot_unused(vi_pipe);
    ot_unused(addr);
    ot_unused(data);
    return TD_SUCCESS;
}

static td_s32 cmos_read_register(ot_vi_pipe vi_pipe, td_u32 addr)
{
    ot_unused(vi_pipe);
    ot_unused(addr);
    return TD_SUCCESS;
}

static td_s32 cmos_set_bus_info(ot_vi_pipe vi_pipe, ot_isp_sns_commbus sns_bus_info)
{
    cis_info *cis = TD_NULL;

    sns_check_pipe_return(vi_pipe);

    cis = cmos_get_info(vi_pipe);
    sns_check_pointer_return(cis);

    cis->bus_info.i2c_dev = sns_bus_info.i2c_dev;
    g_cs5186_bus_info[vi_pipe].i2c_dev = sns_bus_info.i2c_dev;

    return TD_SUCCESS;
}

static td_s32 cmos_set_init(ot_vi_pipe vi_pipe, ot_isp_init_attr *init_attr)
{
    sns_check_pointer_return(init_attr);

    g_init_exposure[vi_pipe]  = init_attr->exposure;
    g_lines_per500ms[vi_pipe] = init_attr->lines_per500ms;
    g_init_wb_gain[vi_pipe][0] = init_attr->wb_r_gain; /* 0: rgain */
    g_init_wb_gain[vi_pipe][1] = init_attr->wb_g_gain; /* 1: ggain */
    g_init_wb_gain[vi_pipe][2] = init_attr->wb_b_gain; /* 2: bgain */
    g_sample_r_gain[vi_pipe] = init_attr->sample_r_gain;
    g_sample_b_gain[vi_pipe] = init_attr->sample_b_gain;
    g_quick_start_en[vi_pipe] = init_attr->quick_start_en;
    g_ae_stat_pos[vi_pipe]       = init_attr->ae_stat_pos;
    g_ae_route_ex_valid[vi_pipe] = init_attr->ae_route_ex_valid;
    (td_void)memcpy_s(&g_init_ae_route[vi_pipe], sizeof(ot_isp_ae_route),
                      &init_attr->ae_route, sizeof(ot_isp_ae_route));
    (td_void)memcpy_s(&g_init_ae_route_ex[vi_pipe], sizeof(ot_isp_ae_route_ex),
                      &init_attr->ae_route_ex, sizeof(ot_isp_ae_route_ex));
    (td_void)memcpy_s(&g_init_ae_route_sf[vi_pipe], sizeof(ot_isp_ae_route),
                      &init_attr->ae_route_sf, sizeof(ot_isp_ae_route));
    (td_void)memcpy_s(&g_init_ae_route_sf_ex[vi_pipe], sizeof(ot_isp_ae_route_ex),
                      &init_attr->ae_route_sf_ex, sizeof(ot_isp_ae_route_ex));

    return TD_SUCCESS;
}

static td_s32 cmos_get_sns_info(ot_vi_pipe vi_pipe, ot_isp_sns_info* sns_info)
{
    sns_check_pointer_return(sns_info);
    sns_check_pipe_return(vi_pipe);

    /* cs5186 产品固定只使用 4K@15fps linear 模式，逐项填入该模式信息 */
    sns_info->mode_tbl[0].ver_lines     = CS5186_VMAX_4K_LINEAR;
    sns_info->mode_tbl[0].max_ver_lines = CS5186_FULL_LINES_MAX_LINEAR;
    sns_info->mode_tbl[0].max_fps       = CS5186_FPS_LINEAR;
    sns_info->mode_tbl[0].min_fps       = CS5186_FPS_MIN_LINEAR;
    sns_info->mode_tbl[0].width         = CS5186_WIDTH;
    sns_info->mode_tbl[0].height        = CS5186_HEIGHT;
    sns_info->mode_tbl[0].sns_mode      = 0;
    sns_info->mode_tbl[0].wdr_mode      = OT_WDR_MODE_NONE;
    sns_info->mode_tbl[0].mode_name     = "CS5186_SENSOR_4K_15FPS_LINEAR_MODE";

    sns_info->bayer_format[0] = OT_ISP_BAYER_GRBG;
    sns_info->sns_info_num = 1;

    return TD_SUCCESS;
}

ot_isp_sns_obj g_sns_cs5186_obj = {
    .pfn_register_callback     = cmos_register_callback,
    .pfn_un_register_callback  = cmos_unregister_callback,
    .pfn_standby               = TD_NULL,
    .pfn_restart               = TD_NULL,
    .pfn_mirror_flip           = TD_NULL,
    .pfn_set_blc_clamp         = TD_NULL,
    .pfn_write_reg             = TD_NULL,
    .pfn_read_reg              = TD_NULL,
    .pfn_set_bus_info          = cmos_set_bus_info,
    .pfn_set_init              = cmos_set_init,
    .pfn_get_sns_info          = cmos_get_sns_info
};

ot_isp_sns_obj *cs5186_get_obj(td_void)
{
    return &g_sns_cs5186_obj;
}
