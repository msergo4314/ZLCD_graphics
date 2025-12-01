#pragma once              // optional (??)
#ifndef ZYNQ_LCD_ST7789_H
#define ZYNQ_LCD_ST7789_H
/****************************************************************************
A comprehensive library for drawing on the built in LCD of the Smart Zynq SP
*****************************************************************************/

#include <xgpio.h>  // DC, Reset, and backlight pins from FPGA fabric
#include <sleep.h>  // LCD needs to wait after some commands are issued
#include <stdarg.h> // variable arguments for ZLCD_printf()
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>  // printf
#include <stdlib.h> // snprintf
#include <xil_printf.h>
#include <xil_types.h>
#include <xparameters.h> // base addresses
#include <xspips.h>      // LCD uses SPI communication

#include "lvgl_compat.h" // LVGL compatibility layer

/*******************************************************************************************
NOTE : "ZLCD" refers to the ST7789VW controlled ZJY-LBS147TC-IG01 on the
SmartZynq SP FPGA
********************************************************************************************/

// user orientation types
typedef enum {
  ZLCD_PORTRAIT_ORIENTATION, // HDMI and ethernet ports on the left side of the
                             // board,
  ZLCD_INVERTED_PORTRAIT_ORIENTATION,  // HDMI and ethernet ports on the right
                                       // side of the board
  ZLCD_LANDSCAPE_ORIENTATION,          // Has the ZYNQ below the LCD
  ZLCD_INVERTED_LANDSCAPE_ORIENTATION, // Has the ZYNQ above the LCD
  ZLCD_UNKNOWN_ORIENTATION = -1
} ZLCD_ORIENTATION;

// Function return codes
typedef enum {
  ZLCD_SUCCESS = 0,
  ZLCD_FAILURE = -1,
  ZLCD_ERR_NOT_INITIALIZED = -2 // if user calls functions before the init
} ZLCD_RETURN_STATUS;

typedef struct pixel_coord {
  // the LCD measures 172 x 320 px -- uint16_t is enough for any valid
  // coordinate
  uint16_t x;
  uint16_t y;
} ZLCD_pixel_coordinate;

// todo: support text alignment
typedef enum {
  ZLCD_ALIGN_LEFT,
  ZLCD_ALIGN_CENTER,
  ZLCD_ALIGN_RIGHT
} ZLCD_TEXT_ALIGNMENT;

typedef enum {
  ZLCD_PRINTF_MODE_SCROLL,   // behaves like printf(), moves to next line
  ZLCD_PRINTF_MODE_OVERWRITE, // stays on same line unless '\n' given
  ZLCD_PRINTF_MODE_UNKNOWN = -1
} ZLCD_PRINTF_MODE;

/****************************************************
Use LVGL format to import fonts easily
Download fonts from a .ttf file using  https://www.dafont.com/
and convert to C arrays using https://lvgl.io/tools/fontconverter
Make sure to use the character range 32-127 for printable characters
****************************************************/

typedef struct {
  char font_name[31]; // might as well use 31 bytes due to padding
  uint8_t font_size;
  // roughly the height from the top of ascenders (like “h”)
  // to the bottom of descenders (like “g”), measured in pixels
  const uint8_t *glyph_bitmap;
  const glyph_dsc_t *glyph_descriptors;
} ZLCD_font;

ZLCD_font lvgl_font_to_ZLCD(const glyph_dsc_t *lv_struct,
                            const uint8_t *glyph_bitmap, const char *name,
                            size_t font_size);

/******************************************
Steps for displaying any image on the LCD:

1 - downsacle the resolution to something workable for the LCD
    The LCD can support up to 172x320 px. Pictures larger than
    that will not be fully visible and will need to be vertically
    or horizontally offset manually. Use:
    https://www.imageresizer.work/resize-image-in-pixel
    or:
    https://image.online-convert.com/convert-to-bmp for BMP images

2 - Use the LVGL converter to convert the image to C array which can
    be copied into a header directly. This will convert any compatible
    image format (jpeg, png, svg, etc.) into a single array of bytes.
    Be sure to select the option for RGB565 encoding. Use:
    https://lvgl.io/tools/imageconverter

*******************************************/

typedef struct {
  uint16_t width;
  uint16_t height;
  size_t data_size;
  const uint8_t *map;
  uint16_t offset_x; // where to start drawing the image x (relative to left)
  uint16_t offset_y; // where to start drawing the image y (relative to top)
} ZLCD_image;

// function to convert lv_image_dsc_t to ZLCD_image
ZLCD_image lvgl_image_to_ZLCD(const lv_image_dsc_t *lv_struct, uint16_t x_off,
                              uint16_t y_off);

/*
rgb565 is a 16-bit RGB encoding which saves space compared to true 24-bit RGB.
Since the human eye is more sensitive to green, the green pixel value can range
from 0-63 instead of 0-31

Bit index:  15         11 10          5 4         0
            [ R R R R R ][ G G G G G G ][ B B B B B ]
*/

typedef uint16_t rgb565;
#define RGB565(r, g, b) ((((r)&0x1F) << 11) | (((g)&0x3F) << 5) | ((b)&0x1F))

// LCD pixel dimensions for vertical (portrait) orientation
#define ZLCD_WIDTH (uint16_t)172  // in pixels
#define ZLCD_HEIGHT (uint16_t)320 // in pixels

// marco for error checking ZLCD functions that return @ZLCD_RETURN_STATUS
#define ZLCD_ERROR_CHECK(call)                                                 \
  do {                                                                         \
    if ((call) != ZLCD_SUCCESS) {                                              \
      fprintf(stderr, "ZLCD error at %s:%d\n", __FILE__, __LINE__);            \
      abort();                                                                 \
    }                                                                          \
  } while (0)

// RGB565 color definitions
#define WHITE 0xFFFF
#define BLACK 0x0000
#define RED 0xF800
#define GREEN 0x07E0
#define BLUE 0x001F
#define CYAN 0x07FF
#define MAGENTA 0xF81F
#define YELLOW 0xFFE0
#define ORANGE 0xFB00
#define PURPLE 0xC819
#define BROWN 0x59A3
#define GRAY 0x8410
#define LIGHT_BLUE 0x069F
#define HOT_PINK 0xF811
#define TURQUOISE 0x1CD0
#define NAVY_GREEN 0x3286

void ZLCD_change_pixel_coordinate(ZLCD_pixel_coordinate *coordinate,
                                  uint16_t new_x, uint16_t new_y);
ZLCD_pixel_coordinate ZLCD_create_coordinate(uint16_t x, uint16_t y);

rgb565 ZLCD_construct_rgb565(uint8_t red, uint8_t green, uint8_t blue);
rgb565 ZLCD_RGB_to_rgb565(uint32_t rgb);

ZLCD_RETURN_STATUS ZLCD_init(ZLCD_ORIENTATION desired_orientation,
                             rgb565 background_colour);
ZLCD_ORIENTATION ZLCD_get_orientation(void);
ZLCD_RETURN_STATUS ZLCD_set_pixel(ZLCD_pixel_coordinate coordinate,
                                  rgb565 colour, bool update_now);
ZLCD_RETURN_STATUS ZLCD_set_pixel_xy(uint16_t x, uint16_t y, rgb565 colour,
                                     bool update_now);
ZLCD_RETURN_STATUS ZLCD_set_orientation(ZLCD_ORIENTATION desired_orientation);
void ZLCD_set_background_colour(rgb565 background_colour);
/*
draws the background in RAM but does not send the pixel data to the LCD
*/
ZLCD_RETURN_STATUS ZLCD_clear(void);
ZLCD_RETURN_STATUS ZLCD_refresh_display(void);
ZLCD_RETURN_STATUS
ZLCD_verify_coordinate_is_valid(ZLCD_pixel_coordinate coordinate);
ZLCD_RETURN_STATUS ZLCD_verify_coordinate_is_valid_xy(uint16_t x, uint16_t y);
ZLCD_RETURN_STATUS ZLCD_draw_line(ZLCD_pixel_coordinate p1,
                                  ZLCD_pixel_coordinate p2, rgb565 colour,
                                  bool update_now);
ZLCD_RETURN_STATUS ZLCD_draw_line_xy(uint16_t x1, uint16_t y1, uint16_t x2,
                                     uint16_t y2, rgb565 colour,
                                     bool update_now);
ZLCD_RETURN_STATUS ZLCD_draw_hline(uint16_t y, uint16_t x1, uint16_t x2,
                                   rgb565 colour, bool update_now);
ZLCD_RETURN_STATUS ZLCD_draw_vline(uint16_t x, uint16_t y1, uint16_t y2,
                                   rgb565 colour, bool update_now);
ZLCD_RETURN_STATUS
ZLCD_draw_unfilled_rectangle(ZLCD_pixel_coordinate origin, uint16_t width_px,
                             uint16_t height_px, uint16_t border_thickness_px,
                             rgb565 border_colour, bool update_now);
ZLCD_RETURN_STATUS ZLCD_draw_unfilled_rectangle_xy(
    uint16_t origin_x, uint16_t origin_y, uint16_t width_px, uint16_t height_px,
    uint16_t border_thickness_px, rgb565 border_colour, bool update_now);

ZLCD_RETURN_STATUS
ZLCD_draw_filled_rectangle(ZLCD_pixel_coordinate origin, uint16_t width_px,
                           uint16_t height_px, uint16_t border_thickness_px,
                           rgb565 border_colour, rgb565 fill_colour,
                           bool update_now);
ZLCD_RETURN_STATUS ZLCD_draw_filled_rectangle_xy(
    uint16_t origin_x, uint16_t origin_y, uint16_t width_px, uint16_t height_px,
    uint16_t border_thickness_px, rgb565 border_colour, rgb565 fill_colour,
    bool update_now);

ZLCD_RETURN_STATUS ZLCD_draw_unfilled_triangle(ZLCD_pixel_coordinate p1,
                                               ZLCD_pixel_coordinate p2,
                                               ZLCD_pixel_coordinate p3,
                                               rgb565 border_colour,
                                               bool update_now);
ZLCD_RETURN_STATUS ZLCD_draw_unfilled_triangle_xy(uint16_t p1x, uint16_t p1y,
                                                  uint16_t p2x, uint16_t p2y,
                                                  uint16_t p3x, uint16_t p3y,
                                                  rgb565 border_colour,
                                                  bool update_now);

ZLCD_RETURN_STATUS
ZLCD_draw_filled_triangle(ZLCD_pixel_coordinate p1, ZLCD_pixel_coordinate p2,
                          ZLCD_pixel_coordinate p3, rgb565 border_colour,
                          rgb565 fill_colour, bool update_now);

ZLCD_RETURN_STATUS ZLCD_draw_filled_triangle_xy(
    uint16_t p1x, uint16_t p1y, uint16_t p2x, uint16_t p2y, uint16_t p3x,
    uint16_t p3y, rgb565 border_colour, rgb565 fill_colour, bool update_now);

ZLCD_RETURN_STATUS ZLCD_draw_unfilled_circle(ZLCD_pixel_coordinate origin,
                                             uint16_t radius_px,
                                             rgb565 circle_colour,
                                             bool update_now);

ZLCD_RETURN_STATUS ZLCD_draw_unfilled_circle_xy(uint16_t origin_x,
                                                uint16_t origin_y,
                                                uint16_t radius_px,
                                                rgb565 circle_colour,
                                                bool update_now);

ZLCD_RETURN_STATUS ZLCD_draw_filled_circle(ZLCD_pixel_coordinate origin,
                                           uint16_t radius_px,
                                           rgb565 border_colour,
                                           rgb565 fill_colour, bool update_now);

ZLCD_RETURN_STATUS
ZLCD_draw_filled_circle_xy(uint16_t origin_x, uint16_t origin_y,
                           uint16_t radius_px, rgb565 border_colour,
                           rgb565 fill_colour, bool update_now);

ZLCD_RETURN_STATUS ZLCD_draw_char_xy(char character, uint16_t base_x,
                                     uint16_t base_y, rgb565 colour,
                                     const ZLCD_font *f, bool update_now);

ZLCD_RETURN_STATUS
ZLCD_draw_char_on_background(char character, ZLCD_pixel_coordinate base,
                             rgb565 colour, rgb565 background_colour,
                             const ZLCD_font *f, bool update_now);

ZLCD_RETURN_STATUS ZLCD_draw_char_on_background_xy(
    char character, uint16_t base_x, uint16_t base_y, rgb565 colour,
    rgb565 background_colour, const ZLCD_font *f, bool update_now);

// note that the string will be printed ABOVE base y
ZLCD_RETURN_STATUS ZLCD_print_string(const char *string,
                                     ZLCD_pixel_coordinate base, rgb565 colour,
                                     const ZLCD_font *f, bool update_now);

// note that the string will be printed ABOVE base y
ZLCD_RETURN_STATUS ZLCD_print_string_xy(const char *string, uint16_t base_x,
                                        uint16_t base_y, rgb565 colour,
                                        const ZLCD_font *f, bool update_now);

// note that the string will be printed ABOVE base y
ZLCD_RETURN_STATUS
ZLCD_print_string_on_background(const char *string, ZLCD_pixel_coordinate base,
                                rgb565 colour, rgb565 background_colour,
                                const ZLCD_font *f, bool update_now);

ZLCD_RETURN_STATUS
ZLCD_print_string_on_background_xy(const char *string, uint16_t base_x,
                                   uint16_t base_y, rgb565 colour,
                                   rgb565 background_colour, const ZLCD_font *f,
                                   bool update_now);

ZLCD_RETURN_STATUS ZLCD_draw_image(ZLCD_pixel_coordinate image_origin,
                                   const ZLCD_image *image, bool update_now);

ZLCD_RETURN_STATUS ZLCD_print_aligned_string(const char *string,
                                             uint16_t base_y,
                                             ZLCD_TEXT_ALIGNMENT alignment,
                                             rgb565 colour, const ZLCD_font *f,
                                             bool update_now);

ZLCD_RETURN_STATUS
ZLCD_print_aligned_string_on_background(const char *string, uint16_t base_y,
                                        ZLCD_TEXT_ALIGNMENT alignment,
                                        rgb565 colour, rgb565 background_colour,
                                        const ZLCD_font *f, bool update_now);

ZLCD_RETURN_STATUS ZLCD_print_wrapped_string_xy(
    const char *string, uint16_t base_x, uint16_t base_y, uint16_t left_margin,
    uint16_t right_margin, rgb565 colour, const ZLCD_font *f, bool update_now);

ZLCD_RETURN_STATUS
ZLCD_print_wrapped_string(const char *string, ZLCD_pixel_coordinate base,
                          uint16_t left_margin, uint16_t right_margin,
                          rgb565 colour, const ZLCD_font *f, bool update_now);

ZLCD_RETURN_STATUS ZLCD_print_wrapped_string_on_background(
    const char *string, ZLCD_pixel_coordinate base, uint16_t left_margin,
    uint16_t right_margin, rgb565 colour, rgb565 background_colour,
    const ZLCD_font *f, bool update_now);

ZLCD_RETURN_STATUS ZLCD_print_wrapped_string_on_background_xy(
    const char *string, uint16_t base_x, uint16_t base_y, uint16_t left_margin,
    uint16_t right_margin, rgb565 colour, rgb565 background_colour,
    const ZLCD_font *f, bool update_now);

ZLCD_RETURN_STATUS ZLCD_sleep(void);
/*
draws the background and clears the screen right away
*/
ZLCD_RETURN_STATUS ZLCD_draw_background(void);
ZLCD_RETURN_STATUS ZLCD_sleep_wake(void);
ZLCD_RETURN_STATUS ZLCD_display_on(void);
ZLCD_RETURN_STATUS ZLCD_display_off(void);
ZLCD_RETURN_STATUS ZLCD_printf(const char *format, ...);
ZLCD_RETURN_STATUS ZLCD_set_printf_mode(ZLCD_PRINTF_MODE mode);
ZLCD_RETURN_STATUS ZLCD_set_printf_cursor(ZLCD_pixel_coordinate coord);
ZLCD_RETURN_STATUS ZLCD_set_printf_cursor_xy(uint16_t cursor_x, uint16_t cursor_y);
ZLCD_PRINTF_MODE ZLCD_get_printf_mode(void);
// print to both stdout and the LCD
void print_output(const char *fmt, ...);

/*
read a BMP file and return a ZLCD_image with the relevant data
*/
ZLCD_image ZLCD_read_BMP(const u8 *BMP_data, size_t BMP_data_length, u8 *map_destination_arr, size_t map_destination_size);

// font used by ZLCD_printf() should always be included - uses ~1.6 Kb of RAM

// the font is defined in the .c file
extern const ZLCD_font printf_font;

static LV_ATTRIBUTE_LARGE_CONST const uint8_t printf_bmp[] = {
    /* U+0020 " " */
    0x0,

    /* U+0021 "!" */
    0xfc, 0x80,

    /* U+0022 "\"" */
    0x99, 0x99,

    /* U+0023 "#" */
    0x24, 0x48, 0x93, 0xf2, 0x89, 0x3f, 0xa4, 0x48,

    /* U+0024 "$" */
    0x75, 0x69, 0xc7, 0x16, 0xb5, 0x71, 0x0,

    /* U+0025 "%" */
    0xe5, 0x4a, 0xa5, 0x8f, 0x5, 0xca, 0xa5, 0x4e,

    /* U+0026 "&" */
    0x31, 0x24, 0x9c, 0x66, 0x99, 0xa2, 0x74,

    /* U+0027 "'" */
    0xf0,

    /* U+0028 "(" */
    0x29, 0x49, 0x24, 0x89, 0x10,

    /* U+0029 ")" */
    0x89, 0x12, 0x49, 0x29, 0x40,

    /* U+002A "*" */
    0x25, 0x5c, 0xa1, 0x0,

    /* U+002B "+" */
    0x21, 0x3e, 0x42, 0x10,

    /* U+002C "," */
    0xea,

    /* U+002D "-" */
    0xe0,

    /* U+002E "." */
    0xc0,

    /* U+002F "/" */
    0x8, 0x21, 0x4, 0x20, 0x84, 0x30, 0x80,

    /* U+0030 "0" */
    0x79, 0x28, 0x61, 0xa6, 0x18, 0x52, 0x78,

    /* U+0031 "1" */
    0x10, 0xcd, 0x4, 0x10, 0x41, 0x4, 0xfc,

    /* U+0032 "2" */
    0x39, 0x14, 0x41, 0x8, 0x42, 0x10, 0xfc,

    /* U+0033 "3" */
    0x74, 0x62, 0x13, 0xe, 0x31, 0x70,

    /* U+0034 "4" */
    0x8, 0x62, 0x8a, 0x4a, 0x2f, 0xc2, 0x8,

    /* U+0035 "5" */
    0xfc, 0x21, 0xe9, 0x84, 0x33, 0x70,

    /* U+0036 "6" */
    0x72, 0x61, 0x6c, 0xc6, 0x39, 0x70,

    /* U+0037 "7" */
    0xf8, 0x44, 0x62, 0x11, 0x8, 0x40,

    /* U+0038 "8" */
    0x74, 0x63, 0x17, 0x46, 0x31, 0x70,

    /* U+0039 "9" */
    0x74, 0xe3, 0x18, 0xbc, 0x32, 0x70,

    /* U+003A ":" */
    0xc6,

    /* U+003B ";" */
    0x50, 0x15, 0xa0,

    /* U+003C "<" */
    0x0, 0x37, 0x20, 0xe0, 0x60, 0x40,

    /* U+003D "=" */
    0xfc, 0x0, 0x3f,

    /* U+003E ">" */
    0x3, 0x83, 0x81, 0x1d, 0x88, 0x0,

    /* U+003F "?" */
    0x7b, 0x18, 0x41, 0x8, 0x42, 0x0, 0x20,

    /* U+0040 "@" */
    0x38, 0x89, 0xed, 0x5c, 0xb9, 0x72, 0xef, 0xfc,
    0x88, 0xe0,

    /* U+0041 "A" */
    0x10, 0x70, 0xa1, 0x46, 0xc8, 0x9f, 0x63, 0x82,

    /* U+0042 "B" */
    0xf2, 0x28, 0xa2, 0xf2, 0x38, 0x61, 0xf8,

    /* U+0043 "C" */
    0x79, 0x28, 0x20, 0x82, 0x8, 0x52, 0x78,

    /* U+0044 "D" */
    0xf2, 0x28, 0x61, 0x86, 0x18, 0x62, 0xf0,

    /* U+0045 "E" */
    0xfa, 0x8, 0x20, 0xfa, 0x8, 0x20, 0xfc,

    /* U+0046 "F" */
    0xfc, 0x21, 0x8, 0x7e, 0x10, 0x80,

    /* U+0047 "G" */
    0x79, 0x38, 0x20, 0x9e, 0x18, 0x51, 0x38,

    /* U+0048 "H" */
    0x8c, 0x63, 0x1f, 0xc6, 0x31, 0x88,

    /* U+0049 "I" */
    0xf9, 0x8, 0x42, 0x10, 0x84, 0xf8,

    /* U+004A "J" */
    0x38, 0x42, 0x10, 0x84, 0x29, 0x70,

    /* U+004B "K" */
    0x8a, 0x6b, 0x28, 0xe2, 0xc9, 0x22, 0x8c,

    /* U+004C "L" */
    0x84, 0x21, 0x8, 0x42, 0x10, 0xf8,

    /* U+004D "M" */
    0xcf, 0x3c, 0xed, 0xb6, 0xd8, 0x61, 0x84,

    /* U+004E "N" */
    0x8e, 0x73, 0x5a, 0xd6, 0x73, 0x98,

    /* U+004F "O" */
    0x79, 0x28, 0x61, 0x86, 0x18, 0x52, 0x78,

    /* U+0050 "P" */
    0xfa, 0x38, 0x61, 0x8f, 0xe8, 0x20, 0x80,

    /* U+0051 "Q" */
    0x79, 0x28, 0x61, 0x86, 0x18, 0x73, 0x78, 0x41,
    0x3,

    /* U+0052 "R" */
    0xfa, 0x18, 0x61, 0xfa, 0x48, 0xa2, 0x84,

    /* U+0053 "S" */
    0x7a, 0x38, 0x30, 0x38, 0x18, 0x61, 0x78,

    /* U+0054 "T" */
    0xfe, 0x20, 0x40, 0x81, 0x2, 0x4, 0x8, 0x10,

    /* U+0055 "U" */
    0x8c, 0x63, 0x18, 0xc6, 0x31, 0x70,

    /* U+0056 "V" */
    0x82, 0x8d, 0x12, 0x22, 0x45, 0xa, 0xc, 0x10,

    /* U+0057 "W" */
    0x83, 0x6, 0xe, 0x95, 0xad, 0x9b, 0x36, 0x64,

    /* U+0058 "X" */
    0x44, 0xc8, 0xa0, 0xc1, 0x5, 0xb, 0x22, 0x42,

    /* U+0059 "Y" */
    0xc6, 0x88, 0xa1, 0x41, 0x2, 0x4, 0x8, 0x10,

    /* U+005A "Z" */
    0xfc, 0x30, 0x84, 0x30, 0x84, 0x30, 0xfc,

    /* U+005B "[" */
    0xf2, 0x49, 0x24, 0x92, 0x70,

    /* U+005C "\\" */
    0x82, 0x4, 0x8, 0x20, 0x41, 0x2, 0x8,

    /* U+005D "]" */
    0xe4, 0x92, 0x49, 0x24, 0xf0,

    /* U+005E "^" */
    0x22, 0x94, 0xa8, 0xc4,

    /* U+005F "_" */
    0xfe,

    /* U+0060 "`" */
    0xc8,

    /* U+0061 "a" */
    0x73, 0x20, 0x9e, 0x8a, 0x6e, 0xc0,

    /* U+0062 "b" */
    0x84, 0x2d, 0x98, 0xc6, 0x31, 0xf0,

    /* U+0063 "c" */
    0x76, 0x61, 0x8, 0x65, 0xc0,

    /* U+0064 "d" */
    0x8, 0x5b, 0x38, 0xc6, 0x33, 0x68,

    /* U+0065 "e" */
    0x73, 0x28, 0xbe, 0x83, 0x27, 0x80,

    /* U+0066 "f" */
    0x3c, 0x8f, 0xc8, 0x20, 0x82, 0x8, 0x20,

    /* U+0067 "g" */
    0x6c, 0xe3, 0x18, 0xcd, 0xa1, 0xcb, 0x80,

    /* U+0068 "h" */
    0x84, 0x2d, 0x98, 0xc6, 0x31, 0x88,

    /* U+0069 "i" */
    0x20, 0x38, 0x42, 0x10, 0x84, 0xf8,

    /* U+006A "j" */
    0x10, 0x71, 0x11, 0x11, 0x11, 0x1e,

    /* U+006B "k" */
    0x84, 0x27, 0x2a, 0x72, 0x92, 0x88,

    /* U+006C "l" */
    0xe1, 0x8, 0x42, 0x10, 0x84, 0xf8,

    /* U+006D "m" */
    0xef, 0x26, 0x4c, 0x99, 0x32, 0x64, 0x80,

    /* U+006E "n" */
    0xb6, 0x63, 0x18, 0xc6, 0x20,

    /* U+006F "o" */
    0x7b, 0x38, 0x61, 0x87, 0x37, 0x80,

    /* U+0070 "p" */
    0xb6, 0x63, 0x18, 0xc7, 0xd0, 0x84, 0x0,

    /* U+0071 "q" */
    0x6c, 0xe3, 0x18, 0xcd, 0xa1, 0x8, 0x40,

    /* U+0072 "r" */
    0xbe, 0x21, 0x8, 0x42, 0x0,

    /* U+0073 "s" */
    0x74, 0x60, 0xe0, 0xc7, 0xc0,

    /* U+0074 "t" */
    0x42, 0x3e, 0x84, 0x21, 0x8, 0x78,

    /* U+0075 "u" */
    0x8c, 0x63, 0x18, 0xcd, 0xa0,

    /* U+0076 "v" */
    0x46, 0x89, 0x11, 0x62, 0x85, 0x4, 0x0,

    /* U+0077 "w" */
    0x83, 0x5, 0x4b, 0x56, 0xcd, 0x99, 0x0,

    /* U+0078 "x" */
    0x89, 0x45, 0x8, 0x51, 0x68, 0x80,

    /* U+0079 "y" */
    0x46, 0x89, 0x11, 0x62, 0x83, 0x4, 0x8, 0x30,
    0xc0,

    /* U+007A "z" */
    0xf8, 0xc4, 0x44, 0x63, 0xe0,

    /* U+007B "{" */
    0x39, 0x8, 0x42, 0x60, 0x84, 0x21, 0x8, 0x30,

    /* U+007C "|" */
    0xff, 0xf0,

    /* U+007D "}" */
    0xe1, 0x8, 0x42, 0xc, 0x84, 0x21, 0x9, 0x80,

    /* U+007E "~" */
    0xe0, 0x70
};

static const lv_font_fmt_txt_glyph_dsc_t printf_dsc[] = {
    {.bitmap_index = 0, .adv_w = 0, .box_w = 0, .box_h = 0, .ofs_x = 0, .ofs_y = 0} /* id = 0 reserved */,
    {.bitmap_index = 0, .adv_w = 115, .box_w = 1, .box_h = 1, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 1, .adv_w = 115, .box_w = 1, .box_h = 9, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 3, .adv_w = 115, .box_w = 4, .box_h = 4, .ofs_x = 2, .ofs_y = 5},
    {.bitmap_index = 5, .adv_w = 115, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 13, .adv_w = 115, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -1},
    {.bitmap_index = 20, .adv_w = 115, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 28, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 35, .adv_w = 115, .box_w = 1, .box_h = 4, .ofs_x = 3, .ofs_y = 5},
    {.bitmap_index = 36, .adv_w = 115, .box_w = 3, .box_h = 12, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 41, .adv_w = 115, .box_w = 3, .box_h = 12, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 46, .adv_w = 115, .box_w = 5, .box_h = 5, .ofs_x = 1, .ofs_y = 4},
    {.bitmap_index = 50, .adv_w = 115, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 54, .adv_w = 115, .box_w = 2, .box_h = 4, .ofs_x = 2, .ofs_y = -2},
    {.bitmap_index = 55, .adv_w = 115, .box_w = 3, .box_h = 1, .ofs_x = 2, .ofs_y = 3},
    {.bitmap_index = 56, .adv_w = 115, .box_w = 1, .box_h = 2, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 57, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 64, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 71, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 78, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 85, .adv_w = 115, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 91, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 98, .adv_w = 115, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 104, .adv_w = 115, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 110, .adv_w = 115, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 116, .adv_w = 115, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 122, .adv_w = 115, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 128, .adv_w = 115, .box_w = 1, .box_h = 7, .ofs_x = 3, .ofs_y = 0},
    {.bitmap_index = 129, .adv_w = 115, .box_w = 2, .box_h = 10, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 132, .adv_w = 115, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 138, .adv_w = 115, .box_w = 6, .box_h = 4, .ofs_x = 1, .ofs_y = 2},
    {.bitmap_index = 141, .adv_w = 115, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 1},
    {.bitmap_index = 147, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 154, .adv_w = 115, .box_w = 7, .box_h = 11, .ofs_x = 1, .ofs_y = -2},
    {.bitmap_index = 164, .adv_w = 115, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 172, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 179, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 186, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 193, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 200, .adv_w = 115, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 206, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 213, .adv_w = 115, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 219, .adv_w = 115, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 225, .adv_w = 115, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 231, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 238, .adv_w = 115, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 244, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 251, .adv_w = 115, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 257, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 264, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 271, .adv_w = 115, .box_w = 6, .box_h = 12, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 280, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 287, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 294, .adv_w = 115, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 302, .adv_w = 115, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 308, .adv_w = 115, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 316, .adv_w = 115, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 324, .adv_w = 115, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 332, .adv_w = 115, .box_w = 7, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 340, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 347, .adv_w = 115, .box_w = 3, .box_h = 12, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 352, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 359, .adv_w = 115, .box_w = 3, .box_h = 12, .ofs_x = 2, .ofs_y = -3},
    {.bitmap_index = 364, .adv_w = 115, .box_w = 5, .box_h = 6, .ofs_x = 1, .ofs_y = 3},
    {.bitmap_index = 368, .adv_w = 115, .box_w = 7, .box_h = 1, .ofs_x = 0, .ofs_y = -2},
    {.bitmap_index = 369, .adv_w = 115, .box_w = 3, .box_h = 2, .ofs_x = 2, .ofs_y = 8},
    {.bitmap_index = 370, .adv_w = 115, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 376, .adv_w = 115, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 382, .adv_w = 115, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 387, .adv_w = 115, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 393, .adv_w = 115, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 399, .adv_w = 115, .box_w = 6, .box_h = 9, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 406, .adv_w = 115, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 413, .adv_w = 115, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 419, .adv_w = 115, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 425, .adv_w = 115, .box_w = 4, .box_h = 12, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 431, .adv_w = 115, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 437, .adv_w = 115, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 443, .adv_w = 115, .box_w = 7, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 450, .adv_w = 115, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 455, .adv_w = 115, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 461, .adv_w = 115, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 468, .adv_w = 115, .box_w = 5, .box_h = 10, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 475, .adv_w = 115, .box_w = 5, .box_h = 7, .ofs_x = 2, .ofs_y = 0},
    {.bitmap_index = 480, .adv_w = 115, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 485, .adv_w = 115, .box_w = 5, .box_h = 9, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 491, .adv_w = 115, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 496, .adv_w = 115, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 503, .adv_w = 115, .box_w = 7, .box_h = 7, .ofs_x = 0, .ofs_y = 0},
    {.bitmap_index = 510, .adv_w = 115, .box_w = 6, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 516, .adv_w = 115, .box_w = 7, .box_h = 10, .ofs_x = 0, .ofs_y = -3},
    {.bitmap_index = 525, .adv_w = 115, .box_w = 5, .box_h = 7, .ofs_x = 1, .ofs_y = 0},
    {.bitmap_index = 530, .adv_w = 115, .box_w = 5, .box_h = 12, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 538, .adv_w = 115, .box_w = 1, .box_h = 12, .ofs_x = 3, .ofs_y = -3},
    {.bitmap_index = 540, .adv_w = 115, .box_w = 5, .box_h = 12, .ofs_x = 1, .ofs_y = -3},
    {.bitmap_index = 548, .adv_w = 115, .box_w = 6, .box_h = 2, .ofs_x = 1, .ofs_y = 3}
};

#endif // ZYNQ_LCD_ST7789_H
