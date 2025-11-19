# Zynq ST7789 LCD Driver

A lightweight, low-level graphics driver for the ST7789 TFT display, designed specifically for the Zynq-7020 based [Smart Zynq SP](http://www.hellofpga.com/index.php/2023/04/27/smart-zynq-sp/) board which features the necessary hardware directly on the board.

This library exposes a simple drawing API, offers optional LVGL compatibility, and provides efficient routines for text, shapes, pixel buffers, and display control. The driver is capable of refreshing with a period of ~30 ms in the worst case scenario where the entire screen needs to be redrawn. However, since only parts of the memory that have been changed since the last update are sent in batches, refreshes that change less of the display from its current state will be faster. For example, using only the top half of the display (in portrait mode) will take roughly half the time to transfer data, and should result in >60 FPS. Even in the worst case, the display operates at ~33 FPS, which is smooth enough for even videos (though they are not supported currently, so the closest thing would be sliding an image across the screen).

## Overview

This project implements a bare-metal graphics driver for the ST7789 display controller using the Zynq Processing System (PS).
The main focus is on:

Speed (minimizing overhead and unnecessary commands)

Simplicity (small API surface, clear function purposes)

Extensibility (easy to hook into LVGL or custom graphics layers)

The library handles everything from low-level SPI transactions all the way up to higher-level primitives like strings, filled rectangles, and circles.

At its core, it wraps the ST7789 initialization and command sequences while offering an ergonomic interface for drawing pixels, shapes, images, and text using LVGL-based font arrays and LVGL-compatible image arrays. Both font and image C arrays can be generated on the fly using the online LVGL conversion tools with minimal changes due to a compatibility layer. This allows for simple and fast integration of fonts and images into the source code directly. 

#### NOTE
A video showcasing the capability of the graphics library can be downloaded and viewed in the "images" folder. This showcases all the various primitive shapes, text strings of different fonts, allignments, and colours, and rendering images

## File Structure

lvgl_compat.h          (optional LVGL glue / API alignment)

zynq_lcd_st7789.h      (public API & graphics primitives)

zynq_lcd_st7789.c      (implementation)

images.h               (example usage of how to load an image)

fonts.h                (example usage of loading any fonts)

## Features
### Display Control

Full ST7789 init sequence (orientation, color mode, inversion, TE line, etc.)

Full-screen and partial updates

Display sleep / wake / inversion commands

Configurable rotations

Configurable background colours

The display only ever updates when the user specifies to do so

### arbitrary orientation support

The library supports the four logical orientations of the physical screen:
-Portrait mode (the default)
-Inverted portrait mode
-Landscape mode
-Inverted landscape mode

This allows the user to dynamically change the orientation of the display to write strings and shapes where the origin (0,0) is placed at the top left of the selected orientation. Internally, the driver maps any non-portrait orientation pixels into the portrait orientation pixels that would produce the desired effect. This allows the driver to support all the possible orientations a user will want, but limits RAM consumption and SPI transactions by only ever using portrait mode when sending actual pixel data to the LCD. 

### Drawing Primitives

Draw individual pixels

Filled rectangles

Lines and horizontal/vertical optimized routines

Unfilled and filled circles

Arbitrary-region writes

Colour helpers (RGB565 handling)

### Text Rendering

Built-in font descriptor support for arbitrary fonts of the .ttf format

Drawing single characters or entire strings at any location

Wrapping support for strings

Background vs. transparent text rendering

### Performance-Oriented Behavior

updates the display by monitoring the internal RAM buffer for changes and only the rows of the buffer that have changed since the last refresh

Avoids floating-point operations for some drawing algorithms

### LVGL Compatibility Layer

lvgl_compat.h provides thin wrappers so you can connect this driver to LVGL as a display backend

this is most useful for displaying any image and using any font with the LVGL online tools that can convert .ttf fonts and any image into C style arrays to be directly inserted into header files (see images.h and fonts.h)

### Implementation Details

This section breaks down the driver internals so future you (or someone else) can confidently extend it.

### Initialization Flow

The implementation sends the required command list to the ST7789, including:

software reset

sleep out

pixel format set to RGB565

memory data access control (MADCTL)

inversion ON

display ON

setting the column/row ranges

There is also logic to configure the Zynq SPI interface and GPIO pins used for:

DC (data/command)

RES (reset)

BL (backlight, not yet supported and always at full brightness now)

### Address Window Management

Before writing pixel data, the driver issues:

CASET (column range)

RASET (row range)

RAMWR (start write command)

The most recent row and column ranges are cached to slightly reduce the SPI transactions if the user redraws over the same space continuously.

### Shape Rendering Implementation
Rectangles

Uses nested loops writing RGB565 directly

Large fills are highly efficient due to contiguous RAMWR streams

Circles

Uses midpoint circle algorithm (integer math)

Filled and unfilled modes supported

Coordinates clipped to display boundaries for partial circles on boundary lines

Lines

Horizontal/vertical lines special-cased for speed

Arbitrary slopes use Bresenham's line algorithm

### Text Rendering Implementation

Text is drawn using font descriptors included in the project.
Each glyph includes:

width

height

x/y offset

bitmap data

ZLCD_draw_char_xy() sets the address window to the glyph bounds and streams the bitmap.
Strings are rendered one glyph at a time with:

optional background fill

automatic x advance

optional wrapping (via internal measurement + bounds checking)

Other helpers include:

computing rendered text height

computing rendered text width

vertical centering offsets based on font metadata

the library also supports aligning your text to the left, center, and right sides of the screen instead of numerical pixel offsets.

### Printf-like functionality

implements a printf-like function called ZLCD_printf() which simply prints formatted strings to the LCD display in a manner similar to printf()

This function renders high contrast text on the currently selected background of the user in a simple and clean built-in font

The function overwrites printed strings by default, but can be configured to move down the display using ZLCD_set_printf_mode().

This allows users to print strings visually without having to use the serial terminal or worrying about exact alignment, the font used, or the colour of the text drawn, which can be useful for debugging or monitoring values

### Future Extensions (Suggested)

DMA-accelerated SPI transfers (could reduce refresh times)

Double-buffering in PS-side RAM

Support for video formats (mkv, mp4, etc.) by breaking down video files into a stream of images and drawing each image

Animations or hardware scrolling (ST7789 supports it!)
