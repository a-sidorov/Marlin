/**
 * Marlin 3D Printer Firmware
 * Copyright (C) 2016 MarlinFirmware [https://github.com/MarlinFirmware/Marlin]
 *
 * Based on Sprinter and grbl.
 * Copyright (C) 2011 Camiel Gubbels / Erik van der Zalm
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

/**
 * This module is off by default, but can be enabled to facilitate the display of
 * extra debug information during code development.
 *
 * Just connect up 5V and GND to give it power, then connect up the pins assigned
 * in Configuration_adv.h. For example, on the Re-ARM you could use:
 *
 *   #define MAX7219_CLK_PIN   77
 *   #define MAX7219_DIN_PIN   78
 *   #define MAX7219_LOAD_PIN  79
 *
 * send() is called automatically at startup, and then there are a number of
 * support functions available to control the LEDs in the 8x8 grid.
 */

#include "MarlinConfig.h"

#if ENABLED(MAX7219_DEBUG)

#define MAX7219_ERRORS // Disable to save 406 bytes of Program Memory

#include "Max7219_Debug_LEDs.h"

#include "planner.h"
#include "stepper.h"
#include "Marlin.h"
#include "delay.h"

Max7219 max7219;

uint8_t Max7219::led_line[MAX7219_LINES]; // = { 0 };

#define LINE_REG(Q)     (max7219_reg_digit0 + ((Q) & 0x7))
#if _ROT == 0 || _ROT == 270
  #define _LED_BIT(Q)   (7 - ((Q) & 0x7))
  #define _LED_UNIT(Q)  ((Q) & ~0x7)
#else
  #define _LED_BIT(Q)   ((Q) & 0x7)
  #define _LED_UNIT(Q)  ((MAX7219_NUMBER_UNITS - 1 - ((Q) >> 3)) << 3)
#endif
#if _ROT < 180
  #define _LED_IND(P,Q) (_LED_UNIT(P) + (Q))
#else
  #define _LED_IND(P,Q) (_LED_UNIT(P) + (7 - ((Q) & 0x7)))
#endif
#if _ROT == 0 || _ROT == 180
  #define LED_IND(X,Y)  _LED_IND(X,Y)
  #define LED_BIT(X,Y)  _LED_BIT(X)
#elif _ROT == 90 || _ROT == 270
  #define LED_IND(X,Y)  _LED_IND(Y,X)
  #define LED_BIT(X,Y)  _LED_BIT(Y)
#endif
#define XOR_7219(X,Y) do{ led_line[LED_IND(X,Y)] ^=  _BV(LED_BIT(X,Y)); }while(0)
#define SET_7219(X,Y) do{ led_line[LED_IND(X,Y)] |=  _BV(LED_BIT(X,Y)); }while(0)
#define CLR_7219(X,Y) do{ led_line[LED_IND(X,Y)] &= ~_BV(LED_BIT(X,Y)); }while(0)
#define BIT_7219(X,Y) TEST(led_line[LED_IND(X,Y)], LED_BIT(X,Y))

#ifdef CPU_32_BIT
  #define SIG_DELAY() DELAY_US(1)   // Approximate a 1Вµs delay on 32-bit ARM
  #undef CRITICAL_SECTION_START
  #undef CRITICAL_SECTION_END
  #define CRITICAL_SECTION_START NOOP
  #define CRITICAL_SECTION_END   NOOP
#else
  #define SIG_DELAY() DELAY_NS(188) // Delay for 0.1875Вµs (16MHz AVR) or 0.15Вµs (20MHz AVR)
#endif

void Max7219::error(const char * const func, const int32_t v1, const int32_t v2/*=-1*/) {
  #if ENABLED(MAX7219_ERRORS)
    SERIAL_ECHOPGM("??? Max7219::");
    serialprintPGM(func);
    SERIAL_CHAR('(');
    SERIAL_ECHO(v1);
    if (v2 > 0) SERIAL_ECHOPAIR(", ", v2);
    SERIAL_CHAR(')');
    SERIAL_EOL();
  #else
    UNUSED(func); UNUSED(v1); UNUSED(v2);
  #endif
}

/**
 * Flip the lowest n_bytes of the supplied bits:
 *  flipped(x, 1) flips the low 8  bits of x.
 *  flipped(x, 2) flips the low 16 bits of x.
 *  flipped(x, 3) flips the low 24 bits of x.
 *  flipped(x, 4) flips the low 32 bits of x.
 */
inline uint32_t flipped(const uint32_t bits, const uint8_t n_bytes) {
  uint32_t mask = 1, outbits = 0;
  for (uint8_t b = 0; b < n_bytes * 8; b++) {
    outbits <<= 1;
    if (bits & mask) outbits |= 1;
    mask <<= 1;
  }
  return outbits;
}

void Max7219::noop() {
  CRITICAL_SECTION_START;
  SIG_DELAY();
  WRITE(MAX7219_DIN_PIN, LOW);
  for (uint8_t i = 16; i--;) {
    SIG_DELAY();
    WRITE(MAX7219_CLK_PIN, LOW);
    SIG_DELAY();
    SIG_DELAY();
    WRITE(MAX7219_CLK_PIN, HIGH);
    SIG_DELAY();
  }
  CRITICAL_SECTION_END;
}

void Max7219::putbyte(uint8_t data) {
  CRITICAL_SECTION_START;
  for (uint8_t i = 8; i--;) {
    SIG_DELAY();
    WRITE(MAX7219_CLK_PIN, LOW);       // tick
    SIG_DELAY();
    WRITE(MAX7219_DIN_PIN, (data & 0x80) ? HIGH : LOW);  // send 1 or 0 based on data bit
    SIG_DELAY();
    WRITE(MAX7219_CLK_PIN, HIGH);      // tock
    SIG_DELAY();
    data <<= 1;
  }
  CRITICAL_SECTION_END;
}

void Max7219::pulse_load() {
  SIG_DELAY();
  WRITE(MAX7219_LOAD_PIN, LOW);  // tell the chip to load the data
  SIG_DELAY();
  WRITE(MAX7219_LOAD_PIN, HIGH);
  SIG_DELAY();
}

void Max7219::send(const uint8_t reg, const uint8_t data) {
  SIG_DELAY();
  CRITICAL_SECTION_START;
  SIG_DELAY();
  putbyte(reg);          // specify register
  SIG_DELAY();
  putbyte(data);         // put data
  CRITICAL_SECTION_END;
}

// Send out a single native row of bits to all units
void Max7219::refresh_line(const uint8_t line) {
  for (uint8_t u = MAX7219_NUMBER_UNITS; u--;)
    send(LINE_REG(line), led_line[(u << 3) | (line & 0x7)]);
  pulse_load();
}

// Send out a single native row of bits to just one unit
void Max7219::refresh_unit_line(const uint8_t line) {
  for (uint8_t u = MAX7219_NUMBER_UNITS; u--;)
    if (u == (line >> 3)) send(LINE_REG(line), led_line[line]); else noop();
  pulse_load();
}

void Max7219::set(const uint8_t line, const uint8_t bits) {
  led_line[line] = bits;
  refresh_line(line);
}

#if ENABLED(MAX7219_NUMERIC)

  // Draw an integer with optional leading zeros and optional decimal point
  void Max7219::print(const uint8_t start, int16_t value, uint8_t size, const bool leadzero=false, bool dec=false) {
    constexpr uint8_t led_numeral[10] = { 0x7E, 0x60, 0x6D, 0x79, 0x63, 0x5B, 0x5F, 0x70, 0x7F, 0x7A },
                      led_decimal = 0x80, led_minus = 0x01;

    bool blank = false, neg = value < 0;
    if (neg) value *= -1;
    while (size--) {
      const bool minus = neg && blank;
      if (minus) neg = false;
      send(
        max7219_reg_digit0 + start + size,
        minus ? led_minus : blank ? 0x00 : led_numeral[value % 10] | (dec ? led_decimal : 0x00)
      );
      pulse_load();  // tell the chips to load the clocked out data
      value /= 10;
      if (!value && !leadzero) blank = true;
      dec = false;
    }
  }

  // Draw a float with a decimal point and optional digits
  void Max7219::print(const uint8_t start, const float value, const uint8_t pre_size, const uint8_t post_size, const bool leadzero=false) {
    if (pre_size) print(start, value, pre_size, leadzero, !!post_size);
    if (post_size) {
      const int16_t after = ABS(value) * (10 ^ post_size);
      print(start + pre_size, after, post_size, true);
    }
  }

#endif // MAX7219_NUMERIC

// Modify a single LED bit and send the changed line
void Max7219::led_set(const uint8_t x, const uint8_t y, const bool on) {
  if (x > MAX7219_X_LEDS - 1 || y > MAX7219_Y_LEDS - 1) return error(PSTR("led_set"), x, y);
  if (BIT_7219(x, y) == on) return;
  XOR_7219(x, y);
  refresh_line(LED_IND(x, y));
}

void Max7219::led_on(const uint8_t x, const uint8_t y) {
  if (x > MAX7219_X_LEDS - 1 || y > MAX7219_Y_LEDS - 1) return error(PSTR("led_on"), x, y);
  led_set(x, y, true);
}

void Max7219::led_off(const uint8_t x, const uint8_t y) {
  if (x > MAX7219_X_LEDS - 1 || y > MAX7219_Y_LEDS - 1) return error(PSTR("led_off"), x, y);
  led_set(x, y, false);
}

void Max7219::led_toggle(const uint8_t x, const uint8_t y) {
  if (x > MAX7219_X_LEDS - 1 || y > MAX7219_Y_LEDS - 1) return error(PSTR("led_toggle"), x, y);
  led_set(x, y, !BIT_7219(x, y));
}

void Max7219::send_row(const uint8_t row) {
  #if _ROT == 0 || _ROT == 180
    refresh_line(LED_IND(0, row));
  #else
    UNUSED(row);
    refresh();
  #endif
}

void Max7219::send_column(const uint8_t col) {
  #if _ROT == 90 || _ROT == 270
    refresh_line(LED_IND(col, 0));
  #else
    UNUSED(col);
    refresh();
  #endif
}

void Max7219::clear() {
  ZERO(led_line);
  refresh();
}

void Max7219::fill() {
  memset(led_line, 0xFF, sizeof(led_line));
  refresh();
}

void Max7219::clear_row(const uint8_t row) {
  if (row >= MAX7219_Y_LEDS) return error(PSTR("clear_row"), row);
  for (uint8_t x = 0; x < MAX7219_X_LEDS; x++) CLR_7219(x, row);
  send_row(row);
}

void Max7219::clear_column(const uint8_t col) {
  if (col >= MAX7219_X_LEDS) return error(PSTR("set_column"), col);
  for (uint8_t y = 0; y < MAX7219_Y_LEDS; y++) CLR_7219(col, y);
  send_column(col);
}

/**
 * Plot the low order bits of val to the specified row of the matrix.
 * With 4 Max7219 units in the chain, it's possible to set 32 bits at once with
 * one call to the function (if rotated 90В° or 180В°).
 */
void Max7219::set_row(const uint8_t row, const uint32_t val) {
  if (row >= MAX7219_Y_LEDS) return error(PSTR("set_row"), row);
  uint32_t mask = _BV32(MAX7219_X_LEDS - 1);
  for (uint8_t x = 0; x < MAX7219_X_LEDS; x++) {
    if (val & mask) SET_7219(x, row); else CLR_7219(x, row);
    mask >>= 1;
  }
  send_row(row);
}

/**
 * Plot the low order bits of val to the specified column of the matrix.
 * With 4 Max7219 units in the chain, it's possible to set 32 bits at once with
 * one call to the function (if rotated 90В° or 180В°).
 */
void Max7219::set_column(const uint8_t col, const uint32_t val) {
  if (col >= MAX7219_X_LEDS) return error(PSTR("set_column"), col);
  uint32_t mask = _BV32(MAX7219_Y_LEDS - 1);
  for (uint8_t y = 0; y < MAX7219_Y_LEDS; y++) {
    if (val & mask) SET_7219(col, y); else CLR_7219(col, y);
    mask >>= 1;
  }
  send_column(col);
}

void Max7219::set_rows_16bits(const uint8_t y, uint32_t val) {
  #if MAX7219_X_LEDS == 8
    if (y > MAX7219_Y_LEDS - 2) return error(PSTR("set_rows_16bits"), y, val);
    set_row(y + 1, val); val >>= 8;
    set_row(y + 0, val);
  #else // at least 16 bits on each row
    if (y > MAX7219_Y_LEDS - 1) return error(PSTR("set_rows_16bits"), y, val);
    set_row(y, val);
  #endif
}

void Max7219::set_rows_32bits(const uint8_t y, uint32_t val) {
  #if MAX7219_X_LEDS == 8
    if (y > MAX7219_Y_LEDS - 4) return error(PSTR("set_rows_32bits"), y, val);
    set_row(y + 3, val); val >>= 8;
    set_row(y + 2, val); val >>= 8;
    set_row(y + 1, val); val >>= 8;
    set_row(y + 0, val);
  #elif MAX7219_X_LEDS == 16
    if (y > MAX7219_Y_LEDS - 2) return error(PSTR("set_rows_32bits"), y, val);
    set_row(y + 1, val); val >>= 16;
    set_row(y + 0, val);
  #else // at least 24 bits on each row.  In the 3 matrix case, just display the low 24 bits
    if (y > MAX7219_Y_LEDS - 1) return error(PSTR("set_rows_32bits"), y, val);
    set_row(y, val);
  #endif
}

void Max7219::set_columns_16bits(const uint8_t x, uint32_t val) {
  #if MAX7219_Y_LEDS == 8
    if (x > MAX7219_X_LEDS - 2) return error(PSTR("set_columns_16bits"), x, val);
    set_column(x + 0, val); val >>= 8;
    set_column(x + 1, val);
  #else // at least 16 bits in each column
    if (x > MAX7219_X_LEDS - 1) return error(PSTR("set_columns_16bits"), x, val);
    set_column(x, val);
  #endif
}

void Max7219::set_columns_32bits(const uint8_t x, uint32_t val) {
  #if MAX7219_Y_LEDS == 8
    if (x > MAX7219_X_LEDS - 4) return error(PSTR("set_rows_32bits"), x, val);
    set_column(x + 3, val); val >>= 8;
    set_column(x + 2, val); val >>= 8;
    set_column(x + 1, val); val >>= 8;
    set_column(x + 0, val);
  #elif MAX7219_Y_LEDS == 16
    if (x > MAX7219_X_LEDS - 2) return error(PSTR("set_rows_32bits"), x, val);
    set_column(x + 1, val); val >>= 16;
    set_column(x + 0, val);
  #else // at least 24 bits on each row.  In the 3 matrix case, just display the low 24 bits
    if (x > MAX7219_X_LEDS - 1) return error(PSTR("set_rows_32bits"), x, val);
    set_column(x, val);
  #endif
}

// Initialize the Max7219
void Max7219::register_setup() {
  for (uint8_t i = 0; i < MAX7219_NUMBER_UNITS; i++)
    send(max7219_reg_scanLimit, 0x07);
  pulse_load();                        // tell the chips to load the clocked out data

  for (uint8_t i = 0; i < MAX7219_NUMBER_UNITS; i++)
    send(max7219_reg_decodeMode, 0x00);     // using an led matrix (not digits)
  pulse_load();                        // tell the chips to load the clocked out data

  for (uint8_t i = 0; i < MAX7219_NUMBER_UNITS; i++)
    send(max7219_reg_shutdown, 0x01);       // not in shutdown mode
  pulse_load();                        // tell the chips to load the clocked out data

  for (uint8_t i = 0; i < MAX7219_NUMBER_UNITS; i++)
    send(max7219_reg_displayTest, 0x00);    // no display test
  pulse_load();                        // tell the chips to load the clocked out data

  for (uint8_t i = 0; i < MAX7219_NUMBER_UNITS; i++)
    send(max7219_reg_intensity, 0x01 & 0x0F); // the first 0x0F is the value you can set
                                                 // range: 0x00 to 0x0F
  pulse_load();                          // tell the chips to load the clocked out data
}

#ifdef MAX7219_INIT_TEST
#if MAX7219_INIT_TEST == 2

  void Max7219::spiral(const bool on, const uint16_t del) {
    constexpr int8_t way[] = { 1, 0, 0, 1, -1, 0, 0, -1 };
    int8_t px = 0, py = 0, dir = 0;
    for (uint8_t i = MAX7219_X_LEDS * MAX7219_Y_LEDS; i--;) {
      led_set(px, py, on);
      delay(del);
      const int8_t x = px + way[dir], y = py + way[dir + 1];
      if (!WITHIN(x, 0, MAX7219_X_LEDS-1) || !WITHIN(y, 0, MAX7219_Y_LEDS-1) || BIT_7219(x, y) == on) dir = (dir + 2) & 0x7;
      px += way[dir]; py += way[dir + 1];
    }
  }

#else

  void Max7219::sweep(const int8_t dir, const uint16_t ms, const bool on) {
    uint8_t x = dir > 0 ? 0 : MAX7219_X_LEDS-1;
    for (uint8_t i = MAX7219_X_LEDS; i--; x += dir) {
      set_column(x, on ? 0xFFFFFFFF : 0x00000000);
      delay(ms);
    }
  }

#endif
#endif // MAX7219_INIT_TEST

void Max7219::init() {
  SET_OUTPUT(MAX7219_DIN_PIN);
  SET_OUTPUT(MAX7219_CLK_PIN);
  OUT_WRITE(MAX7219_LOAD_PIN, HIGH);
  delay(1);

  register_setup();

  for (uint8_t i = 0; i <= 7; i++) {      // Empty registers to turn all LEDs off
    led_line[i] = 0x00;
    send(max7219_reg_digit0 + i, 0);
    pulse_load();                 // tell the chips to load the clocked out data
  }

  #ifdef MAX7219_INIT_TEST
    #if MAX7219_INIT_TEST == 2
      spiral(true, 8);
      delay(150);
      spiral(false, 8);
    #else
      // Do an aesthetically-pleasing pattern to fully test the Max7219 module and LEDs.
      // Light up and turn off columns, both forward and backward.
      sweep(1, 20, true);
      sweep(1, 20, false);
      delay(150);
      sweep(-1, 20, true);
      sweep(-1, 20, false);
    #endif
  #endif
}

/**
 * This code demonstrates some simple debugging using a single 8x8 LED Matrix. If your feature could
 * benefit from matrix display, add its code here. Very little processing is required, so the 7219 is
 * ideal for debugging when realtime feedback is important but serial output can't be used.
 */

// Apply changes to update a marker
void Max7219::mark16(const uint8_t y, const uint8_t v1, const uint8_t v2) {
  #if MAX7219_X_LEDS == 8
    #if MAX7219_Y_LEDS == 8
      led_off(v1 & 0x7, y + (v1 >= 8));
       led_on(v2 & 0x7, y + (v2 >= 8));
    #else
      led_off(y, v1 & 0xF); // At least 16 LEDs down. Use a single column.
       led_on(y, v2 & 0xF);
    #endif
  #else
    led_off(v1 & 0xF, y);   // At least 16 LEDs across. Use a single row.
     led_on(v2 & 0xF, y);
  #endif
}

// Apply changes to update a tail-to-head range
void Max7219::range16(const uint8_t y, const uint8_t ot, const uint8_t nt, const uint8_t oh, const uint8_t nh) {
  #if MAX7219_X_LEDS == 8
    #if MAX7219_Y_LEDS == 8
      if (ot != nt) for (uint8_t n = ot & 0xF; n != (nt & 0xF) && n != (nh & 0xF); n = (n + 1) & 0xF)
        led_off(n & 0x7, y + (n >= 8));
      if (oh != nh) for (uint8_t n = (oh + 1) & 0xF; n != ((nh + 1) & 0xF); n = (n + 1) & 0xF)
         led_on(n & 0x7, y + (n >= 8));
    #else // The Max7219 Y-Axis has at least 16 LED's.  So use a single column
      if (ot != nt) for (uint8_t n = ot & 0xF; n != (nt & 0xF) && n != (nh & 0xF); n = (n + 1) & 0xF)
        led_off(y, n & 0xF);
      if (oh != nh) for (uint8_t n = (oh + 1) & 0xF; n != ((nh + 1) & 0xF); n = (n + 1) & 0xF)
         led_on(y, n & 0xF);
    #endif
  #else   // LED matrix has at least 16 LED's on the X-Axis.  Use single line of LED's
    if (ot != nt) for (uint8_t n = ot & 0xF; n != (nt & 0xF) && n != (nh & 0xF); n = (n + 1) & 0xF)
      led_off(n & 0xF, y);
    if (oh != nh) for (uint8_t n = (oh + 1) & 0xF; n != ((nh + 1) & 0xF); n = (n + 1) & 0xF)
       led_on(n & 0xF, y);
 #endif
}

// Apply changes to update a quantity
void Max7219::quantity16(const uint8_t y, const uint8_t ov, const uint8_t nv) {
  for (uint8_t i = MIN(nv, ov); i < MAX(nv, ov); i++)
    #if MAX7219_X_LEDS == 8
      #if MAX7219_Y_LEDS == 8
        led_set(i >> 1, y + (i & 1), nv >= ov); // single 8x8 LED matrix.  Use two lines to get 16 LED's
      #else
        led_set(y, i, nv >= ov);                // The Max7219 Y-Axis has at least 16 LED's.  So use a single column
      #endif
    #else
      led_set(i, y, nv >= ov);                // LED matrix has at least 16 LED's on the X-Axis.  Use single line of LED's
    #endif
}

void Max7219::idle_tasks() {
  #define MAX7219_USE_HEAD (defined(MAX7219_DEBUG_PLANNER_HEAD) || defined(MAX7219_DEBUG_PLANNER_QUEUE))
  #define MAX7219_USE_TAIL (defined(MAX7219_DEBUG_PLANNER_TAIL) || defined(MAX7219_DEBUG_PLANNER_QUEUE))
  #if MAX7219_USE_HEAD || MAX7219_USE_TAIL
    CRITICAL_SECTION_START;
    #if MAX7219_USE_HEAD
      const uint8_t head = planner.block_buffer_head;
    #endif
    #if MAX7219_USE_TAIL
      const uint8_t tail = planner.block_buffer_tail;
    #endif
    CRITICAL_SECTION_END;
  #endif

  #if ENABLED(MAX7219_DEBUG_PRINTER_ALIVE)
    static uint8_t refresh_cnt; // = 0
    constexpr uint16_t refresh_limit = 5;
    static millis_t next_blink = 0;
    const millis_t ms = millis();
    const bool do_blink = ELAPSED(ms, next_blink);
  #else
    static uint16_t refresh_cnt; // = 0
    constexpr bool do_blink = true;
    constexpr uint16_t refresh_limit = 50000;
  #endif

  // Some Max7219 units are vulnerable to electrical noise, especially
  // with long wires next to high current wires. If the display becomes
  // corrupted, this will fix it within a couple seconds.
  if (do_blink && ++refresh_cnt >= refresh_limit) {
    refresh_cnt = 0;
    register_setup();
  }

  #if ENABLED(MAX7219_DEBUG_PRINTER_ALIVE)
    if (do_blink) {
      led_toggle(MAX7219_X_LEDS - 1, MAX7219_Y_LEDS - 1);
      next_blink = ms + 1000;
    }
  #endif

  #if defined(MAX7219_DEBUG_PLANNER_HEAD) && defined(MAX7219_DEBUG_PLANNER_TAIL) && MAX7219_DEBUG_PLANNER_HEAD == MAX7219_DEBUG_PLANNER_TAIL

    static int16_t last_head_cnt = 0xF, last_tail_cnt = 0xF;

    if (last_head_cnt != head || last_tail_cnt != tail) {
      range16(MAX7219_DEBUG_PLANNER_HEAD, last_tail_cnt, tail, last_head_cnt, head);
      last_head_cnt = head;
      last_tail_cnt = tail;
    }

  #else

    #ifdef MAX7219_DEBUG_PLANNER_HEAD
      static int16_t last_head_cnt = 0x1;
      if (last_head_cnt != head) {
        mark16(MAX7219_DEBUG_PLANNER_HEAD, last_head_cnt, head);
        last_head_cnt = head;
      }
    #endif

    #ifdef MAX7219_DEBUG_PLANNER_TAIL
      static int16_t last_tail_cnt = 0x1;
      if (last_tail_cnt != tail) {
        mark16(MAX7219_DEBUG_PLANNER_TAIL, last_tail_cnt, tail);
        last_tail_cnt = tail;
      }
    #endif

  #endif

  #ifdef MAX7219_DEBUG_PLANNER_QUEUE
    static int16_t last_depth = 0;
    const int16_t current_depth = (head - tail + BLOCK_BUFFER_SIZE) & (BLOCK_BUFFER_SIZE - 1) & 0xF;
    if (current_depth != last_depth) {
      quantity16(MAX7219_DEBUG_PLANNER_QUEUE, last_depth, current_depth);
      last_depth = current_depth;
    }
  #endif
}

#endif // MAX7219_DEBUG

