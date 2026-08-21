// lgfx_conf.h
#ifndef LGFX_CONF_H
#define LGFX_CONF_H

#include <LovyanGFX.hpp>

class LGFX : public lgfx::LGFX_Device
{
  lgfx::Panel_ST7735S   _panel_instance;   // 明确用 ST7735S
  lgfx::Bus_SPI         _bus_instance;

public:
  LGFX(void)
  {
    { // SPI 总线配置
      auto cfg = _bus_instance.config();
      cfg.spi_host    = SPI3_HOST;
      cfg.spi_mode    = 0;
      cfg.freq_write  = 20000000;          // ← 关键：20MHz 更稳定
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

    { // ST7735S 面板参数
      auto cfg = _panel_instance.config();
      cfg.pin_cs           = 10;
      cfg.pin_rst          = 12;
      cfg.pin_busy         = -1;

      cfg.panel_width      = 128;
      cfg.panel_height     = 160;
      // ════════════════════════════════════════════════════
      // 以下三个参数是白屏调试关键，按需调整（见下文说明）
      cfg.offset_x         = 0;    // 常见值：0 或 2
      cfg.offset_y         = 0;    // 常见值：0 或 1
      cfg.invert           = false; // 常见值：false 或 true
      // ════════════════════════════════════════════════════
      cfg.rgb_order        = false; // 颜色异常时改 true
      cfg.offset_rotation  = 0;
      cfg.dummy_read_pixel = 8;
      cfg.dummy_read_bits  = 1;
      cfg.readable         = true;
      cfg.dlen_16bit       = false;
      cfg.bus_shared       = false;
      _panel_instance.config(cfg);
    }

    setPanel(&_panel_instance);
  }
};

#endif