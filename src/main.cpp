// main.cpp
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <vector>
#include <algorithm>
#include <cstring>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "nes.h"
#include "lgfx_conf.h"          // 使用独立的屏幕配置文件
#include "logo_bitmap.h"
#include "driver/i2s.h"
#include "esp_err.h"
#include "esp_timer.h"

// 串口调试开关
#ifndef ENABLE_DEBUG_SERIAL
#define ENABLE_DEBUG_SERIAL false
#endif

// ================ 菜单颜色配置 (高级灰色调) ================
#define MENU_BG_COLOR       0x2104  // 深灰背景 (RGB: 32, 32, 32)
#define MENU_HEADER_COLOR   0x4A69  // 中灰标题背景 (RGB: 72, 77, 72)
#define MENU_TEXT_COLOR     0xC618  // 浅灰文字 (RGB: 192, 192, 192)
#define MENU_HIGHLIGHT_BG   0xFDE0  // 选中项背景 (RGB: 255, 255, 0)
#define MENU_ARROW_COLOR    0xAD75  // 箭头颜色 (RGB: 168, 174, 168)
#define MENU_HINT_COLOR     0x7BCF  // 提示文字颜色 (RGB: 120, 120, 120)
#define MENU_TITLE_COLOR    0xE71C  // 标题文字 (RGB: 224, 224, 224)
#define MENU_BORDER_COLOR   0x52AA  // 边框颜色 (RGB: 80, 85, 80)
#define PAUSE_OVERLAY_COLOR 0x18C3  // 暂停遮罩 (深色半透明效果)

// ================ 菜单状态 ================
enum AppState {
    STATE_MENU,     // 主菜单
    STATE_PLAYING,  // 游戏中
    STATE_PAUSED    // 暂停菜单
};

static AppState currentState = STATE_MENU;
static std::vector<String> romList;       // ROM 文件列表
static int selectedIndex = 0;             // 当前选中的游戏索引
static int scrollOffset = 0;              // 滚动偏移
static const int ITEMS_PER_PAGE = 5;      // 每页显示的游戏数量 (160x128 横屏)
static int pauseMenuIndex = 0;            // 暂停菜单选项索引
static constexpr int PAUSE_OPTION_COUNT = 5;
static constexpr int PAUSE_VOLUME_INDEX = 1;

// ROM 文件名可能包含 UTF-8 中文；默认 Font0 不含中文字形。
static const lgfx::IFont* MENU_ROM_FONT = &fonts::efontCN_16;
static const int MENU_ROM_NAME_MAX_WIDTH = 116;  // 160px 横屏列表可用宽度

// 按键防抖
static unsigned long lastButtonTime = 0;
static const unsigned long BUTTON_DEBOUNCE = 200;  // 200ms防抖

#if ENABLE_DEBUG_SERIAL
#define FPS_PRINT(...) Serial.printf(__VA_ARGS__)
#else
#define FPS_PRINT(...) ((void)0)
#endif

// ================ PIN定义 ================
// SD卡引脚
#define SD_CS_PIN     42
#define SD_SCLK_PIN   40
#define SD_MISO_PIN   39
#define SD_MOSI_PIN   41
#define SD_FREQ       10000000  // 10 MHz

// 游戏控制器按键
#define A_BUTTON      48
#define B_BUTTON      47
#define LEFT_BUTTON   8
#define RIGHT_BUTTON  18
#define UP_BUTTON     17
#define DOWN_BUTTON   3
#define START_BUTTON  15
#define SELECT_BUTTON 16

// I2S / APU -> MAX98357A (I2S DAC)
#define I2S_BCLK_PIN 5
#define I2S_LRCLK_PIN 4
#define I2S_DATA_PIN 6

// 音频参数
constexpr int AUDIO_SAMPLE_RATE = 44100;
constexpr int I2S_NUM = 0;

// ================ 全局变量 ================
NES nes;
LGFX tft;

// ====================================================================
//  屏幕参数 —— 128x160 面板横屏使用 (逻辑 160 x 128)
//  NES 原生 256x240 经 2x 降采样后为 128x120，居中显示
//  左右各 16px、上下各 4px 黑边
// ====================================================================
constexpr int TFT_WIDTH     = 160;   // 横屏逻辑宽度
constexpr int TFT_HEIGHT    = 128;   // 横屏逻辑高度
constexpr int SCREEN_WIDTH  = 256;   // NES 原始分辨率（模拟器内部，勿改）
constexpr int SCREEN_HEIGHT = 240;
constexpr int DOWNSCALE     = 2;                          // 2x 降采样
constexpr int DISP_WIDTH    = SCREEN_WIDTH  / DOWNSCALE;  // 128
constexpr int DISP_HEIGHT   = SCREEN_HEIGHT / DOWNSCALE;  // 120
constexpr int DISP_OFFSET_X = (TFT_WIDTH  - DISP_WIDTH)  / 2;  // 16
constexpr int DISP_OFFSET_Y = (TFT_HEIGHT - DISP_HEIGHT) / 2;  // 4

// FPS 统计变量
static uint32_t last_emulation_us = 0;
static uint32_t fps_count = 0;
static uint32_t fps_last_ms = 0;
static uint32_t last_dma_us = 0;
static uint32_t game_start_ms = 0;
static uint32_t last_rendered_ms = 0;

SPIClass sdSPI(FSPI);

// 双缓冲：模拟器仍以 256x240 渲染
static uint16_t* frame_buf[2] = {nullptr, nullptr};
// 降采样缓冲：256x240 -> 128x120
static uint16_t* disp_downscale_buf = nullptr;
static volatile uint8_t render_buf_idx = 0;
static volatile uint8_t last_displayed_idx = 0;
static QueueHandle_t frame_queue = nullptr;

static void initializeAudio();
static void apu_task(void* arg);
static void muteAudio();
static bool gameJustEntered = false;
static volatile bool gameRunning = false;
static bool sdCardAvailable = false;

// 帧同步
const uint32_t FRAME_TIME_US = 16667;  // ~60 FPS
const int CPU_CYCLES_PER_FRAME = 29780;
static bool ENABLE_FRAMESKIP = true;
static uint64_t next_frame_us = 0;
static uint8_t force_render_frames = 0;
static uint8_t consecutive_skipped_frames = 0;
static uint8_t frameskip_phase = 0;

struct ButtonState {
    uint8_t A = 0;
    uint8_t B = 0;
    uint8_t LEFT = 0;
    uint8_t RIGHT = 0;
    uint8_t UP = 0;
    uint8_t DOWN = 0;
    uint8_t START = 0;
    uint8_t SELECT = 0;
} buttons;

// ================ 函数前向声明 ================
void updateButtons();
void runFrame();
void scanROMFiles();
void playBootAnimation();
void drawBootLogo(int y, uint16_t color);
void drawMainMenu();
void drawMenuList();
void drawPauseMenu();
void drawVolumeBlocks(int x, int y, uint8_t level, bool selected);
void handleMenuInput();
void handlePauseInput();
bool loadSelectedROM();
void returnToMainMenu();
void clearScreenForGame();
bool tryInitSD();

static void resetFrameScheduler(uint8_t forceRenderFrames = 2) {
    next_frame_us = 0;
    force_render_frames = forceRenderFrames;
    consecutive_skipped_frames = 0;
    frameskip_phase = 0;
    nes.requestFrameSkip(false);
}

static void getSaveStatePath(char* savePath, size_t maxLen) {
    const char* romPath = nes.getCurrentRomPath();
    strncpy(savePath, romPath, maxLen - 1);
    savePath[maxLen - 1] = '\0';
    char* dot = strrchr(savePath, '.');
    if (dot && (dot - savePath + 5) < (int)maxLen) {
        strcpy(dot, ".sav");
    } else {
        strncat(savePath, ".sav", maxLen - strlen(savePath) - 1);
    }
}

// ================ 初始化函数 ================
void initializeSerial() {
#if ENABLE_DEBUG_SERIAL
    Serial.begin(115200);
    delay(500);
#else
    (void)0;
#endif
}

void initializeScreen() {
    tft.init();
    tft.setRotation(1);  // 横屏 160x128
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(1);
    tft.setCursor(0, 0);

    // 模拟器双缓冲（保持 256x240 NES 原生分辨率）
    for (int i = 0; i < 2; i++) {
        frame_buf[i] = (uint16_t*)heap_caps_malloc(
            SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t),
            MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL
        );
        if (frame_buf[i]) {
            memset(frame_buf[i], 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));
        }
    }
    // 降采样缓冲 128x120
    disp_downscale_buf = (uint16_t*)heap_caps_malloc(
        DISP_WIDTH * DISP_HEIGHT * sizeof(uint16_t),
        MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL
    );
}

void drawBootLogo(int y, uint16_t color) {
    const int logoX = (TFT_WIDTH - DIJI_LOGO_W) / 2;
    tft.drawBitmap(logoX, y, DIJI_LOGO_BITS, DIJI_LOGO_W, DIJI_LOGO_H, color);
    tft.setTextColor(color, TFT_BLACK);
    tft.setTextSize(1);
    const char* subtitle = "ESP32-S3 NES";
    int subW = strlen(subtitle) * 6;
    tft.setCursor((TFT_WIDTH - subW) / 2, y + DIJI_LOGO_H + 8);
    tft.print(subtitle);
}

static bool bootLogoPixelOn(int x, int y) {
    if (x < 0 || x >= DIJI_LOGO_W || y < 0 || y >= DIJI_LOGO_H) return false;
    int byteIndex = y * ((DIJI_LOGO_W + 7) / 8) + (x >> 3);
    uint8_t mask = 0x80 >> (x & 7);
    return (pgm_read_byte(DIJI_LOGO_BITS + byteIndex) & mask) != 0;
}

static bool bootLogoBlockOn(int x, int y, int blockSize) {
    for (int yy = 0; yy < blockSize; yy++) {
        for (int xx = 0; xx < blockSize; xx++) {
            if (bootLogoPixelOn(x + xx, y + yy)) return true;
        }
    }
    return false;
}

static uint32_t bootHash(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352d;
    value ^= value >> 15;
    value *= 0x846ca68b;
    value ^= value >> 16;
    return value;
}

void playBootAnimation() {
    tft.fillScreen(TFT_BLACK);
    const int logoY = 20;
    const int logoX = (TFT_WIDTH - DIJI_LOGO_W) / 2;
    const int blockSize = 3;
    const int frames = 30;
    for (int frame = 0; frame <= frames; frame++) {
        tft.fillScreen(TFT_BLACK);
        for (int by = 0; by < DIJI_LOGO_H; by += blockSize) {
            for (int bx = 0; bx < DIJI_LOGO_W; bx += blockSize) {
                if (!bootLogoBlockOn(bx, by, blockSize)) continue;
                int targetX = logoX + bx;
                int targetY = logoY + by;
                uint32_t h = bootHash((uint32_t)bx * 131u + (uint32_t)by * 17u);
                int delayFrames = h % 9;
                int travelFrames = 14 + ((h >> 8) % 12);
                int localFrame = frame - delayFrames;
                if (localFrame < 0) {
                    if ((h & 0x03) != 0) continue;
                    localFrame = 0;
                }
                if (localFrame > travelFrames) localFrame = travelFrames;
                int startX = (int)((h >> 16) % 200) - 20;
                int startY = (int)((h >> 1) % 160) - 16;
                int jitterX = (int)((h >> 24) & 0x0F) - 8;
                int jitterY = (int)((h >> 20) & 0x0F) - 8;
                int eased = localFrame * localFrame * (3 * travelFrames - 2 * localFrame);
                int denom = travelFrames * travelFrames * travelFrames;
                int x = startX + ((targetX - startX) * eased) / denom;
                int y = startY + ((targetY - startY) * eased) / denom;
                int remaining = travelFrames - localFrame;
                if (remaining > 4) {
                    x += (jitterX * remaining) / travelFrames;
                    y += (jitterY * remaining) / travelFrames;
                }
                int particleSize = 2 + ((h >> 28) & 0x03);
                if (localFrame == travelFrames) particleSize = blockSize;
                tft.fillRect(x, y, particleSize, particleSize, TFT_WHITE);
            }
        }
        delay(24);
    }
    tft.fillScreen(TFT_BLACK);
    drawBootLogo(logoY, TFT_WHITE);
    delay(2000);
}

static bool tryEnqueueFrame() {
    PPU& ppu = nes.getPPU();
    if (!ppu.frameReady) return false;
    if (!ppu.renderedThisFrame) {
        ppu.frameReady = false;
        return false;
    }
    uint8_t send_idx = render_buf_idx;
    if (xQueueSend(frame_queue, &send_idx, 0) == pdTRUE) {
        render_buf_idx = 1 - render_buf_idx;
        ppu.frameBuffer = frame_buf[render_buf_idx];
        ppu.frameReady = false;
        last_rendered_ms = millis();
        return true;
    }
    return false;
}

// ====================================================================
//  display_task：256x240 帧缓冲 -> 2x 降采样到 128x120 -> DMA 推屏
// ====================================================================
static void display_task(void* arg) {
    uint8_t buf_idx;
    for (;;) {
        if (xQueueReceive(frame_queue, &buf_idx, portMAX_DELAY) != pdTRUE)
            continue;

        uint32_t t0 = micros();
        uint16_t* buf = frame_buf[buf_idx];

        if (disp_downscale_buf) {
            // 2x 最近邻降采样：每隔一个像素取样
            for (int y = 0; y < DISP_HEIGHT; y++) {
                const uint16_t* src = buf + (y * DOWNSCALE) * SCREEN_WIDTH;
                uint16_t* dst = disp_downscale_buf + y * DISP_WIDTH;
                for (int x = 0; x < DISP_WIDTH; x++) {
                    dst[x] = src[x * DOWNSCALE];
                }
            }
            tft.startWrite();
            tft.setAddrWindow(DISP_OFFSET_X, DISP_OFFSET_Y, DISP_WIDTH, DISP_HEIGHT);
            tft.pushPixelsDMA(disp_downscale_buf, DISP_WIDTH * DISP_HEIGHT);
            tft.waitDMA();
            tft.endWrite();
        }
        last_displayed_idx = buf_idx;
        last_dma_us = micros() - t0;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
}

void runFrame() {
    const int SCANLINES_PER_FRAME = 262;
    for (int i = 0; i < SCANLINES_PER_FRAME; ++i) {
        nes.stepScanline();
    }
}

void initializeButtons() {
    pinMode(A_BUTTON, INPUT_PULLUP);
    pinMode(B_BUTTON, INPUT_PULLUP);
    pinMode(LEFT_BUTTON, INPUT_PULLUP);
    pinMode(RIGHT_BUTTON, INPUT_PULLUP);
    pinMode(UP_BUTTON, INPUT_PULLUP);
    pinMode(DOWN_BUTTON, INPUT_PULLUP);
    pinMode(START_BUTTON, INPUT_PULLUP);
    pinMode(SELECT_BUTTON, INPUT_PULLUP);
}

void updateButtons() {
    buttons.A      = !digitalRead(A_BUTTON);
    buttons.B      = !digitalRead(B_BUTTON);
    buttons.LEFT   = !digitalRead(LEFT_BUTTON);
    buttons.RIGHT  = !digitalRead(RIGHT_BUTTON);
    buttons.UP     = !digitalRead(UP_BUTTON);
    buttons.DOWN   = !digitalRead(DOWN_BUTTON);
    buttons.START  = !digitalRead(START_BUTTON);
    buttons.SELECT = !digitalRead(SELECT_BUTTON);
}

// ================ 清除屏幕，进入游戏前调用 ================
void clearScreenForGame() {
    tft.waitDMA();
    tft.fillScreen(TFT_BLACK);  // 整屏涂黑（含四周黑边）

    if (frame_buf[0]) memset(frame_buf[0], 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));
    if (frame_buf[1]) memset(frame_buf[1], 0, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));

    uint8_t dummy;
    while (xQueueReceive(frame_queue, &dummy, 0) == pdTRUE) {}
}

bool tryInitSD() {
    sdSPI.begin(SD_SCLK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    if (!SD.begin(SD_CS_PIN, sdSPI, SD_FREQ)) {
        Serial.println("SD card init failed or not inserted");
        sdCardAvailable = false;
        return false;
    }
    Serial.println("SD card initialized");
    sdCardAvailable = true;
    return true;
}

void initializeSD() {
    tryInitSD();
}

// ================ ROM 文件扫描 ================
void scanROMFiles() {
    romList.clear();
    File root = SD.open("/");
    if (!root) {
        Serial.println("Failed to open root directory");
        return;
    }
    while (true) {
        File entry = root.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) {
            String filename = entry.name();
            String basename = filename;
            int lastSlash = filename.lastIndexOf('/');
            if (lastSlash >= 0) {
                basename = filename.substring(lastSlash + 1);
            }
            if (basename.startsWith("._")) {
                entry.close();
                continue;
            }
            if (filename.endsWith(".nes") || filename.endsWith(".NES") ||
                filename.endsWith(".Nes")) {
                if (!filename.startsWith("/")) {
                    filename = "/" + filename;
                }
                romList.push_back(filename);
                Serial.printf("Found ROM: %s\n", filename.c_str());
            }
        }
        entry.close();
    }
    root.close();
    Serial.printf("Total ROMs found: %d\n", romList.size());
    std::sort(romList.begin(), romList.end());
}

static int nextUtf8CharIndex(const String& text, int index) {
    const int length = text.length();
    if (index >= length) return length;
    uint8_t lead = (uint8_t)text[index];
    int charBytes = 1;
    if ((lead & 0xE0) == 0xC0) {
        charBytes = 2;
    } else if ((lead & 0xF0) == 0xE0) {
        charBytes = 3;
    } else if ((lead & 0xF8) == 0xF0) {
        charBytes = 4;
    }
    if (index + charBytes > length) return length;
    for (int i = 1; i < charBytes; i++) {
        if (((uint8_t)text[index + i] & 0xC0) != 0x80) return index + 1;
    }
    return index + charBytes;
}

static String trimTextToPixelWidth(const String& text, int maxWidth, const lgfx::IFont* font) {
    if (tft.textWidth(text, font) <= maxWidth) return text;
    const String ellipsis = "...";
    int ellipsisWidth = tft.textWidth(ellipsis, font);
    if (ellipsisWidth >= maxWidth) return "";
    String result;
    for (int i = 0; i < text.length();) {
        int next = nextUtf8CharIndex(text, i);
        String candidate = result + text.substring(i, next) + ellipsis;
        if (tft.textWidth(candidate, font) > maxWidth) break;
        result += text.substring(i, next);
        i = next;
    }
    return result + ellipsis;
}

static String getROMDisplayName(const String& romPath) {
    String displayName = romPath;
    int lastSlash = displayName.lastIndexOf('/');
    if (lastSlash >= 0) {
        displayName = displayName.substring(lastSlash + 1);
    }
    int dotPos = displayName.lastIndexOf('.');
    if (dotPos > 0) {
        displayName = displayName.substring(0, dotPos);
    }
    return trimTextToPixelWidth(displayName, MENU_ROM_NAME_MAX_WIDTH, MENU_ROM_FONT);
}

static void drawROMDisplayName(const String& romPath, int x, int y, uint16_t color) {
    tft.setTextSize(1);
    tft.setTextColor(color);
    tft.drawString(getROMDisplayName(romPath), x, y, MENU_ROM_FONT);
}

// ================ 主菜单绘制 (160x128 横屏) ================
void drawMainMenu() {
    tft.fillScreen(MENU_BG_COLOR);

    // ===== 头部标题 =====
    tft.fillRect(0, 0, TFT_WIDTH, 22, MENU_HEADER_COLOR);
    tft.drawRect(0, 0, TFT_WIDTH, 22, MENU_BORDER_COLOR);
    tft.setTextColor(MENU_TITLE_COLOR);
    tft.setTextSize(2);
    tft.setCursor(38, 4);   // "DIJI-NES" 7字*12=84, (160-84)/2=38
    tft.print("DIJI-NES");

    // ===== 游戏列表区域 =====
    const int listStartY  = 26;
    const int itemHeight  = 16;
    const int listWidth   = 152;
    const int listX       = 4;

    tft.drawRect(listX - 2, listStartY - 2, listWidth + 4,
                 ITEMS_PER_PAGE * itemHeight + 4, MENU_BORDER_COLOR);

    if (romList.empty()) {
        tft.setTextColor(MENU_HINT_COLOR);
        tft.setTextSize(1);
        if (!sdCardAvailable) {
            tft.setCursor(40, listStartY + 20);
            tft.print("No SD card");
            tft.setCursor(24, listStartY + 38);
            tft.print("Insert SD with");
            tft.setCursor(44, listStartY + 52);
            tft.print(".nes ROMs");
            tft.setCursor(36, listStartY + 76);
            tft.setTextColor(MENU_ARROW_COLOR);
            tft.print("Press A retry");
        } else {
            tft.setCursor(40, listStartY + 30);
            tft.print("No ROM files");
            tft.setCursor(30, listStartY + 48);
            tft.print("Add .nes files");
        }
    } else {
        int totalPages = (romList.size() + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
        int currentPage = scrollOffset / ITEMS_PER_PAGE + 1;
        tft.setTextSize(1);
        for (int i = 0; i < ITEMS_PER_PAGE; i++) {
            int romIndex = scrollOffset + i;
            if (romIndex >= (int)romList.size()) break;
            int itemY = listStartY + i * itemHeight;
            if (romIndex == selectedIndex) {
                tft.fillRect(listX, itemY, listWidth, itemHeight - 1, MENU_HIGHLIGHT_BG);
                tft.setTextColor(MENU_ARROW_COLOR);
                tft.setCursor(listX + 2, itemY + 4);
                tft.print(">");
                tft.setCursor(listX + listWidth - 12, itemY + 4);
                tft.print("<");
                tft.setTextColor(MENU_TITLE_COLOR);
            } else {
                tft.setTextColor(MENU_TEXT_COLOR);
            }
            drawROMDisplayName(romList[romIndex], listX + 14, itemY,
                               romIndex == selectedIndex ? MENU_TITLE_COLOR : MENU_TEXT_COLOR);
        }
        // 分页信息
        tft.setTextColor(MENU_HINT_COLOR);
        tft.fillRect(120, listStartY + ITEMS_PER_PAGE * itemHeight + 1, 36, 10, MENU_BG_COLOR);
        tft.setCursor(126, listStartY + ITEMS_PER_PAGE * itemHeight + 1);
        char pageInfo[16];
        snprintf(pageInfo, sizeof(pageInfo), "%d/%d", currentPage, totalPages);
        tft.print(pageInfo);
    }

    // ===== 底部操作提示 =====
    const int hintY = 112;
    tft.fillRect(0, hintY, TFT_WIDTH, TFT_HEIGHT - hintY, MENU_HEADER_COLOR);
    tft.drawFastHLine(0, hintY, TFT_WIDTH, MENU_BORDER_COLOR);
    tft.setTextColor(MENU_HINT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(6, hintY + 4);
    tft.print("UP/DOWN:Select  A/START:Play");
}

// ================ 暂停菜单绘制 (160x128) ================
void drawPauseMenu() {
    // 在游戏画面（128x120, x=16~144, y=4~124）上绘制条纹遮罩
    for (int y = DISP_OFFSET_Y; y < DISP_OFFSET_Y + DISP_HEIGHT; y += 2) {
        tft.drawFastHLine(DISP_OFFSET_X, y, DISP_WIDTH, PAUSE_OVERLAY_COLOR);
    }

    // 暂停菜单框
    const int menuWidth  = 120;
    const int menuHeight = 110;
    const int menuX = (TFT_WIDTH - menuWidth) / 2;   // 20
    const int menuY = 9;

    tft.fillRect(menuX, menuY, menuWidth, menuHeight, MENU_BG_COLOR);
    tft.drawRect(menuX, menuY, menuWidth, menuHeight, MENU_BORDER_COLOR);
    tft.drawRect(menuX + 1, menuY + 1, menuWidth - 2, menuHeight - 2, MENU_BORDER_COLOR);

    // 标题
    tft.setTextColor(MENU_TITLE_COLOR);
    tft.setTextSize(2);
    tft.setCursor(menuX + 18, menuY + 6);  // "PAUSED" 7*12=84, (120-84)/2=18
    tft.print("PAUSED");

    tft.drawFastHLine(menuX + 10, menuY + 28, menuWidth - 20, MENU_BORDER_COLOR);

    const char* options[] = {"Continue", "Volume", "Save State", "Load State", "Exit Menu"};
    tft.setTextSize(1);
    for (int i = 0; i < PAUSE_OPTION_COUNT; i++) {
        int optY = menuY + 34 + i * 14;
        if (i == pauseMenuIndex) {
            tft.fillRect(menuX + 8, optY - 2, menuWidth - 16, 13, MENU_HIGHLIGHT_BG);
            tft.setTextColor(MENU_ARROW_COLOR);
            tft.setCursor(menuX + 14, optY + 2);
            tft.print(">");
            tft.setTextColor(MENU_TITLE_COLOR);
        } else {
            tft.setTextColor(MENU_TEXT_COLOR);
            tft.setCursor(menuX + 14, optY + 2);
            tft.print(" ");
        }
        tft.setCursor(menuX + 24, optY + 2);
        tft.print(options[i]);
        if (i == PAUSE_VOLUME_INDEX) {
            drawVolumeBlocks(menuX + 74, optY + 2, nes.apu.getVolumeLevel(), i == pauseMenuIndex);
        }
    }

    tft.setTextColor(MENU_HINT_COLOR);
    tft.setCursor(8, menuY + menuHeight - 8);
    tft.print("UP/DOWN Select  L/R Vol");
}

void drawVolumeBlocks(int x, int y, uint8_t level, bool selected) {
    const int blockW = 7;
    const int blockH = 7;
    const int gap = 2;
    uint16_t filled = selected ? MENU_TITLE_COLOR : MENU_TEXT_COLOR;
    uint16_t empty = selected ? MENU_ARROW_COLOR : MENU_BORDER_COLOR;
    for (int i = 0; i < 5; i++) {
        int bx = x + i * (blockW + gap);
        tft.drawRect(bx, y, blockW, blockH, empty);
        if (i < level) {
            tft.fillRect(bx + 1, y + 1, blockW - 2, blockH - 2, filled);
        } else {
            tft.fillRect(bx + 1, y + 1, blockW - 2, blockH - 2,
                         selected ? MENU_HIGHLIGHT_BG : MENU_BG_COLOR);
        }
    }
}

// ================ 菜单输入处理 ================
void handleMenuInput() {
    unsigned long now = millis();
    if (now - lastButtonTime < BUTTON_DEBOUNCE) return;
    updateButtons();

    if (romList.empty()) {
        if (buttons.A) {
            lastButtonTime = now;
            SD.end();
            delay(100);
            if (tryInitSD()) {
                scanROMFiles();
            }
            drawMainMenu();
        }
        return;
    }

    bool buttonPressed = false;
    if (buttons.UP) {
        if (selectedIndex > 0) {
            selectedIndex--;
            if (selectedIndex < scrollOffset) scrollOffset = selectedIndex;
            buttonPressed = true;
        }
    }
    if (buttons.DOWN) {
        if (selectedIndex < (int)romList.size() - 1) {
            selectedIndex++;
            if (selectedIndex >= scrollOffset + ITEMS_PER_PAGE)
                scrollOffset = selectedIndex - ITEMS_PER_PAGE + 1;
            buttonPressed = true;
        }
    }
    if (buttons.START || buttons.A) {
        if (loadSelectedROM()) currentState = STATE_PLAYING;
        buttonPressed = true;
    }
    if (buttonPressed) {
        lastButtonTime = now;
        drawMenuList();
    }
}

// ================ 绘制菜单列表区域（局部刷新） ================
void drawMenuList() {
    const int listStartY  = 26;
    const int itemHeight  = 16;
    const int listWidth   = 152;
    const int listX       = 4;

    tft.fillRect(listX, listStartY, listWidth, ITEMS_PER_PAGE * itemHeight, MENU_BG_COLOR);
    if (romList.empty()) return;
    tft.setTextSize(1);
    for (int i = 0; i < ITEMS_PER_PAGE; i++) {
        int romIndex = scrollOffset + i;
        if (romIndex >= (int)romList.size()) break;
        int itemY = listStartY + i * itemHeight;
        if (romIndex == selectedIndex) {
            tft.fillRect(listX, itemY, listWidth, itemHeight - 1, MENU_HIGHLIGHT_BG);
            tft.setTextColor(MENU_ARROW_COLOR);
            tft.setCursor(listX + 2, itemY + 4);
            tft.print(">");
            tft.setCursor(listX + listWidth - 12, itemY + 4);
            tft.print("<");
            tft.setTextColor(MENU_TITLE_COLOR);
        } else {
            tft.setTextColor(MENU_TEXT_COLOR);
        }
        drawROMDisplayName(romList[romIndex], listX + 14, itemY,
                           romIndex == selectedIndex ? MENU_TITLE_COLOR : MENU_TEXT_COLOR);
    }
    int totalPages = (romList.size() + ITEMS_PER_PAGE - 1) / ITEMS_PER_PAGE;
    int currentPage = scrollOffset / ITEMS_PER_PAGE + 1;
    tft.setTextColor(MENU_HINT_COLOR);
    tft.fillRect(120, listStartY + ITEMS_PER_PAGE * itemHeight + 1, 36, 10, MENU_BG_COLOR);
    tft.setCursor(126, listStartY + ITEMS_PER_PAGE * itemHeight + 1);
    char pageInfo[16];
    snprintf(pageInfo, sizeof(pageInfo), "%d/%d", currentPage, totalPages);
    tft.print(pageInfo);
}

// ================ 暂停输入处理 ================
void handlePauseInput() {
    unsigned long now = millis();
    if (now - lastButtonTime < BUTTON_DEBOUNCE) return;
    updateButtons();
    bool buttonPressed = false;

    if (buttons.UP) {
        if (pauseMenuIndex > 0) { pauseMenuIndex--; buttonPressed = true; }
    }
    if (buttons.DOWN) {
        if (pauseMenuIndex < PAUSE_OPTION_COUNT - 1) { pauseMenuIndex++; buttonPressed = true; }
    }
    if (pauseMenuIndex == PAUSE_VOLUME_INDEX && (buttons.LEFT || buttons.RIGHT)) {
        uint8_t level = nes.apu.getVolumeLevel();
        if (buttons.LEFT && level > 0) {
            nes.apu.setVolumeLevel(level - 1); buttonPressed = true;
        } else if (buttons.RIGHT && level < 5) {
            nes.apu.setVolumeLevel(level + 1); buttonPressed = true;
        }
    }

    if (buttons.A || buttons.START) {
        delay(100);
        while (digitalRead(A_BUTTON) == LOW || digitalRead(START_BUTTON) == LOW) delay(10);
        delay(50);

        if (pauseMenuIndex == 0) {
            clearScreenForGame();
            resetFrameScheduler(3);
            gameRunning = true;
            currentState = STATE_PLAYING;
        } else if (pauseMenuIndex == PAUSE_VOLUME_INDEX) {
            uint8_t level = nes.apu.getVolumeLevel();
            nes.apu.setVolumeLevel(level < 5 ? level + 1 : 0);
            drawPauseMenu();
        } else if (pauseMenuIndex == 2) {
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(MENU_TITLE_COLOR);
            tft.setTextSize(2);
            tft.setCursor(20, 55);
            tft.print("Saving...");
            char savePath[128];
            getSaveStatePath(savePath, sizeof(savePath));
            if (nes.saveState(savePath)) {
                tft.fillScreen(TFT_BLACK);
                tft.setTextColor(0x07E0);
                tft.setTextSize(2);
                tft.setCursor(38, 55);
                tft.print("SAVED!");
                delay(1000);
            } else {
                tft.fillScreen(TFT_BLACK);
                tft.setTextColor(0xF800);
                tft.setTextSize(2);
                tft.setCursor(32, 55);
                tft.print("FAILED!");
                delay(1500);
            }
            clearScreenForGame();
            resetFrameScheduler(3);
            gameRunning = true;
            currentState = STATE_PLAYING;
        } else if (pauseMenuIndex == 3) {
            tft.fillScreen(TFT_BLACK);
            tft.setTextColor(MENU_TITLE_COLOR);
            tft.setTextSize(2);
            tft.setCursor(20, 55);
            tft.print("Loading...");
            char savePath[128];
            getSaveStatePath(savePath, sizeof(savePath));
            if (nes.loadState(savePath)) {
                tft.fillScreen(TFT_BLACK);
                tft.setTextColor(0x07E0);
                tft.setTextSize(2);
                tft.setCursor(32, 55);
                tft.print("LOADED!");
                delay(1000);
            } else {
                tft.fillScreen(TFT_BLACK);
                tft.setTextColor(0xF800);
                tft.setTextSize(2);
                tft.setCursor(32, 55);
                tft.print("FAILED!");
                tft.setTextColor(MENU_HINT_COLOR);
                tft.setTextSize(1);
                tft.setCursor(38, 80);
                tft.print("No save state");
                delay(1500);
            }
            clearScreenForGame();
            resetFrameScheduler(3);
            gameRunning = true;
            currentState = STATE_PLAYING;
        } else {
            returnToMainMenu();
        }
        return;
    }

    if (buttons.B) {
        delay(100);
        while (digitalRead(B_BUTTON) == LOW) delay(10);
        delay(50);
        clearScreenForGame();
        resetFrameScheduler(3);
        gameRunning = true;
        currentState = STATE_PLAYING;
        return;
    }

    if (buttonPressed) {
        lastButtonTime = now;
        if (currentState == STATE_PAUSED) drawPauseMenu();
    }
}

// ================ 加载选中的ROM ================
bool loadSelectedROM() {
    if (selectedIndex < 0 || selectedIndex >= (int)romList.size()) return false;

    const char* romPath = romList[selectedIndex].c_str();
    Serial.printf("Loading ROM: %s\n", romPath);

    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(MENU_TITLE_COLOR);
    tft.setTextSize(2);
    tft.setCursor(20, 55);
    tft.print("Loading...");

    if (!nes.loadROM(romPath)) {
        Serial.printf("Failed to load ROM: %s\n", romPath);
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(0xF800);
        tft.setTextSize(2);
        tft.setCursor(32, 42);
        tft.print("FAILED!");
        tft.setTextColor(MENU_HINT_COLOR);
        tft.setTextSize(1);
        tft.setCursor(23, 68);
        tft.print("Unsupported mapper");
        tft.setCursor(32, 84);
        tft.print("Mapper 0-4 only");
        tft.setCursor(17, 100);
        tft.print("Returning to menu...");
        delay(3000);
        tft.fillScreen(MENU_BG_COLOR);
        drawMainMenu();
        return false;
    }

    nes.reset();
    nes.setFrameskipEnabled(ENABLE_FRAMESKIP);
    nes.getPPU().frameBuffer = frame_buf[render_buf_idx];
    clearScreenForGame();
    resetFrameScheduler(3);
    game_start_ms = millis();
    last_rendered_ms = 0;
    gameRunning = true;
    Serial.println("ROM loaded successfully");
    gameJustEntered = true;
    return true;
}

// ================ 返回主菜单 ================
void returnToMainMenu() {
    gameRunning = false;
    muteAudio();
    currentState = STATE_MENU;
    selectedIndex = 0;
    scrollOffset = 0;
    pauseMenuIndex = 0;
    tft.fillScreen(MENU_BG_COLOR);
    drawMainMenu();
}

void loadROM() {
    if (sdCardAvailable) scanROMFiles();
}

// ---------------- Audio (I2S) ----------------
static void initializeAudio() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = AUDIO_SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_I2S_MSB,
        .intr_alloc_flags = 0,
        .dma_buf_count = 4,
        .dma_buf_len = 256,
        .use_apll = false
    };
    esp_err_t res = i2s_driver_install((i2s_port_t)I2S_NUM, &i2s_config, 0, NULL);
    if (res != ESP_OK) {
        Serial.printf("I2S driver install failed: %d\n", res);
    } else {
        Serial.println("I2S driver installed");
    }
    i2s_pin_config_t pin_config = {
        .bck_io_num = I2S_BCLK_PIN,
        .ws_io_num = I2S_LRCLK_PIN,
        .data_out_num = I2S_DATA_PIN,
        .data_in_num = I2S_PIN_NO_CHANGE
    };
    i2s_set_pin((i2s_port_t)I2S_NUM, &pin_config);
    i2s_zero_dma_buffer((i2s_port_t)I2S_NUM);
    nes.apu.setSampleRate(AUDIO_SAMPLE_RATE);
    xTaskCreatePinnedToCore(apu_task, "APU", 2048, &nes.apu, 1, NULL, 0);
}

static void apu_task(void* arg) {
    APU* apu = (APU*)arg;
    while (1) {
        if (gameRunning) {
            apu->clock();
        } else {
            vTaskDelay(1);
        }
    }
}

static void muteAudio() {
    i2s_zero_dma_buffer((i2s_port_t)I2S_NUM);
    delay(50);
    i2s_zero_dma_buffer((i2s_port_t)I2S_NUM);
}

// ================ 主程序 ================
void setup() {
    initializeSerial();
    initializeScreen();
    initializeButtons();
    initializeSD();
    loadROM();
    initializeAudio();

    frame_queue = xQueueCreate(1, sizeof(uint8_t));
    if (frame_queue) {
        xTaskCreatePinnedToCore(display_task, "Display", 4096, nullptr, 1, nullptr, 0);
    }

    currentState = STATE_MENU;
    playBootAnimation();
    drawMainMenu();
}

void loop() {
    switch (currentState) {
        case STATE_MENU:
            handleMenuInput();
            delay(50);
            return;
        case STATE_PAUSED:
            handlePauseInput();
            delay(50);
            return;
        case STATE_PLAYING:
            if (gameJustEntered) {
                clearScreenForGame();
                gameJustEntered = false;
            }
            break;
    }

    #define FRAME_TIME_US 16639
    static bool pauseKeyReleased = true;
    updateButtons();

    if (buttons.START && buttons.SELECT) {
        if (pauseKeyReleased) {
            pauseKeyReleased = false;
            currentState = STATE_PAUSED;
            pauseMenuIndex = 0;
            gameRunning = false;
            muteAudio();
            delay(100);
            while (digitalRead(START_BUTTON) == LOW || digitalRead(SELECT_BUTTON) == LOW) delay(10);
            delay(100);
            drawPauseMenu();
            return;
        }
    } else {
        pauseKeyReleased = true;
    }

    uint8_t controllerState = 0;
    if (buttons.A)      controllerState |= 0x01;
    if (buttons.B)      controllerState |= 0x02;
    if (buttons.SELECT) controllerState |= 0x04;
    if (buttons.START)  controllerState |= 0x08;
    if (buttons.UP)     controllerState |= 0x10;
    if (buttons.DOWN)   controllerState |= 0x20;
    if (buttons.LEFT)   controllerState |= 0x40;
    if (buttons.RIGHT)  controllerState |= 0x80;
    nes.setController(0, controllerState);

    if (next_frame_us == 0) next_frame_us = esp_timer_get_time();
    int64_t frameLagUs = (int64_t)esp_timer_get_time() - (int64_t)next_frame_us;
    bool frameskipPhaseAllowsSkip = (frameskip_phase == 0 ||
                                     frameskip_phase == 2 ||
                                     frameskip_phase == 4 ||
                                     frameskip_phase == 6 ||
                                     frameskip_phase == 8);
    bool shouldSkipFrame = ENABLE_FRAMESKIP &&
                           (force_render_frames == 0) &&
                           (consecutive_skipped_frames == 0) &&
                           frameskipPhaseAllowsSkip &&
                           (frameLagUs > (FRAME_TIME_US / 2));
    nes.requestFrameSkip(shouldSkipFrame);

    uint32_t emu0 = micros();
    nes.clock();
    last_emulation_us = micros() - emu0;

    tryEnqueueFrame();
    if (last_rendered_ms == 0 && millis() - game_start_ms > 3500) {
        gameRunning = false;
        muteAudio();
        tft.fillScreen(TFT_BLACK);
        tft.setTextColor(0xF800);
        tft.setTextSize(1);
        tft.setCursor(41, 45);
        tft.print("START FAILED");
        tft.setTextColor(MENU_HINT_COLOR);
        tft.setCursor(26, 68);
        tft.print("Unsupported ROM");
        tft.setCursor(17, 90);
        tft.print("Returning to menu...");
        delay(3000);
        tft.fillScreen(MENU_BG_COLOR);
        drawMainMenu();
        currentState = STATE_MENU;
        return;
    }
    if (shouldSkipFrame) {
        consecutive_skipped_frames++;
    } else {
        consecutive_skipped_frames = 0;
        if (force_render_frames > 0) force_render_frames--;
    }
    frameskip_phase++;
    if (frameskip_phase >= 9) frameskip_phase = 0;

    fps_count++;
    uint32_t curMs = millis();
    if (fps_last_ms == 0) fps_last_ms = curMs;
    if (curMs - fps_last_ms >= 1000) {
        FPS_PRINT("FPS:%u  EMU:%uus  DMA:%uus\n",
            fps_count, last_emulation_us, last_dma_us);
        fps_count = 0;
        fps_last_ms = curMs;
    }

    uint64_t now = esp_timer_get_time();
    if (now < next_frame_us) {
        ets_delay_us(next_frame_us - now);
    }
    next_frame_us += FRAME_TIME_US;
    #undef FRAME_TIME_US
}