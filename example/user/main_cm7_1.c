/*********************************************************************************************************************
* CYT4BB CM7_1 MT9V03X automatic wireless live image export + white hollow-square recognition V4.5
*
* CM7_1 functions:
*   - Continuously acquire MT9V03X gray frames.
*   - P20.3 / KEY_1: capture the next complete frame and save it to Work Flash.
*   - P20.2 / KEY_2: select the next saved frame.
*   - P20.1 / KEY_3: switch RAW / BINARY export mode.
*   - Automatic mode: after boot, continuously export the latest live frame through wireless UART.
*   - P20.0 / KEY_4: manually export one live frame immediately when automatic mode is not desired.
*
* Storage:
*   - Seven raw 188x120 frames are stored persistently in Work Flash.
*   - When all seven slots are used, the next capture overwrites the oldest physical slot.
*   - Power loss does not erase the saved frames.
*
* Important:
*   - This standalone calibration project uses Work Flash pages 0..84.
*   - Do not combine it with another application that also uses these Work Flash pages.
********************************************************************************************************************/

#include "zf_common_headfile.h"

#define CAMERA_TEST_LED                 (P19_0)
#define KEY_SCAN_PERIOD_MS              (10U)
#define FIXED_BINARY_THRESHOLD          (128U)

#define DISPLAY_MODE_RAW                (0U)
#define DISPLAY_MODE_BINARY             (1U)

/* V4.5 wireless serial module output.
 * The CYT4Bx V1.2.1 mainboard wireless-UART connector uses UART1:
 *   module TX -> MCU RX P4.0, module RX -> MCU TX P4.1, RTS -> P22.6.
 * Seekfree Assistant image packets are sent through wireless_uart_send_buffer().
 */
#define ASSISTANT_USE_WIRELESS_UART     (0U)

/* V4.5 automatic wireless image sending.
 * 1: after camera init, every new complete frame will be recognized and sent automatically.
 * 0: keep V4.4 behavior, press KEY_4 to send one live frame.
 *
 * Note: 188x120 gray image is about 22KB. At 115200 bps, real display rate is only
 * about 0.3~0.6 FPS. This is normal and is limited by wireless UART bandwidth.
 */
#define WIRELESS_AUTO_LIVE_SEND         (0U)

#define DEBUG_SIMPLE_FRAME_DUMP         (0U)
#define DEBUG_SIMPLE_FRAME_DUMP_INTERVAL (1U)

/* White hollow-square recognition parameters.
 * These are deliberately kept as macros for quick tuning after real tests.
 */
#define RECOG_ROI_X_MIN                 (3U)
#define RECOG_ROI_X_MAX                 (MT9V03X_W - 5U)
#define RECOG_ROI_Y_MIN                 (25U)
#define RECOG_ROI_Y_MAX                 (MT9V03X_H - 3U)
/* V4.2: real tests showed that the black rectangle drawn in V4.1 was only the ROI,
 * not the final target. This version raises the white-tape threshold and searches
 * the actual white tape edges, so the output center is based on the detected frame.
 */
#define RECOG_OTSU_OFFSET               (20U)
#define RECOG_THRESHOLD_MIN             (160U)
#define RECOG_THRESHOLD_MAX             (245U)
#define RECOG_ROW_MIN_COUNT             (24U)
#define RECOG_COL_MIN_COUNT             (12U)
#define RECOG_MIN_WIDTH                 (35U)
#define RECOG_MIN_HEIGHT                (20U)
#define RECOG_MAX_BANDS                 (12U)
#define RECOG_MIN_BORDER_RATIO          (55U)
#define RECOG_MAX_INNER_RATIO           (45U)
#define RECOG_DEBUG_BAND_ROW_COUNT      (36U)
#define RECOG_DEBUG_BAND_COL_COUNT      (18U)
#define RECOG_FALLBACK_MIN_WHITE        (150U)
#define RECOG_FALLBACK_MIN_WIDTH        (25U)
#define RECOG_FALLBACK_MIN_HEIGHT       (12U)
#define RECOG_FALLBACK_MAX_ROI_RATIO    (90U)

#define STORAGE_MAGIC                   (0x43414D33UL)     /* "CAM3" */
#define STORAGE_VERSION                 (0x00010000UL)
#define STORAGE_MAX_IMAGES              (7U)
#define STORAGE_META_PAGE               (0U)
#define STORAGE_IMAGE_START_PAGE        (1U)
#define STORAGE_PAGES_PER_IMAGE         ((MT9V03X_IMAGE_SIZE + FLASH_PAGE_SIZE - 1U) / FLASH_PAGE_SIZE)

typedef struct
{
    uint32 sequence;
    uint32 min_gray;
    uint32 max_gray;
    uint32 mean_gray;
    uint32 otsu_threshold;
    uint32 image_checksum;
} image_record_t;

typedef struct
{
    uint32 magic;
    uint32 version;
    uint32 width;
    uint32 height;
    uint32 valid_mask;
    uint32 next_slot;
    uint32 capture_total;
    uint32 selected_slot;
    image_record_t record[STORAGE_MAX_IMAGES];
} storage_header_t;

typedef struct
{
    uint16 start;
    uint16 end;
    uint16 center;
    uint16 size;
    uint16 peak_count;
} recog_band_t;

typedef struct
{
    uint8 valid;
    uint8 threshold;
    uint8 confidence;
    int16 center_x;
    int16 center_y;
    int16 error_x;
    int16 error_y;
    uint16 width;
    uint16 height;
    uint16 left;
    uint16 right;
    uint16 top;
    uint16 bottom;
    uint16 border_ratio;
    uint16 inner_ratio;
} mine_target_t;

static uint8 snapshot_image[MT9V03X_H][MT9V03X_W];
static uint8 display_image[MT9V03X_H][MT9V03X_W];
static storage_header_t storage_header;
static uint8 display_mode = DISPLAY_MODE_RAW;
static mine_target_t current_target;
static image_record_t live_record;
static uint16 recog_row_count[MT9V03X_H];
static uint16 recog_col_count[MT9V03X_W];
static uint8 recog_component_visited[MT9V03X_H][MT9V03X_W];
static uint16 recog_component_stack[MT9V03X_IMAGE_SIZE];

static void assistant_text_send(const uint8 *buff, uint32 len)
{
#if ASSISTANT_USE_WIRELESS_UART
    wireless_uart_send_buffer(buff, len);
#else
    debug_send_buffer(buff, len);
#endif
}

#if DEBUG_SIMPLE_FRAME_DUMP
static void debug_simple_write_u16(uint8 *buffer, uint32 *index, uint16 value)
{
    buffer[(*index)++] = (uint8)(value & 0xFFU);
    buffer[(*index)++] = (uint8)((value >> 8) & 0xFFU);
}

static void debug_simple_write_s16(uint8 *buffer, uint32 *index, int16 value)
{
    debug_simple_write_u16(buffer, index, (uint16)value);
}

static void debug_simple_write_u32(uint8 *buffer, uint32 *index, uint32 value)
{
    buffer[(*index)++] = (uint8)(value & 0xFFU);
    buffer[(*index)++] = (uint8)((value >> 8) & 0xFFU);
    buffer[(*index)++] = (uint8)((value >> 16) & 0xFFU);
    buffer[(*index)++] = (uint8)((value >> 24) & 0xFFU);
}

static void debug_simple_frame_dump_send(void)
{
    static uint32 frame_id = 0U;
    static uint8 frame_divider = 0U;
    uint8 header[24];
    uint32 index = 0U;
    const uint8 tail[2] = {0x0D, 0x0A};

    frame_divider++;
    if(frame_divider < DEBUG_SIMPLE_FRAME_DUMP_INTERVAL)
    {
        return;
    }
    frame_divider = 0U;
    frame_id++;

    header[index++] = 'I';
    header[index++] = 'M';
    header[index++] = 'G';
    header[index++] = '0';
    debug_simple_write_u16(header, &index, (uint16)MT9V03X_W);
    debug_simple_write_u16(header, &index, (uint16)MT9V03X_H);
    debug_simple_write_u32(header, &index, frame_id);
    header[index++] = current_target.valid;
    debug_simple_write_s16(header, &index, current_target.center_x);
    debug_simple_write_s16(header, &index, current_target.center_y);
    debug_simple_write_s16(header, &index, current_target.error_x);
    debug_simple_write_u16(header, &index, current_target.width);
    debug_simple_write_u16(header, &index, current_target.height);
    header[index++] = current_target.threshold;

    debug_send_buffer(header, index);
    debug_send_buffer(display_image[0], MT9V03X_IMAGE_SIZE);
    debug_send_buffer(tail, 2U);
}
#endif

static void status_led_blink(uint8 count)
{
    uint8 i;

    for(i = 0U; i < count; i++)
    {
        gpio_set_level(CAMERA_TEST_LED, GPIO_LOW);
        system_delay_ms(100U);
        gpio_set_level(CAMERA_TEST_LED, GPIO_HIGH);
        system_delay_ms(150U);
    }
}

static uint8 count_saved_images(void)
{
    uint8 i;
    uint8 count = 0U;

    for(i = 0U; i < STORAGE_MAX_IMAGES; i++)
    {
        if(0U != (storage_header.valid_mask & (1UL << i)))
        {
            count++;
        }
    }

    return count;
}

static uint32 image_checksum_calculate(const uint8 image[MT9V03X_H][MT9V03X_W])
{
    uint32 i;
    uint32 checksum = 0U;
    const uint8 *data = &image[0][0];

    for(i = 0U; i < MT9V03X_IMAGE_SIZE; i++)
    {
        checksum += data[i];
    }

    return checksum;
}

static uint8 image_otsu_threshold(const uint8 image[MT9V03X_H][MT9V03X_W])
{
    static uint32 histogram[256];
    uint32 total_pixels = (uint32)MT9V03X_W * (uint32)MT9V03X_H;
    uint32 gray_sum = 0U;
    uint32 background_weight = 0U;
    uint32 background_sum = 0U;
    uint64 maximum_score = 0U;
    uint8 best_threshold = 0U;
    uint16 x;
    uint16 y;
    uint16 gray;

    memset(histogram, 0, sizeof(histogram));

    for(y = 0U; y < MT9V03X_H; y++)
    {
        for(x = 0U; x < MT9V03X_W; x++)
        {
            histogram[image[y][x]]++;
        }
    }

    for(gray = 0U; gray < 256U; gray++)
    {
        gray_sum += (uint32)gray * histogram[gray];
    }

    for(gray = 0U; gray < 256U; gray++)
    {
        uint32 foreground_weight;
        int64 mean_numerator;
        uint64 score;

        background_weight += histogram[gray];
        if(0U == background_weight)
        {
            continue;
        }

        foreground_weight = total_pixels - background_weight;
        if(0U == foreground_weight)
        {
            break;
        }

        background_sum += (uint32)gray * histogram[gray];

        /* Integer Otsu score. Division is delayed to avoid float support. */
        mean_numerator = (int64)background_sum * (int64)total_pixels
                       - (int64)gray_sum * (int64)background_weight;
        if(mean_numerator < 0)
        {
            mean_numerator = -mean_numerator;
        }

        score = ((uint64)mean_numerator * (uint64)mean_numerator)
              / ((uint64)background_weight * (uint64)foreground_weight);

        if(score > maximum_score)
        {
            maximum_score = score;
            best_threshold = (uint8)gray;
        }
    }

    return best_threshold;
}

static void image_calculate_statistics(
    const uint8 image[MT9V03X_H][MT9V03X_W],
    image_record_t *record)
{
    uint32 gray_sum = 0U;
    uint32 total_pixels = (uint32)MT9V03X_W * (uint32)MT9V03X_H;
    uint16 x;
    uint16 y;

    record->min_gray = 255U;
    record->max_gray = 0U;

    for(y = 0U; y < MT9V03X_H; y++)
    {
        for(x = 0U; x < MT9V03X_W; x++)
        {
            uint8 gray = image[y][x];

            if(gray < record->min_gray)
            {
                record->min_gray = gray;
            }
            if(gray > record->max_gray)
            {
                record->max_gray = gray;
            }

            gray_sum += gray;
        }
    }

    record->mean_gray = gray_sum / total_pixels;
    record->otsu_threshold = image_otsu_threshold(image);
    record->image_checksum = image_checksum_calculate(image);
}

static void image_binary_convert(
    const uint8 source[MT9V03X_H][MT9V03X_W],
    uint8 output[MT9V03X_H][MT9V03X_W],
    uint8 threshold)
{
    uint16 x;
    uint16 y;

    for(y = 0U; y < MT9V03X_H; y++)
    {
        for(x = 0U; x < MT9V03X_W; x++)
        {
            output[y][x] = (source[y][x] > threshold) ? 255U : 0U;
        }
    }
}


static uint8 recog_threshold_calculate(uint8 otsu_threshold)
{
    uint16 threshold = (uint16)otsu_threshold + RECOG_OTSU_OFFSET;

    if(threshold < RECOG_THRESHOLD_MIN)
    {
        threshold = RECOG_THRESHOLD_MIN;
    }
    if(threshold > RECOG_THRESHOLD_MAX)
    {
        threshold = RECOG_THRESHOLD_MAX;
    }

    return (uint8)threshold;
}

static uint8 recog_is_white(
    const uint8 image[MT9V03X_H][MT9V03X_W],
    uint16 x,
    uint16 y,
    uint8 threshold)
{
    return (image[y][x] >= threshold) ? 1U : 0U;
}

static void recog_projection_build(
    const uint8 image[MT9V03X_H][MT9V03X_W],
    uint8 threshold)
{
    uint16 x;
    uint16 y;

    memset(recog_row_count, 0, sizeof(recog_row_count));
    memset(recog_col_count, 0, sizeof(recog_col_count));

    for(y = RECOG_ROI_Y_MIN; y <= RECOG_ROI_Y_MAX; y++)
    {
        for(x = RECOG_ROI_X_MIN; x <= RECOG_ROI_X_MAX; x++)
        {
            if(recog_is_white(image, x, y, threshold))
            {
                recog_row_count[y]++;
                recog_col_count[x]++;
            }
        }
    }
}

static uint8 recog_find_bands(
    const uint16 *count_array,
    uint16 index_min,
    uint16 index_max,
    uint16 threshold_count,
    uint16 min_size,
    recog_band_t bands[RECOG_MAX_BANDS])
{
    uint16 index = index_min;
    uint8 band_count = 0U;

    while((index <= index_max) && (band_count < RECOG_MAX_BANDS))
    {
        if(count_array[index] >= threshold_count)
        {
            uint16 start = index;
            uint16 end = index;
            uint16 peak = count_array[index];
            uint16 gap = 0U;

            index++;
            while(index <= index_max)
            {
                if(count_array[index] >= threshold_count)
                {
                    end = index;
                    gap = 0U;
                    if(count_array[index] > peak)
                    {
                        peak = count_array[index];
                    }
                }
                else
                {
                    gap++;
                    if(gap > 1U)
                    {
                        break;
                    }
                }
                index++;
            }

            if((uint16)(end - start + 1U) >= min_size)
            {
                bands[band_count].start = start;
                bands[band_count].end = end;
                bands[band_count].center = (uint16)((start + end) / 2U);
                bands[band_count].size = (uint16)(end - start + 1U);
                bands[band_count].peak_count = peak;
                band_count++;
            }
        }
        else
        {
            index++;
        }
    }

    return band_count;
}

static uint32 recog_count_white_rect(
    const uint8 image[MT9V03X_H][MT9V03X_W],
    uint16 x0,
    uint16 y0,
    uint16 x1,
    uint16 y1,
    uint8 threshold,
    uint32 *pixel_count)
{
    uint16 x;
    uint16 y;
    uint32 white_count = 0U;
    uint32 total_count = 0U;

    if(x1 >= MT9V03X_W)
    {
        x1 = MT9V03X_W - 1U;
    }
    if(y1 >= MT9V03X_H)
    {
        y1 = MT9V03X_H - 1U;
    }
    if((x0 > x1) || (y0 > y1))
    {
        *pixel_count = 0U;
        return 0U;
    }

    for(y = y0; y <= y1; y++)
    {
        for(x = x0; x <= x1; x++)
        {
            total_count++;
            if(recog_is_white(image, x, y, threshold))
            {
                white_count++;
            }
        }
    }

    *pixel_count = total_count;
    return white_count;
}

static uint8 recog_white_component_fallback(
    const uint8 image[MT9V03X_H][MT9V03X_W],
    uint8 threshold,
    mine_target_t *target)
{
    const int16 x_offset[8] = {-1, 0, 1, -1, 1, -1, 0, 1};
    const int16 y_offset[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
    mine_target_t best_component;
    uint16 roi_w = (uint16)(RECOG_ROI_X_MAX - RECOG_ROI_X_MIN + 1U);
    uint16 roi_h = (uint16)(RECOG_ROI_Y_MAX - RECOG_ROI_Y_MIN + 1U);
    uint16 x;
    uint16 y;
    uint32 best_score = 0U;

    memset(recog_component_visited, 0, sizeof(recog_component_visited));
    memset(&best_component, 0, sizeof(best_component));
    best_component.threshold = threshold;

    for(y = RECOG_ROI_Y_MIN; y <= RECOG_ROI_Y_MAX; y++)
    {
        for(x = RECOG_ROI_X_MIN; x <= RECOG_ROI_X_MAX; x++)
        {
            uint32 stack_count;
            uint32 component_count;
            uint32 component_score;
            uint16 min_x;
            uint16 max_x;
            uint16 min_y;
            uint16 max_y;
            uint16 component_width;
            uint16 component_height;

            if(recog_component_visited[y][x])
            {
                continue;
            }
            recog_component_visited[y][x] = 1U;
            if(0U == recog_is_white(image, x, y, threshold))
            {
                continue;
            }

            stack_count = 0U;
            component_count = 0U;
            min_x = x;
            max_x = x;
            min_y = y;
            max_y = y;
            recog_component_stack[stack_count++] = (uint16)(y * MT9V03X_W + x);

            while(stack_count > 0U)
            {
                uint16 pixel_index;
                uint16 px;
                uint16 py;
                uint8 offset_i;

                pixel_index = recog_component_stack[--stack_count];
                px = (uint16)(pixel_index % MT9V03X_W);
                py = (uint16)(pixel_index / MT9V03X_W);
                component_count++;

                if(px < min_x)
                {
                    min_x = px;
                }
                if(px > max_x)
                {
                    max_x = px;
                }
                if(py < min_y)
                {
                    min_y = py;
                }
                if(py > max_y)
                {
                    max_y = py;
                }

                for(offset_i = 0U; offset_i < 8U; offset_i++)
                {
                    int16 nx = (int16)px + x_offset[offset_i];
                    int16 ny = (int16)py + y_offset[offset_i];

                    if((nx < (int16)RECOG_ROI_X_MIN)
                    || (nx > (int16)RECOG_ROI_X_MAX)
                    || (ny < (int16)RECOG_ROI_Y_MIN)
                    || (ny > (int16)RECOG_ROI_Y_MAX))
                    {
                        continue;
                    }
                    if(recog_component_visited[(uint16)ny][(uint16)nx])
                    {
                        continue;
                    }
                    recog_component_visited[(uint16)ny][(uint16)nx] = 1U;
                    if(recog_is_white(image, (uint16)nx, (uint16)ny, threshold))
                    {
                        if(stack_count < MT9V03X_IMAGE_SIZE)
                        {
                            recog_component_stack[stack_count++] = (uint16)((uint16)ny * MT9V03X_W + (uint16)nx);
                        }
                    }
                }
            }

            if(component_count < RECOG_FALLBACK_MIN_WHITE)
            {
                continue;
            }

            component_width = (uint16)(max_x - min_x + 1U);
            component_height = (uint16)(max_y - min_y + 1U);

            if((component_width < RECOG_FALLBACK_MIN_WIDTH)
            || (component_height < RECOG_FALLBACK_MIN_HEIGHT)
            || (component_width > (uint16)((roi_w * RECOG_FALLBACK_MAX_ROI_RATIO) / 100U))
            || (component_height > (uint16)((roi_h * RECOG_FALLBACK_MAX_ROI_RATIO) / 100U)))
            {
                continue;
            }

            component_score = component_count
                            + ((uint32)component_width * 3U)
                            + ((uint32)component_height * 3U);

            if(component_score > best_score)
            {
                best_score = component_score;
                best_component.valid = 1U;
                best_component.threshold = threshold;
                best_component.confidence = (component_count > 255U) ? 255U : (uint8)component_count;
                best_component.left = min_x;
                best_component.right = max_x;
                best_component.top = min_y;
                best_component.bottom = max_y;
                best_component.center_x = (int16)((min_x + max_x) / 2U);
                best_component.center_y = (int16)((min_y + max_y) / 2U);
                best_component.error_x = (int16)(best_component.center_x - (int16)(MT9V03X_W / 2U));
                best_component.error_y = (int16)(best_component.center_y - (int16)(MT9V03X_H / 2U));
                best_component.width = component_width;
                best_component.height = component_height;
            }
        }
    }

    if(best_component.valid)
    {
        *target = best_component;
        return 1U;
    }

    return 0U;
}

static uint8 mine_target_recognize(
    const uint8 image[MT9V03X_H][MT9V03X_W],
    uint8 threshold,
    mine_target_t *target)
{
    recog_band_t h_bands[RECOG_MAX_BANDS];
    recog_band_t v_bands[RECOG_MAX_BANDS];
    uint8 h_count;
    uint8 v_count;
    uint8 top_i;
    uint32 best_score = 0U;
    mine_target_t best_target;

    memset(target, 0, sizeof(*target));
    memset(&best_target, 0, sizeof(best_target));
    best_target.threshold = threshold;

    recog_projection_build(image, threshold);

    h_count = recog_find_bands(
        recog_row_count,
        RECOG_ROI_Y_MIN,
        RECOG_ROI_Y_MAX,
        RECOG_ROW_MIN_COUNT,
        2U,
        h_bands);

    v_count = recog_find_bands(
        recog_col_count,
        RECOG_ROI_X_MIN,
        RECOG_ROI_X_MAX,
        RECOG_COL_MIN_COUNT,
        2U,
        v_bands);

    for(top_i = 0U; top_i < h_count; top_i++)
    {
        uint8 bottom_i;

        for(bottom_i = (uint8)(top_i + 1U); bottom_i < h_count; bottom_i++)
        {
            uint8 left_i;
            uint16 top = h_bands[top_i].center;
            uint16 bottom = h_bands[bottom_i].center;
            uint16 height;

            if(bottom <= top)
            {
                continue;
            }
            height = (uint16)(bottom - top);
            if(height < RECOG_MIN_HEIGHT)
            {
                continue;
            }

            for(left_i = 0U; left_i < v_count; left_i++)
            {
                uint8 right_i;

                for(right_i = (uint8)(left_i + 1U); right_i < v_count; right_i++)
                {
                    uint16 left = v_bands[left_i].center;
                    uint16 right = v_bands[right_i].center;
                    uint16 width;
                    uint32 border_white = 0U;
                    uint32 border_total = 0U;
                    uint32 part_total = 0U;
                    uint32 inner_white;
                    uint32 inner_total;
                    uint16 border_ratio;
                    uint16 inner_ratio;
                    uint32 score;
                    uint16 aspect_x100;
                    uint16 ix0;
                    uint16 ix1;
                    uint16 iy0;
                    uint16 iy1;

                    if(right <= left)
                    {
                        continue;
                    }
                    width = (uint16)(right - left);
                    if(width < RECOG_MIN_WIDTH)
                    {
                        continue;
                    }
                    if((width > (uint16)(((RECOG_ROI_X_MAX - RECOG_ROI_X_MIN + 1U) * 80U) / 100U))
                    || (height > (uint16)(((RECOG_ROI_Y_MAX - RECOG_ROI_Y_MIN + 1U) * 80U) / 100U)))
                    {
                        continue;
                    }

                    aspect_x100 = (uint16)(((uint32)width * 100U) / (uint32)height);
                    if((aspect_x100 < 70U) || (aspect_x100 > 330U))
                    {
                        continue;
                    }

                    border_white += recog_count_white_rect(
                        image,
                        left,
                        h_bands[top_i].start,
                        right,
                        h_bands[top_i].end,
                        threshold,
                        &part_total);
                    border_total += part_total;

                    border_white += recog_count_white_rect(
                        image,
                        left,
                        h_bands[bottom_i].start,
                        right,
                        h_bands[bottom_i].end,
                        threshold,
                        &part_total);
                    border_total += part_total;

                    border_white += recog_count_white_rect(
                        image,
                        v_bands[left_i].start,
                        top,
                        v_bands[left_i].end,
                        bottom,
                        threshold,
                        &part_total);
                    border_total += part_total;

                    border_white += recog_count_white_rect(
                        image,
                        v_bands[right_i].start,
                        top,
                        v_bands[right_i].end,
                        bottom,
                        threshold,
                        &part_total);
                    border_total += part_total;

                    if(0U == border_total)
                    {
                        continue;
                    }
                    border_ratio = (uint16)((border_white * 100U) / border_total);
                    if(border_ratio < RECOG_MIN_BORDER_RATIO)
                    {
                        continue;
                    }

                    ix0 = (uint16)(left + width / 4U);
                    ix1 = (uint16)(right - width / 4U);
                    iy0 = (uint16)(top + height / 4U);
                    iy1 = (uint16)(bottom - height / 4U);

                    inner_white = recog_count_white_rect(
                        image,
                        ix0,
                        iy0,
                        ix1,
                        iy1,
                        threshold,
                        &inner_total);
                    if(0U == inner_total)
                    {
                        continue;
                    }
                    inner_ratio = (uint16)((inner_white * 100U) / inner_total);
                    if(inner_ratio > RECOG_MAX_INNER_RATIO)
                    {
                        continue;
                    }

                    score = ((uint32)border_ratio * 3U)
                          + ((uint32)(100U - inner_ratio) * 2U)
                          + ((uint32)width + (uint32)height);

                    if(score > best_score)
                    {
                        best_score = score;
                        best_target.valid = 1U;
                        best_target.threshold = threshold;
                        best_target.confidence = (score > 255U) ? 255U : (uint8)score;
                        best_target.center_x = (int16)((left + right) / 2U);
                        best_target.center_y = (int16)((top + bottom) / 2U);
                        best_target.error_x = (int16)(best_target.center_x - (int16)(MT9V03X_W / 2U));
                        best_target.error_y = (int16)(best_target.center_y - (int16)(MT9V03X_H / 2U));
                        best_target.width = width;
                        best_target.height = height;
                        best_target.left = left;
                        best_target.right = right;
                        best_target.top = top;
                        best_target.bottom = bottom;
                        best_target.border_ratio = border_ratio;
                        best_target.inner_ratio = inner_ratio;
                    }
                }
            }
        }
    }

    if(best_target.valid)
    {
        *target = best_target;
        return 1U;
    }

    if(recog_white_component_fallback(image, threshold, target))
    {
        return 1U;
    }

    target->threshold = threshold;
    return 0U;
}

static void draw_pixel_safe(uint8 image[MT9V03X_H][MT9V03X_W], int16 x, int16 y, uint8 gray)
{
    if((x >= 0) && (x < (int16)MT9V03X_W) && (y >= 0) && (y < (int16)MT9V03X_H))
    {
        image[y][x] = gray;
    }
}

static void draw_hline(uint8 image[MT9V03X_H][MT9V03X_W], int16 x0, int16 x1, int16 y, uint8 gray)
{
    int16 x;

    if(x0 > x1)
    {
        int16 temp = x0;
        x0 = x1;
        x1 = temp;
    }

    for(x = x0; x <= x1; x++)
    {
        draw_pixel_safe(image, x, y, gray);
    }
}

static void draw_vline(uint8 image[MT9V03X_H][MT9V03X_W], int16 x, int16 y0, int16 y1, uint8 gray)
{
    int16 y;

    if(y0 > y1)
    {
        int16 temp = y0;
        y0 = y1;
        y1 = temp;
    }

    for(y = y0; y <= y1; y++)
    {
        draw_pixel_safe(image, x, y, gray);
    }
}

static void draw_roi_overlay(uint8 image[MT9V03X_H][MT9V03X_W])
{
    uint16 x;
    uint16 y;

    /*
     * V4.2: ROI is only a light dashed reference box shown when recognition fails.
     * It is NOT the target result, so it must not look like a solid target box.
     */
    for(x = RECOG_ROI_X_MIN; x <= RECOG_ROI_X_MAX; x += 4U)
    {
        draw_pixel_safe(image, (int16)x, (int16)RECOG_ROI_Y_MIN, 70U);
        draw_pixel_safe(image, (int16)x, (int16)RECOG_ROI_Y_MAX, 70U);
    }

    for(y = RECOG_ROI_Y_MIN; y <= RECOG_ROI_Y_MAX; y += 4U)
    {
        draw_pixel_safe(image, (int16)RECOG_ROI_X_MIN, (int16)y, 70U);
        draw_pixel_safe(image, (int16)RECOG_ROI_X_MAX, (int16)y, 70U);
    }
}

static void draw_projection_debug_overlay(uint8 image[MT9V03X_H][MT9V03X_W])
{
    uint16 x;
    uint16 y;

    /*
     * V4.1 debug display:
     * - If recognition fails, black ROI box still appears.
     * - Short dark lines indicate rows/columns where the program sees many bright pixels.
     * This helps judge whether threshold/ROI is wrong without using serial text.
     */
    for(y = RECOG_ROI_Y_MIN; y <= RECOG_ROI_Y_MAX; y++)
    {
        if(recog_row_count[y] >= RECOG_DEBUG_BAND_ROW_COUNT)
        {
            draw_hline(image, (int16)RECOG_ROI_X_MIN, (int16)(RECOG_ROI_X_MIN + 10U), (int16)y, 0U);
            draw_hline(image, (int16)(RECOG_ROI_X_MAX - 10U), (int16)RECOG_ROI_X_MAX, (int16)y, 0U);
        }
    }

    for(x = RECOG_ROI_X_MIN; x <= RECOG_ROI_X_MAX; x++)
    {
        if(recog_col_count[x] >= RECOG_DEBUG_BAND_COL_COUNT)
        {
            draw_vline(image, (int16)x, (int16)RECOG_ROI_Y_MIN, (int16)(RECOG_ROI_Y_MIN + 6U), 0U);
            draw_vline(image, (int16)x, (int16)(RECOG_ROI_Y_MAX - 6U), (int16)RECOG_ROI_Y_MAX, 0U);
        }
    }
}

static void draw_target_overlay(uint8 image[MT9V03X_H][MT9V03X_W], const mine_target_t *target)
{
    uint8 i;

    if(0U == target->valid)
    {
        return;
    }

    for(i = 0U; i < 2U; i++)
    {
        draw_hline(image, (int16)target->left, (int16)target->right, (int16)(target->top + i), 0U);
        draw_hline(image, (int16)target->left, (int16)target->right, (int16)(target->bottom - i), 0U);
        draw_vline(image, (int16)(target->left + i), (int16)target->top, (int16)target->bottom, 0U);
        draw_vline(image, (int16)(target->right - i), (int16)target->top, (int16)target->bottom, 0U);
    }

    draw_hline(image, (int16)(target->center_x - 4), (int16)(target->center_x + 4), target->center_y, 255U);
    draw_vline(image, target->center_x, (int16)(target->center_y - 4), (int16)(target->center_y + 4), 255U);
}

static void storage_header_default(void)
{
    memset(&storage_header, 0, sizeof(storage_header));
    storage_header.magic = STORAGE_MAGIC;
    storage_header.version = STORAGE_VERSION;
    storage_header.width = MT9V03X_W;
    storage_header.height = MT9V03X_H;
    storage_header.selected_slot = 0U;
}

static void storage_header_write(void)
{
    memset(flash_union_buffer, 0xFF, FLASH_PAGE_SIZE);
    memcpy(flash_union_buffer, &storage_header, sizeof(storage_header));
    flash_write_page(0U, STORAGE_META_PAGE, (const uint32 *)flash_union_buffer, FLASH_PAGE_LENGTH);
}

static void storage_header_load(void)
{
    flash_read_page(0U, STORAGE_META_PAGE, (uint32 *)flash_union_buffer, FLASH_PAGE_LENGTH);
    memcpy(&storage_header, flash_union_buffer, sizeof(storage_header));

    if((STORAGE_MAGIC != storage_header.magic)
    || (STORAGE_VERSION != storage_header.version)
    || (MT9V03X_W != storage_header.width)
    || (MT9V03X_H != storage_header.height)
    || (storage_header.next_slot >= STORAGE_MAX_IMAGES)
    || (storage_header.selected_slot >= STORAGE_MAX_IMAGES))
    {
        storage_header_default();
        storage_header_write();
    }
}

static void storage_image_write(uint8 slot, const uint8 image[MT9V03X_H][MT9V03X_W])
{
    uint32 page;
    uint32 offset = 0U;
    const uint8 *source = &image[0][0];
    uint32 start_page = STORAGE_IMAGE_START_PAGE + ((uint32)slot * STORAGE_PAGES_PER_IMAGE);

    for(page = 0U; page < STORAGE_PAGES_PER_IMAGE; page++)
    {
        uint32 remaining = MT9V03X_IMAGE_SIZE - offset;
        uint32 copy_size = (remaining > FLASH_PAGE_SIZE) ? FLASH_PAGE_SIZE : remaining;

        memset(flash_union_buffer, 0xFF, FLASH_PAGE_SIZE);
        memcpy(flash_union_buffer, source + offset, copy_size);
        flash_write_page(0U, start_page + page, (const uint32 *)flash_union_buffer, FLASH_PAGE_LENGTH);
        offset += copy_size;
    }
}

static void storage_image_read(uint8 slot, uint8 image[MT9V03X_H][MT9V03X_W])
{
    uint32 page;
    uint32 offset = 0U;
    uint8 *destination = &image[0][0];
    uint32 start_page = STORAGE_IMAGE_START_PAGE + ((uint32)slot * STORAGE_PAGES_PER_IMAGE);

    for(page = 0U; page < STORAGE_PAGES_PER_IMAGE; page++)
    {
        uint32 remaining = MT9V03X_IMAGE_SIZE - offset;
        uint32 copy_size = (remaining > FLASH_PAGE_SIZE) ? FLASH_PAGE_SIZE : remaining;

        flash_read_page(0U, start_page + page, (uint32 *)flash_union_buffer, FLASH_PAGE_LENGTH);
        memcpy(destination + offset, flash_union_buffer, copy_size);
        offset += copy_size;
    }
}

static uint8 storage_select_next_valid(void)
{
    uint8 step;

    if(0U == storage_header.valid_mask)
    {
        return 0U;
    }

    for(step = 1U; step <= STORAGE_MAX_IMAGES; step++)
    {
        uint8 candidate = (uint8)((storage_header.selected_slot + step) % STORAGE_MAX_IMAGES);
        if(0U != (storage_header.valid_mask & (1UL << candidate)))
        {
            storage_header.selected_slot = candidate;
            storage_header_write();
            return 1U;
        }
    }

    return 0U;
}

static void prepare_display_image(uint8 slot)
{
    uint8 threshold = recog_threshold_calculate((uint8)storage_header.record[slot].otsu_threshold);

    storage_image_read(slot, snapshot_image);
    mine_target_recognize(snapshot_image, threshold, &current_target);

    if(DISPLAY_MODE_BINARY == display_mode)
    {
        image_binary_convert(snapshot_image, display_image, threshold);
    }
    else
    {
        memcpy(display_image[0], snapshot_image[0], MT9V03X_IMAGE_SIZE);
    }

    draw_target_overlay(display_image, &current_target);
}

static void prepare_live_display_image(void)
{
    uint8 threshold;

    memcpy(snapshot_image[0], mt9v03x_image[0], MT9V03X_IMAGE_SIZE);
    memset(&live_record, 0, sizeof(live_record));
    image_calculate_statistics(snapshot_image, &live_record);
    threshold = recog_threshold_calculate((uint8)live_record.otsu_threshold);

    mine_target_recognize(snapshot_image, threshold, &current_target);

    if(DISPLAY_MODE_BINARY == display_mode)
    {
        image_binary_convert(snapshot_image, display_image, threshold);
    }
    else
    {
        memcpy(display_image[0], snapshot_image[0], MT9V03X_IMAGE_SIZE);
    }

    draw_target_overlay(display_image, &current_target);
}

static void wireless_send_live_image_once(void)
{
    prepare_live_display_image();
    seekfree_assistant_camera_send();
}

static void send_selected_statistics(void)
{
    int8 text_buffer[256];
    uint32 text_length;
    uint8 slot = (uint8)storage_header.selected_slot;
    const image_record_t *record = &storage_header.record[slot];
    const int8 *mode_name = (DISPLAY_MODE_BINARY == display_mode)
                          ? (const int8 *)"BINARY"
                          : (const int8 *)"RAW";

    text_length = zf_sprintf(
        text_buffer,
        (const int8 *)"slot=%u saved=%u total=%u mode=%s min=%u max=%u mean=%u otsu=%u recog_th=%u checksum=%u\r\n",
        (uint32)(slot + 1U),
        (uint32)count_saved_images(),
        storage_header.capture_total,
        mode_name,
        record->min_gray,
        record->max_gray,
        record->mean_gray,
        record->otsu_threshold,
        current_target.threshold,
        record->image_checksum);

    assistant_text_send((const uint8 *)text_buffer, text_length);

    text_length = zf_sprintf(
        text_buffer,
        (const int8 *)"target valid=%u center=(%d,%d) error=(%d,%d) size=%ux%u box=(%u,%u,%u,%u) border=%u inner=%u confidence=%u\r\n",
        current_target.valid,
        current_target.center_x,
        current_target.center_y,
        current_target.error_x,
        current_target.error_y,
        current_target.width,
        current_target.height,
        current_target.left,
        current_target.top,
        current_target.right,
        current_target.bottom,
        current_target.border_ratio,
        current_target.inner_ratio,
        current_target.confidence);

    assistant_text_send((const uint8 *)text_buffer, text_length);
}

static void send_live_statistics(void)
{
    int8 text_buffer[256];
    uint32 text_length;
    const int8 *mode_name = (DISPLAY_MODE_BINARY == display_mode)
                          ? (const int8 *)"BINARY"
                          : (const int8 *)"RAW";

    text_length = zf_sprintf(
        text_buffer,
        (const int8 *)"live mode=%s min=%u max=%u mean=%u otsu=%u recog_th=%u checksum=%u\r\n",
        mode_name,
        live_record.min_gray,
        live_record.max_gray,
        live_record.mean_gray,
        live_record.otsu_threshold,
        current_target.threshold,
        live_record.image_checksum);

    assistant_text_send((const uint8 *)text_buffer, text_length);

    text_length = zf_sprintf(
        text_buffer,
        (const int8 *)"target valid=%u center=(%d,%d) error=(%d,%d) size=%ux%u box=(%u,%u,%u,%u) border=%u inner=%u confidence=%u\r\n",
        current_target.valid,
        current_target.center_x,
        current_target.center_y,
        current_target.error_x,
        current_target.error_y,
        current_target.width,
        current_target.height,
        current_target.left,
        current_target.top,
        current_target.right,
        current_target.bottom,
        current_target.border_ratio,
        current_target.inner_ratio,
        current_target.confidence);

    assistant_text_send((const uint8 *)text_buffer, text_length);
}

static void capture_save_next_frame(void)
{
    uint8 slot = (uint8)storage_header.next_slot;
    image_record_t *record = &storage_header.record[slot];

    memcpy(snapshot_image[0], mt9v03x_image[0], MT9V03X_IMAGE_SIZE);

    memset(record, 0, sizeof(*record));
    record->sequence = storage_header.capture_total + 1U;
    image_calculate_statistics(snapshot_image, record);

    storage_image_write(slot, snapshot_image);

    storage_header.valid_mask |= (1UL << slot);
    storage_header.capture_total++;
    storage_header.selected_slot = slot;
    storage_header.next_slot = (slot + 1U) % STORAGE_MAX_IMAGES;
    storage_header_write();

    status_led_blink(1U);
}

int main(void)
{
    clock_init(SYSTEM_CLOCK_250M);
    debug_init();                         /* keep UART0 debug available when DAP/USB-TTL is connected */

    gpio_init(CAMERA_TEST_LED, GPO, GPIO_HIGH, GPO_PUSH_PULL);
    key_init(KEY_SCAN_PERIOD_MS);
    flash_init();
    storage_header_load();

    seekfree_assistant_interface_init(SEEKFREE_ASSISTANT_DEBUG_UART);
    seekfree_assistant_camera_information_config(
        SEEKFREE_ASSISTANT_MT9V03X,
        display_image[0],
        MT9V03X_W,
        MT9V03X_H);

    while(true)
    {
        if(0U == mt9v03x_init())
        {
            break;
        }

        gpio_toggle_level(CAMERA_TEST_LED);
        system_delay_ms(500U);
    }

    status_led_blink(2U);

    while(true)
    {
        if(mt9v03x_finish_flag)
        {
            mt9v03x_finish_flag = 0U;

            prepare_live_display_image();
            seekfree_assistant_camera_send();
#if DEBUG_SIMPLE_FRAME_DUMP
            debug_simple_frame_dump_send();
#endif
        }

        system_delay_ms(KEY_SCAN_PERIOD_MS);
    }
}
