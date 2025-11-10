#include "zynq_lcd_st7789.h"
#include <sleep.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <xgpio_l.h>
#include <xparameters.h>
#include <xpseudo_asm_gcc.h>
#include <xspips.h>
#include <xstatus.h>

/*************************************************
  header implementation file for ST7789VW conrolled
  172x320 LCD display on the Smart Zynq SP board
**************************************************/

#define LCD_DC 0    // AXI 0 GPIO bit 0
#define LCD_RESET 1 // AXI 0 GPIO bit 1
/*
The LCD backlight does not seem to matter and can be left completely unconnected
in the hardware configuration. It is pulled up and driven high.
*/

// #define LCD_BACKLIGHT 2 // AXI GPIO bit 2

// Memory Access Data control RGB/BGR flags
#define ST7789_MADCTL_RGB 0x00
#define ST7789_MADCTL_BGR 0x08

// ST7789VW memory is 240 x 320, but the LCD is 172 x 320
#define ZLCD_X_OFFSET 34U // (240 - 172) / 2
#define ZLCD_Y_OFFSET 0U  // ZLCD height matches what the ST7789VW expects

#define log_error_message(string)                                              \
  printf("\n%s\nFile: %s%d\n\n", (string), __FILE__, __LINE__);

/*******************************
        TYPEDEFS HERE
********************************/

typedef enum {
  SLEEP_MODE,    // sleeping (saves power)
  SLEEP_OUT_MODE // not sleeping
} ZLCD_SLEEP_MODE;

typedef size_t (*pixel_transform_function)(uint16_t x, uint16_t y);

typedef struct {
  uint16_t horizontal_axis_length_px, vertical_axis_length_px;
  ZLCD_ORIENTATION orientation_type;
} ZLCD_orientation_parameters;

// for handling negative coordinates in internal functions only
typedef struct {
  int16_t x;
  int16_t y;
} ZLCD_internal_coordinate;

/*******************************
  STATIC GLOBAL VARIABLES HERE
********************************/

static XGpio LCD_gpios;
static XSpiPs spi_instance;
// tracks current orientation data
static ZLCD_orientation_parameters current_orientation = {0};

static ZLCD_PRINTF_MODE current_printf_mode = ZLCD_PRINTF_MODE_SCROLL;
// tracks current state of DC pin/reset/backlight GPIO bus
static uint32_t current_gpio_values;
static ZLCD_SLEEP_MODE current_sleep_mode;

static bool ZLCD_initialized = false; // has the user initialized yet?

static uint16_t cached_col_start = 0xFFFF;
static uint16_t cached_col_end = 0xFFFF;
static uint16_t cached_row_start = 0xFFFF;
static uint16_t cached_row_end = 0xFFFF;
static rgb565 current_background_colour;

static pixel_transform_function current_transform_fun = NULL;

// ZLCD_printf cursor index
static uint16_t printf_y = 0, printf_x = 0;

// visible to users because of extern header declaration
const ZLCD_font printf_font = {.font_name = "Liberation Mono",
                               .font_size = 12,
                               // bitmap and descriptors in header file
                               .glyph_bitmap = printf_bmp,
                               .glyph_descriptors = printf_dsc};

/*
Brightness not adjustable because of board layout (?)
The Backlight pin will be pulled up and can be disconnected fully
*/

// static uint16_t current_brightness = 0; // PWM strength of backlight pin

/**************************************************************************************************
An array of bytes is better than an array of rgb565 values because the data must
be sent MSB first to the ST7789 but the ARM CPU is little endian and will send
the LSB first if an array of uint16_ts is cast to an array of uint8_ts (bytes).
Therefore, every two consecutive bytes of the GRAM array represent one single
rgb565 pixel, and must be ordered MSB-first

Each GRAM image is 110.08 Kbytes long
The GRAM images are ALWAYS configured for portrait mode even if the user selects
another mode
***************************************************************************************************/
static uint8_t GRAM_current[ZLCD_WIDTH * ZLCD_HEIGHT * sizeof(rgb565)];
static uint8_t GRAM_previous[ZLCD_WIDTH * ZLCD_HEIGHT * sizeof(rgb565)];

/*******************************
    STATIC FUNCTIONS HERE
********************************/

// function pointer options based on orientation
static size_t pixel_coordinate_to_internal_index_portrait(uint16_t x,
                                                          uint16_t y);
static size_t pixel_coordinate_to_internal_index_inverted_portrait(uint16_t x,
                                                                   uint16_t y);
static size_t pixel_coordinate_to_internal_index_landscape(uint16_t x,
                                                           uint16_t y);
static size_t pixel_coordinate_to_internal_index_inverted_landscape(uint16_t x,
                                                                    uint16_t y);

static ZLCD_RETURN_STATUS ZLCD_gpio_init(void);
static ZLCD_RETURN_STATUS ZLCD_spi_init(void);
static void ZLCD_write_gpio(uint32_t gpio_bit_mask, bool value);
static inline void ZLCD_write_bytes(const uint8_t *byte_stream,
                                    size_t num_bytes);
static inline void ZLCD_send_data_byte(uint8_t data);
static inline void ZLCD_send_data(const uint8_t *byte_stream, size_t num_bytes);
static inline void ZLCD_send_command(uint8_t command);
static ZLCD_RETURN_STATUS
ZLCD_draw_triangle_internal(ZLCD_pixel_coordinate p1, ZLCD_pixel_coordinate p2,
                            ZLCD_pixel_coordinate p3, rgb565 border_colour,
                            bool fill, rgb565 fill_colour, bool update_now);

static void
ZLCD_draw_rectangle_xy_internal(uint16_t origin_x, uint16_t origin_y,
                                uint16_t width_px, uint16_t height_px,
                                uint16_t border_thickness_px, bool fill,
                                rgb565 border_colour, rgb565 fill_colour);

static ZLCD_RETURN_STATUS
ZLCD_draw_circle_xy_internal(uint16_t origin_x, uint16_t origin_y,
                             uint16_t radius_px, rgb565 border_colour,
                             bool fill, rgb565 fill_colour, bool update_now);

static void ZLCD_set_rows(uint16_t y_start, uint16_t y_end);
static void ZLCD_set_columns(uint16_t x_start, uint16_t x_end);
static void ZLCD_set_window(uint16_t x0, uint16_t x1, uint16_t y0, uint16_t y1);
static void fill_bottom_flat_triangle(ZLCD_pixel_coordinate v1,
                                      ZLCD_pixel_coordinate v2,
                                      ZLCD_pixel_coordinate v3, rgb565 colour);

static void fill_top_flat_triangle(ZLCD_pixel_coordinate v1,
                                   ZLCD_pixel_coordinate v2,
                                   ZLCD_pixel_coordinate v3, rgb565 colour);

static void ZLCD_draw_hline_internal(int16_t y, int16_t x1, int16_t x2,
                                     rgb565 colour);
static void ZLCD_draw_vline_internal(int16_t x, int16_t y1, int16_t y2,
                                     rgb565 colour);
static void ZLCD_draw_line_xy_internal(int16_t x1, int16_t y1, int16_t x2,
                                       int16_t y2, rgb565 colour);

static inline void ZLCD_set_pixel_xy_internal(int16_t x, int16_t y,
                                              rgb565 colour);
// static void ZLCD_set_pixel_internal(ZLCD_internal_coordinate p, rgb565
// colour);
static void ZLCD_draw_line_internal(ZLCD_internal_coordinate p1,
                                    ZLCD_internal_coordinate p2, rgb565 colour);

static void ZLCD_draw_char_xy_internal(char character, uint16_t base_x,
                                       uint16_t base_y, rgb565 colour,
                                       bool draw_background,
                                       rgb565 background_colour,
                                       const ZLCD_font *f);

static void ZLCD_print_string_xy_internal(const char *string, uint16_t base_x,
                                          uint16_t base_y, rgb565 colour,
                                          bool draw_background,
                                          rgb565 background_colour,
                                          const ZLCD_font *f);

static void ZLCD_print_aligned_string_internal(
    const char *string, uint16_t base_y, ZLCD_TEXT_ALIGNMENT alignment,
    rgb565 colour, bool draw_background, rgb565 background_colour,
    const ZLCD_font *f, bool update_now);

static uint16_t get_font_height(const char *string, const ZLCD_font *f,
                                int8_t *y_offset);

/*******************************
    FUNCTION DEFINITIONS HERE
********************************/

ZLCD_pixel_coordinate ZLCD_create_coordinate(uint16_t x, uint16_t y) {
  return (ZLCD_pixel_coordinate) {.x=x, .y=y};
}

void ZLCD_change_pixel_coordinate(ZLCD_pixel_coordinate *coordinate,
                                  uint16_t new_x, uint16_t new_y) {
  if (coordinate == NULL) return;
  coordinate->x = new_x;
  coordinate->y = new_y;
}

static void ZLCD_set_rows(uint16_t y_start, uint16_t y_end) {
  uint8_t data[4];
  ZLCD_send_command(0x2B);           // Row address set
  data[0] = (y_start & 0xFF00) >> 8; // MSBs
  data[1] = (y_start & 0x00FF);      // LSBs
  data[2] = (y_end & 0xFF00) >> 8;
  data[3] = y_end & 0x00FF;
  ZLCD_send_data(data, sizeof(data));
}

static void ZLCD_set_columns(uint16_t x_start, uint16_t x_end) {
  uint8_t data[4];
  ZLCD_send_command(0x2A);           // Column address set
  data[0] = (x_start & 0xFF00) >> 8; // MSBs
  data[1] = x_start & 0x00FF;        // LSBs
  data[2] = (x_end & 0xFF00) >> 8;
  data[3] = x_end & 0x00FF;
  ZLCD_send_data(data, sizeof(data));
}

static void ZLCD_set_window(uint16_t x0, uint16_t x1, uint16_t y0,
                            uint16_t y1) {
  if (x0 != cached_col_start || x1 != cached_col_end) {
    ZLCD_set_columns(x0, x1);
    cached_col_start = x0;
    cached_col_end = x1;
  }
  if (y0 != cached_row_start || y1 != cached_row_end) {
    ZLCD_set_rows(y0, y1);
    cached_row_start = y0;
    cached_row_end = y1;
  }
  ZLCD_send_command(0x2C);
}

static inline void ZLCD_send_command(uint8_t command) {
  // set DC to 0 --> indicates command
  if ((current_gpio_values >> LCD_DC) & 0x1) {
    ZLCD_write_gpio(LCD_DC, 0);
  }
  ZLCD_write_bytes(&command, 1);
}

static inline void ZLCD_send_data_byte(uint8_t data) {
  // set DC to 1 --> indicates data
  if (((current_gpio_values >> LCD_DC) & 0x1) == 0) {
    ZLCD_write_gpio(LCD_DC, 1);
  }
  ZLCD_write_bytes(&data, 1);
}

static inline void ZLCD_send_data(const uint8_t *byte_stream,
                                  size_t num_bytes) {
  // set CD to 1 --> indicates data
  if (((current_gpio_values >> LCD_DC) & 0x1) == 0) {
    ZLCD_write_gpio(LCD_DC, 1);
  }
  ZLCD_write_bytes(byte_stream, num_bytes);
}

static inline void ZLCD_write_bytes(const uint8_t *byte_stream,
                                    size_t num_bytes) {
  XSpiPs_PolledTransfer(&spi_instance, (uint8_t *)byte_stream, NULL, num_bytes);
}

static void ZLCD_write_gpio(uint32_t gpio_bit_mask, bool value) {
  current_gpio_values = XGpio_DiscreteRead(&LCD_gpios, 1); // renove me
  if (value) {
    // set with bitwise OR
    current_gpio_values |= (1 << gpio_bit_mask);
  } else {
    // clear by bitwise AND
    current_gpio_values &= ~(1 << gpio_bit_mask);
  }
  XGpio_DiscreteWrite(&LCD_gpios, 1, current_gpio_values);
}

static void ZLCD_software_reset(void) {
  /*
  It will be necessary to wait 5msec before sending new command following
  software reset. The display module loads all display suppliers’ factory
  default values to the registers during this 5msec. If software reset is sent
  during sleep in mode, it will be necessary to wait 120msec before sending
  sleep out command. Software reset command cannot be sent during sleep out
  sequence.
  */

  ZLCD_send_command(0x01);
  msleep(5);
}

static ZLCD_RETURN_STATUS ZLCD_gpio_init(void) {
  if (XGpio_Initialize(&LCD_gpios, XPAR_AXI_GPIO_0_BASEADDR) != XST_SUCCESS) {
    printf("Failed to initialize AXI GPIO 0\n");
    return ZLCD_FAILURE;
  }

  if (XGpio_CfgInitialize(&LCD_gpios,
                          XGpio_LookupConfig(XPAR_AXI_GPIO_0_BASEADDR),
                          XPAR_AXI_GPIO_0_BASEADDR) != XST_SUCCESS) {
    printf("Failed to do configuration of AXI GPIO 0 (LCD DC pin, reset, and "
           "backlight enable)\n");
    return ZLCD_FAILURE;
  }

  // all single channel output only
  XGpio_SetDataDirection(&LCD_gpios, 1, 0); // all ouputs (0)

  current_gpio_values = 0x0;
  XGpio_DiscreteWrite(&LCD_gpios, 1, 0x0); // disable all LCD pins
  return ZLCD_SUCCESS;
}

static ZLCD_RETURN_STATUS ZLCD_spi_init(void) {
  XSpiPs_Config *config_pointer = NULL;

  config_pointer = XSpiPs_LookupConfig(XPAR_SPI0_BASEADDR);
  if (config_pointer == NULL) {
    return ZLCD_FAILURE;
  }
  XSpiPs_CfgInitialize(&spi_instance, config_pointer,
                       config_pointer->BaseAddress);

  XSpiPs_SetOptions(&spi_instance,
                    (XSPIPS_MASTER_OPTION | XSPIPS_FORCE_SSELECT_OPTION));
  // sets the clock of the SPI peripheral to fastest option (4)
  XSpiPs_SetClkPrescaler(&spi_instance, XSPIPS_CLK_PRESCALE_4);
  return ZLCD_SUCCESS;
}

rgb565 ZLCD_construct_rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  // red 5 MSBs, green 6 MSBs, blue 5 MSBs = 16 bits
  return (rgb565)((red & 0xF8) << 11) | ((green & 0xFC) << 5) | (blue & 0xF8);
}

rgb565 ZLCD_RGB_to_rgb565(uint32_t rgb) {
  rgb &= 0x00FFFFFF; // make sure to take only 24 bits (safety)
  uint8_t red, blue, green;
  red = (rgb >> 16) & 0xFF;
  green = (rgb >> 8) & 0xFF;
  blue = rgb & 0xFF;
  return ZLCD_construct_rgb565(red, green, blue);
}

ZLCD_RETURN_STATUS ZLCD_sleep(void) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (current_sleep_mode == SLEEP_MODE) {
    return ZLCD_SUCCESS;
  }
  /*
  This command causes the LCD module to enter the minimum power consumption
  mode. In this mode the DC/DC converter is stopped, internal oscillator is
  stopped, and panel scanning is stopped. MCU interface and memory are still
  working and the memory keeps its contents.
  */

  ZLCD_send_command(0x10);
  msleep(5);
  current_sleep_mode = SLEEP_MODE;
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS ZLCD_sleep_wake(void) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (current_sleep_mode == SLEEP_OUT_MODE) {
    return ZLCD_SUCCESS;
  }
  ZLCD_send_command(0x11);
  /*
  -It will be necessary to wait 120msec after sending sleep out command (when in
  sleep in mode) before sending an sleepin command.
  */
  msleep(5);
  current_sleep_mode = SLEEP_OUT_MODE;
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS ZLCD_set_orientation(ZLCD_ORIENTATION desired_orientation) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (desired_orientation == current_orientation.orientation_type) {
    return ZLCD_SUCCESS;
  }
  switch (desired_orientation) {
  case ZLCD_PORTRAIT_ORIENTATION:
    current_orientation.horizontal_axis_length_px = ZLCD_WIDTH;
    current_orientation.vertical_axis_length_px = ZLCD_HEIGHT;
    current_transform_fun = pixel_coordinate_to_internal_index_portrait;
    break;
  case ZLCD_INVERTED_PORTRAIT_ORIENTATION:
    current_orientation.horizontal_axis_length_px = ZLCD_WIDTH;
    current_orientation.vertical_axis_length_px = ZLCD_HEIGHT;
    current_transform_fun =
        pixel_coordinate_to_internal_index_inverted_portrait;
    break;
  case ZLCD_LANDSCAPE_ORIENTATION:
    current_orientation.horizontal_axis_length_px = ZLCD_HEIGHT;
    current_orientation.vertical_axis_length_px = ZLCD_WIDTH;
    current_transform_fun = pixel_coordinate_to_internal_index_landscape;
    break;
  case ZLCD_INVERTED_LANDSCAPE_ORIENTATION:
    current_orientation.horizontal_axis_length_px = ZLCD_HEIGHT;
    current_orientation.vertical_axis_length_px = ZLCD_WIDTH;
    current_transform_fun =
        pixel_coordinate_to_internal_index_inverted_landscape;
    break;
  default:
    printf("ERROR: invalid orientation value\nFILE: %s\nLINE: %u\n", __FILE__,
           __LINE__);
    return ZLCD_FAILURE;
  }
  current_orientation.orientation_type =
      desired_orientation; // update current orientation
  printf_x = 0;
  printf_y = printf_font.font_size;
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS ZLCD_init(ZLCD_ORIENTATION desired_orientation,
                             rgb565 background_colour) {
  if (ZLCD_initialized) {
    return ZLCD_SUCCESS;
  }
  if (ZLCD_gpio_init() != ZLCD_SUCCESS) {
    printf("GPIO init failed\n");
    return ZLCD_FAILURE;
  }
  if (ZLCD_spi_init() != ZLCD_SUCCESS) {
    printf("SPI init failed\n");
    return ZLCD_FAILURE;
  }

  uint8_t transmission_data[14] = {0};

  // hard reset using the reset pin
  ZLCD_write_gpio(LCD_RESET, 0); // active low
  // msleep(10);
  ZLCD_write_gpio(LCD_RESET, 1);
  // msleep(20);

  // perform a software reset
  ZLCD_software_reset();

  // Set orientation of the display to portrait (default)
  ZLCD_send_command(0x36); // Memory Data Access Control
  uint8_t mdactl = ST7789_MADCTL_RGB;
  ZLCD_send_data_byte(mdactl);

  ZLCD_send_command(0x3A);   // COLMOD
  ZLCD_send_data_byte(0x55); // 16-bit RGB565 with 65K colours

  // porch setting
  ZLCD_send_command(0xB2); // PORCTRL power on sequence
  transmission_data[0] = 0x0C;
  transmission_data[1] = 0x0C;
  transmission_data[2] = 0x00; // disable separate porch control
  transmission_data[3] = 0x33;
  transmission_data[4] = 0x33;
  ZLCD_send_data(transmission_data, 5);

  // Gate control
  ZLCD_send_command(0xB7);
  ZLCD_send_data_byte(0x35); // power on sequence

  // VCOM setting
  ZLCD_send_command(0xBB);
  ZLCD_send_data_byte(0x35); // 1.425 V

  // LCM control
  ZLCD_send_command(0xC0);
  ZLCD_send_data_byte(0x2C); // power on sequence

  // VDV and VRH Command Enable
  ZLCD_send_command(0xC2);
  // CMDEN=”1”, VDV and VRH register value comes from command write.
  ZLCD_send_data_byte(0x01);
  ZLCD_send_data_byte(0xFF); // stuff byte

  // VRH set
  ZLCD_send_command(0xC3);
  ZLCD_send_data_byte(0x13); // 4.5+( vcom + vcom offset + vdv)

  // VDV set
  ZLCD_send_command(0xC4);
  ZLCD_send_data_byte(0x20); // power on sequence

  // Frame rate control in normal mode
  ZLCD_send_command(0xC6);
  ZLCD_send_data_byte(0x0F); // power on sequence

  // power control 1
  ZLCD_send_command(0xD0);
  // send A4 and A1 for power on sequence
  transmission_data[0] = 0xA4;
  transmission_data[1] = 0xA1;
  ZLCD_send_data(transmission_data, 2);

  // positive voltage gamma control
  ZLCD_send_command(0xE0);
  transmission_data[0] = 0xF0;
  transmission_data[1] = 0x00;
  transmission_data[2] = 0x04;
  transmission_data[3] = 0x04;
  transmission_data[4] = 0x04;
  transmission_data[5] = 0x05;
  transmission_data[6] = 0x29;
  transmission_data[7] = 0x33;
  transmission_data[8] = 0x3E;
  transmission_data[9] = 0x38;
  transmission_data[10] = 0x12;
  transmission_data[11] = 0x12;
  transmission_data[12] = 0x28;
  transmission_data[13] = 0x30;
  ZLCD_send_data(transmission_data, 14);

  // negative voltage gamma control
  ZLCD_send_command(0xE1);
  transmission_data[0] = 0xF0;
  transmission_data[1] = 0x07;
  transmission_data[2] = 0x0A;
  transmission_data[3] = 0x0D;
  transmission_data[4] = 0x0B;
  transmission_data[5] = 0x07;
  transmission_data[6] = 0x28;
  transmission_data[7] = 0x33;
  transmission_data[8] = 0x3E;
  transmission_data[9] = 0x36;
  transmission_data[10] = 0x14;
  transmission_data[11] = 0x14;
  transmission_data[12] = 0x29;
  transmission_data[13] = 0x32;
  ZLCD_send_data(transmission_data, 14);

  ZLCD_send_command(0x21); // Inversion ON (improves image)

  ZLCD_send_command(0x13); // Normal display mode ON

  ZLCD_initialized = true;
  ZLCD_sleep_wake(); // wake from sleep mode (cmd 0x11)
  // msleep(20);

  ZLCD_display_on(); // command 0x29

  // set background colour
  current_orientation.orientation_type = ZLCD_UNKNOWN_ORIENTATION;
  ZLCD_set_orientation(desired_orientation);
  // Force a full refresh by making GRAM_previous differ from the target colour
  for (size_t i = 0; i < ZLCD_WIDTH * ZLCD_HEIGHT; i++) {
    GRAM_previous[2 * i] = (uint8_t) ~(background_colour >> 8);
    GRAM_previous[2 * i + 1] = (uint8_t) ~(background_colour & 0x00FF);
  }
  ZLCD_set_background_colour(background_colour);
  ZLCD_draw_background(); // set pixels and refresh screen
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS ZLCD_display_on(void) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  ZLCD_send_command(0x29); // Display ON
  msleep(10);
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS ZLCD_display_off(void) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  ZLCD_send_command(0x28); // Display OFF
  msleep(10);
  return ZLCD_SUCCESS;
}

static inline void ZLCD_set_pixel_xy_internal(int16_t x, int16_t y,
                                              rgb565 colour) {
  if (x < 0 || x >= current_orientation.horizontal_axis_length_px) {
    return;
  }
  if (y < 0 || y >= current_orientation.vertical_axis_length_px) {
    return;
  }
  size_t index = current_transform_fun(x, y);
  GRAM_current[index] = (uint8_t)(colour >> 8); // MSB first
  GRAM_current[index + 1] = (uint8_t)(colour & 0x00FF);
}

// static void ZLCD_set_pixel_internal(ZLCD_internal_coordinate p, rgb565
// colour) { 	return ZLCD_set_pixel_xy_internal(p.x, p.y, colour);
// }

ZLCD_RETURN_STATUS ZLCD_set_pixel_xy(uint16_t x, uint16_t y, rgb565 colour,
                                     bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }

  // inlined version of the ZLCD_verify_coordinate_is_valid_xy() function
  if (x >= current_orientation.horizontal_axis_length_px) {
    char error_message[100];
    snprintf(error_message, sizeof(error_message) - 1,
             "Error: coordinate %d exceeds maximum allowed value of %d", x,
             current_orientation.horizontal_axis_length_px - 1);
    log_error_message(error_message);
    return ZLCD_FAILURE;
  }
  if (y >= current_orientation.vertical_axis_length_px) {
    char error_message[100];
    snprintf(error_message, sizeof(error_message) - 1,
             "Error: y coordinate %d exceeds maximum allowed value of %d", y,
             current_orientation.vertical_axis_length_px - 1);
    log_error_message(error_message);
    return ZLCD_FAILURE;
  }

  // convert x and y to portrait coordinates
  uint16_t converted_x, converted_y;
  switch (current_orientation.orientation_type) {
  case ZLCD_PORTRAIT_ORIENTATION:
    converted_x = x;
    converted_y = y;
    break;
  case ZLCD_LANDSCAPE_ORIENTATION:
    // 90 degrees clockwise relative to portrait
    converted_x = ZLCD_WIDTH - 1 - (y);
    converted_y = x;
    break;
  case ZLCD_INVERTED_PORTRAIT_ORIENTATION:
    // 180 degrees clockwise relative to portrait
    converted_x = ZLCD_WIDTH - x - 1;
    converted_y = ZLCD_HEIGHT - y - 1;
    break;
  case ZLCD_INVERTED_LANDSCAPE_ORIENTATION:
    // 90 degrees counter-clockwise relative to portrait
    converted_x = y;
    converted_y = (ZLCD_HEIGHT - 1 - x);
    break;
  default:; // for no statement warning
    char error_message[100] = "";
    snprintf(error_message, sizeof(error_message) - 1,
             "ERROR: invalid orientation type %d found",
             current_orientation.orientation_type);
    log_error_message(error_message);
    return ZLCD_FAILURE;
  }
  size_t index = (converted_y * ZLCD_WIDTH + converted_x) * 2;
  GRAM_current[index] = (uint8_t)(colour >> 8); // MSB first
  GRAM_current[index + 1] = (uint8_t)(colour & 0x00FF);

  if (update_now) {
    ZLCD_set_window(ZLCD_X_OFFSET + converted_x, ZLCD_X_OFFSET + converted_x,
                    converted_y, converted_y);
    ZLCD_send_data(&GRAM_current[index], sizeof(rgb565));
    memcpy(&GRAM_previous[index], &GRAM_current[index], sizeof(rgb565));
  }
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS ZLCD_set_pixel(ZLCD_pixel_coordinate coordinate,
                                  rgb565 colour, bool update_now) {
  return ZLCD_set_pixel_xy(coordinate.x, coordinate.y, colour, update_now);
}

// todo: PWM for backlight
// ZLCD_RETURN_STATUS ZLCD_set_brightness(uint16_t brightness) {
//   if (!ZLCD_initialized) {
//     printf("Initialize the LCD before calling other ZLCD functions\n");
//     return ZLCD_ERR_NOT_INITIALIZED;
//   }
//   if (brightness == current_brightness) {
//     return ZLCD_SUCCESS;
//   }
//   // use FPGA fabric to control backlight with PWM instead
//   XGpio_DiscreteWrite(&LCD_backlight_duty_cycle_16_bit, 1, brightness);
//   printf("Reading of backlight: %u\n", (XGpio_DiscreteRead(&LCD_gpios, 1) >>
//   LCD_BACKLIGHT) & 0x1); printf("Reading of duty cycle: %u\n",
//   XGpio_DiscreteRead(&LCD_backlight_duty_cycle_16_bit, 1));
//   current_brightness = brightness;
//   return ZLCD_SUCCESS;
// }

ZLCD_RETURN_STATUS ZLCD_refresh_display(void) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  // see which pages need to be updated by checking the previous GDDRAM buffer
  // state
  bool exit_early = true;
  bool dirty_rows[ZLCD_HEIGHT] = {false};
  for (uint16_t y = 0; y < ZLCD_HEIGHT; y++) {
    size_t row_offset = (ZLCD_WIDTH * y) * sizeof(rgb565);
    if (memcmp(&GRAM_current[row_offset], &GRAM_previous[row_offset],
               ZLCD_WIDTH * sizeof(rgb565)) != 0) {
      exit_early = false;
      dirty_rows[y] = true;
    }
  }
  if (exit_early) {
    return ZLCD_SUCCESS;
  }
  for (uint16_t y = 0; y < ZLCD_HEIGHT; y++) {
    if (!dirty_rows[y])
      continue;
    // calculate the index of the pixel, then multiply by 2
    size_t row_offset = (ZLCD_WIDTH * y) * sizeof(rgb565);
    // send the 172 pixel row
    ZLCD_set_window(ZLCD_X_OFFSET, ZLCD_X_OFFSET + ZLCD_WIDTH - 1, y, y);
    ZLCD_send_data(&GRAM_current[row_offset], ZLCD_WIDTH * sizeof(rgb565));

    memcpy(&GRAM_previous[row_offset], &GRAM_current[row_offset],
           ZLCD_WIDTH * sizeof(rgb565));
  }
  // memcpy(GRAM_previous, GRAM_current, sizeof(GRAM_current)); // flat 108 us
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS ZLCD_verify_coordinate_is_valid_xy(uint16_t x, uint16_t y) {
  uint16_t horizontal_axis_length =
      current_orientation.horizontal_axis_length_px;
  uint16_t vertical_axis_length = current_orientation.vertical_axis_length_px;

  // depends on current orientation set by user
  if (x >= horizontal_axis_length) {
    char error_message[100];
    snprintf(error_message, sizeof(error_message) - 1,
             "Error: coordinate %d exceeds maxium allowed value of %d", x,
             horizontal_axis_length - 1);
    log_error_message(error_message);
    return ZLCD_FAILURE;
  }
  if (y >= vertical_axis_length) {
    char error_message[100];
    snprintf(error_message, sizeof(error_message) - 1,
             "Error: y coordinate %d exceeds maxium allowed value of %d", y,
             vertical_axis_length - 1);
    log_error_message(error_message);
    return ZLCD_FAILURE;
  }
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS
ZLCD_verify_coordinate_is_valid(ZLCD_pixel_coordinate coordinate) {
  return ZLCD_verify_coordinate_is_valid_xy(coordinate.x, coordinate.y);
}

ZLCD_RETURN_STATUS ZLCD_draw_line(ZLCD_pixel_coordinate p1,
                                  ZLCD_pixel_coordinate p2, rgb565 colour,
                                  bool update_now) {
  return ZLCD_draw_line_xy(p1.x, p1.y, p2.x, p2.y, colour, update_now);
}

ZLCD_RETURN_STATUS ZLCD_draw_line_xy(uint16_t x1, uint16_t y1, uint16_t x2,
                                     uint16_t y2, rgb565 colour,
                                     bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (ZLCD_verify_coordinate_is_valid_xy(x1, y1) != ZLCD_SUCCESS) {
    printf("Line point (%u, %u) is not valid\n", x1, y1);
    return ZLCD_FAILURE;
  }
  if (ZLCD_verify_coordinate_is_valid_xy(x2, y2) != ZLCD_SUCCESS) {
    printf("Line point (%u, %u) is not valid\n", x2, y2);
    return ZLCD_FAILURE;
  }
  if (x1 == x2) {
    return ZLCD_draw_vline(x1, y1, y2, colour, update_now);
  } else if (y1 == y2) {
    return ZLCD_draw_hline(y1, x1, x2, colour, update_now);
  }

  // Implement Bresenham's line algorithm

  int dx = abs(x2 - x1);
  int dy = -abs(y2 - y1);
  int sx = x1 < x2 ? 1 : -1;
  int sy = y1 < y2 ? 1 : -1;
  int err = dx + dy;

  while (1) {
    ZLCD_set_pixel_xy_internal(x1, y1, colour);
    if (x1 == x2 && y1 == y2)
      break;

    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x1 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y1 += sy;
    }
  }
  if (update_now) {
    // will send one 172 pixel long row (slow)
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS ZLCD_draw_hline(uint16_t y, uint16_t x1, uint16_t x2,
                                   rgb565 colour, bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (ZLCD_verify_coordinate_is_valid_xy(x1, y) != ZLCD_SUCCESS) {
    printf("Point (x1,y) invalid\n");
    return ZLCD_FAILURE;
  }
  if (ZLCD_verify_coordinate_is_valid_xy(x2, y) != ZLCD_SUCCESS) {
    printf("Point (x2,y) invalid\n");
    return ZLCD_FAILURE;
  }
  ZLCD_draw_hline_internal(y, x1, x2, colour);
  if (update_now) {
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS ZLCD_draw_vline(uint16_t x, uint16_t y1, uint16_t y2,
                                   rgb565 colour, bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (ZLCD_verify_coordinate_is_valid_xy(x, y1) != ZLCD_SUCCESS) {
    printf("Point (x,y1) invalid\n");
    return ZLCD_FAILURE;
  }
  if (ZLCD_verify_coordinate_is_valid_xy(x, y2) != ZLCD_SUCCESS) {
    printf("Point (x,y2) invalid\n");
    return ZLCD_FAILURE;
  }
  ZLCD_draw_vline_internal(x, y1, y2, colour);
  if (update_now) {
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

void ZLCD_set_background_colour(rgb565 background_colour) {
  current_background_colour = background_colour;
}

ZLCD_RETURN_STATUS ZLCD_clear(void) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  ZLCD_ORIENTATION temp = current_orientation.orientation_type;
  // switch to portrait mode briefly
  ZLCD_set_orientation(ZLCD_PORTRAIT_ORIENTATION);
  ZLCD_draw_rectangle_xy_internal(0, 0, ZLCD_WIDTH, ZLCD_HEIGHT, 1, true,
                                  current_background_colour,
                                  current_background_colour);
  // restore user orientation
  ZLCD_set_orientation(temp);
  printf_x = 0;
  printf_y = printf_font.font_size;
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS ZLCD_draw_background(void) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  ZLCD_ORIENTATION temp = current_orientation.orientation_type;
  ZLCD_set_orientation(ZLCD_PORTRAIT_ORIENTATION);
  ZLCD_draw_filled_rectangle_xy(0, 0, ZLCD_WIDTH, ZLCD_HEIGHT, 1,
                                current_background_colour,
                                current_background_colour, true);
  ZLCD_set_orientation(temp);
  printf_x = 0;
  printf_y = printf_font.font_size;
  return ZLCD_SUCCESS;
}

static void ZLCD_draw_hline_internal(int16_t y, int16_t x1, int16_t x2,
                                     rgb565 colour) {
  if (y < 0 || y >= current_orientation.vertical_axis_length_px) {
    return;
  }
  int16_t start = (x1 < x2) ? x1 : x2;
  int16_t end = (x1 > x2) ? x1 : x2;
  // clamp to bounds
  if (start < 0) {
    start = 0;
  }
  if (end >= current_orientation.horizontal_axis_length_px) {
    end = current_orientation.horizontal_axis_length_px - 1;
  }
  size_t start_index = current_transform_fun(start, y);
  uint16_t length = end - start + 1;
  switch (current_orientation.orientation_type) {
  case ZLCD_PORTRAIT_ORIENTATION:
    for (int i = 0; i < length; i++) {
      GRAM_current[start_index + (i * 2)] = (uint8_t)(colour >> 8);
      GRAM_current[start_index + (i * 2) + 1] = (uint8_t)(colour & 0xFF);
    }
    break;

  case ZLCD_LANDSCAPE_ORIENTATION:
    for (int i = 0; i < length; i++) {
      GRAM_current[start_index + (i * ZLCD_WIDTH * 2)] = (uint8_t)(colour >> 8);
      GRAM_current[start_index + (i * ZLCD_WIDTH * 2) + 1] =
          (uint8_t)(colour & 0xFF);
    }
    break;

  case ZLCD_INVERTED_PORTRAIT_ORIENTATION:
    for (int i = 0; i < length; i++) {
      GRAM_current[start_index - (i * 2)] = (uint8_t)(colour >> 8);
      GRAM_current[start_index - (i * 2) + 1] = (uint8_t)(colour & 0xFF);
    }
    break;

  case ZLCD_INVERTED_LANDSCAPE_ORIENTATION:
    for (int i = 0; i < length; i++) {
      GRAM_current[start_index - (i * ZLCD_WIDTH * 2)] = (uint8_t)(colour >> 8);
      GRAM_current[start_index - (i * ZLCD_WIDTH * 2) + 1] =
          (uint8_t)(colour & 0xFF);
    }
    break;
  default:
    break;
  }
}

static void ZLCD_draw_vline_internal(int16_t x, int16_t y1, int16_t y2,
                                     rgb565 colour) {
  if (x < 0 || x >= current_orientation.horizontal_axis_length_px) {
    return;
  }
  int16_t start = (y1 < y2) ? y1 : y2;
  int16_t end = (y1 > y2) ? y1 : y2;

  // clamp to bounds
  if (start < 0) {
    start = 0;
  }
  if (end >= current_orientation.vertical_axis_length_px) {
    end = current_orientation.vertical_axis_length_px - 1;
  }
  size_t start_index = current_transform_fun(x, start);
  uint16_t length = end - start + 1;
  switch (current_orientation.orientation_type) {
  case ZLCD_PORTRAIT_ORIENTATION:
    for (int i = 0; i < length; i++) {
      GRAM_current[start_index + (i * ZLCD_WIDTH * 2)] = (uint8_t)(colour >> 8);
      GRAM_current[start_index + (i * ZLCD_WIDTH * 2) + 1] =
          (uint8_t)(colour & 0xFF);
    }
    break;

  case ZLCD_LANDSCAPE_ORIENTATION:
    for (int i = 0; i < length; i++) {
      GRAM_current[start_index - (i * 2)] = (uint8_t)(colour >> 8);
      GRAM_current[start_index - (i * 2) + 1] = (uint8_t)(colour & 0xFF);
    }
    break;

  case ZLCD_INVERTED_PORTRAIT_ORIENTATION:
    for (int i = 0; i < length; i++) {
      GRAM_current[start_index - (i * ZLCD_WIDTH * 2)] = (uint8_t)(colour >> 8);
      GRAM_current[start_index - (i * ZLCD_WIDTH * 2) + 1] =
          (uint8_t)(colour & 0xFF);
    }
    break;

  case ZLCD_INVERTED_LANDSCAPE_ORIENTATION:
    for (int i = 0; i < length; i++) {
      GRAM_current[start_index + (i * 2)] = (uint8_t)(colour >> 8);
      GRAM_current[start_index + (i * 2) + 1] = (uint8_t)(colour & 0xFF);
    }
    break;
  default:
    break;
  }
}

static void ZLCD_draw_line_xy_internal(int16_t x1, int16_t y1, int16_t x2,
                                       int16_t y2, rgb565 colour) {
  if (x1 == x2) {
    ZLCD_draw_vline_internal(x1, y1, y2, colour);
    return;
  } else if (y1 == y2) {
    ZLCD_draw_hline_internal(y1, x1, x2, colour);
    return;
  }

  // Implement Bresenham's line algorithm

  int dx = abs(x2 - x1);
  int dy = -abs(y2 - y1);
  int sx = x1 < x2 ? 1 : -1;
  int sy = y1 < y2 ? 1 : -1;
  int err = dx + dy;

  while (1) {
    // pixel set function will fail if coordinates are not valid
    ZLCD_set_pixel_xy_internal(x1, y1, colour);
    if (x1 == x2 && y1 == y2)
      break;

    int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x1 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y1 += sy;
    }
  }
}

static void ZLCD_draw_line_internal(ZLCD_internal_coordinate p1,
                                    ZLCD_internal_coordinate p2,
                                    rgb565 colour) {
  ZLCD_draw_line_xy_internal(p1.x, p1.y, p2.x, p2.y, colour);
}

static void
ZLCD_draw_rectangle_xy_internal(uint16_t origin_x, uint16_t origin_y,
                                uint16_t width_px, uint16_t height_px,
                                uint16_t border_thickness_px, bool fill,
                                rgb565 border_colour, rgb565 fill_colour) {
  if (border_thickness_px == 0) {
    // if user enters 0, set thickness to 1
    border_thickness_px = 1;
  }
  if (width_px == 0 || height_px == 0) {
    return;
  }
  if (ZLCD_verify_coordinate_is_valid_xy(origin_x, origin_y) != ZLCD_SUCCESS) {
    return;
  }
  if (fill) {
    for (uint16_t y_current = origin_y; y_current < origin_y + height_px;
         y_current++) {
      ZLCD_draw_hline_internal(y_current, origin_x, origin_x + width_px - 1,
                               fill_colour);
    }
  }
  // Draw borders of thickness border_thickness_px
  for (uint16_t t = 0; t < border_thickness_px; t++) {
    // Top border
    ZLCD_draw_hline_internal(origin_y + t, origin_x, origin_x + width_px - 1,
                             border_colour);
    // Bottom border
    ZLCD_draw_hline_internal(origin_y + height_px - t - 1, origin_x,
                             origin_x + width_px - 1, border_colour);
    // Left border
    ZLCD_draw_vline_internal(origin_x + t, origin_y, origin_y + height_px - 1,
                             border_colour);
    // Right border
    ZLCD_draw_vline_internal(origin_x + width_px - t - 1, origin_y,
                             origin_y + height_px - 1, border_colour);
  }
  return;
}

ZLCD_RETURN_STATUS
ZLCD_draw_unfilled_rectangle(ZLCD_pixel_coordinate origin, uint16_t width_px,
                             uint16_t height_px, uint16_t border_thickness_px,
                             rgb565 border_colour, bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (width_px == 0 || height_px == 0) {
    return ZLCD_FAILURE;
  }
  if (ZLCD_verify_coordinate_is_valid(origin) != ZLCD_SUCCESS) {
    printf("Rectangle origin point must be on the screen\n");
    return ZLCD_FAILURE;
  }
  if ((border_thickness_px >= width_px / 2) ||
      (border_thickness_px >= height_px / 2)) {
    uint16_t smaller_side = (width_px > height_px) ? height_px : width_px;
    printf("border thickness for rectangle is too great. Passed %u but "
           "thickness should not exceed %u\n",
           border_thickness_px, smaller_side / 2);
    return ZLCD_FAILURE;
  }
  ZLCD_draw_rectangle_xy_internal(origin.x, origin.y, width_px, height_px,
                                  border_thickness_px, false, border_colour,
                                  0x0);
  if (update_now) {
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS ZLCD_draw_unfilled_rectangle_xy(
    uint16_t origin_x, uint16_t origin_y, uint16_t width_px, uint16_t height_px,
    uint16_t border_thickness_px, rgb565 border_colour, bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (width_px == 0 || height_px == 0) {
    return ZLCD_FAILURE;
  }
  if (ZLCD_verify_coordinate_is_valid_xy(origin_x, origin_y) != ZLCD_SUCCESS) {
    printf("Rectangle origin point must be on the screen\n");
    return ZLCD_FAILURE;
  }
  if ((border_thickness_px >= width_px / 2) ||
      (border_thickness_px >= height_px / 2)) {
    uint16_t smaller_side = (width_px > height_px) ? height_px : width_px;
    printf("border thickness for rectangle is too great. Passed %u but "
           "thickness should not exceed %u\n",
           border_thickness_px, smaller_side / 2);
    return ZLCD_FAILURE;
  }
  ZLCD_draw_rectangle_xy_internal(origin_x, origin_y, width_px, height_px,
                                  border_thickness_px, false, border_colour,
                                  0x0);
  if (update_now) {
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS
ZLCD_draw_filled_rectangle(ZLCD_pixel_coordinate origin, uint16_t width_px,
                           uint16_t height_px, uint16_t border_thickness_px,
                           rgb565 border_colour, rgb565 fill_colour,
                           bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (width_px == 0 || height_px == 0) {
    return ZLCD_FAILURE;
  }
  if (ZLCD_verify_coordinate_is_valid(origin) != ZLCD_SUCCESS) {
    printf("Rectangle origin point must be on the screen\n");
    return ZLCD_FAILURE;
  }
  if ((border_thickness_px >= width_px / 2) ||
      (border_thickness_px >= height_px / 2)) {
    uint16_t smaller_side = (width_px > height_px) ? height_px : width_px;
    printf("border thickness for rectangle is too great. Passed %u but "
           "thickness should not exceed %u\n",
           border_thickness_px, smaller_side / 2);
    return ZLCD_FAILURE;
  }
  ZLCD_draw_rectangle_xy_internal(origin.x, origin.y, width_px, height_px,
                                  border_thickness_px, true, border_colour,
                                  fill_colour);
  if (update_now) {
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS ZLCD_draw_filled_rectangle_xy(
    uint16_t origin_x, uint16_t origin_y, uint16_t width_px, uint16_t height_px,
    uint16_t border_thickness_px, rgb565 border_colour, rgb565 fill_colour,
    bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (width_px == 0 || height_px == 0) {
    return ZLCD_FAILURE;
  }
  if (ZLCD_verify_coordinate_is_valid_xy(origin_x, origin_y) != ZLCD_SUCCESS) {
    printf("Rectangle origin point must be on the screen\n");
    return ZLCD_FAILURE;
  }
  if ((border_thickness_px >= width_px / 2) ||
      (border_thickness_px >= height_px / 2)) {
    uint16_t smaller_side = (width_px > height_px) ? height_px : width_px;
    printf("border thickness for rectangle is too great. Passed %u but "
           "thickness should not exceed %u\n",
           border_thickness_px, smaller_side / 2);
    return ZLCD_FAILURE;
  }
  ZLCD_draw_rectangle_xy_internal(origin_x, origin_y, width_px, height_px,
                                  border_thickness_px, true, border_colour,
                                  fill_colour);
  if (update_now) {
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

static void fill_bottom_flat_triangle(ZLCD_pixel_coordinate v1,
                                      ZLCD_pixel_coordinate v2,
                                      ZLCD_pixel_coordinate v3, rgb565 colour) {
  // use 16.16 fixed-point
  int32_t dx1 = ((int32_t)v2.x - (int32_t)v1.x) << 16;
  int32_t dx2 = ((int32_t)v3.x - (int32_t)v1.x) << 16;
  int32_t dy = (int32_t)v2.y - (int32_t)v1.y;

  if (dy == 0)
    return; // avoid divide by zero

  int32_t slope_1 = dx1 / dy;
  int32_t slope_2 = dx2 / dy;

  int32_t x1 = ((int32_t)v1.x) << 16;
  int32_t x2 = ((int32_t)v1.x) << 16;

  for (int y = v1.y; y <= v2.y; y++) {
    uint16_t start_x = (uint16_t)(x1 >> 16);
    uint16_t end_x = (uint16_t)(x2 >> 16);

    ZLCD_draw_hline_internal(y, start_x, end_x, colour);

    x1 += slope_1;
    x2 += slope_2;
  }

  return;
}

static void fill_top_flat_triangle(ZLCD_pixel_coordinate v1,
                                   ZLCD_pixel_coordinate v2,
                                   ZLCD_pixel_coordinate v3, rgb565 colour) {
  // use 16.16 fixed-point
  int32_t dx1 = ((int32_t)v3.x - (int32_t)v1.x) << 16;
  int32_t dx2 = ((int32_t)v3.x - (int32_t)v2.x) << 16;
  int32_t dy = (int32_t)v3.y - (int32_t)v1.y;

  if (dy == 0)
    return; // avoid divide by zero

  int32_t slope_1 = dx1 / dy;
  int32_t slope_2 = dx2 / dy;

  int32_t x1 = ((int32_t)v3.x) << 16;
  int32_t x2 = ((int32_t)v3.x) << 16;

  for (int y = v3.y; y > v1.y; y--) {
    uint16_t start_x = (uint16_t)(x1 >> 16);
    uint16_t end_x = (uint16_t)(x2 >> 16);

    ZLCD_draw_hline_internal(y, start_x, end_x, colour);

    x1 -= slope_1; // decrement because we're iterating downward
    x2 -= slope_2;
  }
  return;
}

static ZLCD_RETURN_STATUS
ZLCD_draw_triangle_internal(ZLCD_pixel_coordinate p1, ZLCD_pixel_coordinate p2,
                            ZLCD_pixel_coordinate p3, rgb565 border_colour,
                            bool fill, rgb565 fill_colour, bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (ZLCD_verify_coordinate_is_valid(p1) != ZLCD_SUCCESS) {
    printf("coordinate 1 of triangle is not in screen bounds\n");
    return ZLCD_FAILURE;
  }
  if (ZLCD_verify_coordinate_is_valid(p2) != ZLCD_SUCCESS) {
    printf("coordinate 2 of triangle is not in screen bounds\n");
    return ZLCD_FAILURE;
  }
  if (ZLCD_verify_coordinate_is_valid(p3) != ZLCD_SUCCESS) {
    printf("coordinate 3 of triangle is not in screen bounds\n");
    return ZLCD_FAILURE;
  }
  // sort points by y
  ZLCD_pixel_coordinate tmp;
  if (p2.y < p1.y) {
    tmp = p1;
    p1 = p2;
    p2 = tmp;
  }
  if (p3.y < p1.y) {
    tmp = p1;
    p1 = p3;
    p3 = tmp;
  }
  if (p3.y < p2.y) {
    tmp = p2;
    p2 = p3;
    p3 = tmp;
  }

  if (fill) {
    // handle trivial cases
    if (p2.y == p3.y) {
      fill_bottom_flat_triangle(p1, p2, p3, fill_colour);
    } else if (p1.y == p2.y) {
      fill_top_flat_triangle(p1, p2, p3, fill_colour);
    } else {
      /* general case - split the triangle in a topflat and bottom-flat one */
      ZLCD_pixel_coordinate p4 = {
          .x = (uint16_t)(p1.x + ((float)(p2.y - p1.y) / (float)(p3.y - p1.y)) *
                                     (p3.x - p1.x)),
          .y = p2.y};

      fill_bottom_flat_triangle(p1, p2, p4, fill_colour);
      fill_top_flat_triangle(p2, p4, p3, fill_colour);
    }
  }
  ZLCD_internal_coordinate p1_temp, p2_temp, p3_temp;
  p1_temp.x = p1.x;
  p1_temp.y = p1.y;
  p2_temp.x = p2.x;
  p2_temp.y = p2.y;
  p3_temp.x = p3.x;
  p3_temp.y = p3.y;
  ZLCD_draw_line_internal(p1_temp, p2_temp, border_colour);
  ZLCD_draw_line_internal(p2_temp, p3_temp, border_colour);
  ZLCD_draw_line_internal(p1_temp, p3_temp, border_colour);
  if (update_now) {
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS ZLCD_draw_unfilled_triangle(ZLCD_pixel_coordinate p1,
                                               ZLCD_pixel_coordinate p2,
                                               ZLCD_pixel_coordinate p3,
                                               rgb565 border_colour,
                                               bool update_now) {
  return ZLCD_draw_triangle_internal(p1, p2, p3, border_colour, false, 0x00,
                                     update_now);
}

ZLCD_RETURN_STATUS ZLCD_draw_unfilled_triangle_xy(uint16_t p1x, uint16_t p1y,
                                                  uint16_t p2x, uint16_t p2y,
                                                  uint16_t p3x, uint16_t p3y,
                                                  rgb565 border_colour,
                                                  bool update_now) {
  ZLCD_pixel_coordinate p1, p2, p3;
  p1 = ZLCD_create_coordinate(p1x, p1y);
  p2 = ZLCD_create_coordinate(p2x, p2y);
  p3 = ZLCD_create_coordinate(p3x, p3y);
  return ZLCD_draw_triangle_internal(p1, p2, p3, border_colour, false, 0x00,
                                     update_now);
}

ZLCD_RETURN_STATUS
ZLCD_draw_filled_triangle(ZLCD_pixel_coordinate p1, ZLCD_pixel_coordinate p2,
                          ZLCD_pixel_coordinate p3, rgb565 border_colour,
                          rgb565 fill_colour, bool update_now) {
  return ZLCD_draw_triangle_internal(p1, p2, p3, border_colour, true,
                                     fill_colour, update_now);
}

ZLCD_RETURN_STATUS ZLCD_draw_filled_triangle_xy(
    uint16_t p1x, uint16_t p1y, uint16_t p2x, uint16_t p2y, uint16_t p3x,
    uint16_t p3y, rgb565 border_colour, rgb565 fill_colour, bool update_now) {
  ZLCD_pixel_coordinate p1, p2, p3;
  p1 = ZLCD_create_coordinate(p1x, p1y);
  p2 = ZLCD_create_coordinate(p2x, p2y);
  p3 = ZLCD_create_coordinate(p3x, p3y);
  return ZLCD_draw_triangle_internal(p1, p2, p3, border_colour, true,
                                     fill_colour, update_now);
}

static ZLCD_RETURN_STATUS
ZLCD_draw_circle_xy_internal(uint16_t origin_x, uint16_t origin_y,
                             uint16_t radius_px, rgb565 border_colour,
                             bool fill, rgb565 fill_colour, bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (ZLCD_verify_coordinate_is_valid_xy(origin_x, origin_y) != ZLCD_SUCCESS) {
    printf("Circle origin is not on the screen\n");
    return ZLCD_FAILURE;
  }
  if (radius_px > ZLCD_WIDTH) {
    printf("Circle radius is too large\n");
    return ZLCD_FAILURE;
  }
  int16_t origin_x_signed = origin_x;
  int16_t origin_y_signed = origin_y;
  if (fill) {
    // draw filled part of circle
    int x = 0;
    int y = radius_px;
    int d = 3 - (2 * radius_px);

    while (x <= y) {
      int16_t x_start_1 = origin_x - x;
      int16_t x_start_2 = origin_x - y;
      ZLCD_draw_hline_internal(origin_y_signed - y, x_start_1, origin_x + x,
                               fill_colour);
      ZLCD_draw_hline_internal(origin_y_signed - x, x_start_2, origin_x + y,
                               fill_colour);
      ZLCD_draw_hline_internal(origin_y + x, x_start_2, origin_x + y,
                               fill_colour);
      ZLCD_draw_hline_internal(origin_y + y, x_start_1, origin_x + x,
                               fill_colour);

      if (d < 0) {
        d += (4 * x) + 6;
      } else {
        d += 4 * (x - y) + 10;
        y--;
      }
      x++;
    }
  }

  // draw circle border
  int x = 0;
  int y = radius_px;
  int d = 3 - (2 * radius_px);
  while (x <= y) {
    // draw all 8 symmetric points
    ZLCD_set_pixel_xy_internal(origin_x_signed + x, origin_y_signed + y,
                               border_colour);
    ZLCD_set_pixel_xy_internal(origin_x_signed - x, origin_y_signed + y,
                               border_colour);
    ZLCD_set_pixel_xy_internal(origin_x_signed + x, origin_y_signed - y,
                               border_colour);
    ZLCD_set_pixel_xy_internal(origin_x_signed - x, origin_y_signed - y,
                               border_colour);
    ZLCD_set_pixel_xy_internal(origin_x_signed + y, origin_y_signed + x,
                               border_colour);
    ZLCD_set_pixel_xy_internal(origin_x_signed - y, origin_y_signed + x,
                               border_colour);
    ZLCD_set_pixel_xy_internal(origin_x_signed + y, origin_y_signed - x,
                               border_colour);
    ZLCD_set_pixel_xy_internal(origin_x_signed - y, origin_y_signed - x,
                               border_colour);
    if (d < 0) {
      d += (4 * x) + 6;
    } else {
      d += 4 * (x - y) + 10;
      y--;
    }
    x++;
  }
  if (update_now) {
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS ZLCD_draw_unfilled_circle(ZLCD_pixel_coordinate origin,
                                             uint16_t radius_px,
                                             rgb565 circle_colour,
                                             bool update_now) {
  return ZLCD_draw_circle_xy_internal(origin.x, origin.y, radius_px,
                                      circle_colour, false, 0x0, update_now);
}

ZLCD_RETURN_STATUS ZLCD_draw_unfilled_circle_xy(uint16_t origin_x,
                                                uint16_t origin_y,
                                                uint16_t radius_px,
                                                rgb565 circle_colour,
                                                bool update_now) {
  return ZLCD_draw_circle_xy_internal(origin_x, origin_y, radius_px,
                                      circle_colour, false, 0x0, update_now);
}

ZLCD_RETURN_STATUS ZLCD_draw_filled_circle(ZLCD_pixel_coordinate origin,
                                           uint16_t radius_px,
                                           rgb565 border_colour,
                                           rgb565 fill_colour,
                                           bool update_now) {
  return ZLCD_draw_circle_xy_internal(origin.x, origin.y, radius_px,
                                      border_colour, true, fill_colour,
                                      update_now);
}

ZLCD_RETURN_STATUS
ZLCD_draw_filled_circle_xy(uint16_t origin_x, uint16_t origin_y,
                           uint16_t radius_px, rgb565 border_colour,
                           rgb565 fill_colour, bool update_now) {
  return ZLCD_draw_circle_xy_internal(origin_x, origin_y, radius_px,
                                      border_colour, true, fill_colour,
                                      update_now);
}

ZLCD_ORIENTATION ZLCD_get_orientation(void) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_UNKNOWN_ORIENTATION;
  }
  return current_orientation.orientation_type;
}

ZLCD_RETURN_STATUS ZLCD_draw_char_xy(char character, uint16_t base_x,
                                     uint16_t base_y, rgb565 colour,
                                     const ZLCD_font *f, bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (ZLCD_verify_coordinate_is_valid_xy(base_x, base_y) != ZLCD_SUCCESS) {
    printf("Base coordinate for char draw invalid\n");
    return ZLCD_FAILURE;
  }
  ZLCD_draw_char_xy_internal(character, base_x, base_y, colour, false, 0x00, f);
  if (update_now) {
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS ZLCD_draw_char(char character, ZLCD_pixel_coordinate base,
                                  rgb565 colour, const ZLCD_font *f,
                                  bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (ZLCD_verify_coordinate_is_valid(base) != ZLCD_SUCCESS) {
    printf("Base coordinate for char draw invalid\n");
    return ZLCD_FAILURE;
  }
  ZLCD_draw_char_xy_internal(character, base.x, base.y, colour, false, 0x00, f);
  if (update_now) {
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS ZLCD_draw_char_on_background_xy(
    char character, uint16_t base_x, uint16_t base_y, rgb565 colour,
    rgb565 background_colour, const ZLCD_font *f, bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (ZLCD_verify_coordinate_is_valid_xy(base_x, base_y) != ZLCD_SUCCESS) {
    printf("Base coordinate for char draw invalid\n");
    return ZLCD_FAILURE;
  }
  ZLCD_draw_char_xy_internal(character, base_x, base_y, colour, true,
                             background_colour, f);
  if (update_now) {
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS
ZLCD_draw_char_on_background(char character, ZLCD_pixel_coordinate base,
                             rgb565 colour, rgb565 background_colour,
                             const ZLCD_font *f, bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (ZLCD_verify_coordinate_is_valid(base) != ZLCD_SUCCESS) {
    printf("Base coordinate for char draw invalid\n");
    return ZLCD_FAILURE;
  }
  ZLCD_draw_char_xy_internal(character, base.x, base.y, colour, true,
                             background_colour, f);
  if (update_now) {
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

static void ZLCD_draw_char_xy_internal(char character, uint16_t base_x,
                                       uint16_t base_y, rgb565 colour,
                                       bool draw_background,
                                       rgb565 background_colour,
                                       const ZLCD_font *f) {
  // assume pointers are valid if we've reached this point
  if (character < 32 || character > 127) {
    return;
  }
  // subtract 31 not 32 because of the reserved spot
  const glyph_dsc_t *dsc = &(f->glyph_descriptors[character - 31]);
  const uint8_t *current_character_bitmap =
      &(f->glyph_bitmap[dsc->bitmap_index]);

  int box_w = dsc->box_w;
  int box_h = dsc->box_h;
  int ofs_x = dsc->ofs_x;
  int ofs_y = dsc->ofs_y;

  uint16_t bit_index;
  uint8_t byte_index, bit_offset;

  int glyph_x0 = base_x + ofs_x;
  int glyph_y0 = base_y - box_h - ofs_y;

  if (draw_background) {
    int cell_w = dsc->adv_w >> 4;
    int cell_h = f->font_size;

    int8_t offset_y = dsc->ofs_y;
    for (int y = base_y - offset_y; y > (base_y - cell_h); y--) {
      for (int x = base_x; x < (base_x + cell_w); x++) {
        ZLCD_set_pixel_xy_internal(x, y, background_colour);
      }
    }
  }

  for (int16_t row = 0; row < box_h; row++) {
    for (int16_t column = 0; column < box_w; column++) {
      bit_index = (row * box_w) + column;
      byte_index = bit_index / 8;
      bit_offset = 7 - (bit_index % 8);
      if ((current_character_bitmap[byte_index] >> bit_offset) & 0x1) {
        ZLCD_set_pixel_xy_internal(glyph_x0 + column, glyph_y0 + row, colour);
      }
    }
  }
}

static void ZLCD_print_string_xy_internal(const char *string, uint16_t base_x,
                                          uint16_t base_y, rgb565 colour,
                                          bool draw_background,
                                          rgb565 background_colour,
                                          const ZLCD_font *f) {

  if (string == NULL || f == NULL) {
    return;
  }
  // all characters must be drawable
  for (size_t i = 0; i < strlen(string); i++) {
    char c = *(string + i);
    if (c > 127 || c < 32) {
      // special case: newlines are ok
      if (c == '\n' || c == '\r') {
        continue;
      }
      return;
    }
  }
  const glyph_dsc_t *dsc;
  const char *string_cpy = string;
  int cursor_x = base_x, cursor_y = base_y;

  int8_t y_offset;
  uint16_t text_height = get_font_height(string_cpy, f, &y_offset);

  if (draw_background) {
    int16_t rectangle_length = 0;
    int16_t rectangle_start_x = base_x;
    uint16_t rectangle_start_y = base_y - text_height - y_offset;

    string_cpy = string;
    while (*string_cpy) {
      char c = *(string_cpy++);

      if (c == '\n' || c == '\r') {
        // draw current rectangle and move rectangle start position
        ZLCD_draw_rectangle_xy_internal(rectangle_start_x, rectangle_start_y,
                                        rectangle_length >> 4, text_height, 1,
                                        draw_background, background_colour,
                                        background_colour);
        rectangle_length = 0;
        rectangle_start_y += text_height;
        if (rectangle_start_y >= current_orientation.vertical_axis_length_px) {
          break;
        }
        continue;
      } else {
        dsc = &(f->glyph_descriptors[c - 31]);
        rectangle_length += (dsc->adv_w);
      }
    }
    ZLCD_draw_rectangle_xy_internal(
        rectangle_start_x, rectangle_start_y, rectangle_length >> 4,
        text_height, 1, draw_background, background_colour, background_colour);
  }

  string_cpy = string;
  cursor_x = base_x << 4;
  cursor_y = base_y;
  while (*string_cpy) {
    char c = *(string_cpy++);
    if (c == '\n' || c == '\r') {
      cursor_x = base_x << 4;
      cursor_y += text_height; // move down the screen
      if (cursor_y >= current_orientation.vertical_axis_length_px)
        break;
      continue;
    }
    dsc = &(f->glyph_descriptors[c - 31]);
    ZLCD_draw_char_xy_internal(c, cursor_x >> 4, cursor_y, colour, false,
                               background_colour, f);
    cursor_x += (dsc->adv_w);
  }
}

// right margin indactes number of pixels that will not be touched
static void ZLCD_print_wrapped_string_xy_internal(
    const char *string, uint16_t base_x, uint16_t base_y, uint16_t left_margin,
    uint16_t right_margin, rgb565 colour, bool draw_background,
    rgb565 background_colour, const ZLCD_font *f) {
  if (string == NULL || f == NULL) {
    return;
  }
  if (right_margin >= current_orientation.horizontal_axis_length_px) {
    printf("Right margin too large for wrapped string\n");
    return;
  }
  if (left_margin >= current_orientation.horizontal_axis_length_px) {
    printf("Left margin too large for wrapped string\n");
    return;
  }

  // all characters must be drawable
  for (size_t i = 0; i < strlen(string); i++) {
    char c = *(string + i);
    if (c > 127 || c < 32) {
      // special case: newlines are ok
      if (c == '\n' || c == '\r') {
        continue;
      }
      return;
    }
  }
  const glyph_dsc_t *dsc;
  const char *string_cpy = string;
  int cursor_x = base_x, cursor_y = base_y;

  int8_t y_offset;
  uint16_t text_height = get_font_height(string_cpy, f, &y_offset);

  if (draw_background) {
    string_cpy = string;
    int cursor_x = base_x << 4;
    int cursor_y = base_y;
    int line_start_x = base_x;
    int line_length_px = 0;

    while (*string_cpy) {
      char c = *(string_cpy++);
      if (c == '\n' || c == '\r' ||
          ((cursor_x + f->glyph_descriptors[c - 31].adv_w) >> 4) >=
              (current_orientation.horizontal_axis_length_px - right_margin)) {

        // Draw background for this line
        ZLCD_draw_rectangle_xy_internal(
            line_start_x, cursor_y - text_height - y_offset,
            line_length_px >> 4, text_height, 1, true, background_colour,
            background_colour);

        // Move to next line
        cursor_y += text_height;
        cursor_x = left_margin << 4;
        line_start_x = left_margin;
        line_length_px = 0;
        if (cursor_y >= current_orientation.vertical_axis_length_px)
          break;

        if (c == '\n' || c == '\r')
          continue;
      }

      const glyph_dsc_t *dsc = &(f->glyph_descriptors[c - 31]);
      line_length_px += dsc->adv_w;
      cursor_x += dsc->adv_w;
    }

    // draw background for last line if any text remains
    if (line_length_px > 0) {
      ZLCD_draw_rectangle_xy_internal(
          line_start_x, cursor_y - text_height - y_offset, line_length_px >> 4,
          text_height, 1, true, background_colour, background_colour);
    }
  }

  string_cpy = string;
  cursor_x = base_x << 4;
  cursor_y = base_y;
  while (*string_cpy) {
    char c = *(string_cpy++);
    if (c == '\n' || c == '\r' ||
        ((cursor_x + (f->glyph_descriptors[c - 31].adv_w)) >> 4) >=
            (current_orientation.horizontal_axis_length_px - right_margin)) {
      cursor_y += text_height;
      cursor_x = left_margin << 4;
      if (c == '\n' || c == '\r') {
        continue;
      }
    }
    dsc = &(f->glyph_descriptors[c - 31]);
    ZLCD_draw_char_xy_internal(c, cursor_x >> 4, cursor_y, colour, false,
                               background_colour, f);
    cursor_x += (dsc->adv_w);
  }
}

ZLCD_RETURN_STATUS ZLCD_print_wrapped_string_xy(
    const char *string, uint16_t base_x, uint16_t base_y, uint16_t left_margin,
    uint16_t right_margin, rgb565 colour, const ZLCD_font *f, bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (ZLCD_verify_coordinate_is_valid_xy(base_x, base_y) != ZLCD_SUCCESS) {
    printf("Base coordinate for string write invalid\n");
    return ZLCD_FAILURE;
  }
  ZLCD_print_wrapped_string_xy_internal(string, base_x, base_y, left_margin,
                                        right_margin, colour, false, 0x0, f);
  if (update_now) {
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS
ZLCD_print_wrapped_string(const char *string, ZLCD_pixel_coordinate base,
                          uint16_t left_margin, uint16_t right_margin,
                          rgb565 colour, const ZLCD_font *f, bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (ZLCD_verify_coordinate_is_valid(base) != ZLCD_SUCCESS) {
    printf("Base coordinate for string write invalid\n");
    return ZLCD_FAILURE;
  }
  ZLCD_print_wrapped_string_xy_internal(string, base.x, base.y, left_margin,
                                        right_margin, colour, false, 0x0, f);
  if (update_now) {
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS ZLCD_print_wrapped_string_on_background(
    const char *string, ZLCD_pixel_coordinate base, uint16_t left_margin,
    uint16_t right_margin, rgb565 colour, rgb565 background_colour,
    const ZLCD_font *f, bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (ZLCD_verify_coordinate_is_valid(base) != ZLCD_SUCCESS) {
    printf("Base coordinate for string write invalid\n");
    return ZLCD_FAILURE;
  }
  ZLCD_print_wrapped_string_xy_internal(string, base.x, base.y, left_margin,
                                        right_margin, colour, true,
                                        background_colour, f);
  if (update_now) {
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS ZLCD_print_wrapped_string_on_background_xy(
    const char *string, uint16_t base_x, uint16_t base_y, uint16_t left_margin,
    uint16_t right_margin, rgb565 colour, rgb565 background_colour,
    const ZLCD_font *f, bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (ZLCD_verify_coordinate_is_valid_xy(base_x, base_y) != ZLCD_SUCCESS) {
    printf("Base coordinate for string write invalid\n");
    return ZLCD_FAILURE;
  }
  ZLCD_print_wrapped_string_xy_internal(string, base_x, base_y, left_margin,
                                        right_margin, colour, true,
                                        background_colour, f);
  if (update_now) {
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS ZLCD_print_string_xy(const char *string, uint16_t base_x,
                                        uint16_t base_y, rgb565 colour,
                                        const ZLCD_font *f, bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (ZLCD_verify_coordinate_is_valid_xy(base_x, base_y) != ZLCD_SUCCESS) {
    printf("Base coordinate for string write invalid\n");
    return ZLCD_FAILURE;
  }
  ZLCD_print_string_xy_internal(string, base_x, base_y, colour, false, 0x0, f);
  if (update_now) {
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS ZLCD_print_string(const char *string,
                                     ZLCD_pixel_coordinate base, rgb565 colour,
                                     const ZLCD_font *f, bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (ZLCD_verify_coordinate_is_valid(base) != ZLCD_SUCCESS) {
    printf("Base coordinate for string write invalid\n");
    return ZLCD_FAILURE;
  }
  ZLCD_print_string_xy_internal(string, base.x, base.y, colour, false, 0x0, f);
  if (update_now) {
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS
ZLCD_print_string_on_background(const char *string, ZLCD_pixel_coordinate base,
                                rgb565 colour, rgb565 background_colour,
                                const ZLCD_font *f, bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (ZLCD_verify_coordinate_is_valid(base) != ZLCD_SUCCESS) {
    printf("Base coordinate for string write invalid\n");
    return ZLCD_FAILURE;
  }
  ZLCD_print_string_xy_internal(string, base.x, base.y, colour, true,
                                background_colour, f);
  if (update_now) {
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS
ZLCD_print_string_on_background_xy(const char *string, uint16_t base_x,
                                   uint16_t base_y, rgb565 colour,
                                   rgb565 background_colour, const ZLCD_font *f,
                                   bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (ZLCD_verify_coordinate_is_valid_xy(base_x, base_y) != ZLCD_SUCCESS) {
    printf("Base coordinate for string write invalid\n");
    return ZLCD_FAILURE;
  }

  ZLCD_print_string_xy_internal(string, base_x, base_y, colour, true,
                                background_colour, f);
  if (update_now) {
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS ZLCD_draw_image(ZLCD_pixel_coordinate image_origin,
                                   const ZLCD_image *image, bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (image == NULL) {
    printf("ZLCD_image provided to ZLCD_draw_image is NULL\n");
    return ZLCD_FAILURE;
  }
  if (ZLCD_verify_coordinate_is_valid(image_origin) != ZLCD_SUCCESS) {
    printf("Base coordinate for image draw is invalid\n");
    return ZLCD_FAILURE;
  }
  uint16_t width = image->width;
  uint16_t height = image->height;

  uint16_t offset_x = image->offset_x;
  uint16_t offset_y = image->offset_y;
  if (offset_x >= width || offset_y >= height) {
    printf("Image offset too large (x=%u, y=%u)\n", offset_x, offset_y);
    return ZLCD_FAILURE;
  }

  uint16_t start_x = image_origin.x;
  uint16_t start_y = image_origin.y;

  uint16_t max_x = current_orientation.horizontal_axis_length_px;
  uint16_t max_y = current_orientation.vertical_axis_length_px;

  // clamp draw area to LCD bounds
  uint16_t draw_w = width - offset_x;
  uint16_t draw_h = height - offset_y;
  uint16_t end_x = (start_x + draw_w <= max_x) ? (start_x + draw_w) : max_x;
  uint16_t end_y = (start_y + draw_h <= max_y) ? (start_y + draw_h) : max_y;

  const uint8_t *map = image->map;

  switch (current_orientation.orientation_type) {
  case ZLCD_PORTRAIT_ORIENTATION:
    for (uint16_t y = 0; y < draw_h; y++) {
      const uint8_t *src = &map[((offset_y + y) * width + offset_x) * 2];
      uint8_t *dst = &GRAM_current[((start_y + y) * ZLCD_WIDTH + start_x) * 2];
      for (uint16_t i = 0; i < draw_w; i++) {
        dst[2 * i] = src[2 * i + 1];
        dst[2 * i + 1] = src[2 * i];
      }
    }
    break;

  case ZLCD_LANDSCAPE_ORIENTATION:;
    for (uint16_t y = start_y; y < end_y; y++) {
      size_t LCD_index = (ZLCD_WIDTH - y - 1) * 2;
      size_t image_index = ((offset_y + y) * width + offset_x) * 2;
      for (uint16_t x = start_x; x < end_x; x++) {
        GRAM_current[LCD_index] = map[image_index + 1]; // MSB
        GRAM_current[LCD_index + 1] = map[image_index]; // LSB

        LCD_index += ZLCD_WIDTH * 2;
        image_index += 2;
      }
    }
    break;
  case ZLCD_INVERTED_PORTRAIT_ORIENTATION:
    for (uint16_t y = start_y; y < end_y; y++) {
      size_t LCD_index =
          ((ZLCD_HEIGHT - y - 1) * ZLCD_WIDTH + (ZLCD_WIDTH - 1)) * 2;
      size_t image_index = ((offset_y + y) * width + offset_x) * 2;
      for (uint16_t x = start_x; x < end_x; x++) {
        GRAM_current[LCD_index] = map[image_index + 1]; // MSB
        GRAM_current[LCD_index + 1] = map[image_index]; // LSB
        LCD_index -= 2;
        image_index += 2;
      }
    }
    break;
  case ZLCD_INVERTED_LANDSCAPE_ORIENTATION:
    for (uint16_t y = start_y; y < end_y; y++) {
      size_t LCD_index = ((ZLCD_HEIGHT - 1) * ZLCD_WIDTH + y) * 2;
      size_t image_index = ((offset_y + y) * width + offset_x) * 2;

      for (uint16_t x = start_x; x < end_x; x++) {
        GRAM_current[LCD_index] = map[image_index + 1]; // MSB
        GRAM_current[LCD_index + 1] = map[image_index]; // LSB

        LCD_index -= (ZLCD_WIDTH)*2;
        image_index += 2;
      }
    }
    break;
  default:
    break;
  }
  if (update_now) {
    return ZLCD_refresh_display();
  }
  return ZLCD_SUCCESS;
}

static void ZLCD_print_aligned_string_internal(
    const char *string, uint16_t base_y, ZLCD_TEXT_ALIGNMENT alignment,
    rgb565 colour, bool draw_background, rgb565 background_colour,
    const ZLCD_font *f, bool update_now) {
  switch (alignment) {
  case ZLCD_ALIGN_LEFT:
    ZLCD_print_string_xy_internal(string, 0, base_y, colour, draw_background,
                                  background_colour, f);
    break;
  case ZLCD_ALIGN_CENTER:
  case ZLCD_ALIGN_RIGHT:;
    char string_cpy[256];
    if (strlen(string) >= sizeof(string_cpy)) {
      printf("Warning: Truncating long string for alignment\n");
    }

    strncpy(string_cpy, string, sizeof(string_cpy) - 1);
    string_cpy[sizeof(string_cpy) - 1] = '\0';
    uint16_t string_length_px = 0;

    uint16_t width = current_orientation.horizontal_axis_length_px;
    char *token = strtok(string_cpy, "\n");
    if (token == NULL) {
      for (size_t i = 0; i < strlen(string_cpy); i++) {
        char c = string_cpy[i];
        string_length_px += f->glyph_descriptors[c - 31].adv_w;
      }
      string_length_px >>= 4; // account for scaling factor (16)
      uint16_t unused_px = width - string_length_px;
      uint16_t x_offset = unused_px;
      if (alignment == ZLCD_ALIGN_CENTER) {
        x_offset /= 2;
      }
      ZLCD_print_string_xy_internal(string, x_offset, base_y, colour,
                                    draw_background, background_colour, f);
      break;
    }
    while (token) {
      string_length_px = 0;
      // get pixel length of each substring
      const glyph_dsc_t *dsc;
      int16_t min_y = current_orientation.vertical_axis_length_px - 1;
      int16_t max_y = 0;
      for (size_t i = 0; i < strlen(token); i++) {
        char c = token[i];
        dsc = &(f->glyph_descriptors[c - 31]);
        int16_t glyph_y0 = base_y - dsc->box_h - dsc->ofs_y;
        int16_t glyph_y1 = glyph_y0 + dsc->box_h;

        if (glyph_y0 < min_y)
          min_y = glyph_y0;
        if (glyph_y1 > max_y)
          max_y = glyph_y1;
        string_length_px += f->glyph_descriptors[c - 31].adv_w;
      }
      int16_t text_height = max_y - min_y;
      string_length_px >>= 4; // account for scaling factor (16)

      uint16_t unused_px = width - string_length_px;
      uint16_t x_offset = unused_px;
      if (alignment == ZLCD_ALIGN_CENTER) {
        x_offset /= 2;
      }
      ZLCD_print_string_xy_internal(token, x_offset, base_y, colour,
                                    draw_background, background_colour, f);
      base_y += text_height;
      token = strtok(NULL, "\n");
    }
    if (update_now) {
      ZLCD_refresh_display();
    }
    break;
  default:
    return;
  }
}

ZLCD_RETURN_STATUS ZLCD_print_aligned_string(const char *string,
                                             uint16_t base_y,
                                             ZLCD_TEXT_ALIGNMENT alignment,
                                             rgb565 colour, const ZLCD_font *f,
                                             bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (base_y >= current_orientation.vertical_axis_length_px) {
    printf(
        "String vertical allignment is invalid. Passed %u, must be below %u\n",
        base_y, current_orientation.vertical_axis_length_px);
    return ZLCD_FAILURE;
  }
  if (!string || !f) {
    return ZLCD_FAILURE;
  }
  switch (alignment) {
  case ZLCD_ALIGN_LEFT:
  case ZLCD_ALIGN_CENTER:
  case ZLCD_ALIGN_RIGHT:
    ZLCD_print_aligned_string_internal(string, base_y, alignment, colour, false,
                                       0x0, f, update_now);
    return ZLCD_SUCCESS;
  default:
    printf("Passed in invalid alignment option\n");
    return ZLCD_FAILURE;
  }
  return ZLCD_SUCCESS;
}

ZLCD_RETURN_STATUS
ZLCD_print_aligned_string_on_background(const char *string, uint16_t base_y,
                                        ZLCD_TEXT_ALIGNMENT alignment,
                                        rgb565 colour, rgb565 background_colour,
                                        const ZLCD_font *f, bool update_now) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (base_y >= current_orientation.vertical_axis_length_px) {
    printf(
        "String vertical allignment is invalid. Passed %u, must be below %u\n",
        base_y, current_orientation.vertical_axis_length_px);
    return ZLCD_FAILURE;
  }
  if (!string || !f) {
    return ZLCD_FAILURE;
  }
  switch (alignment) {
  case ZLCD_ALIGN_LEFT:
  case ZLCD_ALIGN_CENTER:
  case ZLCD_ALIGN_RIGHT:
    ZLCD_print_aligned_string_internal(string, base_y, alignment, colour, true,
                                       background_colour, f, update_now);
    return ZLCD_SUCCESS;
  default:
    printf("Passed in invalid alignment option\n");
    return ZLCD_FAILURE;
  }
}

ZLCD_image lvgl_image_to_ZLCD(const lv_image_dsc_t *lv_struct, uint16_t x_off,
                              uint16_t y_off) {
  if (lv_struct == NULL) {
    ZLCD_image empty_img = {0};
    return empty_img;
  }
  ZLCD_image img = {.width = lv_struct->header.w,
                    .height = lv_struct->header.h,
                    .offset_x = x_off,
                    .offset_y = y_off,
                    .data_size = lv_struct->data_size,
                    .map = lv_struct->data};
  return img;
}

ZLCD_font lvgl_font_to_ZLCD(const glyph_dsc_t *lv_struct,
                            const uint8_t *glyph_bitmap, const char *name,
                            size_t font_size) {
  if (lv_struct == NULL || glyph_bitmap == NULL) {
    ZLCD_font empty_font = {0};
    return empty_font;
  }
  ZLCD_font font = {.font_size = font_size,
                    .glyph_descriptors = lv_struct,
                    .glyph_bitmap = glyph_bitmap};
  strncpy(font.font_name, name, sizeof(font.font_name) - 1);
  font.font_name[sizeof(font.font_name) - 1] = '\0';
  return font;
}

static size_t pixel_coordinate_to_internal_index_portrait(uint16_t x,
                                                          uint16_t y) {
  return ((size_t)y * ZLCD_WIDTH + x) * 2;
}

static size_t pixel_coordinate_to_internal_index_inverted_portrait(uint16_t x,
                                                                   uint16_t y) {
  size_t converted_x, converted_y;
  converted_x = ZLCD_WIDTH - x - 1;
  converted_y = ZLCD_HEIGHT - y - 1;
  return (converted_y * ZLCD_WIDTH + converted_x) * 2;
}

static size_t pixel_coordinate_to_internal_index_landscape(uint16_t x,
                                                           uint16_t y) {
  size_t converted_x, converted_y;
  converted_x = ZLCD_WIDTH - y - 1;
  converted_y = x;
  return (converted_y * ZLCD_WIDTH + converted_x) * 2;
}

ZLCD_RETURN_STATUS ZLCD_printf(const char *format, ...) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  char buffer[256];
  buffer[sizeof(buffer) - 1] = '\0';

  if (printf_y >= current_orientation.vertical_axis_length_px) {
    printf_y = printf_font.font_size;
  }
  if (current_printf_mode == ZLCD_PRINTF_MODE_OVERWRITE) {
    printf_x = 0;
  }

  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer) - 1, format, args);
  va_end(args);

  rgb565 fg = ~current_background_colour; // simple bitwise inverse for contrast

  // all characters must be drawable
  for (size_t i = 0; i < strlen(buffer); i++) {
    char c = *(buffer + i);
    if (c > 127 || c < 32) {
      // special case: newlines are ok
      if (c == '\n' || c == '\r') {
        continue;
      }
      printf("ZLCD_printf() string argument must have printable characters\n");
      return ZLCD_FAILURE;
    }
  }
  const glyph_dsc_t *dsc;
  const char *string_cpy = buffer;

  int8_t y_offset = -3;
  uint16_t text_height = printf_font.font_size;
  // uint16_t text_height = get_font_height(string_cpy, &printf_font,
  // &y_offset);

  string_cpy = buffer;
  int cursor_x = printf_x << 4;
  int cursor_y = printf_y;
  int line_start_x = printf_x;
  int line_length_px = 0;

  uint16_t starting_y_value = printf_y;

  while (*string_cpy) {
    char c = *(string_cpy++);
    if (c == '\n' || c == '\r' ||
        ((cursor_x + printf_font.glyph_descriptors[c - 31].adv_w) >> 4) >=
            (current_orientation.horizontal_axis_length_px)) {

      // Draw background for this line
      ZLCD_draw_rectangle_xy_internal(
          line_start_x, cursor_y - text_height - y_offset, line_length_px >> 4,
          text_height, 1, true, current_background_colour,
          current_background_colour);

      // Move to next line
      cursor_y += text_height;
      cursor_x = 0;
      line_start_x = 0;
      line_length_px = 0;
      if (cursor_y >= current_orientation.vertical_axis_length_px) {
        break;
      }
      if (c == '\n' || c == '\r')
        continue;
    }
    const glyph_dsc_t *dsc = &(printf_font.glyph_descriptors[c - 31]);
    line_length_px += dsc->adv_w;
    cursor_x += dsc->adv_w;
  }

  // draw background for last line if any text remains
  if (line_length_px > 0) {
    ZLCD_draw_rectangle_xy_internal(
        line_start_x, cursor_y - text_height - y_offset, line_length_px >> 4,
        text_height, 1, true, current_background_colour,
        current_background_colour);
  }

  string_cpy = buffer;
  cursor_x = printf_x << 4;
  cursor_y = printf_y;
  while (*string_cpy) {
    char c = *(string_cpy++);
    if (c == '\n' || c == '\r' ||
        ((cursor_x + (printf_font.glyph_descriptors[c - 31].adv_w)) >> 4) >=
            (current_orientation.horizontal_axis_length_px)) {
      cursor_y += text_height;
      cursor_x = 0;
      if (c == '\n' || c == '\r') {
        continue;
      }
    }
    dsc = &(printf_font.glyph_descriptors[c - 31]);
    ZLCD_draw_char_xy_internal(c, cursor_x >> 4, cursor_y, fg, false,
                               current_background_colour, &printf_font);
    cursor_x += (dsc->adv_w);
  }
  printf_x = cursor_x >> 4;
  printf_y = cursor_y;
  if (current_printf_mode == ZLCD_PRINTF_MODE_OVERWRITE) {
    printf_x = 0;
    printf_y = starting_y_value;
  }
  return ZLCD_refresh_display();
}

static size_t
pixel_coordinate_to_internal_index_inverted_landscape(uint16_t x, uint16_t y) {
  size_t converted_x, converted_y;
  converted_x = y;
  converted_y = ZLCD_HEIGHT - x - 1;
  return (converted_y * ZLCD_WIDTH + converted_x) * 2;
}

static uint16_t get_font_height(const char *string, const ZLCD_font *f,
                                int8_t *y_offset) {
  if (string == NULL || f == NULL || f->glyph_descriptors == NULL) {
    if (y_offset)
      *y_offset = 0;
    return 0;
  }

  uint8_t max_px_value = 0;
  int8_t min_px_value = INT8_MAX;

  while (*string) {
    char c = *(string++);
    if (c == '\n' || c == '\r')
      continue;

    int index = (int)c - 31;
    const lv_font_fmt_txt_glyph_dsc_t *dsc = &f->glyph_descriptors[index];

    int16_t glyph_over = (int)dsc->box_h + dsc->ofs_y;
    int8_t glyph_under = dsc->ofs_y;

    if (glyph_over > max_px_value) {
      max_px_value = glyph_over;
    }
    if (glyph_under < min_px_value) {
      min_px_value = glyph_under;
    }
  }

  if (y_offset)
    *y_offset = (int8_t)min_px_value;
  return (uint16_t)(max_px_value - min_px_value);
}

ZLCD_RETURN_STATUS ZLCD_set_printf_mode(ZLCD_PRINTF_MODE mode) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  if (mode != ZLCD_PRINTF_MODE_OVERWRITE && mode != ZLCD_PRINTF_MODE_SCROLL) {
    printf("invalid printf mode selected!\n");
    return ZLCD_FAILURE;
  }
  current_printf_mode = mode;
  return ZLCD_SUCCESS;
}

ZLCD_PRINTF_MODE ZLCD_get_printf_mode() {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_PRINTF_MODE_UNKNOWN;
  }
  return current_printf_mode;
}

ZLCD_RETURN_STATUS ZLCD_set_printf_cursor(ZLCD_pixel_coordinate coord) {
  return ZLCD_set_printf_cursor_xy(coord.x, coord.y);
}

ZLCD_RETURN_STATUS ZLCD_set_printf_cursor_xy(uint16_t cursor_x,
                                             uint16_t cursor_y) {
  if (!ZLCD_initialized) {
    printf("Initialize the LCD before calling other ZLCD functions\n");
    return ZLCD_ERR_NOT_INITIALIZED;
  }
  uint16_t max_x = current_orientation.horizontal_axis_length_px - 1;
  uint16_t max_y = current_orientation.vertical_axis_length_px - 1;
  if (cursor_x > max_x || cursor_y > max_y) {
    char msg[120];
    snprintf(msg, sizeof(msg) - 1,
             "Error: cannot set printf cursor to (%hu, %hu) because max bounds "
             "are (%hu, %hu)",
             cursor_x, cursor_y, max_x, max_y);
    log_error_message(msg);
    return ZLCD_FAILURE;
  }
  if (cursor_y < printf_font.font_size) {
    cursor_y = printf_font.font_size;
  }
  printf_x = cursor_x;
  printf_y = cursor_y;
  return ZLCD_SUCCESS;
}