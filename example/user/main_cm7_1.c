/*********************************************************************************************************************
 * CYT4BB7 CM7_1 MT9V03X four-stage vision diagnostic build.
 *
 * S0 aligns to the box, S1 confirms line crossing, S2 waits for forward completion, and S3 protects spinning.
 * Detection only reads work_image; drawing only writes display_image.
 ********************************************************************************************************************/

#include "zf_common_headfile.h"

#define CAMERA_MINIMAL_RAW_TEST                  (1U)
#define CAMERA_DIAG_TEST_PATTERN                 (0U)

#define VISION_RAW_IMAGE_TEST                    (0U)
#define VISION_STATUS_BAR_ENABLE                 (1U)
#define VISION_OBJECT_OVERLAY_ENABLE             (0U)
#define ENABLE_FORCE_NEXT_KEY                    (0U)

#define BOX_WHITE_THRESHOLD                     (225U)
#define BOX_ROI_X_MIN                           (5)
#define BOX_ROI_X_MAX                           (MT9V03X_W - 6)
#define BOX_ROI_Y_MIN                           (12)
#define BOX_ROI_Y_MAX                           (MT9V03X_H - 8)
#define BOX_MIN_WIDTH                           (45U)
#define BOX_MIN_HEIGHT                          (25U)
#define BOX_MAX_WIDTH                           (MT9V03X_W - 10U)
#define BOX_MAX_HEIGHT                          (MT9V03X_H - 15U)
#define BOX_MIN_WHITE_PIXELS                    (250U)
#define BOX_ALIGN_ERROR_X_MAX                   (10)
#define BOX_ALIGN_STABLE_FRAMES                 (5U)

#define ENTER_WHITE_THRESHOLD                   (235U)
#define ENTER_LINE_X_MIN                        (5)
#define ENTER_LINE_X_MAX                        (MT9V03X_W - 6)
#define ENTER_LINE_Y_MIN                        (MT9V03X_H / 3)
#define ENTER_LINE_Y_MAX                        (MT9V03X_H - 5)
#define ENTER_LINE_MIN_WHITE_COUNT              (50U)
#define ENTER_LINE_MIN_WIDTH                    (45U)
#define ENTER_LINE_MIN_BAND_HEIGHT              (2U)
#define ENTER_LINE_MAX_BAND_HEIGHT              (35U)
#define FIRST_LINE_CROSS_Y                      (MT9V03X_H - 25)
#define FIRST_LINE_CROSS_STABLE_FRAMES          (3U)

#define NEAREST_LINE_PROTECT_DISTANCE           (45)
#define NEAREST_LINE_X_MIN                      (5)
#define NEAREST_LINE_X_MAX                      (MT9V03X_W - 6)
#define NEAREST_LINE_Y_MIN                      (MT9V03X_H - 1 - NEAREST_LINE_PROTECT_DISTANCE)
#define NEAREST_LINE_Y_MAX                      (MT9V03X_H - 3)
#define SPIN_WHITE_THRESHOLD                    (225U)
#define NEAREST_LINE_ROW_WHITE_MIN              (18U)
#define NEAREST_LINE_MIN_RUN                    (18U)
#define NEAREST_LINE_MIN_BAND_HEIGHT            (2U)
#define NEAREST_LINE_MAX_BAND_HEIGHT            (35U)
#define NEAREST_LINE_MIN_WIDTH                  (35U)
#define NEAREST_LINE_MIN_SCORE                  (80U)

#define SPIN_WARN_DISTANCE                      (35)
#define SPIN_SLOW_DISTANCE                      (22)
#define SPIN_STOP_DISTANCE                      (10)
#define SPIN_SPEED_NORMAL                       (100U)
#define SPIN_SPEED_WARN                         (70U)
#define SPIN_SPEED_SLOW                         (40U)
#define SPIN_SPEED_STOP                         (0U)

#define BOUNDARY_RELEASE_DISTANCE               (30)
#define BOUNDARY_RELEASE_STABLE_FRAMES          (3U)

#define VISION_KEY_DEBOUNCE_COUNT               (20U)
#define VISION_KEY_COUNT                        (4U)
#define VISION_KEY_POST_FORWARD_INDEX           (0U)
#define VISION_KEY_SPIN_DONE_INDEX              (1U)
#define VISION_KEY_FORCE_NEXT_INDEX             (2U)
#define VISION_KEY_RESET_INDEX                  (3U)

typedef enum
{
    VISION_STATE_BOX_ALIGN = 0,
    VISION_STATE_ENTER_LINE = 1,
    VISION_STATE_POST_LINE_FORWARD = 2,
    VISION_STATE_SPIN_PROTECT = 3
} vision_state_t;

typedef struct
{
    uint8 box_valid;
    int16 box_left;
    int16 box_right;
    int16 box_top;
    int16 box_bottom;
    int16 box_center_x;
    int16 box_center_y;
    int16 box_error_x;
    int16 box_error_y;
    uint16 box_width;
    uint16 box_height;
    uint16 box_score;
    uint8 box_align_ok;
    uint8 enter_line_enable;
} box_result_t;

typedef struct
{
    uint8 line1_valid;
    int16 line1_x_left;
    int16 line1_x_right;
    int16 line1_y_top;
    int16 line1_y_bottom;
    int16 line1_y_center;
    uint16 line1_width;
    uint16 line1_height;
    uint16 line1_score;
    uint8 first_line_crossed;
    uint8 enter_ready;
} enter_line_result_t;

typedef struct
{
    uint8 spin_line_valid;
    int16 spin_line_x_left;
    int16 spin_line_x_right;
    int16 spin_line_y_top;
    int16 spin_line_y_bottom;
    int16 spin_line_y_center;
    int16 spin_line_distance;
    uint8 spin_speed_limit;
    uint16 spin_line_score;
} spin_result_t;

static uint8 work_image[MT9V03X_H][MT9V03X_W];
static uint8 display_image[MT9V03X_H][MT9V03X_W];
static box_result_t box_result;
static enter_line_result_t enter_result;
static spin_result_t spin_result;

static uint8 s_box_align_stable_count = 0U;
static uint8 s_box_align_latched = 0U;
static uint8 s_first_line_cross_count = 0U;
static uint8 s_first_line_crossed_latched = 0U;
static uint8 s_boundary_release_count = 0U;

static const gpio_pin_enum s_vision_key_pins[VISION_KEY_COUNT] =
{
    P20_3, P20_2, P20_1, P20_0
};
static uint8 s_key_last_sample[VISION_KEY_COUNT] = {1U, 1U, 1U, 1U};
static uint8 s_key_stable_level[VISION_KEY_COUNT] = {1U, 1U, 1U, 1U};
static uint8 s_key_debounce_count[VISION_KEY_COUNT] = {0U, 0U, 0U, 0U};

volatile uint8 g_vision_state = VISION_STATE_BOX_ALIGN;
volatile uint16 g_mine_area_count = 0U;

volatile uint8 g_forward_enter_request = 0U;
volatile uint8 g_line1_crossed = 0U;
volatile uint8 g_post_line_forward_request = 0U;
volatile uint8 g_post_line_forward_done_input = 0U;
volatile uint8 g_spin_start_request = 0U;
volatile uint8 g_spin_done_input = 0U;
volatile uint8 g_boundary_stop_request = 0U;
volatile uint8 g_boundary_back_request = 0U;

volatile uint8 g_key_post_forward_done = 0U;
volatile uint8 g_key_spin_done = 0U;
volatile uint8 g_key_force_next_state = 0U;
volatile uint8 g_key_reset_to_s0 = 0U;

volatile uint8 g_box_valid = 0U;
volatile int16 g_box_center_x = -1;
volatile int16 g_box_center_y = -1;
volatile int16 g_box_error_x = 0;
volatile int16 g_box_error_y = 0;
volatile uint8 g_box_align_ok = 0U;
volatile uint8 g_enter_line_enable = 0U;

volatile uint8 g_line1_valid = 0U;
volatile int16 g_line1_y_bottom = -1;
volatile uint8 g_first_line_crossed = 0U;
volatile uint8 g_enter_ready = 0U;

volatile uint8 g_spin_line_valid = 0U;
volatile int16 g_spin_line_y_bottom = -1;
volatile int16 g_spin_line_distance = -1;
volatile uint8 g_spin_speed_limit = SPIN_SPEED_NORMAL;
volatile uint16 g_spin_line_score = 0U;

static void draw_safe_hline(uint8 *image, int16 y, int16 x0, int16 x1, uint8 color)
{
    int16 x;

    if((NULL == image) || (y < 0) || (y >= (int16)MT9V03X_H) || (x0 > x1))
    {
        return;
    }
    if((x1 < 0) || (x0 >= (int16)MT9V03X_W))
    {
        return;
    }
    if(x0 < 0)
    {
        x0 = 0;
    }
    if(x1 >= (int16)MT9V03X_W)
    {
        x1 = (int16)MT9V03X_W - 1;
    }

    for(x = x0; x <= x1; x++)
    {
        image[(uint32)y * MT9V03X_W + (uint32)x] = color;
    }
}

static void draw_safe_vline(uint8 *image, int16 x, int16 y0, int16 y1, uint8 color)
{
    int16 y;

    if((NULL == image) || (x < 0) || (x >= (int16)MT9V03X_W) || (y0 > y1))
    {
        return;
    }
    if((y1 < 0) || (y0 >= (int16)MT9V03X_H))
    {
        return;
    }
    if(y0 < 0)
    {
        y0 = 0;
    }
    if(y1 >= (int16)MT9V03X_H)
    {
        y1 = (int16)MT9V03X_H - 1;
    }

    for(y = y0; y <= y1; y++)
    {
        image[(uint32)y * MT9V03X_W + (uint32)x] = color;
    }
}

static void draw_safe_cross(uint8 *image, int16 x, int16 y, uint8 color)
{
    draw_safe_hline(image, y, (int16)(x - 3), (int16)(x + 3), color);
    draw_safe_vline(image, x, (int16)(y - 3), (int16)(y + 3), color);
}

static void draw_small_pixel(uint8 *image, int16 x, int16 y, uint8 color)
{
    if((NULL == image)
    || (x < 0) || (x >= (int16)MT9V03X_W)
    || (y < 0) || (y >= (int16)MT9V03X_H))
    {
        return;
    }

    image[(uint32)y * MT9V03X_W + (uint32)x] = color;
}

static void draw_small_char(uint8 *image, int16 x, int16 y, char ch, uint8 color)
{
    uint8 rows[5] = {0U, 0U, 0U, 0U, 0U};
    uint8 row;
    uint8 column;

    switch(ch)
    {
        case 'S': rows[0] = 7U; rows[1] = 4U; rows[2] = 7U; rows[3] = 1U; rows[4] = 7U; break;
        case 'X': rows[0] = 5U; rows[1] = 5U; rows[2] = 2U; rows[3] = 5U; rows[4] = 5U; break;
        case 'E': rows[0] = 7U; rows[1] = 4U; rows[2] = 6U; rows[3] = 4U; rows[4] = 7U; break;
        case 'N': rows[0] = 5U; rows[1] = 7U; rows[2] = 7U; rows[3] = 7U; rows[4] = 5U; break;
        case 'Y': rows[0] = 5U; rows[1] = 5U; rows[2] = 2U; rows[3] = 2U; rows[4] = 2U; break;
        case 'C': rows[0] = 7U; rows[1] = 4U; rows[2] = 4U; rows[3] = 4U; rows[4] = 7U; break;
        case 'R': rows[0] = 6U; rows[1] = 5U; rows[2] = 6U; rows[3] = 5U; rows[4] = 5U; break;
        case 'D': rows[0] = 6U; rows[1] = 5U; rows[2] = 5U; rows[3] = 5U; rows[4] = 6U; break;
        case 'P': rows[0] = 6U; rows[1] = 5U; rows[2] = 6U; rows[3] = 4U; rows[4] = 4U; break;
        case 'A': rows[0] = 2U; rows[1] = 5U; rows[2] = 7U; rows[3] = 5U; rows[4] = 5U; break;
        case 'B': rows[0] = 6U; rows[1] = 5U; rows[2] = 6U; rows[3] = 5U; rows[4] = 6U; break;
        case 'F': rows[0] = 7U; rows[1] = 4U; rows[2] = 6U; rows[3] = 4U; rows[4] = 4U; break;
        case 'I': rows[0] = 7U; rows[1] = 2U; rows[2] = 2U; rows[3] = 2U; rows[4] = 7U; break;
        case 'K': rows[0] = 5U; rows[1] = 5U; rows[2] = 6U; rows[3] = 5U; rows[4] = 5U; break;
        case 'M': rows[0] = 5U; rows[1] = 7U; rows[2] = 7U; rows[3] = 5U; rows[4] = 5U; break;
        case 'T': rows[0] = 7U; rows[1] = 2U; rows[2] = 2U; rows[3] = 2U; rows[4] = 2U; break;
        case 'W': rows[0] = 5U; rows[1] = 5U; rows[2] = 7U; rows[3] = 7U; rows[4] = 5U; break;
        case ' ': break;
        case '+': rows[0] = 0U; rows[1] = 2U; rows[2] = 7U; rows[3] = 2U; rows[4] = 0U; break;
        case '-': rows[0] = 0U; rows[1] = 0U; rows[2] = 7U; rows[3] = 0U; rows[4] = 0U; break;
        case '0': rows[0] = 7U; rows[1] = 5U; rows[2] = 5U; rows[3] = 5U; rows[4] = 7U; break;
        case '1': rows[0] = 2U; rows[1] = 6U; rows[2] = 2U; rows[3] = 2U; rows[4] = 7U; break;
        case '2': rows[0] = 7U; rows[1] = 1U; rows[2] = 7U; rows[3] = 4U; rows[4] = 7U; break;
        case '3': rows[0] = 7U; rows[1] = 1U; rows[2] = 7U; rows[3] = 1U; rows[4] = 7U; break;
        case '4': rows[0] = 5U; rows[1] = 5U; rows[2] = 7U; rows[3] = 1U; rows[4] = 1U; break;
        case '5': rows[0] = 7U; rows[1] = 4U; rows[2] = 7U; rows[3] = 1U; rows[4] = 7U; break;
        case '6': rows[0] = 7U; rows[1] = 4U; rows[2] = 7U; rows[3] = 5U; rows[4] = 7U; break;
        case '7': rows[0] = 7U; rows[1] = 1U; rows[2] = 2U; rows[3] = 2U; rows[4] = 2U; break;
        case '8': rows[0] = 7U; rows[1] = 5U; rows[2] = 7U; rows[3] = 5U; rows[4] = 7U; break;
        case '9': rows[0] = 7U; rows[1] = 5U; rows[2] = 7U; rows[3] = 1U; rows[4] = 7U; break;
        default: break;
    }

    for(row = 0U; row < 5U; row++)
    {
        for(column = 0U; column < 3U; column++)
        {
            if(rows[row] & (uint8)(1U << (2U - column)))
            {
                draw_small_pixel(image, (int16)(x + column), (int16)(y + row), color);
            }
        }
    }
}

static void draw_small_text(uint8 *image, int16 x, int16 y, const char *text, uint8 color)
{
    if((NULL == image) || (NULL == text))
    {
        return;
    }

    while((*text) && (x < (int16)MT9V03X_W))
    {
        draw_small_char(image, x, y, *text, color);
        x = (int16)(x + 4);
        text++;
    }
}

static int16 draw_small_int(uint8 *image, int16 x, int16 y, int16 value, uint8 color)
{
    int32 magnitude = value;
    uint16 divisor = 100U;

    if(magnitude < 0)
    {
        draw_small_char(image, x, y, '-', color);
        x = (int16)(x + 4);
        magnitude = -magnitude;
    }
    if(magnitude > 999)
    {
        magnitude = 999;
    }

    while(divisor > 0U)
    {
        draw_small_char(image, x, y, (char)('0' + (magnitude / divisor) % 10), color);
        x = (int16)(x + 4);
        divisor = (uint16)(divisor / 10U);
    }

    return x;
}

static void reset_box_stage(void)
{
    memset(&box_result, 0, sizeof(box_result));
    box_result.box_center_x = -1;
    box_result.box_center_y = -1;
    s_box_align_stable_count = 0U;
    s_box_align_latched = 0U;
}

static void reset_enter_line_stage(void)
{
    memset(&enter_result, 0, sizeof(enter_result));
    enter_result.line1_x_left = -1;
    enter_result.line1_x_right = -1;
    enter_result.line1_y_top = -1;
    enter_result.line1_y_bottom = -1;
    enter_result.line1_y_center = -1;
    s_first_line_cross_count = 0U;
    s_first_line_crossed_latched = 0U;
}

static void reset_spin_stage(void)
{
    memset(&spin_result, 0, sizeof(spin_result));
    spin_result.spin_line_x_left = -1;
    spin_result.spin_line_x_right = -1;
    spin_result.spin_line_y_top = -1;
    spin_result.spin_line_y_bottom = -1;
    spin_result.spin_line_y_center = -1;
    spin_result.spin_line_distance = -1;
    spin_result.spin_speed_limit = SPIN_SPEED_NORMAL;
    s_boundary_release_count = 0U;
    g_boundary_stop_request = 0U;
    g_boundary_back_request = 0U;
}

static void reset_control_requests_for_new_area(void)
{
    g_forward_enter_request = 0U;
    g_line1_crossed = 0U;
    g_post_line_forward_request = 0U;
    g_post_line_forward_done_input = 0U;
    g_spin_start_request = 0U;
    g_spin_done_input = 0U;
    g_boundary_stop_request = 0U;
    g_boundary_back_request = 0U;

    g_key_post_forward_done = 0U;
    g_key_spin_done = 0U;
    g_key_force_next_state = 0U;
    g_key_reset_to_s0 = 0U;
}

static void vision_reset_to_s0(void)
{
    reset_box_stage();
    reset_enter_line_stage();
    reset_spin_stage();
    reset_control_requests_for_new_area();
    g_vision_state = VISION_STATE_BOX_ALIGN;
}

static void vision_set_state(vision_state_t new_state)
{
    if((vision_state_t)g_vision_state == new_state)
    {
        return;
    }

    if(VISION_STATE_BOX_ALIGN == new_state)
    {
        vision_reset_to_s0();
        return;
    }

    reset_box_stage();
    reset_enter_line_stage();
    reset_spin_stage();
    g_vision_state = (uint8)new_state;
}

#if ENABLE_FORCE_NEXT_KEY
static void vision_force_next_state(void)
{
    switch((vision_state_t)g_vision_state)
    {
        case VISION_STATE_BOX_ALIGN:
            g_forward_enter_request = 1U;
            vision_set_state(VISION_STATE_ENTER_LINE);
            break;

        case VISION_STATE_ENTER_LINE:
            g_forward_enter_request = 0U;
            g_line1_crossed = 1U;
            g_post_line_forward_request = 1U;
            vision_set_state(VISION_STATE_POST_LINE_FORWARD);
            break;

        case VISION_STATE_POST_LINE_FORWARD:
            g_post_line_forward_request = 0U;
            g_spin_start_request = 1U;
            vision_set_state(VISION_STATE_SPIN_PROTECT);
            break;

        case VISION_STATE_SPIN_PROTECT:
            g_mine_area_count++;
            vision_set_state(VISION_STATE_BOX_ALIGN);
            break;

        default:
            vision_reset_to_s0();
            break;
    }
}
#endif

static void vision_key_init(void)
{
    uint8 index;

    for(index = 0U; index < VISION_KEY_COUNT; index++)
    {
        gpio_init(s_vision_key_pins[index], GPI, GPIO_HIGH, GPI_PULL_UP);
        s_key_last_sample[index] = gpio_get_level(s_vision_key_pins[index]);
        s_key_stable_level[index] = s_key_last_sample[index];
        s_key_debounce_count[index] = 0U;
    }
}

static void vision_key_update(void)
{
    uint8 index;
    uint8 pressed_edge[VISION_KEY_COUNT] = {0U, 0U, 0U, 0U};

    system_delay_ms(1U);

    for(index = 0U; index < VISION_KEY_COUNT; index++)
    {
        uint8 sample = gpio_get_level(s_vision_key_pins[index]);

        if(sample == s_key_last_sample[index])
        {
            if(s_key_debounce_count[index] < VISION_KEY_DEBOUNCE_COUNT)
            {
                s_key_debounce_count[index]++;
            }
        }
        else
        {
            s_key_last_sample[index] = sample;
            s_key_debounce_count[index] = 0U;
        }

        if((s_key_debounce_count[index] >= VISION_KEY_DEBOUNCE_COUNT)
        && (s_key_stable_level[index] != sample))
        {
            s_key_stable_level[index] = sample;
            s_key_debounce_count[index] = 0U;
            if(GPIO_LOW == sample)
            {
                pressed_edge[index] = 1U;
            }
        }
    }

    if(pressed_edge[VISION_KEY_POST_FORWARD_INDEX]
    && (VISION_STATE_POST_LINE_FORWARD == (vision_state_t)g_vision_state))
    {
        g_key_post_forward_done = 1U;
    }

    if(pressed_edge[VISION_KEY_SPIN_DONE_INDEX]
    && (VISION_STATE_SPIN_PROTECT == (vision_state_t)g_vision_state))
    {
        g_key_spin_done = 1U;
    }

#if ENABLE_FORCE_NEXT_KEY
    if(pressed_edge[VISION_KEY_FORCE_NEXT_INDEX])
    {
        g_key_force_next_state = 1U;
        vision_force_next_state();
        g_key_force_next_state = 0U;
    }
#endif

    if(pressed_edge[VISION_KEY_RESET_INDEX])
    {
        g_key_reset_to_s0 = 1U;
        vision_reset_to_s0();
    }
}

static void vision_clear_current_frame_result(void)
{
    switch((vision_state_t)g_vision_state)
    {
        case VISION_STATE_BOX_ALIGN:
            memset(&box_result, 0, sizeof(box_result));
            box_result.box_center_x = -1;
            box_result.box_center_y = -1;
            break;

        case VISION_STATE_ENTER_LINE:
            memset(&enter_result, 0, sizeof(enter_result));
            enter_result.line1_x_left = -1;
            enter_result.line1_x_right = -1;
            enter_result.line1_y_top = -1;
            enter_result.line1_y_bottom = -1;
            enter_result.line1_y_center = -1;
            break;

        case VISION_STATE_POST_LINE_FORWARD:
            break;

        case VISION_STATE_SPIN_PROTECT:
            memset(&spin_result, 0, sizeof(spin_result));
            spin_result.spin_line_x_left = -1;
            spin_result.spin_line_x_right = -1;
            spin_result.spin_line_y_top = -1;
            spin_result.spin_line_y_bottom = -1;
            spin_result.spin_line_y_center = -1;
            spin_result.spin_line_distance = -1;
            spin_result.spin_speed_limit = SPIN_SPEED_NORMAL;
            break;

        default:
            break;
    }
}

static uint8 detect_box_center(const uint8 *image, box_result_t *result)
{
    int16 x;
    int16 y;
    int16 left = (int16)MT9V03X_W;
    int16 right = 0;
    int16 top = (int16)MT9V03X_H;
    int16 bottom = 0;
    uint32 white_pixels = 0U;
    uint16 width;
    uint16 height;

    if((NULL == image) || (NULL == result))
    {
        return 0U;
    }

    for(y = (int16)BOX_ROI_Y_MIN; y <= (int16)BOX_ROI_Y_MAX; y++)
    {
        for(x = (int16)BOX_ROI_X_MIN; x <= (int16)BOX_ROI_X_MAX; x++)
        {
            if(image[(uint32)y * MT9V03X_W + (uint32)x] >= BOX_WHITE_THRESHOLD)
            {
                white_pixels++;
                if(x < left) { left = x; }
                if(x > right) { right = x; }
                if(y < top) { top = y; }
                if(y > bottom) { bottom = y; }
            }
        }
    }

    if((white_pixels < BOX_MIN_WHITE_PIXELS) || (left >= right) || (top >= bottom))
    {
        return 0U;
    }

    width = (uint16)(right - left + 1);
    height = (uint16)(bottom - top + 1);
    if((width < BOX_MIN_WIDTH)
    || (height < BOX_MIN_HEIGHT)
    || (width > BOX_MAX_WIDTH)
    || (height > BOX_MAX_HEIGHT)
    || ((uint32)width > (uint32)height * 6U)
    || ((uint32)height > (uint32)width * 3U))
    {
        return 0U;
    }

    result->box_valid = 1U;
    result->box_left = left;
    result->box_right = right;
    result->box_top = top;
    result->box_bottom = bottom;
    result->box_center_x = (int16)((left + right) / 2);
    result->box_center_y = (int16)((top + bottom) / 2);
    result->box_error_x = (int16)(result->box_center_x - (int16)(MT9V03X_W / 2U));
    result->box_error_y = (int16)(result->box_center_y - (int16)(MT9V03X_H / 2U));
    result->box_width = width;
    result->box_height = height;
    result->box_score = (white_pixels > 65535U) ? 65535U : (uint16)white_pixels;
    return 1U;
}

static void update_box_align_state(box_result_t *result)
{
    int16 absolute_error_x;

    if(NULL == result)
    {
        return;
    }

    if(s_box_align_latched)
    {
        result->box_align_ok = 1U;
        result->enter_line_enable = 1U;
        return;
    }

    absolute_error_x = (result->box_error_x < 0)
        ? (int16)(-result->box_error_x) : result->box_error_x;
    if(result->box_valid && (absolute_error_x <= BOX_ALIGN_ERROR_X_MAX))
    {
        if(s_box_align_stable_count < BOX_ALIGN_STABLE_FRAMES)
        {
            s_box_align_stable_count++;
        }
    }
    else
    {
        s_box_align_stable_count = 0U;
    }

    if(s_box_align_stable_count >= BOX_ALIGN_STABLE_FRAMES)
    {
        s_box_align_latched = 1U;
    }

    result->box_align_ok = s_box_align_latched;
    result->enter_line_enable = s_box_align_latched;
}

static void draw_box_overlay(uint8 *image, const box_result_t *result)
{
    if((NULL == image) || (NULL == result)
    || (VISION_STATE_BOX_ALIGN != (vision_state_t)g_vision_state)
    || (0U == result->box_valid))
    {
        return;
    }

    draw_safe_hline(image, result->box_top, result->box_left, result->box_right, 0U);
    draw_safe_hline(image, result->box_bottom, result->box_left, result->box_right, 0U);
    draw_safe_vline(image, result->box_left, result->box_top, result->box_bottom, 0U);
    draw_safe_vline(image, result->box_right, result->box_top, result->box_bottom, 0U);
    draw_safe_cross(image, result->box_center_x, result->box_center_y, 0U);
}

static void run_box_align_stage(void)
{
    detect_box_center(work_image[0], &box_result);
    update_box_align_state(&box_result);
    if(VISION_OBJECT_OVERLAY_ENABLE)
    {
        draw_box_overlay(display_image[0], &box_result);
    }

    if(box_result.enter_line_enable)
    {
        g_forward_enter_request = 1U;
        vision_set_state(VISION_STATE_ENTER_LINE);
    }
}

static uint8 detect_enter_line1_segment(const uint8 *image, enter_line_result_t *result)
{
    int16 x;
    int16 y;
    uint8 band_active = 0U;
    int16 band_top = -1;
    int16 band_bottom = -1;
    int16 band_left = (int16)MT9V03X_W;
    int16 band_right = -1;
    uint32 band_score = 0U;

    if((NULL == image) || (NULL == result))
    {
        return 0U;
    }

    for(y = (int16)ENTER_LINE_Y_MAX; y >= ((int16)ENTER_LINE_Y_MIN - 1); y--)
    {
        uint16 white_count = 0U;
        uint16 current_run = 0U;
        uint16 longest_run = 0U;
        int16 first_white = -1;
        int16 last_white = -1;
        uint8 row_candidate = 0U;

        if(y >= (int16)ENTER_LINE_Y_MIN)
        {
            for(x = (int16)ENTER_LINE_X_MIN; x <= (int16)ENTER_LINE_X_MAX; x++)
            {
                if(image[(uint32)y * MT9V03X_W + (uint32)x] >= ENTER_WHITE_THRESHOLD)
                {
                    if(first_white < 0) { first_white = x; }
                    last_white = x;
                    white_count++;
                    current_run++;
                    if(current_run > longest_run) { longest_run = current_run; }
                }
                else
                {
                    current_run = 0U;
                }
            }

            if((white_count >= ENTER_LINE_MIN_WHITE_COUNT)
            && (longest_run >= ENTER_LINE_MIN_WIDTH))
            {
                row_candidate = 1U;
            }
        }

        if(row_candidate)
        {
            if(0U == band_active)
            {
                band_active = 1U;
                band_bottom = y;
                band_left = first_white;
                band_right = last_white;
                band_score = 0U;
            }

            band_top = y;
            if(first_white < band_left) { band_left = first_white; }
            if(last_white > band_right) { band_right = last_white; }
            band_score += white_count;
        }
        else if(band_active)
        {
            uint16 width = (uint16)(band_right - band_left + 1);
            uint16 height = (uint16)(band_bottom - band_top + 1);

            if((band_left < band_right)
            && (width >= ENTER_LINE_MIN_WIDTH)
            && (height >= ENTER_LINE_MIN_BAND_HEIGHT)
            && (height <= ENTER_LINE_MAX_BAND_HEIGHT))
            {
                result->line1_valid = 1U;
                result->line1_x_left = band_left;
                result->line1_x_right = band_right;
                result->line1_y_top = band_top;
                result->line1_y_bottom = band_bottom;
                result->line1_y_center = (int16)((band_top + band_bottom) / 2);
                result->line1_width = width;
                result->line1_height = height;
                result->line1_score = (band_score > 65535U) ? 65535U : (uint16)band_score;
                return 1U;
            }

            band_active = 0U;
            band_top = -1;
            band_bottom = -1;
            band_left = (int16)MT9V03X_W;
            band_right = -1;
            band_score = 0U;
        }
    }

    return 0U;
}

static void update_first_line_cross_state(enter_line_result_t *result)
{
    if(NULL == result)
    {
        return;
    }

    if(s_first_line_crossed_latched)
    {
        result->first_line_crossed = 1U;
        result->enter_ready = 1U;
        return;
    }

    if(result->line1_valid && (result->line1_y_bottom >= (int16)FIRST_LINE_CROSS_Y))
    {
        if(s_first_line_cross_count < FIRST_LINE_CROSS_STABLE_FRAMES)
        {
            s_first_line_cross_count++;
        }
    }
    else
    {
        s_first_line_cross_count = 0U;
    }

    if(s_first_line_cross_count >= FIRST_LINE_CROSS_STABLE_FRAMES)
    {
        s_first_line_crossed_latched = 1U;
    }

    result->first_line_crossed = s_first_line_crossed_latched;
    result->enter_ready = s_first_line_crossed_latched;
}

static void draw_enter_line_overlay(uint8 *image, const enter_line_result_t *result)
{
    if((NULL == image) || (NULL == result)
    || (VISION_STATE_ENTER_LINE != (vision_state_t)g_vision_state)
    || (0U == result->line1_valid))
    {
        return;
    }

    draw_safe_hline(image, result->line1_y_top,
        result->line1_x_left, result->line1_x_right, 80U);
    draw_safe_hline(image, result->line1_y_bottom,
        result->line1_x_left, result->line1_x_right, 80U);
    draw_safe_hline(image, result->line1_y_center,
        result->line1_x_left, result->line1_x_right, 0U);
}

static void run_enter_line_stage(void)
{
    detect_enter_line1_segment(work_image[0], &enter_result);
    update_first_line_cross_state(&enter_result);
    if(VISION_OBJECT_OVERLAY_ENABLE)
    {
        draw_enter_line_overlay(display_image[0], &enter_result);
    }

    if(enter_result.enter_ready)
    {
        g_forward_enter_request = 0U;
        g_line1_crossed = 1U;
        g_post_line_forward_request = 1U;
        vision_set_state(VISION_STATE_POST_LINE_FORWARD);
    }
}

static void run_post_line_forward_stage(void)
{
    if(g_post_line_forward_done_input || g_key_post_forward_done)
    {
        g_post_line_forward_done_input = 0U;
        g_key_post_forward_done = 0U;
        g_post_line_forward_request = 0U;
        g_spin_start_request = 1U;
        vision_set_state(VISION_STATE_SPIN_PROTECT);
    }
}

static void detect_nearest_boundary_line(const uint8 *image, spin_result_t *result)
{
    int16 x;
    int16 y;
    uint8 band_active = 0U;
    int16 band_top = -1;
    int16 band_bottom = -1;
    int16 band_left = (int16)MT9V03X_W;
    int16 band_right = -1;
    uint32 band_score = 0U;

    if((NULL == image) || (NULL == result))
    {
        return;
    }

    for(y = (int16)NEAREST_LINE_Y_MAX; y >= ((int16)NEAREST_LINE_Y_MIN - 1); y--)
    {
        uint16 white_count = 0U;
        uint16 current_run = 0U;
        uint16 longest_run = 0U;
        int16 first_white = -1;
        int16 last_white = -1;
        uint8 row_candidate = 0U;

        if(y >= (int16)NEAREST_LINE_Y_MIN)
        {
            for(x = (int16)NEAREST_LINE_X_MIN; x <= (int16)NEAREST_LINE_X_MAX; x++)
            {
                if(image[(uint32)y * MT9V03X_W + (uint32)x] >= SPIN_WHITE_THRESHOLD)
                {
                    if(first_white < 0) { first_white = x; }
                    last_white = x;
                    white_count++;
                    current_run++;
                    if(current_run > longest_run) { longest_run = current_run; }
                }
                else
                {
                    current_run = 0U;
                }
            }

            if((white_count >= NEAREST_LINE_ROW_WHITE_MIN)
            || (longest_run >= NEAREST_LINE_MIN_RUN))
            {
                row_candidate = 1U;
            }
        }

        if(row_candidate)
        {
            if(0U == band_active)
            {
                band_active = 1U;
                band_bottom = y;
                band_left = first_white;
                band_right = last_white;
                band_score = 0U;
            }

            band_top = y;
            if(first_white < band_left) { band_left = first_white; }
            if(last_white > band_right) { band_right = last_white; }
            band_score += white_count;
        }
        else if(band_active)
        {
            uint16 width = (uint16)(band_right - band_left + 1);
            uint16 height = (uint16)(band_bottom - band_top + 1);

            if((band_left < band_right)
            && (width >= NEAREST_LINE_MIN_WIDTH)
            && (height >= NEAREST_LINE_MIN_BAND_HEIGHT)
            && (height <= NEAREST_LINE_MAX_BAND_HEIGHT)
            && (band_score >= NEAREST_LINE_MIN_SCORE))
            {
                result->spin_line_valid = 1U;
                result->spin_line_x_left = band_left;
                result->spin_line_x_right = band_right;
                result->spin_line_y_top = band_top;
                result->spin_line_y_bottom = band_bottom;
                result->spin_line_y_center = (int16)((band_top + band_bottom) / 2);
                result->spin_line_distance = (int16)((MT9V03X_H - 1) - band_bottom);
                result->spin_line_score = (band_score > 65535U) ? 65535U : (uint16)band_score;
                return;
            }

            band_active = 0U;
            band_top = -1;
            band_bottom = -1;
            band_left = (int16)MT9V03X_W;
            band_right = -1;
            band_score = 0U;
        }
    }
}

static void update_spin_speed_limit(spin_result_t *result)
{
    if(NULL == result)
    {
        return;
    }

    result->spin_speed_limit = SPIN_SPEED_NORMAL;
    if(result->spin_line_valid)
    {
        if(result->spin_line_distance <= SPIN_STOP_DISTANCE)
        {
            result->spin_speed_limit = SPIN_SPEED_STOP;
        }
        else if(result->spin_line_distance <= SPIN_SLOW_DISTANCE)
        {
            result->spin_speed_limit = SPIN_SPEED_SLOW;
        }
        else if(result->spin_line_distance <= SPIN_WARN_DISTANCE)
        {
            result->spin_speed_limit = SPIN_SPEED_WARN;
        }
    }
}

static void update_boundary_requests(spin_result_t *result)
{
    if(NULL == result)
    {
        return;
    }

    if(result->spin_line_valid && (result->spin_line_distance <= SPIN_STOP_DISTANCE))
    {
        g_boundary_stop_request = 1U;
        g_boundary_back_request = 1U;
        s_boundary_release_count = 0U;
    }
    else if((0U == result->spin_line_valid)
        || (result->spin_line_distance >= BOUNDARY_RELEASE_DISTANCE))
    {
        if(s_boundary_release_count < BOUNDARY_RELEASE_STABLE_FRAMES)
        {
            s_boundary_release_count++;
        }
        if(s_boundary_release_count >= BOUNDARY_RELEASE_STABLE_FRAMES)
        {
            g_boundary_stop_request = 0U;
            g_boundary_back_request = 0U;
            result->spin_speed_limit = SPIN_SPEED_NORMAL;
        }
    }
    else
    {
        s_boundary_release_count = 0U;
    }

    if(g_boundary_stop_request || g_boundary_back_request)
    {
        result->spin_speed_limit = SPIN_SPEED_STOP;
    }
}

static void draw_nearest_line_overlay(uint8 *image, const spin_result_t *result)
{
    if((NULL == image) || (NULL == result)
    || (VISION_STATE_SPIN_PROTECT != (vision_state_t)g_vision_state)
    || (0U == result->spin_line_valid))
    {
        return;
    }

    draw_safe_hline(image, result->spin_line_y_top,
        result->spin_line_x_left, result->spin_line_x_right, 80U);
    draw_safe_hline(image, result->spin_line_y_bottom,
        result->spin_line_x_left, result->spin_line_x_right, 80U);
    draw_safe_hline(image, result->spin_line_y_center,
        result->spin_line_x_left, result->spin_line_x_right, 0U);
}

static void run_spin_protect_stage(void)
{
    detect_nearest_boundary_line(work_image[0], &spin_result);
    update_spin_speed_limit(&spin_result);
    update_boundary_requests(&spin_result);
    if(VISION_OBJECT_OVERLAY_ENABLE)
    {
        draw_nearest_line_overlay(display_image[0], &spin_result);
    }

    if(g_spin_done_input || g_key_spin_done)
    {
        g_spin_done_input = 0U;
        g_key_spin_done = 0U;
        g_spin_start_request = 0U;
        g_boundary_stop_request = 0U;
        g_boundary_back_request = 0U;
        g_mine_area_count++;
        vision_set_state(VISION_STATE_BOX_ALIGN);
    }
}

static void draw_status_overlay(uint8 *image)
{
    int16 x = 2;

    if(NULL == image)
    {
        return;
    }

    switch((vision_state_t)g_vision_state)
    {
        case VISION_STATE_BOX_ALIGN:
            if(box_result.box_valid)
            {
                draw_small_text(image, x, 2, "S0 X", 0U);
                x = draw_small_int(image, 18, 2, box_result.box_center_x, 0U);
                draw_small_text(image, x, 2, " EX", 0U);
                x = (int16)(x + 12);
                if(box_result.box_error_x >= 0)
                {
                    draw_small_char(image, x, 2, '+', 0U);
                    x = (int16)(x + 4);
                }
                x = draw_small_int(image, x, 2, box_result.box_error_x, 0U);
                draw_small_text(image, x, 2, " EN", 0U);
                draw_small_char(image, (int16)(x + 12), 2,
                    box_result.enter_line_enable ? '1' : '0', 0U);
            }
            else
            {
                draw_small_text(image, x, 2, "S0 X--- EX--- EN0", 0U);
            }
            break;

        case VISION_STATE_ENTER_LINE:
            draw_small_text(image, x, 2, "S1 Y", 0U);
            if(enter_result.line1_valid)
            {
                x = draw_small_int(image, 18, 2, enter_result.line1_y_bottom, 0U);
            }
            else
            {
                draw_small_text(image, 18, 2, "---", 0U);
                x = 30;
            }
            draw_small_text(image, x, 2, " C", 0U);
            draw_small_char(image, (int16)(x + 8), 2,
                enter_result.first_line_crossed ? '1' : '0', 0U);
            x = (int16)(x + 12);
            draw_small_text(image, x, 2, " R", 0U);
            draw_small_char(image, (int16)(x + 8), 2,
                enter_result.enter_ready ? '1' : '0', 0U);
            break;

        case VISION_STATE_POST_LINE_FORWARD:
            draw_small_text(image, x, 2, "S2 WAIT FWD K", 0U);
            draw_small_char(image, 54, 2,
                g_key_post_forward_done ? '1' : '0', 0U);
            draw_small_text(image, 58, 2, " M", 0U);
            draw_small_char(image, 66, 2,
                g_post_line_forward_done_input ? '1' : '0', 0U);
            break;

        case VISION_STATE_SPIN_PROTECT:
            if(g_boundary_back_request)
            {
                draw_small_text(image, x, 2, "S3 BACK D", 0U);
                if(spin_result.spin_line_valid)
                {
                    x = draw_small_int(image, 38, 2,
                        spin_result.spin_line_distance, 0U);
                }
                else
                {
                    draw_small_text(image, 38, 2, "---", 0U);
                    x = 50;
                }
                draw_small_text(image, x, 2, " SPD", 0U);
                draw_small_int(image, (int16)(x + 16), 2,
                    spin_result.spin_speed_limit, 0U);
            }
            else
            {
                draw_small_text(image, x, 2, "S3 Y", 0U);
                if(spin_result.spin_line_valid)
                {
                    x = draw_small_int(image, 18, 2,
                        spin_result.spin_line_y_bottom, 0U);
                    draw_small_text(image, x, 2, " D", 0U);
                    x = draw_small_int(image, (int16)(x + 8), 2,
                        spin_result.spin_line_distance, 0U);
                }
                else
                {
                    draw_small_text(image, 18, 2, "--- D---", 0U);
                    x = 50;
                }
                draw_small_text(image, x, 2, " SPD", 0U);
                draw_small_int(image, (int16)(x + 16), 2,
                    spin_result.spin_speed_limit, 0U);
            }
            break;

        default:
            break;
    }
}

static void update_global_debug_variables(void)
{
    g_box_valid = 0U;
    g_box_center_x = -1;
    g_box_center_y = -1;
    g_box_error_x = 0;
    g_box_error_y = 0;
    g_box_align_ok = 0U;
    g_enter_line_enable = 0U;

    g_line1_valid = 0U;
    g_line1_y_bottom = -1;
    g_first_line_crossed = 0U;
    g_enter_ready = 0U;

    g_spin_line_valid = 0U;
    g_spin_line_y_bottom = -1;
    g_spin_line_distance = -1;
    g_spin_speed_limit = SPIN_SPEED_NORMAL;
    g_spin_line_score = 0U;

    switch((vision_state_t)g_vision_state)
    {
        case VISION_STATE_BOX_ALIGN:
            g_box_valid = box_result.box_valid;
            g_box_center_x = box_result.box_center_x;
            g_box_center_y = box_result.box_center_y;
            g_box_error_x = box_result.box_error_x;
            g_box_error_y = box_result.box_error_y;
            g_box_align_ok = box_result.box_align_ok;
            g_enter_line_enable = box_result.enter_line_enable;
            break;

        case VISION_STATE_ENTER_LINE:
            g_line1_valid = enter_result.line1_valid;
            g_line1_y_bottom = enter_result.line1_y_bottom;
            g_first_line_crossed = enter_result.first_line_crossed;
            g_enter_ready = enter_result.enter_ready;
            break;

        case VISION_STATE_POST_LINE_FORWARD:
            break;

        case VISION_STATE_SPIN_PROTECT:
            g_spin_line_valid = spin_result.spin_line_valid;
            g_spin_line_y_bottom = spin_result.spin_line_y_bottom;
            g_spin_line_distance = spin_result.spin_line_distance;
            g_spin_speed_limit = spin_result.spin_speed_limit;
            g_spin_line_score = spin_result.spin_line_score;
            break;

        default:
            break;
    }
}

static void vision_process_one_frame(void)
{
    memcpy(work_image[0], mt9v03x_image[0], MT9V03X_IMAGE_SIZE);
    memcpy(display_image[0], mt9v03x_image[0], MT9V03X_IMAGE_SIZE);

    vision_clear_current_frame_result();

    switch((vision_state_t)g_vision_state)
    {
        case VISION_STATE_BOX_ALIGN:
            run_box_align_stage();
            break;

        case VISION_STATE_ENTER_LINE:
            run_enter_line_stage();
            break;

        case VISION_STATE_POST_LINE_FORWARD:
            run_post_line_forward_stage();
            break;

        case VISION_STATE_SPIN_PROTECT:
            run_spin_protect_stage();
            break;

        default:
            vision_set_state(VISION_STATE_BOX_ALIGN);
            break;
    }

    update_global_debug_variables();
    if(VISION_STATUS_BAR_ENABLE)
    {
        draw_status_overlay(display_image[0]);
    }
}

int main(void)
{
    clock_init(SYSTEM_CLOCK_250M);
    debug_init();

    seekfree_assistant_interface_init(SEEKFREE_ASSISTANT_DEBUG_UART);
    seekfree_assistant_camera_information_config(
        SEEKFREE_ASSISTANT_MT9V03X,
        display_image[0],
        MT9V03X_W,
        MT9V03X_H);

#if !CAMERA_MINIMAL_RAW_TEST
    vision_reset_to_s0();
    vision_key_init();
#endif

    while(mt9v03x_init())
    {
        system_delay_ms(500U);
    }

    while(true)
    {
#if !CAMERA_MINIMAL_RAW_TEST
        vision_key_update();
#endif

        if(mt9v03x_finish_flag)
        {
            mt9v03x_finish_flag = 0U;
#if CAMERA_MINIMAL_RAW_TEST
    #if CAMERA_DIAG_TEST_PATTERN
            uint16 x;
            uint16 y;

            for(y = 0U; y < MT9V03X_H; y++)
            {
                for(x = 0U; x < MT9V03X_W; x++)
                {
                    display_image[y][x] = (uint8)((uint32)x * 255U / (MT9V03X_W - 1U));
                }
            }
    #else
            memcpy(display_image[0], mt9v03x_image[0], MT9V03X_IMAGE_SIZE);
    #endif
#else
#if VISION_RAW_IMAGE_TEST
            memcpy(display_image[0], mt9v03x_image[0], MT9V03X_IMAGE_SIZE);
#else
            vision_process_one_frame();
#endif
#endif
            seekfree_assistant_camera_send();
        }
    }
}
