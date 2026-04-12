#pragma once
/**
 * board_config.h — ESP32-2432S028Rv3 (Cheap Yellow Display)
 *
 * Pines y parametros extraidos del board JSON oficial:
 * https://github.com/rzeldent/platformio-espressif32-sunton
 *   esp32-2432S028Rv3.json
 *
 * Incluir este fichero en lugar de definir los pines manualmente.
 */

// ── DISPLAY ST7789 (SPI2 / HSPI) ─────────────────────────────
#define DISP_MOSI    13
#define DISP_MISO    -1   // NC — no conectado
#define DISP_SCLK    14
#define DISP_CS      15
#define DISP_DC       2
#define DISP_RST     -1   // NC — reset por software
#define DISP_BL      21   // backlight PWM
#define DISP_WIDTH   240  // resolucion nativa portrait
#define DISP_HEIGHT  320
#define DISP_SPI_HZ  24000000   // 24 MHz segun spec (TFT_eSPI usa 40 MHz en practica)
// SPI_MODE3 segun el JSON

// ── TOUCH XPT2046 (SPI3 / VSPI) ──────────────────────────────
#define TOUCH_MOSI   32
#define TOUCH_MISO   39
#define TOUCH_SCLK   25
#define TOUCH_CS     33
#define TOUCH_IRQ    36   // GPIO36: input-only, sin pull-up interno
#define TOUCH_SPI_HZ  2000000  // 2 MHz segun spec
// SPI_MODE0 segun el JSON
// TOUCH_MIRROR_X = true  (eje X invertido respecto al display)
// TOUCH_MIRROR_Y = false
// TOUCH_SWAP_XY  = false
#define TOUCH_Z_THRESHOLD  600  // presion minima valida (del JSON: XPT2046_Z_THRESHOLD)

// ── TARJETA SD (SPI independiente) ───────────────────────────
#define SD_CS        5
#define SD_MOSI      23
#define SD_SCLK      18
#define SD_MISO      19

// ── LED RGB ───────────────────────────────────────────────────
// Anodo comun: HIGH = apagado, LOW = encendido
#define LED_R        4
#define LED_G       16
#define LED_B       17

// ── OTROS ─────────────────────────────────────────────────────
#define PIN_SPEAKER  26   // altavoz
#define PIN_LDR      34   // sensor de luz (ADC)
