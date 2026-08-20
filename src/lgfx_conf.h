#include <LovyanGFX.hpp>

// ====================================================================
//  DIJI-NES  屏幕配置 —— ST7735 128x160 面板，横屏使用 (160x128)
//
//  panel_width/height 填物理分辨率 128x160；
//  main.cpp 里 tft.setRotation(1) 旋转为横屏 160x128。
//
//  引脚沿用原项目定义，如你的接线不同请修改 pin_sclk/mosi/dc/cs/rst。
// ====================================================================

class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ST7735     _panel_instance;   // ST7735

  lgfx::Bus_SPI          _bus_instance;     // SPI 总线

public:
  LGFX(void)
  {
    { // 总线配置
      auto cfg = _bus_instance.config();
      cfg.spi_host    = SPI3_HOST;
      cfg.spi_mode    = 0;
      cfg.freq_write  = 40000000;   // ST7735 建议 40MHz；花屏可降到 27000000
      cfg.freq_read   = 6000000;
      cfg.spi_3wire   = true;
      cfg.use_lock    = false;
      cfg.dma_channel = SPI_DMA_CH_AUTO;
      cfg.pin_sclk    = 14;
      cfg.pin_mosi    = 13;
      cfg.pin_miso    = -1;
      cfg.pin_dc      = 11;
      _bus_instance.config(cfg);
      _panel_instance.setBus(&_bus_instance);
    }

    { // 面板配置（物理分辨率 128x160）
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = 10;
      cfg.pin_rst          = 12;
      cfg.pin_busy         = -1;

      cfg.panel_width      = 128;   // 物理宽
      cfg.panel_height     = 160;   // 物理高
      cfg.offset_x         = 0;     // 画面偏移/被裁切时调：绿Tab 0,0 | 红Tab 2,1
      cfg.offset_y         = 0;
      cfg.offset_rotation  = 0;     // 横屏错位时可试 1~7

      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = true;
      cfg.invert           = true;  // 颜色发白/反色时切换 true/false
      cfg.rgb_order        = false; // 颜色红蓝对调时改为 true
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = false;
      _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance);
  }
};