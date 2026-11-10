#ifndef CS5186_CMOS_H
#define CS5186_CMOS_H

#include "ot_common_isp.h"
#include "ot_sns_ctrl.h"
#include "securec.h"

#ifdef __cplusplus
#if __cplusplus
extern "C" {
#endif
#endif

/****************************************************************************
 * basic info config                                                        *
 ****************************************************************************/
/* sensor ID & Resolution */
#define CS5186_ID             123
#define CS5186_WIDTH          3840
#define CS5186_HEIGHT         2160

/* sensor Register Address */
#define CS5186_EXPO_H_ADDR           0x40020968
#define CS5186_EXPO_M_ADDR           0x40020968
#define CS5186_EXPO_L_ADDR           0x40020968
#define CS5186_AGAIN_H_ADDR          0x40020964
#define CS5186_AGAIN_L_ADDR          0x40020964
#define CS5186_DGAIN_H_ADDR          0x40020964
#define CS5186_DGAIN_L_ADDR          0x40020964
#define CS5186_VMAX_H_ADDR           0x3025
#define CS5186_VMAX_L_ADDR           0x3024
#define CS5186_SHORT_EXPO_H_ADDR     0x40020b48
#define CS5186_SHORT_EXPO_L_ADDR     0x40020b48
#define CS5186_SHORT_AGAIN_H_ADDR    0x40020b44
#define CS5186_SHORT_AGAIN_L_ADDR    0x40020b44
#define CS5186_SHORT_DGAIN_H_ADDR    0x40020b44
#define CS5186_SHORT_DGAIN_L_ADDR    0x40020b44
#define CS5186_SHORT_EXP_MAX_H_ADDR  0x40020b10
#define CS5186_SHORT_EXP_MAX_L_ADDR  0x40020b10

/****************************************************************************
 * i2c bus config                                                           *
 ****************************************************************************/
/* sensor I2C bus config */
#define CS5186_I2C_ADDR    0x06
#define CS5186_ADDR_BYTE   4
#define CS5186_DATA_BYTE   4

/* common I2C bus config */
#define I2C_DEV_FILE_NUM     16
#define I2C_BUF_NUM          8

/****************************************************************************
 * sensor lines configs                                                     *
 ****************************************************************************/
 /* increase line */
 #define CS5186_INCREASE_LINES 1  /* make real fps less than stand fps because NVR require */
/* linear mode */
#define CS5186_FULL_LINES_MAX_LINEAR 0xFFFFF
#define CS5186_VMAX_4K_LINEAR 2250      /* use FH not act_H */
#define CS5186_VMAX_2K_LINEAR 1500      /* use FH not act_H */
#define CS5186_VMAX_1K_LINEAR 1126      /* use FH not act_H */
#define CS5186_VMAX_640_480_LINEAR 1126
#define CS5186_VMAX_4K_15FPS_WDR 4500
#define CS5186_FPS_MAX_LINEAR 30
#define CS5186_FPS_LINEAR 15
#define CS5186_FPS_MIN_LINEAR 0.07
#define CS5186_WIDTH_4K_LINEAR 3840
#define CS5186_HEIGHT_4K_LINEAR 2160
#define CS5186_WIDTH_2K_LINEAR 2560
#define CS5186_HEIGHT_2K_LINEAR 1440
#define CS5186_WIDTH_1K_LINEAR 1920
#define CS5186_HEIGHT_1K_LINEAR 1080
#define CS5186_MODE_LINEAR 0

/****************************************************************************
 * sensor ae configs                                                        *
 ****************************************************************************/
 /* linear mode */
#define CS5186_AGAIN_MIN    1024
#define CS5186_AGAIN_MAX    (16 * 1024)  /* 32768 */

#define CS5186_DGAIN_MIN    1024
#define CS5186_DGAIN_MAX    65535

#define CS5186_AD_GAIN_TBL_RANGE  241
#define CS5186_AGAIN_TBL_RANGE    100
#define CS5186_DGAIN_TBL_RANGE    140

#define CS5186_EXP_TIME_LMT    6

#define INT_TIME_ACCURACY_LINEAR  1

#define AGAIN_ACCURACY            1
#define DGAIN_ACCURACY            1
#define ISP_DGAIN_SHIFT           8
#define ISP_DGAIN_TARGET_MIN      1
#define ISP_DGAIN_TARGET_MAX     32

#define ISP_DGAIN_TARGET_WDR_MAX  4

#define FL_OFFSET_LINEAR                  6
#define FL_OFFSET_WDR_LONG                26
#define FL_OFFSET_WDR_SHORT               22
#define CS5186_SEXP_MAX_DEFAULT         0x178
 /****************************************************************************
 * sensor awb calibrate configs                                             *
 ****************************************************************************/
/* awb static param for Fuji Lens New IR_Cut */
#define CALIBRATE_STATIC_TEMP       4950
#define CALIBRATE_STATIC_WB_R_GAIN  471
#define CALIBRATE_STATIC_WB_GR_GAIN 256
#define CALIBRATE_STATIC_WB_GB_GAIN 256
#define CALIBRATE_STATIC_WB_B_GAIN  490

/* Calibration results for Auto WB Planck */
#define CALIBRATE_AWB_P1 (-14)
#define CALIBRATE_AWB_P2 270
#define CALIBRATE_AWB_Q1 0
#define CALIBRATE_AWB_A1 188408
#define CALIBRATE_AWB_B1 128
#define CALIBRATE_AWB_C1 (-138920)

/* Rgain and Bgain of the golden sample */
#define GOLDEN_RGAIN                                  0
#define GOLDEN_BGAIN                                  0

/****************************************************************************
 * sensor other configs                                                     *
 ****************************************************************************/
#define STANDARD_FPS              30
#define FLICKER_FREQ            (50 * 256)  /* light flicker freq: 50Hz, accuracy: 256 */

#define INIT_EXP_DEFAULT_LINEAR   76151

#define MAX_INT_TIME_TARGET        65535

#define AE_COMENSATION_DEFAULT       0x38
#define AE_COMENSATION_WDR_LONG_FRM    56
#define AE_COMENSATION_WDR_NORM_FRM    32

#define AE_ARR_RATIO_0_WDR             0x400
#define AE_ARR_RATIO_1_WDR             0x40
#define AE_ARR_RATIO_2_WDR             0x40

/* Black level */
#define BLACK_LEVEL_DEFAULT          0x320
/* DNG */
#define DNG_RAW_FORMAT_BIT_LINEAR         10 /* raw 10 bit */
#define DNG_RAW_FORMAT_WHITE_LEVEL_LINEAR 4095 /* max */
#define DNG_RAW_FORMAT_BIT_WDR            10 /* raw 10 bit */
#define DNG_RAW_FORMAT_WHITE_LEVEL_WDR    1023 /* 2^10 - 1 */

/****************************************************************************
 * assist function macros                                                   *
 ****************************************************************************/
#define high_8bits(x)   (((x) & 0xff00) >> 8)
#define low_8bits(x)    ((x) & 0x00ff)
#define lower_4bits(x)  (((x) & 0x000F) << 4)
#define higher_4bits(x) (((x) & 0x0F000) >> 12)
#define higher_8bits(x) (((x) & 0x0FF0) >> 4)
#define higher_4bits_from_20(x) (((x) & 0xF0000) >> 16)
#ifndef clip3
#define clip3(x, min, max) ((x) < (min) ? (min) : ((x) > (max) ? (max) : (x)))
#endif
#ifndef max
#define max(a, b) (((a) < (b)) ?  (b) : (a))
#endif
#ifndef min
#define min(a, b) (((a) > (b)) ?  (b) : (a))
#endif

/****************************************************************************
 * sensor data type defines                                                 *
 ****************************************************************************/
typedef struct {
    td_u32      ver_lines;
    td_u32      max_ver_lines;
    td_float    max_fps;
    td_float    min_fps;
    td_u32      width;
    td_u32      height;
    td_u8       sns_mode;
    ot_wdr_mode wdr_mode;
    const char *mode_name;
} cs5186_video_mode_tbl;

typedef enum {
    CS5186_SENSOR_24M_60FPS_LINEAR_MODE = 0,
    CS5186_SENSOR_1K_60FPS_LINEAR_MODE,
    CS5186_SENSOR_2K_60FPS_LINEAR_MODE,
    CS5186_SENSOR_4K_60FPS_LINEAR_MODE,
    CS5186_SENSOR_1K_90FPS_DOL2_MODE,
    CS5186_SENSOR_1K_60FPS_WDR_MODE,
    CS5186_SENSOR_2K_90FPS_DOL2_MODE,
    CS5186_SENSOR_2K_60FPS_WDR_MODE,
    CS5186_SENSOR_4K_90FPS_DOL2_MODE,
    CS5186_SENSOR_4K_60FPS_WDR_MODE,
    CS5186_SENSOR_4K_60FPS_LINEAR_OB_MODE,
    CS5186_SENSOR_4K_15FPS_LINEAR_WDR_MODE,
    CS5186_SENSOR_640_480_15FPS_LINEAR_MODE,
    CS5186_MODE_BUTT
} cs5186_res_mode;

typedef enum {
    AE_INTTIME_IDX = 0,
    AE_AGAIN_DGAIN_IDX,
    AE_W_INTTIME_MAX_MIN_IDX,
    AE_W_SHORT_INTTIME_MAX_MIN_IDX,
    AE_W_AGAIN_MAX_MIN_IDX,
    AE_W_DGAIN_MAX_MIN_IDX,
    AE_INTTIME_SHORT_IDX,
    AE_AGAIN_DGAIN_SHORT_IDX,
    AE_E0_IDX,
    AE_E4_IDX,
    REG_MAX_IDX
} cs5186_reg_index;

ot_isp_sns_obj *cs5186_get_obj(td_void);

#ifdef __cplusplus
#if __cplusplus
}
#endif
#endif /* End of #ifdef __cplusplus */
#endif /* CS5186_CMOS_H */
