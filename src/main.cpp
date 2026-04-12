/**
 * RELOJ RETRO - ESP32-2432S028Rv3 (Cheap Yellow Display)
 *
 *  - Reloj HH:MM con animacion Tetris, sincronizado por NTP
 *  - Piezas decorativas de fondo (sin invadir zona del reloj ni fecha)
 *  - Menu tactil: manten pulsado 2 segundos para entrar
 *      · Configurar WiFi (portal AP con WiFiManager)
 *      · Zona horaria (UTC-12 .. UTC+12)
 *      · Brillo de pantalla
 *      · Formato 12h / 24h  (12h omite el cero delantero)
 *      · Horario de pantalla (encender/apagar automaticamente)
 *  - Ajustes guardados en flash (Preferences / NVS)
 *
 * Touch: XPT2046 en HSPI (TFT_eSPI ocupa VSPI)
 * Display: ST7789 en VSPI gestionado por TFT_eSPI
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <time.h>
#include "board_config.h"

// ===================================================
// DISPLAY
// ===================================================
TFT_eSPI tft = TFT_eSPI();
#define W  320
#define H  240

// ===================================================
// TOUCH  (HSPI, libre - TFT_eSPI ocupa VSPI)
// ===================================================
// Calibracion raw del XPT2046 para landscape rotation=1.
// Ajusta estos valores si el toque no coincide con el area visual.
#define T_XMIN  200
#define T_XMAX  3900
#define T_YMIN  200
#define T_YMAX  3800

// TFT_eSPI ocupa VSPI internamente; el touch usa HSPI para evitar conflicto
SPIClass            touchSPI(HSPI);
XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

struct Punto { int16_t x, y; bool pulsado; };

Punto leerTouch() {
    static Punto        ultimo  = {0, 0, false};
    static unsigned long tUlt   = 0;
    unsigned long ahora = millis();
    if (ahora - tUlt < 20) return ultimo;
    tUlt = ahora;

    Punto p = {0, 0, false};
    if (touch.touched()) {
        TS_Point raw = touch.getPoint();
        // Comenta la linea siguiente una vez calibrado
        // Serial.printf("TOUCH raw x=%d y=%d z=%d\n", raw.x, raw.y, raw.z);
        p.x = constrain(map(raw.x, T_XMIN, T_XMAX, 0, W), 0, W-1);
        p.y = constrain(map(raw.y, T_YMIN, T_YMAX, 0, H), 0, H-1);
        p.pulsado = true;
    }
    ultimo = p;
    return p;
}

// ===================================================
// PREFERENCIAS (NVS / flash)
// ===================================================
Preferences prefs;

long    zonaHoraria = 3600;
int     indicZona   = 13;
uint8_t brillo      = 200;
bool    formato12h  = false;
bool    esAM        = true;   // true=AM, false=PM (valido solo cuando formato12h)
#define MODO_TETRIS    0
#define MODO_ANALOGICO 1
#define MODO_PACMAN    2
#define MODO_CONWAY    3
#define MODO_RETRO     4
#define MODO_FRACTAL   5
#define MODO_PIXEL     6
#define MODO_SOLAR     7
uint8_t modoReloj   = MODO_TETRIS;
// Horario de pantalla
bool    horActivo   = false;
uint8_t encHH       = 7;    // hora de encendido
uint8_t encMM       = 0;
uint8_t apagHH      = 23;   // hora de apagado
uint8_t apagMM      = 0;

struct Zona { const char* nombre; long offset; };
const Zona ZONAS[] = {
    {"UTC-12",-43200},{"UTC-11",-39600},{"UTC-10",-36000},
    {"UTC-9", -32400},{"UTC-8", -28800},{"UTC-7", -25200},
    {"UTC-6", -21600},{"UTC-5", -18000},{"UTC-4", -14400},
    {"UTC-3", -10800},{"UTC-2",  -7200},{"UTC-1",  -3600},
    {"UTC+0",      0},{"UTC+1",   3600},{"UTC+2",   7200},
    {"UTC+3",  10800},{"UTC+4",  14400},{"UTC+5",  18000},
    {"UTC+5:30",19800},{"UTC+6", 21600},{"UTC+7",  25200},
    {"UTC+8",  28800},{"UTC+9",  32400},{"UTC+9:30",34200},
    {"UTC+10", 36000},{"UTC+11", 39600},{"UTC+12", 43200},
};
#define N_ZONAS 27

void cargarPrefs() {
    prefs.begin("reloj", true);
    zonaHoraria = prefs.getLong ("zona",     3600);
    brillo      = prefs.getUChar("brillo",    200);
    formato12h  = prefs.getBool ("fmt12h",  false);
    horActivo   = prefs.getBool ("horAct",  false);
    encHH       = prefs.getUChar("encHH",       7);
    encMM       = prefs.getUChar("encMM",       0);
    apagHH      = prefs.getUChar("apagHH",     23);
    apagMM        = prefs.getUChar("apagMM",      0);
    modoReloj     = prefs.getUChar("modoRel", MODO_TETRIS);
    prefs.end();
    indicZona = 13;
    for (int i = 0; i < N_ZONAS; i++)
        if (ZONAS[i].offset == zonaHoraria) { indicZona = i; break; }
}

void guardarPrefs() {
    prefs.begin("reloj", false);
    prefs.putLong ("zona",     zonaHoraria);
    prefs.putUChar("brillo",   brillo);
    prefs.putBool ("fmt12h",   formato12h);
    prefs.putBool ("horAct",   horActivo);
    prefs.putUChar("encHH",    encHH);
    prefs.putUChar("encMM",    encMM);
    prefs.putUChar("apagHH",   apagHH);
    prefs.putUChar("apagMM",   apagMM);
    prefs.putUChar("modoRel",  modoReloj);
    prefs.end();
}

// ===================================================
// NTP  (offset=0; aplicamos zona manualmente)
// ===================================================
WiFiUDP   udpNTP;
NTPClient ntp(udpNTP, "pool.ntp.org", 0, 30000);
bool wifiOK = false;

struct tm tiempoLocal() {
    time_t local = (time_t)ntp.getEpochTime() + zonaHoraria;
    struct tm* p = gmtime(&local);
    return *p;
}

// ===================================================
// CONFIGURACION TETRIS
// ===================================================
#define BS      13
#define DW       4
#define DH       7
#define GAP      5
#define DW_PX   (DW * BS)
#define DH_PX   (DH * BS)
#define COL_PX  (2  * BS)
#define RX      ((W - (4*DW_PX + COL_PX + 4*GAP)) / 2)
#define RY      ((H - DH_PX) / 2 - 10)

int px[5];

// ===================================================
// COLORES
// ===================================================
#define C_BG    0x0000
#define C_I     0x07FF
#define C_O     0xFFE0
#define C_T     0xC81F
#define C_S     0x07E0
#define C_Z     0xF800
#define C_L     0xFC00
#define C_J     0x001F
#define C_COLON 0xFD20

const uint16_t PALETA[]   = { C_I, C_O, C_T, C_S, C_Z, C_L, C_J };
const uint16_t PAL_DECO[] = { 0x0411, 0x4200, 0x3003, 0x0300,
                               0x3800, 0x3C00, 0x0009 };
#define N_PAL   7

uint16_t colorOscuro(uint16_t c) {
    return (uint16_t)((((c>>11)&0x1F)/2)<<11
                    | (((c>> 5)&0x3F)/2)<< 5
                    |  ((c     &0x1F)/2)      );
}

// ===================================================
// FUENTE 4x7  (indice 10 = digito en blanco para 12h)
// ===================================================
#define DIGITO_BLANCO 10
const uint8_t FONT[11][DH] = {
    {0xE,0xA,0xA,0xA,0xA,0xA,0xE},  // 0
    {0x4,0xC,0x4,0x4,0x4,0x4,0xE},  // 1
    {0xE,0x2,0x2,0xE,0x8,0x8,0xE},  // 2
    {0xE,0x2,0x2,0xE,0x2,0x2,0xE},  // 3
    {0xA,0xA,0xA,0xE,0x2,0x2,0x2},  // 4
    {0xE,0x8,0x8,0xE,0x2,0x2,0xE},  // 5
    {0xE,0x8,0x8,0xE,0xA,0xA,0xE},  // 6
    {0xE,0x2,0x2,0x4,0x4,0x4,0x4},  // 7
    {0xE,0xA,0xA,0xE,0xA,0xA,0xE},  // 8
    {0xE,0xA,0xA,0xE,0x2,0x2,0xE},  // 9
    {0x0,0x0,0x0,0x0,0x0,0x0,0x0},  // 10 = blanco (12h hora <10)
};
inline bool pixelOn(int d, int col, int fila) {
    return (FONT[d][fila] >> (3-col)) & 1;
}

// ===================================================
// PIEZAS DECORATIVAS
// ===================================================
#define N_DECO 10

const int8_t SHAPES[7][4][2] = {
    {{0,0},{1,0},{2,0},{3,0}},
    {{0,0},{1,0},{0,1},{1,1}},
    {{1,0},{0,1},{1,1},{2,1}},
    {{1,0},{2,0},{0,1},{1,1}},
    {{0,0},{1,0},{1,1},{2,1}},
    {{0,0},{0,1},{1,1},{2,1}},
    {{2,0},{0,1},{1,1},{2,1}},
};

struct Deco {
    float x, y, prevX, prevY, vy;
    uint8_t tipo; uint16_t color; bool drawn;
};

Deco decos[N_DECO];

void resetDeco(Deco& d, bool fromTop) {
    d.tipo  = random(0, 7);
    d.color = PAL_DECO[d.tipo];
    d.vy    = 0.3f + random(0, 20) / 20.0f;
    d.x     = random(0, W/BS) * BS;
    d.y     = fromTop ? (float)(-BS*5) : (float)random(-H, 0);
    d.prevX = d.x;  d.prevY = d.y;
    d.drawn = false;
}

// ===================================================
// BLOQUES DE DIGITOS
// ===================================================
struct Bloque {
    float y, prevY, vy;
    int16_t yDest;
    uint16_t color;
    bool activo, cayendo, drawn;
};

Bloque bloques[4][DW][DH];
int    digMostrado[4] = {-1,-1,-1,-1};

// ===================================================
// ZONAS EXCLUIDAS PARA DECO
// ===================================================
#define CLOCK_X1  RX
#define CLOCK_Y1  RY
#define CLOCK_X2  (RX + 4*DW_PX + COL_PX + 4*GAP)
#define CLOCK_Y2  (RY + DH_PX)
#define DATE_Y1   (H - 24)

inline bool enZonaExcluida(int x, int y) {
    bool reloj = (x+BS > CLOCK_X1 && x < CLOCK_X2 &&
                  y+BS > CLOCK_Y1 && y < CLOCK_Y2);
    bool fecha = (y+BS > DATE_Y1);
    return reloj || fecha;
}

// ===================================================
// PRIMITIVAS
// ===================================================
void calcPosiciones() {
    px[0] = RX;
    px[1] = px[0] + DW_PX + GAP;
    px[2] = px[1] + DW_PX + GAP;
    px[3] = px[2] + COL_PX + GAP;
    px[4] = px[3] + DW_PX + GAP;
}
inline int xDig(int d) { return (d < 2) ? px[d] : px[d+1]; }

void drawBlock(int x, int y, uint16_t c) {
    uint16_t dark = colorOscuro(c);
    tft.fillRect(x+1, y+1, BS-2, BS-2, c);
    tft.drawFastHLine(x,      y,      BS,   TFT_WHITE);
    tft.drawFastVLine(x,      y+1,    BS-1, TFT_WHITE);
    tft.drawFastHLine(x+1,    y+BS-1, BS-1, dark);
    tft.drawFastVLine(x+BS-1, y+1,    BS-1, dark);
}

inline void eraseRect(int x, int y, int w, int h) {
    tft.fillRect(x, y, w, h, C_BG);
}

// ===================================================
// GESTION DE DIGITOS
// ===================================================
void lanzarDigito(int pos, int dig) {
    int xBase = xDig(pos);
    for (int c = 0; c < DW; c++)
        for (int f = 0; f < DH; f++) {
            Bloque& b = bloques[pos][c][f];
            if (b.activo && b.drawn) {
                int by = (int)b.y;
                if (by >= 0 && by+BS <= H)
                    eraseRect(xBase + c*BS, by, BS, BS);
            }
        }
    for (int c = 0; c < DW; c++)
        for (int f = 0; f < DH; f++) {
            Bloque& b = bloques[pos][c][f];
            if (pixelOn(dig, c, f)) {
                b.yDest   = RY + f * BS;
                b.y       = (float)(-(random(1,6) + f) * BS);
                b.prevY   = b.y;
                b.vy      = 1.5f + random(0, 40) / 10.0f;
                b.color   = PALETA[random(0, N_PAL)];
                b.activo  = true;
                b.cayendo = true;
                b.drawn   = false;
            } else {
                b.activo  = false;
                b.cayendo = false;
                b.drawn   = false;
            }
        }
}

void setHora(int h, int m) {
    int d[4];
    // Formato 12h: horas 1-9 no llevan cero delante (digito en blanco)
    d[0] = (formato12h && h < 10) ? DIGITO_BLANCO : h/10;
    d[1] = h % 10;
    d[2] = m / 10;
    d[3] = m % 10;
    for (int i = 0; i < 4; i++)
        if (d[i] != digMostrado[i]) {
            lanzarDigito(i, d[i]);
            digMostrado[i] = d[i];
        }
}

// ===================================================
// COLON Y FECHA
// ===================================================
bool colonVis = true;
bool colonAnt = false;
int  diaAnt   = -1;
bool wifiAnt  = false;

void drawColon(bool visible) {
    int cx  = px[2] + (COL_PX - BS) / 2;
    int cy1 = RY + 2*BS;
    int cy2 = RY + 4*BS;
    if (visible) {
        drawBlock(cx, cy1, C_COLON);
        drawBlock(cx, cy2, C_COLON);
    } else {
        eraseRect(cx, cy1, BS, BS);
        eraseRect(cx, cy2, BS, BS);
    }
}

void drawFechaWifi() {
    const char* MES[] = {"Ene","Feb","Mar","Abr","May","Jun",
                         "Jul","Ago","Sep","Oct","Nov","Dic"};
    struct tm ti = tiempoLocal();
    char buf[24];
    snprintf(buf, sizeof(buf), "%d %s %d",
             ti.tm_mday, MES[ti.tm_mon], ti.tm_year + 1900);
    eraseRect(0, H-22, W, 22);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x7BEF, C_BG);
    tft.drawString(buf, W/2, H-11);
    tft.setTextDatum(MR_DATUM);
    if (wifiOK) {
        tft.setTextColor(0x07E0, C_BG);
        tft.drawString("WiFi", W-4, H-11);
    } else {
        tft.setTextColor(TFT_RED, C_BG);
        tft.drawString("Sin WiFi", W-4, H-11);
    }
    diaAnt  = ti.tm_mday;
    wifiAnt = wifiOK;
}

// ===================================================
// RENDER FRAME
// ===================================================
void renderFrame() {
    tft.startWrite();
    for (int i = 0; i < N_DECO; i++) {
        Deco& d = decos[i];
        if (!d.drawn) continue;
        for (int b = 0; b < 4; b++) {
            int bx = (int)d.prevX + SHAPES[d.tipo][b][0]*BS;
            int by = (int)d.prevY + SHAPES[d.tipo][b][1]*BS;
            if (bx >= 0 && bx+BS <= W && by >= 0 && by+BS <= H && !enZonaExcluida(bx, by))
                tft.fillRect(bx, by, BS, BS, C_BG);
        }
    }
    for (int pos = 0; pos < 4; pos++) {
        int xBase = xDig(pos);
        for (int c = 0; c < DW; c++)
            for (int f = 0; f < DH; f++) {
                Bloque& b = bloques[pos][c][f];
                if (!b.activo || !b.cayendo || !b.drawn) continue;
                int by = (int)b.prevY;
                if (by >= 0 && by+BS <= H)
                    tft.fillRect(xBase + c*BS, by, BS, BS, C_BG);
            }
    }
    for (int i = 0; i < N_DECO; i++) {
        Deco& d = decos[i];
        d.prevX = d.x; d.prevY = d.y;
        d.y += d.vy;
        if (d.y > H + BS*4) resetDeco(d, true);
    }
    for (int pos = 0; pos < 4; pos++)
        for (int c = 0; c < DW; c++)
            for (int f = 0; f < DH; f++) {
                Bloque& b = bloques[pos][c][f];
                if (!b.cayendo) continue;
                b.prevY = b.y;
                b.y    += b.vy;
                if (b.y >= b.yDest) { b.y = b.yDest; b.cayendo = false; }
            }
    for (int i = 0; i < N_DECO; i++) {
        Deco& d = decos[i];
        for (int b = 0; b < 4; b++) {
            int bx = (int)d.x + SHAPES[d.tipo][b][0]*BS;
            int by = (int)d.y + SHAPES[d.tipo][b][1]*BS;
            if (bx >= 0 && bx+BS <= W && by >= 0 && by+BS <= H && !enZonaExcluida(bx, by))
                tft.fillRect(bx+1, by+1, BS-2, BS-2, d.color);
        }
        d.drawn = true;
    }
    for (int pos = 0; pos < 4; pos++) {
        int xBase = xDig(pos);
        for (int c = 0; c < DW; c++)
            for (int f = 0; f < DH; f++) {
                Bloque& b = bloques[pos][c][f];
                if (!b.activo) continue;
                int bx = xBase + c*BS;
                int by = (int)b.y;
                if (by+BS > 0 && by < H) {
                    if (by >= 0) drawBlock(bx, by, b.color);
                    else {
                        int vis = BS + by;
                        if (vis > 0) tft.fillRect(bx+1, 0, BS-2, vis-1, b.color);
                    }
                    b.drawn = true;
                }
            }
    }
    if (colonVis != colonAnt) { drawColon(colonVis); colonAnt = colonVis; }
    tft.endWrite();
}

// ===================================================
// RELOJ ANALOGICO  (alto detalle, sin parpadeo)
// Estrategia: esfera estatica dibujada UNA sola vez;
// cada segundo solo se borran y redibujan las manecillas.
// Pixels actualizados por tick: ~2000 vs ~31000 antes.
// ===================================================
#define AC_CX    (W / 2)
#define AC_CY    ((H - 24) / 2)
#define AC_R     102
#define AC_FACE  0x0C63   // color de la esfera (azul muy oscuro)

int  sAntAna     = -1;    // segundo anterior
bool anaFaceReady = false; // false = hay que (re)dibujar la esfera

// ── Manecilla como triangulo ──────────────────────
void drawHandAna(int cx, int cy, float a,
                 int len, int tail, int thick, uint16_t col) {
    float sa = sinf(a), ca = cosf(a);
    float sp = sinf(a + (float)M_PI / 2.0f);
    float cp = cosf(a + (float)M_PI / 2.0f);
    int hw = thick / 2;
    int16_t x0 = cx - (int)(tail*sa) + (int)(hw*sp);
    int16_t y0 = cy + (int)(tail*ca) - (int)(hw*cp);
    int16_t x1 = cx - (int)(tail*sa) - (int)(hw*sp);
    int16_t y1 = cy + (int)(tail*ca) + (int)(hw*cp);
    int16_t x2 = cx + (int)(len*sa);
    int16_t y2 = cy - (int)(len*ca);
    tft.fillTriangle(x0, y0, x1, y1, x2, y2, col);
}

// ── Ticks (60 marcas) ────────────────────────────
void drawTicksAna(int cx, int cy, int r) {
    for (int i = 0; i < 60; i++) {
        float a  = i * 6.0f * DEG_TO_RAD;
        float sa = sinf(a), ca = cosf(a);
        bool  isH = (i % 5 == 0);
        int r1 = r - 6, r2 = isH ? r - 18 : r - 10;
        tft.drawLine(cx+(int)(r1*sa)+1, cy-(int)(r1*ca)+1,
                     cx+(int)(r2*sa)+1, cy-(int)(r2*ca)+1, 0x0841); // sombra
        uint16_t mc = isH ? TFT_WHITE : 0x8C71;
        if (isH) {
            float px = cosf(a), py = sinf(a);
            for (int d = -1; d <= 1; d++)
                tft.drawLine(cx+(int)(r1*sa+d*px), cy-(int)(r1*ca-d*py),
                             cx+(int)(r2*sa+d*px), cy-(int)(r2*ca-d*py), mc);
        } else {
            tft.drawLine(cx+(int)(r1*sa), cy-(int)(r1*ca),
                         cx+(int)(r2*sa), cy-(int)(r2*ca), mc);
        }
    }
}

// ── Numeros 1-12 ─────────────────────────────────
void drawNumsAna(int cx, int cy, int r) {
    tft.setTextFont(2);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0xFFF0, AC_FACE);
    for (int i = 1; i <= 12; i++) {
        float a = (i % 12) * 30.0f * DEG_TO_RAD;
        tft.drawString(String(i).c_str(),
                       cx + (int)((r-26)*sinf(a)),
                       cy - (int)((r-26)*cosf(a)));
    }
}

// ── Dibuja manecillas nuevas ──────────────────────
void drawHandsAna(int cx, int cy, int r,
                  float hA, float mA, float sA) {
    // sombras
    drawHandAna(cx+2, cy+2, hA, r*52/100, r*10/100, 10, 0x0821);
    drawHandAna(cx+2, cy+2, mA, r*76/100, r* 6/100,  6, 0x0821);
    // horas (dorado), minutos (blanco)
    drawHandAna(cx, cy, hA, r*52/100, r*10/100, 10, 0xEF7D);
    drawHandAna(cx, cy, mA, r*76/100, r* 6/100,  6, 0xFFFF);
    // segundos: linea roja + contrapeso
    float sa = sinf(sA), ca = cosf(sA);
    int tx = cx-(int)(r*18/100*sa), ty = cy+(int)(r*18/100*ca);
    int hx = cx+(int)(r*86/100*sa), hy = cy-(int)(r*86/100*ca);
    tft.drawLine(tx,   ty, hx,   hy, 0xF800);
    tft.drawLine(tx+1, ty, hx+1, hy, 0xF800);
    tft.fillCircle(tx, ty, 5, 0xF800);
    // centro
    tft.fillCircle(cx, cy, 5, 0xF800);
    tft.fillCircle(cx, cy, 3, TFT_WHITE);
}

// ── Borra manecillas (pinta en color esfera) ─────
void eraseHandsAna(int cx, int cy, int r,
                   float hA, float mA, float sA) {
    // horas (+2px margen para cubrir sombra)
    drawHandAna(cx+2, cy+2, hA, r*52/100, r*10/100, 14, AC_FACE);
    drawHandAna(cx,   cy,   hA, r*52/100, r*10/100, 14, AC_FACE);
    // minutos
    drawHandAna(cx+2, cy+2, mA, r*76/100, r* 6/100, 10, AC_FACE);
    drawHandAna(cx,   cy,   mA, r*76/100, r* 6/100, 10, AC_FACE);
    // segundos
    float sa = sinf(sA), ca = cosf(sA);
    int tx = cx-(int)(r*18/100*sa), ty = cy+(int)(r*18/100*ca);
    int hx = cx+(int)(r*86/100*sa), hy = cy-(int)(r*86/100*ca);
    for (int d = -1; d <= 1; d++) {
        tft.drawLine(tx+d, ty,   hx+d, hy,   AC_FACE);
        tft.drawLine(tx,   ty+d, hx,   hy+d, AC_FACE);
    }
    tft.fillCircle(tx, ty, 7, AC_FACE);  // contrapeso
    tft.fillCircle(cx, cy, 7, AC_FACE);  // centro
}

// ── Punto de entrada principal ───────────────────
void drawAnalogClock(int h, int m, int s) {
    const int cx = AC_CX, cy = AC_CY, r = AC_R;

    float hA = ((h % 12) * 30.0f + m * 0.5f + s / 120.0f) * DEG_TO_RAD;
    float mA = (m * 6.0f  + s * 0.1f)                     * DEG_TO_RAD;
    float sA =  s * 6.0f                                   * DEG_TO_RAD;

    static float prevHA = 0, prevMA = 0, prevSA = 0;

    tft.startWrite();

    if (!anaFaceReady) {
        // Dibujar esfera completa (solo la primera vez)
        tft.fillRect(0, 0, W, H - 24, C_BG);
        tft.fillCircle(cx, cy, r + 2, 0x6B4D);  // bisel dorado
        tft.fillCircle(cx, cy, r,     0x9492);  // anillo plata
        tft.fillCircle(cx, cy, r - 3, 0x3186);  // anillo azul
        tft.fillCircle(cx, cy, r - 5, AC_FACE); // cara
        drawTicksAna(cx, cy, r);
        drawNumsAna(cx, cy, r);
        anaFaceReady = true;
    } else {
        // Borrar manecillas anteriores (~1400 px)
        eraseHandsAna(cx, cy, r, prevHA, prevMA, prevSA);
        // Restaurar ticks y numeros que pudieran haberse tapado
        drawTicksAna(cx, cy, r);
        drawNumsAna(cx, cy, r);
    }

    // Dibujar nuevas manecillas (~600 px)
    drawHandsAna(cx, cy, r, hA, mA, sA);

    // AM/PM fuera del circulo, derecha
    if (formato12h) {
        tft.setTextFont(2); tft.setTextDatum(MR_DATUM);
        tft.setTextColor(TFT_WHITE, C_BG);
        tft.drawString(esAM ? "AM" : "PM", W - 4, AC_CY);
    }

    prevHA = hA;  prevMA = mA;  prevSA = sA;
    tft.endWrite();
}

// ===================================================
// RELOJ PAC-MAN - LABERINTO AUTENTICO
// Layout: hora (font4) en y=0..31; laberinto 28x31
// tiles (11x6 px) en y=32..217; fecha en y=218..239.
// IA: BFS a punto mas cercano para Pac-Man;
//     persecucion/huida para los 4 fantasmas.
// ===================================================

#define MT_EMPTY  0   // corredor (sin punto o ya comido)
#define MT_WALL   1   // pared
#define MT_DOT    2   // punto pequeno
#define MT_PILL   3   // pastilla de poder
#define MT_DOOR   4   // puerta casa de fantasmas

#define PM_COLS  28
#define PM_ROWS  31
#define PM_TW    11          // ancho tile px
#define PM_TH    6           // alto tile px
#define PM_MX    6           // margen izquierdo: (320-28*11)/2
#define PM_MY    32          // margen superior (zona hora = 32 px)
#define PM_SPD   1.4f        // velocidad Pac-Man px/frame
#define PM_GSPD  0.9f        // velocidad fantasmas px/frame

#define PM_WALL_C  0x0010
#define PM_DOT_C   0xFFF0
#define PM_PILL_C  0xFFFF
#define PM_BG_C    TFT_BLACK
#define PM_PAC_C   TFT_YELLOW

const uint16_t PM_GHOST_C[4] = { TFT_RED, 0xFE19, TFT_CYAN, 0xFD20 };

static const int8_t PM_DX[4] = {  1, 0, -1,  0 };
static const int8_t PM_DY[4] = {  0, 1,  0, -1 };

// Mapa clasico Pac-Man (Level 1, aproximacion fiel)
static const uint8_t PM_MAP[PM_ROWS][PM_COLS] PROGMEM = {
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}, // 0
    {1,2,2,2,2,2,2,2,2,2,2,2,2,1,1,2,2,2,2,2,2,2,2,2,2,2,2,1}, // 1
    {1,2,1,1,1,1,2,1,1,1,1,1,2,1,1,2,1,1,1,1,1,2,1,1,1,1,2,1}, // 2
    {1,3,1,0,0,1,2,1,0,0,0,1,2,1,1,2,1,0,0,0,1,2,1,0,0,1,3,1}, // 3
    {1,2,1,1,1,1,2,1,1,1,1,1,2,1,1,2,1,1,1,1,1,2,1,1,1,1,2,1}, // 4
    {1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1}, // 5
    {1,2,1,1,1,1,2,1,1,2,1,1,1,1,1,1,1,1,2,1,1,2,1,1,1,1,2,1}, // 6
    {1,2,1,1,1,1,2,1,1,2,1,1,1,1,1,1,1,1,2,1,1,2,1,1,1,1,2,1}, // 7
    {1,2,2,2,2,2,2,1,1,2,2,2,2,1,1,2,2,2,2,1,1,2,2,2,2,2,2,1}, // 8
    {1,1,1,1,1,1,2,1,1,1,1,1,0,1,1,0,1,1,1,1,1,2,1,1,1,1,1,1}, // 9
    {0,0,0,0,0,1,2,1,1,0,0,0,0,0,0,0,0,0,0,1,1,2,1,0,0,0,0,0}, // 10
    {0,0,0,0,0,1,2,1,1,0,1,1,1,0,0,1,1,1,0,1,1,2,1,0,0,0,0,0}, // 11
    {0,0,0,0,0,1,2,1,1,0,1,0,0,0,0,0,0,1,0,1,1,2,1,0,0,0,0,0}, // 12
    {1,1,1,1,1,1,2,0,0,0,1,0,0,0,0,0,0,1,0,0,0,2,1,1,1,1,1,1}, // 13
    {0,0,0,0,0,0,2,0,0,0,1,0,0,0,0,0,0,1,0,0,0,2,0,0,0,0,0,0}, // 14 tunel
    {1,1,1,1,1,1,2,0,0,0,1,1,4,1,1,4,1,1,0,0,0,2,1,1,1,1,1,1}, // 15 puerta
    {0,0,0,0,0,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,1,0,0,0,0,0}, // 16
    {0,0,0,0,0,1,2,0,0,0,1,1,1,1,1,1,1,1,0,0,0,2,1,0,0,0,0,0}, // 17
    {0,0,0,0,0,1,2,0,0,0,0,0,0,0,0,0,0,0,0,0,0,2,1,0,0,0,0,0}, // 18
    {1,1,1,1,1,1,2,1,1,1,1,1,0,1,1,0,1,1,1,1,1,2,1,1,1,1,1,1}, // 19
    {1,2,2,2,2,2,2,2,2,2,2,2,2,1,1,2,2,2,2,2,2,2,2,2,2,2,2,1}, // 20
    {1,2,1,1,1,1,2,1,1,1,1,1,2,1,1,2,1,1,1,1,1,2,1,1,1,1,2,1}, // 21
    {1,2,1,1,1,1,2,1,1,1,1,1,2,1,1,2,1,1,1,1,1,2,1,1,1,1,2,1}, // 22
    {1,3,2,2,1,1,2,2,2,2,2,2,2,0,0,2,2,2,2,2,2,2,1,1,2,2,3,1}, // 23
    {1,1,1,2,1,1,2,1,1,2,1,1,1,1,1,1,1,1,2,1,1,2,1,1,2,1,1,1}, // 24
    {1,1,1,2,1,1,2,1,1,2,1,1,1,1,1,1,1,1,2,1,1,2,1,1,2,1,1,1}, // 25
    {1,2,2,2,2,2,2,1,1,2,2,2,2,1,1,2,2,2,2,1,1,2,2,2,2,2,2,1}, // 26
    {1,2,1,1,1,1,1,1,1,1,1,1,2,1,1,2,1,1,1,1,1,1,1,1,1,1,2,1}, // 27
    {1,2,1,1,1,1,1,1,1,1,1,1,2,1,1,2,1,1,1,1,1,1,1,1,1,1,2,1}, // 28
    {1,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,2,1}, // 29
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}, // 30
};

uint8_t pmMaze[PM_ROWS][PM_COLS];
int     pmDotsLeft  = 0;
int     pmTotalDots = 0;

struct PmEnt {
    float  x, y;
    int    col, row;
    int8_t dir, nextDir;
    float  speed;
};

PmEnt  pmPac;
PmEnt  pmGhosts[4];
bool   pmFaceReady = false;
bool   pmMouthOpen = true;
int    pmMouthTick = 0;
int    pmTimePrev  = -1;

inline int pmTilePx(int c) { return PM_MX + c * PM_TW + PM_TW / 2; }
inline int pmTilePy(int r) { return PM_MY + r * PM_TH + PM_TH / 2; }

inline bool pmCanMove(int c, int r) {
    if (r < 0 || r >= PM_ROWS) return false;
    if (c < 0 || c >= PM_COLS) return true; // tunel
    uint8_t t = pmMaze[r][c];
    return t != MT_WALL && t != MT_DOOR;
}
inline bool pmGhostOk(int c, int r) {
    if (r < 0 || r >= PM_ROWS) return false;
    if (c < 0 || c >= PM_COLS) return true;
    return pmMaze[r][c] != MT_WALL;
}

void pmDrawTile(int c, int r) {
    if (c < 0 || c >= PM_COLS || r < 0 || r >= PM_ROWS) return;
    int px0 = PM_MX + c * PM_TW, py0 = PM_MY + r * PM_TH;
    uint8_t t = pmMaze[r][c];
    if (t == MT_WALL) {
        tft.fillRect(px0, py0, PM_TW, PM_TH, PM_WALL_C);
    } else if (t == MT_DOOR) {
        tft.fillRect(px0, py0, PM_TW, PM_TH, PM_WALL_C);
        tft.drawFastHLine(px0, py0 + PM_TH / 2, PM_TW, 0xFFE0);
    } else if (t == MT_DOT) {
        tft.fillRect(px0, py0, PM_TW, PM_TH, PM_BG_C);
        tft.fillRect(pmTilePx(c) - 1, pmTilePy(r) - 1, 2, 2, PM_DOT_C);
    } else if (t == MT_PILL) {
        tft.fillRect(px0, py0, PM_TW, PM_TH, PM_BG_C);
        tft.fillCircle(pmTilePx(c), pmTilePy(r), 3, PM_PILL_C);
    } else {
        tft.fillRect(px0, py0, PM_TW, PM_TH, PM_BG_C);
    }
}

void pmDrawMaze() {
    tft.fillRect(0, 0, W, PM_MY, PM_BG_C);
    // Margenes laterales (fuera del area de tiles) en color pared
    tft.fillRect(0,       PM_MY, PM_MX, PM_ROWS * PM_TH, PM_WALL_C);
    tft.fillRect(W-PM_MX, PM_MY, PM_MX, PM_ROWS * PM_TH, PM_WALL_C);
    for (int r = 0; r < PM_ROWS; r++)
        for (int c = 0; c < PM_COLS; c++)
            pmDrawTile(c, r);
}

void pmDrawTime(int h, int m) {
    char buf[6];
    snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    tft.fillRect(0, 0, W, PM_MY, PM_BG_C);
    tft.setTextFont(4);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_YELLOW, PM_BG_C);
    tft.drawString(buf, W / 2, PM_MY / 2);
    // 3 vidas a la izquierda
    for (int i = 0; i < 3; i++)
        tft.fillCircle(8 + i * 18, PM_MY / 2, 5, TFT_YELLOW);
    // Puntuacion (dots comidos) a la derecha
    char sc[8]; snprintf(sc, sizeof(sc), "%d", pmTotalDots - pmDotsLeft);
    tft.setTextDatum(MR_DATUM); tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, PM_BG_C);
    tft.drawString(sc, W - 4, PM_MY / 2);
    if (formato12h) {
        tft.setTextFont(1); tft.setTextDatum(MR_DATUM);
        tft.setTextColor(TFT_YELLOW, PM_BG_C);
        tft.drawString(esAM ? "AM" : "PM", W - 4, 5);
    }
}

// BFS desde (sc,sr): devuelve primera direccion hacia el punto mas cercano
int8_t pmBfsDir(int sc, int sr, int8_t curDir) {
    static uint8_t  visited[PM_ROWS][PM_COLS];
    static int8_t   fdir[PM_ROWS][PM_COLS];
    static uint16_t queue[PM_ROWS * PM_COLS];
    memset(visited, 0, sizeof(visited));
    int qH = 0, qT = 0;
    visited[sr][sc] = 1;
    for (int8_t d = 0; d < 4; d++) {
        if (d == ((curDir + 2) & 3)) continue;
        int nc = sc + PM_DX[d], nr = sr + PM_DY[d];
        if (nc < 0) nc = PM_COLS - 1; if (nc >= PM_COLS) nc = 0;
        if (!pmCanMove(nc, nr) || visited[nr][nc]) continue;
        visited[nr][nc] = 1; fdir[nr][nc] = d;
        queue[qT++] = (uint16_t)(nr * PM_COLS + nc);
    }
    while (qH < qT) {
        uint16_t cur = queue[qH++];
        int r = cur / PM_COLS, c = cur % PM_COLS;
        uint8_t t = pmMaze[r][c];
        if (t == MT_DOT || t == MT_PILL) return fdir[r][c];
        for (int8_t d = 0; d < 4; d++) {
            int nc = c + PM_DX[d], nr = r + PM_DY[d];
            if (nc < 0) nc = PM_COLS - 1; if (nc >= PM_COLS) nc = 0;
            if (!pmCanMove(nc, nr) || visited[nr][nc]) continue;
            visited[nr][nc] = 1; fdir[nr][nc] = fdir[r][c];
            queue[qT++] = (uint16_t)(nr * PM_COLS + nc);
        }
    }
    return curDir;
}

// IA fantasma: hacia Pac-Man (con variantes por indice)
int8_t pmGhostDir(int gc, int gr, int8_t gdir, int pc, int pr, int idx) {
    int8_t rev = (gdir + 2) & 3, best = gdir;
    int bestD = 99999;
    for (int8_t d = 0; d < 4; d++) {
        if (d == rev) continue;
        int nc = gc + PM_DX[d], nr = gr + PM_DY[d];
        if (nc < 0) nc = PM_COLS - 1; if (nc >= PM_COLS) nc = 0;
        if (!pmGhostOk(nc, nr)) continue;
        int dist = abs(nc - pc) + abs(nr - pr);
        if (idx == 1) dist = 200 - dist;              // Pinky huye
        if (idx == 3) dist += (int)random(0, 30);     // Clyde: aleatorio
        if (dist < bestD) { bestD = dist; best = d; }
    }
    return best;
}

void initPacman() {
    pmDotsLeft = pmTotalDots = 0;
    for (int r = 0; r < PM_ROWS; r++)
        for (int c = 0; c < PM_COLS; c++) {
            pmMaze[r][c] = pgm_read_byte(&PM_MAP[r][c]);
            uint8_t t = pmMaze[r][c];
            if (t == MT_DOT || t == MT_PILL) { pmDotsLeft++; pmTotalDots++; }
        }
    pmPac     = { (float)pmTilePx(13), (float)pmTilePy(23), 13, 23, 0, 0, PM_SPD };
    // Fantasmas: 2 dentro casa, 2 saliendo
    int gc[4] = {13, 14, 11, 16};
    int gr[4] = {13, 12, 12, 12};
    for (int i = 0; i < 4; i++)
        pmGhosts[i] = { (float)pmTilePx(gc[i]), (float)pmTilePy(gr[i]),
                        gc[i], gr[i], (int8_t)(i%4), (int8_t)(i%4), PM_GSPD };
    pmFaceReady = false; pmMouthOpen = true;
    pmMouthTick = 0;     pmTimePrev  = -1;
}

// Restaura los tiles cubiertos por un sprite
void pmErase(float fx, float fy, int rad) {
    int x = (int)fx, y = (int)fy;
    int c0 = constrain((x - rad - PM_MX) / PM_TW,     0, PM_COLS-1);
    int c1 = constrain((x + rad - PM_MX) / PM_TW + 1, 0, PM_COLS-1);
    int r0 = constrain((y - rad - PM_MY) / PM_TH,     0, PM_ROWS-1);
    int r1 = constrain((y + rad - PM_MY) / PM_TH + 1, 0, PM_ROWS-1);
    for (int r = r0; r <= r1; r++)
        for (int c = c0; c <= c1; c++)
            pmDrawTile(c, r);
}

void pmDrawPac(int x, int y, bool mouth, int8_t dir) {
    tft.fillCircle(x, y, 7, PM_PAC_C);
    if (mouth) {
        float da = dir * (float)M_PI / 2.0f, ma = 0.48f, R2 = 14.0f;
        tft.fillTriangle(x, y,
            x+(int)(R2*cosf(da-ma)), y+(int)(R2*sinf(da-ma)),
            x+(int)(R2*cosf(da+ma)), y+(int)(R2*sinf(da+ma)),
            PM_BG_C);
    }
    float ea = dir*(float)M_PI/2.0f - (float)M_PI/3.0f;
    tft.fillCircle(x+(int)(4*cosf(ea)), y+(int)(4*sinf(ea)), 1, PM_BG_C);
}

void pmDrawGhost(int x, int y, uint16_t col) {
    tft.fillCircle(x, y-2, 6, col);
    tft.fillRect(x-6, y-2, 12, 9, col);
    tft.fillTriangle(x-6,y+7, x-3,y+4, x,  y+7, PM_BG_C);
    tft.fillTriangle(x,  y+7, x+3,y+4, x+6,y+7, PM_BG_C);
    tft.fillCircle(x-2, y-3, 2, TFT_WHITE); tft.fillCircle(x+3, y-3, 2, TFT_WHITE);
    tft.fillCircle(x-2, y-3, 1, TFT_BLUE);  tft.fillCircle(x+3, y-3, 1, TFT_BLUE);
}

// Mueve entidad un paso; devuelve true al llegar al centro de un nuevo tile
bool pmMove(PmEnt& e, bool isPac) {
    int tc = e.col + PM_DX[e.dir], tr = e.row + PM_DY[e.dir];
    if (tc < 0) tc = PM_COLS-1; if (tc >= PM_COLS) tc = 0;
    bool ok = isPac ? pmCanMove(tc, tr) : pmGhostOk(tc, tr);
    if (!ok) {
        if (e.nextDir != e.dir) {
            int nc2 = e.col + PM_DX[e.nextDir], nr2 = e.row + PM_DY[e.nextDir];
            if (nc2 < 0) nc2 = PM_COLS-1; if (nc2 >= PM_COLS) nc2 = 0;
            bool ok2 = isPac ? pmCanMove(nc2,nr2) : pmGhostOk(nc2,nr2);
            if (ok2) { e.dir = e.nextDir; return pmMove(e, isPac); }
        }
        return false;
    }
    float tx = (float)pmTilePx(tc), ty = (float)pmTilePy(tr);
    float dist = fabsf(tx-e.x) + fabsf(ty-e.y);
    if (dist <= e.speed) {
        e.x = tx; e.y = ty; e.col = tc; e.row = tr;
        return true;
    }
    e.x += PM_DX[e.dir] * e.speed;
    e.y += PM_DY[e.dir] * e.speed;
    return false;
}

void updatePacmanClock(int h, int m, int s) {
    (void)s;
    pmMouthTick++;
    if (pmMouthTick >= 8) { pmMouthOpen = !pmMouthOpen; pmMouthTick = 0; }

    tft.startWrite();

    if (!pmFaceReady) { pmDrawMaze(); pmFaceReady = true; pmTimePrev = -1; }

    int tNow = h * 60 + m;
    if (tNow != pmTimePrev) { pmTimePrev = tNow; pmDrawTime(h, m); }

    // Guardar posiciones viejas
    float opx = pmPac.x, opy = pmPac.y;
    float ogx[4], ogy[4];
    for (int i = 0; i < 4; i++) { ogx[i]=pmGhosts[i].x; ogy[i]=pmGhosts[i].y; }

    // Mover Pac-Man
    if (pmMove(pmPac, true)) {
        uint8_t& tile = pmMaze[pmPac.row][pmPac.col];
        if (tile == MT_DOT || tile == MT_PILL) { tile = MT_EMPTY; pmDotsLeft--; }
        int8_t nd = pmBfsDir(pmPac.col, pmPac.row, pmPac.dir);
        int nc2 = pmPac.col+PM_DX[nd], nr2 = pmPac.row+PM_DY[nd];
        if (nc2<0) nc2=PM_COLS-1; if (nc2>=PM_COLS) nc2=0;
        if (pmCanMove(nc2,nr2)) pmPac.dir = nd;
        pmPac.nextDir = nd;
        if (pmDotsLeft <= 0) {
            for (int r=0;r<PM_ROWS;r++) for (int c=0;c<PM_COLS;c++) {
                uint8_t orig = pgm_read_byte(&PM_MAP[r][c]);
                if (orig==MT_DOT||orig==MT_PILL) pmMaze[r][c]=orig;
            }
            pmDotsLeft = pmTotalDots;
            pmFaceReady = false;
        }
    }

    // Mover fantasmas
    for (int i = 0; i < 4; i++) {
        if (pmMove(pmGhosts[i], false)) {
            int8_t nd = pmGhostDir(pmGhosts[i].col, pmGhosts[i].row,
                                    pmGhosts[i].dir, pmPac.col, pmPac.row, i);
            int nc2 = pmGhosts[i].col+PM_DX[nd], nr2 = pmGhosts[i].row+PM_DY[nd];
            if (nc2<0) nc2=PM_COLS-1; if (nc2>=PM_COLS) nc2=0;
            if (pmGhostOk(nc2,nr2)) pmGhosts[i].dir = nd;
            pmGhosts[i].nextDir = nd;
        }
    }

    // Borrar posiciones antiguas (restaurar tiles)
    pmErase(opx, opy, 9);
    for (int i=0;i<4;i++) pmErase(ogx[i], ogy[i], 9);

    // Dibujar posiciones nuevas
    for (int i=0;i<4;i++) pmDrawGhost((int)pmGhosts[i].x,(int)pmGhosts[i].y,PM_GHOST_C[i]);
    pmDrawPac((int)pmPac.x,(int)pmPac.y, pmMouthOpen, pmPac.dir);

    tft.endWrite();
}

// ===================================================
// RELOJ CONWAY - JUEGO DE LA VIDA
// Cuadricula 80x46 celdas (4x4 px) en y=32..215.
// Hora en banda superior y=0..31 (font4 verde).
// Dirty-rect: solo se redibujan celdas que cambian.
// Coloreo por edad: cian=recien nacida, verde oscuro=vieja.
// ===================================================

#define CL_COLS  80
#define CL_ROWS  46
#define CL_CS    4        // tamano celda en px
#define CL_OX    0        // margen izquierdo
#define CL_OY    32       // margen superior (bajo la hora)

// Paleta por edad de celda
#define CL_NEW   0x07FF   // cian     (age 1-2)
#define CL_YNG   0x07E0   // verde    (age 3-9)
#define CL_MID   0x0380   // verde oscuro (age 10-24)
#define CL_OLD   0x0140   // verde muy oscuro (age 25+)
#define CL_BGC   TFT_BLACK

uint8_t clGrid[CL_ROWS][CL_COLS];  // 0=muerta, 1-255=edad
uint8_t clNext[CL_ROWS][CL_COLS];
bool    clFaceReady = false;
int     clTimePrev  = -1;
int     clTickDiv   = 0;   // tick: actualiza CA cada 3 frames
int     clLowPop    = 0;   // contador de gens con baja poblacion

inline uint16_t clColor(uint8_t age) {
    if (age == 0)  return CL_BGC;
    if (age <  3)  return CL_NEW;
    if (age < 10)  return CL_YNG;
    if (age < 25)  return CL_MID;
    return CL_OLD;
}

void clDrawTime(int h, int m) {
    char buf[6]; snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    tft.fillRect(0, 0, W, CL_OY, CL_BGC);
    tft.setTextFont(4); tft.setTextDatum(MC_DATUM);
    tft.setTextColor(0x07E0, CL_BGC);  // verde brillante
    tft.drawString(buf, W / 2, CL_OY / 2);
    if (formato12h) {
        tft.setTextFont(2); tft.setTextDatum(MR_DATUM);
        tft.setTextColor(0x07E0, CL_BGC);
        tft.drawString(esAM ? "AM" : "PM", W - 4, CL_OY / 2);
    }
}

void clDrawFull() {
    tft.fillRect(CL_OX, CL_OY, CL_COLS*CL_CS, CL_ROWS*CL_CS, CL_BGC);
    for (int r = 0; r < CL_ROWS; r++)
        for (int c = 0; c < CL_COLS; c++)
            if (clGrid[r][c])
                tft.fillRect(CL_OX+c*CL_CS, CL_OY+r*CL_CS,
                             CL_CS-1, CL_CS-1, clColor(clGrid[r][c]));
}

// Siembra el tablero con ~30% de celdas vivas al azar
void clSeed() {
    for (int r = 0; r < CL_ROWS; r++)
        for (int c = 0; c < CL_COLS; c++)
            clGrid[r][c] = (random(100) < 30) ? 1 : 0;
    // Un planeador en esquina superior izquierda
    if (CL_ROWS > 7 && CL_COLS > 7) {
        clGrid[2][1]=1; clGrid[3][2]=1;
        clGrid[4][0]=1; clGrid[4][1]=1; clGrid[4][2]=1;
    }
    clLowPop = 0;
}

void initConway() {
    memset(clGrid, 0, sizeof(clGrid));
    clSeed();
    clFaceReady = false;
    clTimePrev  = -1;
    clTickDiv   = 0;
}

// Calcula una generacion y dibuja solo las celdas que cambian
void clStep() {
    int pop = 0;
    for (int r = 0; r < CL_ROWS; r++) {
        for (int c = 0; c < CL_COLS; c++) {
            int n = 0;
            for (int dr = -1; dr <= 1; dr++)
                for (int dc = -1; dc <= 1; dc++) {
                    if (dr==0 && dc==0) continue;
                    n += clGrid[(r+dr+CL_ROWS)%CL_ROWS][(c+dc+CL_COLS)%CL_COLS] ? 1 : 0;
                }
            bool alive = clGrid[r][c] > 0;
            bool next  = alive ? (n==2||n==3) : (n==3);
            clNext[r][c] = next
                ? (alive ? (clGrid[r][c] < 255 ? clGrid[r][c]+1 : 255) : 1)
                : 0;
            if (clNext[r][c]) pop++;
        }
    }
    // Dirty-rect: redibujar solo celdas que cambian estado o de color
    tft.startWrite();
    for (int r = 0; r < CL_ROWS; r++) {
        for (int c = 0; c < CL_COLS; c++) {
            bool wasAlive = clGrid[r][c] > 0;
            bool isAlive  = clNext[r][c]  > 0;
            bool colChg   = isAlive && clColor(clNext[r][c]) != clColor(clGrid[r][c]);
            if (wasAlive != isAlive || colChg) {
                uint16_t col = clColor(clNext[r][c]);
                int px0 = CL_OX + c*CL_CS, py0 = CL_OY + r*CL_CS;
                if (col == CL_BGC) tft.fillRect(px0, py0, CL_CS,   CL_CS,   col);
                else               tft.fillRect(px0, py0, CL_CS-1, CL_CS-1, col);
            }
        }
    }
    tft.endWrite();
    memcpy(clGrid, clNext, sizeof(clGrid));
    if (pop < 150) { if (++clLowPop >= 3) { clSeed(); clFaceReady = false; } }
    else clLowPop = 0;
}

void updateConwayClock(int h, int m, int s) {
    (void)s;
    tft.startWrite();
    if (!clFaceReady) {
        tft.fillRect(0, 0, W, CL_OY + CL_ROWS*CL_CS, CL_BGC);
        clDrawFull();
        clFaceReady = true; clTimePrev = -1;
    }
    int tNow = h * 60 + m;
    if (tNow != clTimePrev) { clTimePrev = tNow; clDrawTime(h, m); }
    tft.endWrite();

    // Actualizar CA cada 3 frames (~20 fps)
    if (++clTickDiv >= 3) { clTickDiv = 0; clStep(); }
}

// ===================================================
// RELOJ RETRO - SPACE INVADERS (tematica 80s)
// Sprites 8x8 px en PROGMEM, dibujados a 2x → 16x16.
// Formacion 11x5; jugador auto-play; hora como marcador.
// ===================================================

#define MODO_RETRO 4

// Sprites: 3 tipos × 2 frames × 8 filas (1 byte/fila, bit7=izq)
static const uint8_t SI_SPR[3][2][8] PROGMEM = {
    // Tipo 0: Medusa - fila superior (cian)
    {{0x3C,0x7E,0xDB,0xFF,0x7E,0x24,0x5A,0x81},   // frame 0
     {0x3C,0x7E,0xDB,0xFF,0x7E,0x24,0x99,0x42}},  // frame 1
    // Tipo 1: Cangrejo - filas medias (amarillo)
    {{0x42,0x3C,0x7E,0xDB,0xFF,0x66,0xA5,0x42},
     {0x42,0xBD,0x7E,0xDB,0xFF,0x66,0x5A,0x81}},
    // Tipo 2: Pulpo - filas bajas (verde)
    {{0x3C,0x7E,0xFF,0xBD,0xFF,0x3C,0x42,0xA5},
     {0x3C,0x7E,0xFF,0xBD,0xFF,0x3C,0xA5,0x42}},
};
const uint16_t SI_PAL[3] = { TFT_CYAN, TFT_YELLOW, TFT_GREEN };

#define SI_NC    11              // columnas
#define SI_NR     5              // filas
#define SI_SC     2              // escala px por pixel
#define SI_SW   (8 * SI_SC)     // 16 px ancho sprite
#define SI_SH   (8 * SI_SC)     // 16 px alto sprite
#define SI_GAPX   5
#define SI_GAPY   4
#define SI_CW   (SI_SW + SI_GAPX)   // 21 px celda
#define SI_CH   (SI_SH + SI_GAPY)   // 20 px celda
#define SI_X0   ((W - SI_NC * SI_CW) / 2)  // 44 px margen
#define SI_Y0    42              // top formacion
#define SI_PLY  198              // Y jugador
#define SI_BGC  TFT_BLACK

uint8_t  siAlive[SI_NR][SI_NC];
int      siCount;
float    siOX;        // offset X formacion
int      siOY;        // offset Y formacion
int8_t   siDX;        // +1=der, -1=izq
int      siTick;      // contador frames
uint8_t  siAFrm;      // frame animacion
float    siPX, siPVX; // jugador X y velocidad
bool     siPBAct;     // bala jugador activa
float    siPBX, siPBY;
struct   SiIBul { float x, y; bool act; };
SiIBul   siIBul[3];
int      siBTick;
bool     siUFO;
float    siUFOX;
int      siUFOTick;
uint16_t siUFOCol;
int      siScore;
bool     siFaceRdy;
int      siTPrev;

inline int siSprType(int row) { return row == 0 ? 0 : (row <= 2 ? 1 : 2); }

void siDrawSprite(int x, int y, int type, uint8_t frm, uint16_t col) {
    for (int r = 0; r < 8; r++) {
        uint8_t bits = pgm_read_byte(&SI_SPR[type][frm][r]);
        for (int b = 0; b < 8; b++)
            if (bits & (0x80 >> b))
                tft.fillRect(x + b*SI_SC, y + r*SI_SC, SI_SC, SI_SC, col);
    }
}

void siDrawAll(float ox, int oy, uint8_t frm) {
    for (int r = 0; r < SI_NR; r++) {
        int t = siSprType(r);
        for (int c = 0; c < SI_NC; c++) {
            if (!siAlive[r][c]) continue;
            siDrawSprite(SI_X0+(int)ox+c*SI_CW, SI_Y0+oy+r*SI_CH, t, frm, SI_PAL[t]);
        }
    }
}

void siEraseFormation(float ox, int oy) {
    tft.fillRect(max(0, SI_X0+(int)ox-2), max(0, SI_Y0+oy-2),
                 SI_NC*SI_CW+4, SI_NR*SI_CH+4, SI_BGC);
}

void siDrawPlayer(int x) {
    tft.fillRect(x-2,  SI_PLY-10, 4,  5, TFT_WHITE);
    tft.fillRect(x-10, SI_PLY-6,  20, 6, TFT_WHITE);
    tft.fillRect(x-14, SI_PLY,    28, 5, TFT_WHITE);
}
void siErasePlayer(int x) {
    tft.fillRect(x-16, SI_PLY-12, 33, 18, SI_BGC);
}

void siDrawUFO(int x) {
    tft.fillRoundRect(x-16, 33, 32,  8, 4, siUFOCol);
    tft.fillRoundRect(x- 8, 27, 16, 10, 4, TFT_RED);
    tft.fillCircle(x-10, 37, 2, TFT_WHITE);
    tft.fillCircle(x,    37, 2, TFT_WHITE);
    tft.fillCircle(x+10, 37, 2, TFT_WHITE);
}
void siEraseUFO(int x) { tft.fillRect(x-20, 25, 42, 18, SI_BGC); }

void siDrawHUD(int h, int m, int score) {
    tft.fillRect(0, 0, W, 32, SI_BGC);
    char tb[6]; snprintf(tb, sizeof(tb), "%02d:%02d", h, m);
    tft.setTextFont(4); tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, SI_BGC);
    tft.drawString(tb, W/2, 16);
    char sc[10]; snprintf(sc, sizeof(sc), "%05d", score);
    tft.setTextFont(1); tft.setTextDatum(ML_DATUM);
    tft.setTextColor(TFT_GREEN, SI_BGC);
    tft.drawString("SCORE", 4,  6);
    tft.drawString(sc,    4, 22);
    if (formato12h) {
        tft.setTextDatum(MR_DATUM);
        tft.setTextColor(TFT_WHITE, SI_BGC);
        tft.drawString(esAM ? "AM" : "PM", W - 4, 5);
    }
    for (int i = 0; i < 3; i++) {   // 3 mini naves = vidas
        int lx = W - 10 - i*20;
        tft.fillRect(lx-1, 22, 3, 4, TFT_WHITE);
        tft.fillRect(lx-7, 26, 14, 4, TFT_WHITE);
    }
}

void siFireInvaderBullet() {
    for (int i = 0; i < 3; i++) {
        if (siIBul[i].act) continue;
        for (int tries = 0; tries < 12; tries++) {
            int col = random(SI_NC);
            for (int r = SI_NR-1; r >= 0; r--) {
                if (!siAlive[r][col]) continue;
                siIBul[i] = { (float)(SI_X0+(int)siOX+col*SI_CW+SI_SW/2),
                               (float)(SI_Y0+siOY+r*SI_CH+SI_SH), true };
                return;
            }
        }
        return;
    }
}

void initRetro() {
    siCount = SI_NR * SI_NC;
    for (int r = 0; r < SI_NR; r++)
        for (int c = 0; c < SI_NC; c++) siAlive[r][c] = 1;
    siOX=0; siOY=0; siDX=1; siTick=0; siAFrm=0;
    siPX=W/2; siPVX=1.5f; siPBAct=false;
    for (int i=0;i<3;i++) siIBul[i].act=false;
    siBTick=0; siUFO=false; siUFOX=-40; siUFOTick=0; siUFOCol=TFT_RED;
    siScore=0; siFaceRdy=false; siTPrev=-1;
}

void updateRetroSI(int h, int m, int s) {
    (void)s;
    tft.startWrite();

    // ── Primer frame ─────────────────────────────
    if (!siFaceRdy) {
        tft.fillRect(0, 0, W, H-22, SI_BGC);
        tft.drawFastHLine(0, SI_PLY+8, W, 0x02E0); // linea divisoria verde
        siDrawAll(siOX, siOY, siAFrm);
        siDrawPlayer((int)siPX);
        siFaceRdy=true; siTPrev=-1;
    }

    // ── Hora (HUD) ───────────────────────────────
    int tNow = h*60+m;
    if (tNow != siTPrev) { siTPrev=tNow; siDrawHUD(h,m,siScore); }

    // ── Movimiento + animacion formacion ─────────
    int speed = max(4, 14 - (SI_NR*SI_NC - siCount) / 5);
    if (++siTick >= speed) {
        siTick = 0;
        // Limites actuales
        int cL=SI_NC-1, cR=0;
        for (int r=0;r<SI_NR;r++) for(int c=0;c<SI_NC;c++)
            if (siAlive[r][c]) { if(c<cL)cL=c; if(c>cR)cR=c; }
        int xL = SI_X0+(int)siOX+cL*SI_CW;
        int xR = SI_X0+(int)siOX+cR*SI_CW+SI_SW;
        bool drop = (siDX>0 && xR>=W-4) || (siDX<0 && xL<=4);
        siEraseFormation(siOX, siOY);
        if (drop) {
            siDX = -siDX;
            siOY += SI_CH/2;
            if (SI_Y0+siOY+SI_NR*SI_CH >= SI_PLY-12) {
                siOX=0; siOY=0; siDX=1;
                siCount=SI_NR*SI_NC;
                for(int r=0;r<SI_NR;r++) for(int c=0;c<SI_NC;c++) siAlive[r][c]=1;
            }
        } else {
            siOX += siDX * 2;
        }
        siAFrm ^= 1;
        siDrawAll(siOX, siOY, siAFrm);
    }

    // ── Jugador (auto-play) ───────────────────────
    float opx = siPX;
    siPX += siPVX;
    if (siPX < 18)   { siPX=18;    siPVX= fabsf(siPVX); }
    if (siPX > W-18) { siPX=W-18;  siPVX=-fabsf(siPVX); }
    if ((int)siPX != (int)opx) { siErasePlayer((int)opx); siDrawPlayer((int)siPX); }

    // ── Bala jugador ─────────────────────────────
    if (!siPBAct) {
        siPBX=siPX; siPBY=(float)(SI_PLY-14); siPBAct=true;
    } else {
        tft.fillRect((int)siPBX-1, (int)siPBY, 3, 8, SI_BGC);
        siPBY -= 5.0f;
        if (siPBY < SI_Y0-4) { siPBAct=false; }
        else {
            bool hit=false;
            for (int r=0;r<SI_NR&&!hit;r++) {
                for (int c=0;c<SI_NC&&!hit;c++) {
                    if (!siAlive[r][c]) continue;
                    int ix=SI_X0+(int)siOX+c*SI_CW, iy=SI_Y0+siOY+r*SI_CH;
                    if (siPBX>=ix&&siPBX<=ix+SI_SW&&siPBY+8>=iy&&siPBY<=iy+SI_SH) {
                        siAlive[r][c]=0; siCount--;
                        tft.fillRect(ix, iy, SI_SW+1, SI_SH+1, SI_BGC);
                        siScore += (SI_NR-r)*10;
                        siPBAct=false; hit=true;
                        siDrawHUD(h,m,siScore);
                        if (siCount==0) {
                            siOX=0; siOY=0; siDX=1; siCount=SI_NR*SI_NC;
                            for(int rr=0;rr<SI_NR;rr++) for(int cc=0;cc<SI_NC;cc++) siAlive[rr][cc]=1;
                            siEraseFormation(siOX,siOY);
                            siDrawAll(siOX,siOY,siAFrm);
                        }
                    }
                }
            }
            if (!hit && siUFO && fabsf(siPBX-siUFOX)<20 && siPBY<45) {
                siEraseUFO((int)siUFOX); siUFO=false; siPBAct=false;
                siScore+=300; siDrawHUD(h,m,siScore);
                hit=true;
            }
            if (!hit && siPBAct) tft.fillRect((int)siPBX-1,(int)siPBY,2,7,TFT_WHITE);
        }
    }

    // ── Balas invasores ───────────────────────────
    if (++siBTick >= 80) { siBTick=0; siFireInvaderBullet(); }
    for (int i=0;i<3;i++) {
        SiIBul& b = siIBul[i];
        if (!b.act) continue;
        tft.fillRect((int)b.x-1,(int)b.y,3,8,SI_BGC);
        b.y += 3.0f;
        if (b.y > SI_PLY+8) { b.act=false; continue; }
        if (b.y+8>=SI_PLY-10 && fabsf(b.x-siPX)<15) { b.act=false; continue; }
        uint16_t bc = (((int)b.y/4)&1) ? 0xF800 : 0xFE19;
        tft.fillRect((int)b.x-1,(int)b.y,2,7,bc);
    }

    // ── UFO ───────────────────────────────────────
    if (!siUFO) {
        if (++siUFOTick >= 500) { siUFOTick=0; siUFO=true; siUFOX=-40; }
    } else {
        siEraseUFO((int)siUFOX);
        siUFOX += 2.0f;
        if (siUFOX > W+40) siUFO=false;
        else siDrawUFO((int)siUFOX);
    }

    tft.endWrite();
}

// ===================================================
// RELOJ FRACTAL - JULIA SET ANIMADO  (c = r*exp(iθ))
// Buffer 320×186 uint8 (60KB); paleta coseno cíclica.
// Fase 1: computa 6 filas/frame (~31 frames = 0.5s).
// Fase 2: animación de paleta cíclica (~35fps).
// c gira cada minuto: θ = (h*60+m) * 2π/60.
// ===================================================
#define MODO_FRACTAL 5
#define FRC_W    320
#define FRC_H    186
#define FRC_OY    32
#define FRC_ITER  48
#define FRC_BATCH  6

uint8_t  fracBuf[FRC_H][FRC_W];   // 59,520 bytes
uint16_t fracPal[FRC_ITER + 1];
uint16_t fracRowBuf[FRC_W];
uint8_t  fracShift   = 0;
float    fracCR      = 0.7885f, fracCI = 0.0f;
int      fracRow     = FRC_H;
int      fracTPrev   = -1;
bool     fracFaceRdy = false;

void fracBuildPal() {
    float base = fracShift * 0.02f;
    for (int i = 0; i < FRC_ITER; i++) {
        float t = (float)i / (float)(FRC_ITER - 1) + base;
        uint8_t r = (uint8_t)(127.5f + 127.5f * cosf(6.2832f *  t           ));
        uint8_t g = (uint8_t)(127.5f + 127.5f * cosf(6.2832f * (t + 0.333f) ));
        uint8_t b = (uint8_t)(127.5f + 127.5f * cosf(6.2832f * (t + 0.667f) ));
        fracPal[i] = tft.color565(r, g, b);
    }
    fracPal[FRC_ITER] = TFT_BLACK;
}

void fracComputeBatch() {
    const float sx = 3.2f / (float)FRC_W;
    const float sy = 1.86f / (float)FRC_H;
    tft.startWrite();
    int end = min(fracRow + FRC_BATCH, FRC_H);
    for (int y = fracRow; y < end; y++) {
        float zi0 = -0.93f + y * sy;
        for (int x = 0; x < FRC_W; x++) {
            float zr = -1.6f + x * sx, zi = zi0;
            uint8_t it = 0;
            while (it < FRC_ITER) {
                float zr2 = zr * zr, zi2 = zi * zi;
                if (zr2 + zi2 > 4.0f) break;
                zi = 2.0f * zr * zi + fracCI;
                zr = zr2 - zi2 + fracCR;
                it++;
            }
            fracBuf[y][x] = it;
            fracRowBuf[x] = fracPal[it < FRC_ITER ? it : FRC_ITER];
        }
        tft.setWindow(0, FRC_OY + y, FRC_W - 1, FRC_OY + y);
        tft.pushColors(fracRowBuf, FRC_W, false);
    }
    fracRow = end;
    tft.endWrite();
}

void fracRedrawAll() {
    tft.startWrite();
    for (int y = 0; y < FRC_H; y++) {
        const uint8_t* row = fracBuf[y];
        for (int x = 0; x < FRC_W; x++)
            fracRowBuf[x] = fracPal[row[x] < FRC_ITER ? row[x] : FRC_ITER];
        tft.setWindow(0, FRC_OY + y, FRC_W - 1, FRC_OY + y);
        tft.pushColors(fracRowBuf, FRC_W, false);
    }
    tft.endWrite();
}

void fracDrawTime(int h, int m) {
    tft.fillRect(0, 0, W, FRC_OY, TFT_BLACK);
    char buf[6]; snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    tft.setTextFont(4); tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(buf, W / 2, FRC_OY / 2);
    tft.setTextFont(1); tft.setTextDatum(ML_DATUM);
    tft.setTextColor(0x528A, TFT_BLACK);
    tft.drawString("Julia Set", 4, FRC_OY / 2);
    if (formato12h) {
        tft.setTextFont(2); tft.setTextDatum(MR_DATUM);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString(esAM ? "AM" : "PM", W - 4, FRC_OY / 2);
    }
}

void initFractal() {
    fracShift = 0; fracRow = 0; fracFaceRdy = false; fracTPrev = -1;
    fracCR = 0.7885f; fracCI = 0.0f;
    fracBuildPal();
}

void updateFractalClock(int h, int m, int s) {
    (void)s;
    if (!fracFaceRdy) {
        tft.fillRect(0, 0, W, H - 22, TFT_BLACK);
        fracFaceRdy = true;
    }
    int tNow = h * 60 + m;
    if (tNow != fracTPrev) {
        fracTPrev = tNow;
        float theta = tNow * 6.2832f / 60.0f;
        fracCR = 0.7885f * cosf(theta);
        fracCI = 0.7885f * sinf(theta);
        fracRow = 0;
        fracDrawTime(h, m);
    }
    if (fracRow < FRC_H) {
        fracComputeBatch();
    } else {
        fracShift++;
        fracBuildPal();
        fracRedrawAll();
    }
}

// ===================================================
// RELOJ PIXEL ART - Personajes clasicos de los 80
// Mario, Link, Donkey Kong, Fantasma, Space Invader
// Sprites 8x8 escalados 3x; saltos y movimiento.
// ===================================================
#define MODO_PIXEL  6
#define PX_NC       6     // personajes en pantalla
#define PX_SC       3     // factor de escala
#define PX_SW       24    // sprite width  (8 * PX_SC)
#define PX_SH       24    // sprite height (8 * PX_SC)
#define PX_OY       32    // inicio zona de juego
#define PX_GROUND   (H - 22 - PX_SH - 4)   // y del suelo (donde posan los pies)

//  5 tipos x 2 frames x 8 filas (bit7=izquierda)
static const uint8_t PX_SPR[5][2][8] PROGMEM = {
    // 0: Mario (rojo) ─────────────────────────────
    {{ 0x70,0xF8,0xD8,0x7C,0x7C,0xCC,0x84,0x00 },
     { 0x70,0xF8,0xD8,0x7C,0x7C,0x6C,0x6C,0x00 }},
    // 1: Link / Zelda (verde) ─────────────────────
    {{ 0x60,0xF0,0x70,0x78,0xFC,0x70,0xA8,0x00 },
     { 0x60,0xF0,0x70,0x70,0xF8,0x78,0x50,0x00 }},
    // 2: Donkey Kong (marron) ─────────────────────
    {{ 0x70,0xF8,0xA8,0xF8,0x70,0xF8,0x50,0x00 },
     { 0x70,0xF8,0xA8,0xF8,0x70,0xF8,0x90,0x00 }},
    // 3: Fantasma Pac-Man (cian) ──────────────────
    {{ 0x7C,0xFE,0xFE,0xFE,0xFE,0xFE,0xAA,0x00 },
     { 0x7C,0xFE,0xFE,0xFE,0xFE,0xFE,0x54,0x00 }},
    // 4: Space Invader (magenta) ──────────────────
    {{ 0x3C,0x7E,0xDB,0xFF,0x7E,0x24,0x42,0x81 },
     { 0x3C,0x7E,0xDB,0xFF,0x7E,0x42,0xA5,0x42 }},
};

static const uint16_t PX_PAL[5] = {
    TFT_RED, 0x07E0, 0xA145, TFT_CYAN, TFT_MAGENTA
};
static const char* PX_NAME[5] = {
    "Mario", "Link", "DK", "Ghost", "Invader"
};

struct PxChar {
    float   x, jy;    // posicion x  y offset vertical (0=suelo)
    int8_t  vx;       // velocidad horizontal con signo
    float   jv;       // velocidad vertical del salto
    uint16_t jtmr;    // frames hasta el proximo salto
    uint8_t type, tick, frame;
    int16_t px;
    float   pjy;
};

PxChar pxCh[PX_NC];
bool   pxFaceRdy = false;
int    pxTPrev   = -1;

void pxDrawSprite(int x, int y, uint8_t t, uint8_t frm, bool flip, uint16_t col) {
    for (int r = 0; r < 8; r++) {
        uint8_t bits = pgm_read_byte(&PX_SPR[t][frm][r]);
        for (int c = 0; c < 8; c++) {
            int b = flip ? (bits >> c) & 1 : (bits >> (7 - c)) & 1;
            if (b) tft.fillRect(x + c * PX_SC, y + r * PX_SC, PX_SC, PX_SC, col);
        }
    }
}

void pxDrawHUD(int h, int m) {
    tft.fillRect(0, 0, W, PX_OY, C_BG);
    char buf[6]; snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    tft.setTextFont(4); tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, C_BG);
    tft.drawString(buf, W / 2, PX_OY / 2);
    tft.setTextFont(1); tft.setTextDatum(ML_DATUM);
    tft.setTextColor(0xF7BE, C_BG);
    tft.drawString("Pixel Art", 4, PX_OY / 2);
    if (formato12h) {
        tft.setTextFont(2); tft.setTextDatum(MR_DATUM);
        tft.setTextColor(TFT_WHITE, C_BG);
        tft.drawString(esAM ? "AM" : "PM", W - 4, PX_OY / 2);
    }
}

void pxDrawGround() {
    for (int x = 0; x < W; x += 8) {
        uint16_t col = ((x / 8) % 2 == 0) ? 0x5429 : 0x3186;
        tft.fillRect(x, PX_GROUND + PX_SH, 8, 4, col);
    }
}

void initPixelArt() {
    pxFaceRdy = false; pxTPrev = -1;
    const uint8_t types[PX_NC] = { 0, 1, 2, 3, 4, 0 };
    for (int i = 0; i < PX_NC; i++) {
        PxChar& c = pxCh[i];
        c.type  = types[i];
        c.x     = random(0, W - PX_SW);
        c.jy    = 0.0f;
        c.vx    = (random(0, 2) == 0) ? 1 : -1;
        c.jv    = 0.0f;
        c.jtmr  = (uint16_t)random(30, 180);
        c.tick  = 0;
        c.frame = 0;
        c.px    = (int16_t)c.x;
        c.pjy   = 0.0f;
    }
}

void updatePixelArt(int h, int m, int s) {
    (void)s;
    tft.startWrite();
    if (!pxFaceRdy) {
        tft.fillRect(0, 0, W, H - 22, C_BG);
        pxDrawGround();
        pxFaceRdy = true; pxTPrev = -1;
    }
    int tNow = h * 60 + m;
    if (tNow != pxTPrev) { pxTPrev = tNow; pxDrawHUD(h, m); }

    for (int i = 0; i < PX_NC; i++) {
        PxChar& c = pxCh[i];

        // Borrar posicion anterior
        int oldY = PX_GROUND - (int)c.pjy;
        tft.fillRect(c.px, oldY, PX_SW, PX_SH, C_BG);

        // Mover horizontalmente
        float spd = 0.7f + c.type * 0.15f;
        c.x += c.vx * spd;
        if (c.x < 0)       { c.x = 0;       c.vx = -c.vx; }
        if (c.x > W - PX_SW) { c.x = W - PX_SW; c.vx = -c.vx; }

        // Fisica del salto
        if (c.jy <= 0.0f && c.jv == 0.0f) {
            if (c.jtmr > 0) c.jtmr--;
            else { c.jv = 4.5f; c.jtmr = (uint16_t)random(80, 220); }
        }
        if (c.jv != 0.0f || c.jy > 0.0f) {
            c.jy += c.jv;
            c.jv -= 0.35f;
            if (c.jy <= 0.0f) { c.jy = 0.0f; c.jv = 0.0f; }
        }

        // Animacion frame
        c.tick++;
        if (c.tick >= 8) { c.tick = 0; c.frame ^= 1; }

        // Dibujar nueva posicion
        bool flip = (c.vx < 0);
        int nx = (int)c.x, ny = PX_GROUND - (int)c.jy;
        pxDrawSprite(nx, ny, c.type, c.frame, flip, PX_PAL[c.type]);
        c.px = (int16_t)nx; c.pjy = c.jy;
    }
    tft.endWrite();
}

// ===================================================
// RELOJ SISTEMA SOLAR
// Posiciones reales desde fecha NTP (epoch J2000.0).
// Velocidad: 14.67 dias simulados / seg real ->
//   Mercury ~6 s / orbita, Earth ~25 s, Saturn ~12 min.
// Calidad: sombra lateral, corona solar, detalles
//   por planeta (bandas Jupiter, anillos Saturn,
//   continentes Tierra, casquetes polares...).
// ===================================================
#define MODO_SOLAR   7
#define SOL_OY       32
#define SOL_N         8
#define SOL_CX       (W / 2)
#define SOL_CY       (SOL_OY + (H - 22 - SOL_OY) / 2)   // 125
#define SOL_ANIM    14.67f   // dias simulados por segundo real

// Periodos sidereos reales (dias)
static const float SOL_PER[SOL_N] PROGMEM = {
     87.9691f, 224.701f, 365.256f,  686.971f,
    4332.59f, 10759.2f, 30688.5f, 60182.0f
};
// Longitudes medias en J2000.0 (grados)
static const float SOL_L0[SOL_N] PROGMEM = {
    252.25f, 181.98f, 100.46f, 355.43f,
     34.40f,  49.94f, 313.23f, 304.88f
};
// Radios de orbita visual (px), centrado en (160,125)
static const uint8_t SOL_OR[SOL_N] PROGMEM = { 15, 24, 34, 45, 59, 71, 81, 88 };
// Radios visuales del disco del planeta (px)
static const uint8_t SOL_SR[SOL_N] PROGMEM = {  2,  3,  3,  2,  5,  4,  3,  3 };
// Colores principales RGB565
static const uint16_t SOL_PC[SOL_N] PROGMEM = {
    0x9492,  // Mercury: gris medio
    0xF6F4,  // Venus:   crema amarillento
    0x0D37,  // Earth:   azul oceano
    0xC368,  // Mars:    rojo-naranja
    0xE634,  // Jupiter: ocre arenoso
    0xEEF5,  // Saturn:  crema dorado
    0x8E3A,  // Uranus:  azul-verde palido
    0x0BF8,  // Neptune: azul profundo
};

float    solBase[SOL_N];
float    solAngle[SOL_N];
float    solDays    = 0.0f;
uint32_t solLastMs  = 0;
uint32_t solDrawMs  = 0;
bool     solFaceRdy = false;
int      solTPrev   = -1;
uint8_t  solStarX[60], solStarY[60];
int16_t  solPX[SOL_N], solPY[SOL_N];   // posicion pixel previa de cada planeta

void solHUD(int h, int m) {
    tft.fillRect(0, 0, W, SOL_OY, TFT_BLACK);
    char buf[6]; snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    tft.setTextFont(4); tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.drawString(buf, W / 2, SOL_OY / 2);
    tft.setTextFont(1); tft.setTextDatum(ML_DATUM);
    tft.setTextColor(0xFBA0, TFT_BLACK);
    tft.drawString("Sistema Solar", 4, SOL_OY / 2);
    if (formato12h) {
        tft.setTextFont(2); tft.setTextDatum(MR_DATUM);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString(esAM ? "AM" : "PM", W - 4, SOL_OY / 2);
    }
}

// Dibuja un planeta con todos sus detalles (reutilizado en init y update)
static void solPlanetDraw(int i, int px, int py, int cx, int cy) {
    uint8_t  sr  = pgm_read_byte(&SOL_SR[i]);
    uint16_t col = pgm_read_word(&SOL_PC[i]);
    if (i == 5) {
        tft.drawCircle(px, py, sr + 5, 0x7B4F);
        tft.drawCircle(px, py, sr + 4, 0xCE79);
        tft.drawCircle(px, py, sr + 3, 0xA534);
    }
    if (sr >= 3) {
        float sdx = (float)(px - cx), sdy = (float)(py - cy);
        float sd = sqrtf(sdx * sdx + sdy * sdy);
        if (sd > 0.5f)
            tft.fillCircle(px + (int)(sdx / sd + 0.5f),
                           py + (int)(sdy / sd + 0.5f), sr, TFT_BLACK);
    }
    tft.fillCircle(px, py, sr, col);
    switch (i) {
    case 1: tft.drawCircle(px, py, sr, 0xFFFF); break;
    case 2:
        tft.drawPixel(px - 1, py,     0x0660);
        tft.drawPixel(px + 1, py - 1, 0x0440);
        tft.drawCircle(px, py, sr, 0x07FF); break;
    case 3: tft.drawPixel(px, py - (int)sr + 1, 0xFFFF); break;
    case 4: {
        int isr = (int)sr;
        for (int b = -isr + 2; b <= isr - 2; b += 2) {
            int hw = (int)sqrtf((float)(isr * isr - b * b));
            if (hw > 1) tft.drawFastHLine(px - hw, py + b, hw * 2, 0xAC0A);
        }
        break;
    }
    case 6: tft.drawCircle(px, py, sr, 0xAFFF); break;
    case 7: tft.drawPixel(px, py - (int)sr + 1, 0x87FF); break;
    }
    if (sr >= 2) tft.drawPixel(px - 1, py - 1, 0xFFFF);
}

// Dibuja el sol con corona giratoria (siempre se redibuja cada frame)
static void solSunDraw(int cx, int cy) {
    tft.fillCircle(cx, cy, 12, 0x6200);
    tft.fillCircle(cx, cy, 10, 0xFF80);
    tft.fillCircle(cx, cy,  8, 0xFFE0);
    tft.fillCircle(cx, cy,  6, 0xFFFF);
    float sunRot = fmodf(solDays * 0.15f, 2.0f * (float)M_PI);
    for (int r = 0; r < 16; r++) {
        float ra = r * 0.3927f + sunRot;
        float fc = cosf(ra), fs = sinf(ra);
        int   rl = (r % 2 == 0) ? 6 : 4;
        tft.drawLine(cx + (int)(13 * fc), cy + (int)(13 * fs),
                     cx + (int)((13 + rl) * fc), cy + (int)((13 + rl) * fs),
                     0xFF80);
    }
    // Restaurar orbita de Mercurio (los rayos la solapan a r=15)
    tft.drawCircle(cx, cy, pgm_read_byte(&SOL_OR[0]), 0x2104);
}

// Dibuja fondo estatico (llamado una sola vez al entrar al modo)
static void solBgDraw() {
    const int cx = SOL_CX, cy = SOL_CY;
    tft.fillRect(0, SOL_OY, W, H - 22 - SOL_OY, TFT_BLACK);
    static const uint16_t sBr[3] = { 0xFFFF, 0x8C71, 0x4208 };
    for (int i = 0; i < 60; i++) {
        int sx = (int)solStarX[i] * W / 256;
        int sy = SOL_OY + (int)solStarY[i] * (H - 22 - SOL_OY) / 256;
        tft.drawPixel(sx, sy, sBr[i % 3]);
    }
    for (int i = 0; i < SOL_N; i++)
        tft.drawCircle(cx, cy, pgm_read_byte(&SOL_OR[i]), 0x2104);
    for (int i = 0; i < SOL_N; i++) { solPX[i] = -999; solPY[i] = -999; }
}

// Actualizacion incremental: solo borra/redibuja posiciones que cambiaron
void solDrawScene() {
    const int cx = SOL_CX, cy = SOL_CY;
    bool dirty = false;

    // Fase 1: borrar planetas que se movieron, actualizar posicion
    for (int i = 0; i < SOL_N; i++) {
        uint8_t orb = pgm_read_byte(&SOL_OR[i]);
        uint8_t sr  = pgm_read_byte(&SOL_SR[i]);
        int npx = cx + (int)((float)orb * sinf(solAngle[i]));
        int npy = cy - (int)((float)orb * cosf(solAngle[i]));
        if (npx != (int)solPX[i] || npy != (int)solPY[i]) {
            if (solPX[i] != -999) {
                tft.fillCircle(solPX[i], solPY[i],
                               (int)sr + (i == 5 ? 7 : 2), TFT_BLACK);
                dirty = true;
            }
            solPX[i] = (int16_t)npx;
            solPY[i] = (int16_t)npy;
        }
    }

    // Fase 2: restaurar orbitas si alguna fue borrada
    if (dirty)
        for (int i = 0; i < SOL_N; i++)
            tft.drawCircle(cx, cy, pgm_read_byte(&SOL_OR[i]), 0x2104);

    // Fase 3: redibujar sol + corona (siempre: la corona rota cada frame)
    solSunDraw(cx, cy);

    // Fase 4: redibujar todos los planetas
    // (necesario cuando orbitas fueron restauradas o corona toco a Mercurio)
    for (int i = 0; i < SOL_N; i++)
        if (solPX[i] != -999)
            solPlanetDraw(i, solPX[i], solPY[i], cx, cy);
}

void solComputeBase(time_t t) {
    // J2000.0 = 2000-01-01 12:00 UTC = Unix 946728000
    float d = (float)((long)t - 946728000L) / 86400.0f;
    for (int i = 0; i < SOL_N; i++) {
        float per = pgm_read_float(&SOL_PER[i]);
        float l0  = pgm_read_float(&SOL_L0[i]);
        solBase[i]  = fmodf(l0 + fmodf(d / per, 1.0f) * 360.0f, 360.0f) * DEG_TO_RAD;
        solAngle[i] = solBase[i];
    }
    solDays = 0.0f;
}

void initSolar() {
    solFaceRdy = false; solTPrev = -1; solDrawMs = 0;
    // Estrellas fijas (semilla constante para reproducibilidad)
    randomSeed(1234);
    for (int i = 0; i < 60; i++) {
        solStarX[i] = (uint8_t)random(256);
        solStarY[i] = (uint8_t)random(256);
    }
    randomSeed(millis());
    // Posicion inicial real desde NTP
    solComputeBase((time_t)ntp.getEpochTime());
    solLastMs = millis();
}

void updateSolarClock(int h, int m, int s) {
    (void)s;
    uint32_t now = millis();
    // Avanzar tiempo de animacion
    float dt = (now - solLastMs) / 1000.0f;
    if (dt > 0.2f) dt = 0.2f;
    solLastMs = now;
    solDays += dt * SOL_ANIM;
    for (int i = 0; i < SOL_N; i++) {
        float per = pgm_read_float(&SOL_PER[i]);
        solAngle[i] = solBase[i] + solDays / per * 2.0f * (float)M_PI;
    }
    // Primera vez: dibujar fondo estatico (estrellas + orbitas)
    if (!solFaceRdy) {
        solFaceRdy = true; solTPrev = -1;
        tft.startWrite(); solBgDraw(); tft.endWrite();
    }
    // HUD (hora): solo cuando cambia el minuto
    int tNow = h * 60 + m;
    if (tNow != solTPrev) {
        solTPrev = tNow;
        tft.startWrite(); solHUD(h, m); tft.endWrite();
    }
    // Actualizacion incremental a ~30 fps (sin borrado de pantalla completa)
    if (now - solDrawMs >= 33) {
        solDrawMs = now;
        tft.startWrite();
        solDrawScene();
        tft.endWrite();
    }
}

// ===================================================
// MENU TACTIL
// ===================================================
void aplicarHorario(int hh, int mm);  // forward declaration

#define M_BG    0x18C3
#define M_HDR   0x1967
#define M_BTN   0x2965
#define M_BTN_A 0x035F
#define M_GUARD 0x0340
#define M_SALIR 0x9000
#define M_TXT   0xFFFF
#define M_VAL   0xFFE0

struct Rect { int16_t x, y, w, h; };

inline bool toca(Rect r, Punto p) {
    return p.pulsado && p.x >= r.x && p.x < r.x+r.w
                     && p.y >= r.y && p.y < r.y+r.h;
}

void boton(Rect r, const char* txt, uint16_t bg) {
    tft.fillRoundRect(r.x, r.y, r.w, r.h, 5, bg);
    tft.drawRoundRect(r.x, r.y, r.w, r.h, 5, 0x7BEF);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(M_TXT, bg);
    tft.drawString(txt, r.x + r.w/2, r.y + r.h/2);
}

// ── Variables de edicion del menu ──────────────────
int     mIndZona  = 0;
uint8_t mBrillo   = 200;
bool    mFmt12    = false;
uint8_t mModoRel  = MODO_TETRIS;

// ── Variables de edicion del horario ───────────────
bool    mHorActivo = false;
uint8_t mEncHH = 7,  mEncMM = 0;
uint8_t mApagHH = 23, mApagMM = 0;

// ──────────────────────────────────────────────────
// SUB-MENU: HORARIO DE PANTALLA
// ──────────────────────────────────────────────────
// Layout fila de hora: [label 0..92] [HH- 96] [HH 138] [HH+ 150] [:193] [MM- 206] [MM 248] [MM+ 260]
// Botones 28x28.  Filas: estado cy=52, encender cy=88, apagar cy=124.
void dibujarHorario() {
    tft.fillScreen(M_BG);
    tft.fillRect(0, 0, W, 30, M_HDR);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(M_TXT, M_HDR);
    tft.drawString("HORARIO PANTALLA", W/2, 15);
    tft.setTextFont(2);

    char buf[8];

    // ── Estado ──────────────────────────────────── cy=52 (y=38..66)
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(0xAD75, M_BG);
    tft.drawString("Estado:", 10, 52);
    boton({150, 38, 140, 28}, mHorActivo ? "Activado" : "Desactivado",
          mHorActivo ? M_GUARD : M_BTN);

    // ── Encender a ──────────────────────────────── cy=88 (y=74..102)
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(0xAD75, M_BG);
    tft.drawString("Encender a:", 10, 88);

    boton({96,  74, 28, 28}, "-", M_BTN);            // HH-
    snprintf(buf, sizeof(buf), "%02d", mEncHH);
    tft.setTextDatum(MC_DATUM); tft.setTextColor(M_VAL, M_BG);
    tft.drawString(buf, 138, 88);                    // HH value
    boton({150, 74, 28, 28}, "+", M_BTN);            // HH+

    tft.setTextColor(M_TXT, M_BG);
    tft.drawString(":", 193, 88);                    // separador

    boton({206, 74, 28, 28}, "-", M_BTN);            // MM-
    snprintf(buf, sizeof(buf), "%02d", mEncMM);
    tft.setTextColor(M_VAL, M_BG);
    tft.drawString(buf, 248, 88);                    // MM value
    boton({260, 74, 28, 28}, "+", M_BTN);            // MM+

    // ── Apagar a ────────────────────────────────── cy=124 (y=110..138)
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(0xAD75, M_BG);
    tft.drawString("Apagar a:", 10, 124);

    boton({96,  110, 28, 28}, "-", M_BTN);           // HH-
    snprintf(buf, sizeof(buf), "%02d", mApagHH);
    tft.setTextDatum(MC_DATUM); tft.setTextColor(M_VAL, M_BG);
    tft.drawString(buf, 138, 124);                   // HH value
    boton({150, 110, 28, 28}, "+", M_BTN);           // HH+

    tft.setTextColor(M_TXT, M_BG);
    tft.drawString(":", 193, 124);                   // separador

    boton({206, 110, 28, 28}, "-", M_BTN);           // MM-
    snprintf(buf, sizeof(buf), "%02d", mApagMM);
    tft.setTextColor(M_VAL, M_BG);
    tft.drawString(buf, 248, 124);                   // MM value
    boton({260, 110, 28, 28}, "+", M_BTN);           // MM+

    // ── Nota ────────────────────────────────────── y=152
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(1);
    tft.setTextColor(0x528A, M_BG);
    tft.drawString("La pantalla se encendera entre esas horas", W/2, 152);

    // ── Guardar / Cancelar ───────────────────────── y=164..200
    boton({ 10, 164, 140, 36}, "Guardar",  M_GUARD);
    boton({170, 164, 140, 36}, "Cancelar", M_SALIR);
}

void mostrarHorario() {
    mHorActivo = horActivo;
    mEncHH  = encHH;  mEncMM  = encMM;
    mApagHH = apagHH; mApagMM = apagMM;
    dibujarHorario();

    bool salir = false;
    bool antP  = false;
    bool esperandoSoltar = true;
    uint32_t tDeb = 0;

    while (!salir) {
        delay(20);
        Punto p = leerTouch();
        if (esperandoSoltar) {
            if (p.pulsado) continue;
            esperandoSoltar = false;
            antP = false;
            tDeb = millis();
            continue;
        }
        if (!p.pulsado) { antP = false; continue; }
        if (antP) continue;
        if (millis() - tDeb < 180) continue;
        antP = true; tDeb = millis();

        // Toggle activo
        if (toca({150, 38, 140, 28}, p))
            { mHorActivo = !mHorActivo; dibujarHorario(); }

        // Hora encendido  HH- HH+ MM- MM+
        else if (toca({96,  74, 28, 28}, p))
            { mEncHH = (mEncHH + 23) % 24; dibujarHorario(); }
        else if (toca({150, 74, 28, 28}, p))
            { mEncHH = (mEncHH +  1) % 24; dibujarHorario(); }
        else if (toca({206, 74, 28, 28}, p))
            { mEncMM = (mEncMM + 55) % 60; dibujarHorario(); }
        else if (toca({260, 74, 28, 28}, p))
            { mEncMM = (mEncMM +  5) % 60; dibujarHorario(); }

        // Hora apagado  HH- HH+ MM- MM+
        else if (toca({96,  110, 28, 28}, p))
            { mApagHH = (mApagHH + 23) % 24; dibujarHorario(); }
        else if (toca({150, 110, 28, 28}, p))
            { mApagHH = (mApagHH +  1) % 24; dibujarHorario(); }
        else if (toca({206, 110, 28, 28}, p))
            { mApagMM = (mApagMM + 55) % 60; dibujarHorario(); }
        else if (toca({260, 110, 28, 28}, p))
            { mApagMM = (mApagMM +  5) % 60; dibujarHorario(); }

        // Guardar
        else if (toca({10, 164, 140, 36}, p)) {
            horActivo = mHorActivo;
            encHH  = mEncHH;  encMM  = mEncMM;
            apagHH = mApagHH; apagMM = mApagMM;
            salir = true;
        }
        // Cancelar
        else if (toca({170, 164, 140, 36}, p))
            salir = true;
    }
}

// ──────────────────────────────────────────────────
// MENU PRINCIPAL
// ──────────────────────────────────────────────────
// Layout menu (320x240):
//  Header 0..28 | Zona top=32 | Brillo top=56 | Formato top=80
//  Modo top=104 | WiFi+Horario top=128 | Status y=157 | Guardar/Salir top=165
void dibujarMenu() {
    tft.fillScreen(M_BG);
    tft.fillRect(0, 0, W, 28, M_HDR);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(M_TXT, M_HDR);
    tft.drawString("CONFIGURACION", W/2, 14);
    tft.setTextFont(2);

    // Zona horaria  top=32 cy=42
    tft.setTextDatum(ML_DATUM); tft.setTextColor(0xAD75, M_BG);
    tft.drawString("Zona horaria", 10, 42);
    tft.setTextDatum(MC_DATUM); tft.setTextColor(M_VAL, M_BG);
    tft.drawString(ZONAS[mIndZona].nombre, 155, 42);
    boton({200, 32, 30, 20}, "<", M_BTN);
    boton({234, 32, 30, 20}, ">", M_BTN);

    // Brillo  top=56 cy=66
    tft.setTextDatum(ML_DATUM); tft.setTextColor(0xAD75, M_BG);
    tft.drawString("Brillo", 10, 66);
    char bufB[8]; snprintf(bufB, sizeof(bufB), "%d%%", mBrillo*100/255);
    tft.setTextDatum(MC_DATUM); tft.setTextColor(M_VAL, M_BG);
    tft.drawString(bufB, 155, 66);
    boton({200, 56, 30, 20}, "<", M_BTN);
    boton({234, 56, 30, 20}, ">", M_BTN);

    // Formato  top=80 cy=90
    tft.setTextDatum(ML_DATUM); tft.setTextColor(0xAD75, M_BG);
    tft.drawString("Formato hora", 10, 90);
    tft.setTextDatum(MC_DATUM); tft.setTextColor(M_VAL, M_BG);
    tft.drawString(mFmt12 ? "12 h" : "24 h", 155, 90);
    boton({200, 80, 64, 20}, "Cambiar", M_BTN);

    // Modo reloj  top=104 cy=114
    tft.setTextDatum(ML_DATUM); tft.setTextColor(0xAD75, M_BG);
    tft.drawString("Modo reloj", 10, 114);
    tft.setTextDatum(MC_DATUM); tft.setTextColor(M_VAL, M_BG);
    const char* modoNombres[] = {"Tetris","Analogico","Pac-Man","Conway","Retro","Fractal","Pixel Art","Solar"};
    tft.drawString(modoNombres[mModoRel], 155, 114);
    boton({200, 104, 64, 20}, "Cambiar", M_BTN);

    // WiFi + Horario  top=128 h=24
    boton({  8, 128, 148, 24}, "Configurar WiFi",  M_BTN_A);
    boton({164, 128, 148, 24}, "Horario pantalla", M_BTN);

    // Status  y=157 font1
    tft.setTextDatum(ML_DATUM); tft.setTextFont(1);
    if (wifiOK) {
        tft.setTextColor(0x07E0, M_BG);
        char bw[36]; snprintf(bw, sizeof(bw), "WiFi: %s", WiFi.SSID().c_str());
        tft.drawString(bw, 8, 157);
    } else {
        tft.setTextColor(TFT_RED, M_BG);
        tft.drawString("Sin conexion WiFi", 8, 157);
    }
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(horActivo ? 0x07E0 : 0x528A, M_BG);
    char bh[24];
    if (horActivo)
        snprintf(bh, sizeof(bh), "%02d:%02d-%02d:%02d", encHH,encMM,apagHH,apagMM);
    else
        snprintf(bh, sizeof(bh), "Horario: OFF");
    tft.drawString(bh, W-8, 157);
    tft.setTextFont(2);

    // Guardar / Salir  top=165 h=34
    boton({  8, 165, 148, 34}, "Guardar",           M_GUARD);
    boton({164, 165, 148, 34}, "Salir sin guardar",  M_SALIR);
}

void configurarWifiPortal() {
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4); tft.setTextColor(TFT_WHITE,  TFT_BLACK);
    tft.drawString("Config. WiFi", W/2, 28);
    tft.setTextFont(2);
    tft.setTextColor(TFT_CYAN,   TFT_BLACK); tft.drawString("1. Conecta al WiFi:", W/2, 70);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.drawString("RelojTetris",         W/2, 90);
    tft.setTextColor(TFT_CYAN,   TFT_BLACK); tft.drawString("2. Abre en el navegador:", W/2, 115);
    tft.setTextColor(TFT_YELLOW, TFT_BLACK); tft.drawString("192.168.4.1",         W/2, 135);
    tft.setTextColor(0x7BEF,     TFT_BLACK); tft.drawString("3. Elige red y contrasena", W/2, 162);
    tft.setTextFont(1); tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("Timeout: 2 min", W/2, 215);

    WiFiManager wm;
    wm.setConfigPortalTimeout(120);
    wm.startConfigPortal("RelojTetris");
    wifiOK = (WiFi.status() == WL_CONNECTED);
    if (wifiOK) { ntp.begin(); for (int i = 0; i < 10 && !ntp.update(); i++) delay(500); }
}

void mostrarMenu() {
    mIndZona = indicZona;
    mBrillo  = brillo;
    mFmt12   = formato12h;
    mModoRel = modoReloj;

    dibujarMenu();

    bool salir = false, antP = false;
    bool esperandoSoltar = true;
    uint32_t tDeb = 0;

    while (!salir) {
        delay(20);
        Punto p = leerTouch();
        if (esperandoSoltar) {
            if (p.pulsado) continue;
            esperandoSoltar = false;
            antP = false;
            tDeb = millis();
            continue;
        }
        if (!p.pulsado) { antP = false; continue; }
        if (antP) continue;
        if (millis() - tDeb < 180) continue;
        antP = true; tDeb = millis();

        // Zona horaria
        if      (toca({200, 32, 30, 20}, p) && mIndZona > 0)
            { mIndZona--; dibujarMenu(); }
        else if (toca({234, 32, 30, 20}, p) && mIndZona < N_ZONAS-1)
            { mIndZona++; dibujarMenu(); }
        // Brillo
        else if (toca({200, 56, 30, 20}, p) && mBrillo > 25)
            { mBrillo -= 25; analogWrite(DISP_BL, mBrillo); dibujarMenu(); }
        else if (toca({234, 56, 30, 20}, p) && mBrillo <= 230)
            { mBrillo += 25; analogWrite(DISP_BL, mBrillo); dibujarMenu(); }
        // Formato
        else if (toca({200, 80, 64, 20}, p))
            { mFmt12 = !mFmt12; dibujarMenu(); }
        // Modo
        else if (toca({200, 104, 64, 20}, p))
            { mModoRel = (mModoRel + 1) % 8; dibujarMenu(); }
        // WiFi
        else if (toca({8, 128, 148, 24}, p))
            { configurarWifiPortal(); dibujarMenu(); }
        // Horario
        else if (toca({164, 128, 148, 24}, p))
            { mostrarHorario(); dibujarMenu(); }
        // Guardar
        else if (toca({8, 165, 148, 34}, p)) {
            indicZona     = mIndZona;
            zonaHoraria   = ZONAS[mIndZona].offset;
            brillo        = mBrillo;
            formato12h    = mFmt12;
            modoReloj     = mModoRel;
            guardarPrefs();
            salir = true;
        }
        // Salir sin guardar
        else if (toca({164, 165, 148, 34}, p)) {
            analogWrite(DISP_BL, brillo);
            salir = true;
        }
    }

    // Restaurar pantalla segun el modo activo
    tft.fillScreen(C_BG);
    struct tm ti = tiempoLocal();
    int h = ti.tm_hour, m = ti.tm_min, s = ti.tm_sec;
    if (formato12h) { esAM = (ti.tm_hour < 12); if (h == 0) h = 12; else if (h > 12) h -= 12; }

    if (modoReloj == MODO_ANALOGICO) {
        anaFaceReady = false;
        sAntAna = -1;
        drawAnalogClock(h, m, s);
        diaAnt = -1;
        tft.startWrite(); drawFechaWifi(); tft.endWrite();
    } else if (modoReloj == MODO_PACMAN) {
        initPacman();
        updatePacmanClock(h, m, s);
        diaAnt = -1;
        tft.startWrite(); drawFechaWifi(); tft.endWrite();
    } else if (modoReloj == MODO_CONWAY) {
        initConway();
        updateConwayClock(h, m, s);
        diaAnt = -1;
        tft.startWrite(); drawFechaWifi(); tft.endWrite();
    } else if (modoReloj == MODO_RETRO) {
        initRetro();
        updateRetroSI(h, m, s);
        diaAnt = -1;
        tft.startWrite(); drawFechaWifi(); tft.endWrite();
    } else if (modoReloj == MODO_FRACTAL) {
        initFractal();
        updateFractalClock(h, m, s);
        diaAnt = -1;
        tft.startWrite(); drawFechaWifi(); tft.endWrite();
    } else if (modoReloj == MODO_PIXEL) {
        initPixelArt();
        updatePixelArt(h, m, s);
        diaAnt = -1;
        tft.startWrite(); drawFechaWifi(); tft.endWrite();
    } else if (modoReloj == MODO_SOLAR) {
        initSolar();
        updateSolarClock(h, m, s);
        diaAnt = -1;
        tft.startWrite(); drawFechaWifi(); tft.endWrite();
    } else {
        memset(digMostrado, -1, sizeof(digMostrado));
        for (int i = 0; i < N_DECO; i++) resetDeco(decos[i], false);
        for (int pos = 0; pos < 4; pos++)
            for (int c = 0; c < DW; c++)
                for (int f = 0; f < DH; f++)
                    bloques[pos][c][f] = {0,0,0,0,0,false,false,false};
        setHora(h, m);
        colonAnt = !colonVis;
        diaAnt   = -1;
        tft.startWrite();
        drawColon(colonVis); colonAnt = colonVis;
        drawFechaWifi();
        tft.endWrite();
    }
    aplicarHorario(ti.tm_hour, ti.tm_min);
}

// ===================================================
// HORARIO: aplicar brillo segun programacion
// ===================================================
unsigned long tWake       = 0;     // millis del ultimo wake por toque (0=sin wake)
bool          pantallaOff = false; // true cuando el horario tiene la pantalla apagada

void aplicarHorario(int hh, int mm) {
    if (!horActivo) {
        pantallaOff = false;
        tWake = 0;
        analogWrite(DISP_BL, brillo);  // restaurar brillo si el horario esta desactivado
        return;
    }
    int minAct  = hh * 60 + mm;
    int minEnc  = encHH  * 60 + encMM;
    int minApag = apagHH * 60 + apagMM;
    bool encendido;
    if (minEnc <= minApag)
        encendido = (minAct >= minEnc && minAct < minApag);
    else  // horario cruza la medianoche
        encendido = (minAct >= minEnc || minAct < minApag);
    pantallaOff = !encendido;
    if (encendido) {
        tWake = 0;                    // ya encendida por horario, anular wake
        analogWrite(DISP_BL, brillo);
    } else if (tWake == 0) {
        analogWrite(DISP_BL, 0);     // apagar solo si no hay wake activo
    }
}

// ===================================================
// PANTALLA DE CONEXION WiFi (primer arranque)
// ===================================================
// (gestionada por WiFiManager - no se usa la funcion manual anterior)

// ===================================================
// Variables del loop
// ===================================================
#define FPS       60
#define MS_FRAME  (1000 / FPS)

unsigned long tFrame      = 0;
unsigned long tNTP        = 0;
unsigned long tPulsacion  = 0;
unsigned long tTouchReady = 0;
int           segAnt      = -2;
int           ampmAnt     = -2;   // -2=sin dibujar, 0=PM, 1=AM
bool          antTocando  = false;

// ===================================================
// SETUP
// ===================================================
void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Reloj Tetris ===");

    cargarPrefs();

    pinMode(DISP_BL, OUTPUT);
    analogWrite(DISP_BL, brillo);

    // Touch en HSPI (TFT_eSPI ocupa VSPI)
    touchSPI.begin(TOUCH_SCLK, TOUCH_MISO, TOUCH_MOSI);
    touch.begin(touchSPI);
    touch.setRotation(1);

    tft.init();
    delay(120);
    tft.setRotation(1);
    tft.invertDisplay(false);
    tft.fillScreen(C_BG);

    calcPosiciones();
    randomSeed(analogRead(PIN_LDR));
    for (int i = 0; i < N_DECO; i++) resetDeco(decos[i], false);

    // Pantalla de espera
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4); tft.setTextColor(TFT_WHITE, C_BG);
    tft.drawString("Retro Clock", W/2, 60);
    tft.setTextFont(2); tft.setTextColor(TFT_CYAN, C_BG);
    tft.drawString("Conectando a WiFi...", W/2, 110);
    tft.setTextColor(TFT_DARKGREY, C_BG);
    tft.drawString("(manten pulsado para portal)", W/2, 135);

    WiFiManager wm;
    wm.setConnectTimeout(20);
    wm.setConfigPortalTimeout(120);
    if (wm.autoConnect("RelojTetris")) {
        wifiOK = true;
        Serial.println("WiFi: " + WiFi.localIP().toString());
        ntp.begin();
        for (int i = 0; i < 10 && !ntp.update(); i++) delay(500);
        Serial.printf("NTP: %02d:%02d\n", ntp.getHours(), ntp.getMinutes());
    } else {
        Serial.println("Sin WiFi.");
        tft.fillScreen(TFT_BLACK);
        tft.setTextDatum(MC_DATUM); tft.setTextFont(2);
        tft.setTextColor(TFT_RED,    TFT_BLACK);
        tft.drawString("Sin conexion WiFi",            W/2, H/2-15);
        tft.setTextColor(TFT_YELLOW, TFT_BLACK);
        tft.drawString("La hora no esta sincronizada", W/2, H/2+10);
        delay(3000);
    }

    tft.fillScreen(C_BG);
    {
        struct tm ti = tiempoLocal();
        int h = ti.tm_hour, m = ti.tm_min, s = ti.tm_sec;
        if (formato12h) { esAM = (ti.tm_hour < 12); if (h == 0) h = 12; else if (h > 12) h -= 12; }
        if (modoReloj == MODO_ANALOGICO) {
            anaFaceReady = false;
            drawAnalogClock(h, m, s);
            tft.startWrite(); drawFechaWifi(); tft.endWrite();
        } else if (modoReloj == MODO_PACMAN) {
            initPacman();
            updatePacmanClock(h, m, s);
            tft.startWrite(); drawFechaWifi(); tft.endWrite();
        } else if (modoReloj == MODO_CONWAY) {
            initConway();
            updateConwayClock(h, m, s);
            tft.startWrite(); drawFechaWifi(); tft.endWrite();
        } else if (modoReloj == MODO_RETRO) {
            initRetro();
            updateRetroSI(h, m, s);
            tft.startWrite(); drawFechaWifi(); tft.endWrite();
        } else if (modoReloj == MODO_FRACTAL) {
            initFractal();
            updateFractalClock(h, m, s);
            tft.startWrite(); drawFechaWifi(); tft.endWrite();
        } else if (modoReloj == MODO_PIXEL) {
            initPixelArt();
            updatePixelArt(h, m, s);
            tft.startWrite(); drawFechaWifi(); tft.endWrite();
        } else if (modoReloj == MODO_SOLAR) {
            initSolar();
            updateSolarClock(h, m, s);
            tft.startWrite(); drawFechaWifi(); tft.endWrite();
        } else {
            setHora(h, m);
            tft.startWrite();
            drawColon(colonVis); colonAnt = colonVis;
            drawFechaWifi();
            tft.endWrite();
        }
    }

    tTouchReady = millis() + 2000;
    Serial.println("Listo. Manten pulsado 2s para menu.");
}

// ===================================================
// LOOP
// ===================================================
void loop() {
    unsigned long now = millis();

    // Wake por toque cuando la pantalla esta apagada por horario
    if (pantallaOff && tWake == 0) {
        Punto pw = leerTouch();
        if (pw.pulsado) {
            tWake = now;
            analogWrite(DISP_BL, brillo);
            antTocando    = false;
            tTouchReady   = now + 800;   // pausa para evitar que cuente como pulsacion larga
        }
        return;   // no procesar mas hasta el siguiente frame
    }

    // Expirar wake (10 s) y volver a apagar
    if (pantallaOff && tWake > 0 && now - tWake >= 10000) {
        tWake = 0;
        analogWrite(DISP_BL, 0);
        antTocando = false;
    }

    // Deteccion pulsacion larga (2s) para menu
    Punto p = (now >= tTouchReady) ? leerTouch() : Punto{0,0,false};
    if (p.pulsado) {
        if (!antTocando) { tPulsacion = now; antTocando = true; }
        else if (now - tPulsacion >= 2000) {
            antTocando = false;
            mostrarMenu();
            tTouchReady = millis() + 1000;  // pausa tras salir del menu
            return;
        }
    } else {
        antTocando = false;
        tPulsacion = 0;
    }

    if (now - tFrame < MS_FRAME) return;
    tFrame = now;

    // Reconectar / NTP cada 60s
    if (now - tNTP >= 60000) {
        tNTP = now;
        if (WiFi.status() != WL_CONNECTED) { WiFi.reconnect(); wifiOK = false; }
        else { ntp.update(); wifiOK = true; }
    }

    struct tm ti = tiempoLocal();
    int h = ti.tm_hour, m = ti.tm_min, s = ti.tm_sec;
    if (formato12h) { esAM = (ti.tm_hour < 12); if (h == 0) h = 12; else if (h > 12) h -= 12; }

    if (modoReloj == MODO_ANALOGICO) {
        if (s != sAntAna) {
            drawAnalogClock(h, m, s);
            sAntAna = s;
        }
    } else if (modoReloj == MODO_PACMAN) {
        updatePacmanClock(h, m, s);
    } else if (modoReloj == MODO_CONWAY) {
        updateConwayClock(h, m, s);
    } else if (modoReloj == MODO_RETRO) {
        updateRetroSI(h, m, s);
    } else if (modoReloj == MODO_FRACTAL) {
        updateFractalClock(h, m, s);
    } else if (modoReloj == MODO_PIXEL) {
        updatePixelArt(h, m, s);
    } else if (modoReloj == MODO_SOLAR) {
        updateSolarClock(h, m, s);
    } else {
        // Modo Tetris
        if (s != segAnt && segAnt != -2) colonVis = !colonVis;
        segAnt = s;
        setHora(h, m);
        // AM/PM debajo del reloj (solo cuando formato12h y cambia)
        {
            int ampmNow = formato12h ? (int)esAM : -1;
            if (ampmNow != ampmAnt) {
                tft.startWrite();
                tft.fillRect(CLOCK_X2 - 42, CLOCK_Y2 + 2, 42, 14, C_BG);
                if (ampmNow >= 0) {
                    tft.setTextFont(2); tft.setTextDatum(MR_DATUM);
                    tft.setTextColor(C_COLON, C_BG);
                    tft.drawString(ampmNow ? "AM" : "PM", CLOCK_X2, CLOCK_Y2 + 9);
                }
                tft.endWrite();
                ampmAnt = ampmNow;
            }
        }
        renderFrame();
    }

    // Redibujar fecha/wifi si cambia (ambos modos)
    if (ti.tm_mday != diaAnt || wifiOK != wifiAnt) {
        tft.startWrite(); drawFechaWifi(); tft.endWrite();
    }

    // Aplicar horario de pantalla (comprobar cada minuto)
    static int minAnt = -1;
    int minAct = ti.tm_hour * 60 + ti.tm_min;
    if (minAct != minAnt) { minAnt = minAct; aplicarHorario(ti.tm_hour, ti.tm_min); }
}
