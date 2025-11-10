#include "fonts.h"           // lvgl compatible fonts here
#include "images.h"          // lvgl compatible images here
#include "zynq_lcd_st7789.h" // custom driver

#include <sleep.h>
#include <stdbool.h>
#include <stdint.h>
#include <xparameters.h>
#include <xscutimer.h>

#define TIME_SECTION(start, end, func_call)                                    \
  do {                                                                         \
    start = get_timer_value();                                                 \
    func_call;                                                                 \
    end = get_timer_value();                                                   \
  } while (0)

static XScuTimer TimerInstance;
static inline uint32_t get_timer_value(void) {
  return XScuTimer_GetCounterValue(&TimerInstance);
}

static void setup_timer() {
  XScuTimer_Config *config = XScuTimer_LookupConfig(XPAR_SCUTIMER_BASEADDR);
  if (!config) {
    printf("Timer config lookup failed!\n");
    return;
  }
  // Initialize timer driver
  XScuTimer_CfgInitialize(&TimerInstance, config, config->BaseAddr);

  // Disable auto reload so it runs once up to overflow
  XScuTimer_DisableAutoReload(&TimerInstance);

  // Load timer with max value (counts down)
  XScuTimer_LoadTimer(&TimerInstance, 0xFFFFFFFF);

  // Start the timer
  XScuTimer_Start(&TimerInstance);
};

XGpio PWM_1, PWM_2; // LED indicator

// ZLCD_RETURN_STATUS setup_PWM_LEDs(float brightness_percentage) {
//   // configure PWM for LED1 on AXI 1 -- shows DC pin status
//   if (XGpio_Initialize(&PWM_1, XPAR_AXI_GPIO_1_BASEADDR) != XST_SUCCESS) {
//     printf("Failed to initialize AXI GPIO 1\n");
//     return ZLCD_FAILURE;
//   }
//   // channel 1
//   XGpio_SetDataDirection(&PWM_1, 1, 0x0); // all ouputs (0)
//   int pwm = brightness_percentage * UINT16_MAX / 100;
//   pwm = (pwm > UINT16_MAX) ? UINT16_MAX : pwm;
//   XGpio_DiscreteWrite(&PWM_1, 1, pwm);

//   if (XGpio_Initialize(&PWM_2, XPAR_AXI_GPIO_2_BASEADDR) != XST_SUCCESS) {
//     printf("Failed to initialize AXI GPIO 3\n");
//     return ZLCD_FAILURE;
//   }
//   XGpio_SetDataDirection(&PWM_2, 1, 0x0); // all ouputs (0)
//   XGpio_DiscreteWrite(&PWM_2, 1, pwm);
//   return ZLCD_SUCCESS;
// }

const char *demo =
    "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod "
    "tempor incididunt ut labore et dolore magna aliqua. Ut enim ad minim "
    "veniam, quis nostrud exercitation ullamco laboris nisi ut aliquip ex ea "
    "commodo consequat. Duis aute irure dolor in reprehenderit in voluptate "
    "velit esse cillum dolore eu fugiat nulla pariatur. Excepteur sint "
    "occaecat cupidatat non proident, sunt in culpa qui officia deserunt "
    "mollit anim id est laborum.";

int main(void) {
  setup_timer();

  int success = ZLCD_init(ZLCD_INVERTED_LANDSCAPE_ORIENTATION, BLACK);
  printf("ZLCD init status: %s\n",
         (success == ZLCD_SUCCESS) ? "successful" : "failed");
  if (success)
    return 1;

  ZLCD_print_wrapped_string_xy(demo, 19, 20, 0, 30, RED, &printf_font, true);

  ZLCD_print_wrapped_string_on_background_xy(demo, 15, 20, 0, 30, WHITE, BLUE,
                                             &printf_font, true);

  ZLCD_clear();
  ZLCD_print_wrapped_string_on_background_xy(
      "Hey there gurl\nwhatcha up to\n\n\n?", 22, 30, 0, 30, WHITE, RED,
      &kiwi_soda_25, true);
  uint32_t t1, t2;
  ZLCD_draw_filled_rectangle(ZLCD_create_coordinate(0, 0), 172, 320, 6, RED,
                             BLUE, true);
  TIME_SECTION(t1, t2,
               ZLCD_draw_filled_rectangle(ZLCD_create_coordinate(0, 0), 172,
                                          320, 6, RED, BLUE, false));
  printf("Time to write full GRAM image: %.3f µs\n", (double)(t1 - t2) / 333.0);

  ZLCD_draw_filled_rectangle(ZLCD_create_coordinate(0, 0), 320, 320, 4, WHITE,
                             ORANGE, false);
  TIME_SECTION(t1, t2, ZLCD_refresh_display());
  printf("Time to refresh screen only: %.3f µs\n", (double)(t1 - t2) / 333.0);

  TIME_SECTION(t1, t2,
               ZLCD_draw_filled_rectangle(ZLCD_create_coordinate(0, 0), 320,
                                          320, 6, BLUE, GRAY, true));
  printf("Time to set GRAM image and transmit: %.3f µs\n",
         (double)(t1 - t2) / 333.0);

  ZLCD_clear();
  ZLCD_set_orientation(ZLCD_INVERTED_LANDSCAPE_ORIENTATION);
  ZLCD_ERROR_CHECK(
      ZLCD_print_aligned_string_on_background("Aligned left 1\nAligned left again", 30,
                                ZLCD_ALIGN_LEFT, WHITE, GRAY, &simple_font_12, true));
  msleep(250);
  // ZLCD_set_orientation(ZLCD_LANDSCAPE_ORIENTATION);
  ZLCD_ERROR_CHECK(ZLCD_print_aligned_string(
      "Aligned center 1\nAligned center 2 lala", 50, ZLCD_ALIGN_CENTER,
      LIGHT_BLUE, &simple_font_12, true));
  msleep(250);
  char *test = "Aligned right 1\nAligned right 2 is here";
  ZLCD_ERROR_CHECK(ZLCD_print_aligned_string_on_background(
      test, 75, ZLCD_ALIGN_RIGHT, NAVY_GREEN, WHITE, &kiwi_soda_25, true));
  msleep(250);
  ZLCD_image Zimg_1 = lvgl_image_to_ZLCD(&img_1, 0, 0);

  ZLCD_set_orientation(ZLCD_INVERTED_PORTRAIT_ORIENTATION);
  TIME_SECTION(t1, t2,
               ZLCD_draw_image(ZLCD_create_coordinate(0, 0), &Zimg_1, false));
  printf("Time to load image into GRAM: %.3f µs\n", (double)(t1 - t2) / 333.0);

  TIME_SECTION(t1, t2, ZLCD_refresh_display());
  printf("Time to show image: %.3f µs\n", (double)(t1 - t2) / 333.0);

  ZLCD_set_orientation(ZLCD_PORTRAIT_ORIENTATION);
  ZLCD_image Zimg_2 = lvgl_image_to_ZLCD(&img_2, 0, 0);

  ZLCD_image Zimg_3 = lvgl_image_to_ZLCD(&shrek_grin, 0, 0);
  ZLCD_pixel_coordinate top_left = ZLCD_create_coordinate(0, 0);

  ZLCD_clear();

  ZLCD_print_wrapped_string_xy("testing wrap\n\nbig and long string...", 30, 30,
                               0, 30, RED, &printf_font, true);

  ZLCD_set_background_colour(WHITE);
  ZLCD_draw_filled_rectangle(ZLCD_create_coordinate(0, 0), 100, 100, 6, RED, NAVY_GREEN, true);
  ZLCD_set_orientation(ZLCD_INVERTED_LANDSCAPE_ORIENTATION);
  ZLCD_ERROR_CHECK(ZLCD_printf("  Hi form ZLCD_printf()\n\n"));
  ZLCD_ERROR_CHECK(
      ZLCD_printf("Integer 10 is: %d\nfloat 2.1 is: %3.1f\n", 10, 2.1f));
  ZLCD_ERROR_CHECK(ZLCD_printf("No newline here"));
  ZLCD_ERROR_CHECK(ZLCD_printf("No newline here 2 - overwrite..."));
  ZLCD_set_printf_mode(ZLCD_PRINTF_MODE_OVERWRITE);
  ZLCD_ERROR_CHECK(ZLCD_printf("Should fully overwrite"));
  ZLCD_ERROR_CHECK(ZLCD_printf("Should fully overwrite again!"));

  ZLCD_set_printf_mode(ZLCD_PRINTF_MODE_SCROLL);
  ZLCD_ERROR_CHECK(ZLCD_printf("\nmany newlines\n\n\n\n\n"));
  ZLCD_ERROR_CHECK(
      ZLCD_printf("\ntest with really a very long string. It's a big one..."));
      ZLCD_draw_background();
      ZLCD_set_printf_cursor(ZLCD_create_coordinate(0, 0));
  ZLCD_ERROR_CHECK(ZLCD_printf("\ntest 2"));
  ZLCD_set_printf_cursor(ZLCD_create_coordinate(13, 9));
  ZLCD_ERROR_CHECK(ZLCD_printf("test 3"));
  ZLCD_set_printf_cursor(ZLCD_create_coordinate(0, 21));
  ZLCD_ERROR_CHECK(ZLCD_printf("test 4\ntest5 (one string!)"));
  ZLCD_ERROR_CHECK(ZLCD_printf("\nstarting at the top?\n"));

  for (;;) {
    // camera pan effect
    ZLCD_set_orientation(ZLCD_PORTRAIT_ORIENTATION);
    while (Zimg_2.offset_x <= Zimg_2.width - ZLCD_WIDTH) {
      Zimg_2.offset_x += 1;
      Zimg_2.offset_y = 20;
      ZLCD_draw_image(top_left, &Zimg_2, true);
    }
    msleep(200);
    while (Zimg_2.offset_x >= 1) {
      Zimg_2.offset_x -= 1; // ensure no underflow
      Zimg_2.offset_y = 20;
      ZLCD_draw_image(top_left, &Zimg_2, true);
    }
    msleep(200);

    // draw shrek
    ZLCD_set_orientation(ZLCD_INVERTED_LANDSCAPE_ORIENTATION);
    ZLCD_draw_image(top_left, &Zimg_3, true);
    sleep(2);
  }
  return 0;
}