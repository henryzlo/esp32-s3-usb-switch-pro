#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#include "driver/uart.h"
#include "logger.hpp"
#include "switch_pro.hpp"
#include "usb_device.hpp"

using namespace std::chrono_literals;

// Host (Orange Pi / PC) talks on the board's COM UART at 19200 baud.
// Wire format matches Liminal's controller packets: handshake bytes, then
// 8 payload bytes + CRC-8. USB port still presents a Switch Pro HID device.
static constexpr uart_port_t kHostUart = UART_NUM_0;
static constexpr int kHostBaud = 19200;
static constexpr int kHostRxBuf = 1024;
static constexpr uint8_t kHatCenter = 0x08;

static uint8_t crc8(const uint8_t *data, size_t len) {
  uint8_t crc = 0;
  for (size_t i = 0; i < len; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc & 0x80) ? static_cast<uint8_t>(((crc << 1) ^ 0x07) & 0xFF)
                         : static_cast<uint8_t>((crc << 1) & 0xFF);
  }
  return crc;
}

static float stick_axis(uint8_t raw) {
  // Liminal sends 1..255 with 128 centered, y positive down (screen coords).
  return (static_cast<int>(raw) - 128) / 127.0f;
}

static void apply_packet(espp::SwitchPro &controller, const uint8_t pkt[8]) {
  const uint16_t buttons = (static_cast<uint16_t>(pkt[0]) << 8) | pkt[1];
  const uint8_t hat = pkt[2];
  using InputReport = espp::SwitchPro::InputReport;
  controller.update_input_report([&](InputReport &r) {
    r.reset();
    r.set_button_y(buttons & 0x0001);
    r.set_button_b(buttons & 0x0002);
    r.set_button_a(buttons & 0x0004);
    r.set_button_x(buttons & 0x0008);
    r.set_button_l(buttons & 0x0010);
    r.set_button_r(buttons & 0x0020);
    r.set_button_zl(buttons & 0x0040);
    r.set_button_zr(buttons & 0x0080);
    r.set_button_minus(buttons & 0x0100);
    r.set_button_plus(buttons & 0x0200);
    r.set_button_thumb_l(buttons & 0x0400);
    r.set_button_thumb_r(buttons & 0x0800);
    r.set_button_home(buttons & 0x1000);
    r.set_button_capture(buttons & 0x2000);
    switch (hat) {
    case 0:
      r.set_dpad(true, false, false, false);
      break;
    case 1:
      r.set_dpad(true, false, false, true);
      break;
    case 2:
      r.set_dpad(false, false, false, true);
      break;
    case 3:
      r.set_dpad(false, true, false, true);
      break;
    case 4:
      r.set_dpad(false, true, false, false);
      break;
    case 5:
      r.set_dpad(false, true, true, false);
      break;
    case 6:
      r.set_dpad(false, false, true, false);
      break;
    case 7:
      r.set_dpad(true, false, true, false);
      break;
    default: // centered / unknown
      r.set_dpad(false, false, false, false);
      break;
    }
    r.set_left_joystick(stick_axis(pkt[3]), stick_axis(pkt[4]));
    r.set_right_joystick(stick_axis(pkt[5]), stick_axis(pkt[6]));
    (void)pkt[7]; // unused
    (void)kHatCenter;
  });
}

static void host_uart_init() {
  uart_config_t cfg = {
      .baud_rate = kHostBaud,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      .rx_flow_ctrl_thresh = 0,
      .source_clk = UART_SCLK_DEFAULT,
      .flags = {},
  };
  ESP_ERROR_CHECK(uart_driver_install(kHostUart, kHostRxBuf, 0, 0, nullptr, 0));
  ESP_ERROR_CHECK(uart_param_config(kHostUart, &cfg));
  // UART0 pins are the board's COM USB-UART bridge; leave default pin routing.
}

//! [switch_pro example]
extern "C" void app_main(void) {
  espp::Logger logger({.tag = "SwitchProExample", .level = espp::Logger::Verbosity::INFO});
  logger.info("Starting Nintendo Switch Pro USB controller + COM host protocol");

  espp::SwitchPro controller({.log_level = espp::Logger::Verbosity::WARN});

  struct OutReport {
    uint8_t id;
    std::vector<uint8_t> data;
  };
  std::deque<OutReport> tx_queue;
  std::mutex tx_mutex;
  std::condition_variable tx_cv;
  auto enqueue = [&](espp::SwitchPro::ReportData rd) {
    {
      std::lock_guard<std::mutex> lock(tx_mutex);
      if (tx_queue.size() < 16)
        tx_queue.push_back({rd.first, std::move(rd.second)});
    }
    tx_cv.notify_one();
  };

  espp::UsbDevice::Config usb_cfg;
  usb_cfg.vid = espp::SwitchPro::vid;
  usb_cfg.pid = espp::SwitchPro::pid;
  usb_cfg.manufacturer = espp::SwitchPro::manufacturer_name;
  usb_cfg.product = espp::SwitchPro::product_name;
  usb_cfg.bcd_device = espp::SwitchPro::bcd_device;
  usb_cfg.max_power_ma = espp::SwitchPro::max_power_ma;
  usb_cfg.remote_wakeup = espp::SwitchPro::remote_wakeup;
  usb_cfg.log_level = espp::Logger::Verbosity::WARN;

  espp::UsbDevice::HidFunction hid;
  hid.interface_name = "Switch Pro Controller";
  hid.report_descriptor = controller.get_report_descriptor();
  hid.has_out_endpoint = true;
  hid.poll_interval_ms = 8;
  hid.on_receive = [&](std::span<const uint8_t> data) {
    if (data.empty())
      return;
    if (auto reply = controller.on_hid_report(data[0], data.data(), data.size()))
      enqueue(std::move(*reply));
  };
  usb_cfg.hid = hid;

  espp::UsbDevice usb(usb_cfg);
  usb.set_mount_callback([&]() {
    if (auto init = controller.on_attach())
      enqueue(std::move(*init));
  });

  // COM host protocol must run even when the USB port has no host yet (Switch
  // still off / cable only on COM). Start UART before USB init so Liminal can
  // handshake as soon as the Orange Pi sees /dev/ttyACM*.
  host_uart_init();
  logger.info("Host commands on COM @ {} baud", kHostBaud);

  std::error_code ec;
  bool usb_ok = usb.initialize(ec);
  if (!usb_ok) {
    logger.error("USB device init failed ({}). COM host protocol still running.",
                 ec.message());
  } else {
    logger.info("USB HID ready on USB port");
  }

  std::atomic<bool> running{true};

  std::thread sender([&]() {
    while (running.load()) {
      OutReport rep;
      bool have = false;
      {
        std::unique_lock<std::mutex> lock(tx_mutex);
        tx_cv.wait_for(lock, 15ms, [&]() { return !tx_queue.empty() || !running.load(); });
        if (!tx_queue.empty()) {
          rep = std::move(tx_queue.front());
          tx_queue.pop_front();
          have = true;
        }
      }
      if (!usb_ok) {
        std::this_thread::sleep_for(20ms);
        continue;
      }
      if (have) {
        std::error_code send_ec;
        for (int i = 0; i < 20 && !usb.write_hid_report(rep.id, rep.data, send_ec); ++i)
          std::this_thread::sleep_for(1ms);
      } else if (controller.is_ready()) {
        auto std_report = controller.get_input_report();
        if (!std_report.empty()) {
          std::error_code send_ec;
          usb.write_hid_report(controller.input_report_id(), std_report, send_ec);
        }
      }
    }
  });

  // Host COM protocol: nine 0xFF arm sync, then handshake, then 9-byte packets.
  enum class HostState { NeedFlush, WantFf, Want44, WantEe, Packet };
  HostState state = HostState::NeedFlush;
  uint8_t packet[9];
  size_t packet_filled = 0;
  int ff_run = 0;

  while (true) {
    uint8_t byte = 0;
    int n = uart_read_bytes(kHostUart, &byte, 1, pdMS_TO_TICKS(50));
    if (n != 1)
      continue;

    if (byte == 0xFF) {
      if (++ff_run >= 9) {
        state = HostState::WantFf;
        packet_filled = 0;
        ff_run = 0;
        continue; // flush byte is not the handshake 0xFF that follows after the host clears RX
      }
    } else {
      ff_run = 0;
    }

    // Ignore traffic until the host has flushed with nine 0xFF (Liminal clears RX after).
    if (state == HostState::NeedFlush)
      continue;

    switch (state) {
    case HostState::WantFf:
      if (byte == 0xFF) {
        uart_write_bytes(kHostUart, "\xFF", 1);
        state = HostState::Want44;
      }
      break;
    case HostState::Want44:
      if (byte == 0x44) {
        uart_write_bytes(kHostUart, "\xEE", 1);
        state = HostState::WantEe;
      } else if (byte != 0xFF) {
        state = HostState::WantFf;
      }
      break;
    case HostState::WantEe:
      if (byte == 0xEE) {
        uart_write_bytes(kHostUart, "\x03", 1);
        state = HostState::Packet;
        packet_filled = 0;
        logger.info("Host COM handshake complete");
      } else if (byte != 0xFF) {
        state = HostState::WantFf;
      }
      break;
    case HostState::Packet:
      packet[packet_filled++] = byte;
      if (packet_filled == 9) {
        packet_filled = 0;
        if (crc8(packet, 8) == packet[8])
          apply_packet(controller, packet);
        else
          logger.warn("dropping host packet with bad CRC");
      }
      break;
    default:
      break;
    }
  }

  running.store(false);
  tx_cv.notify_all();
  sender.join();
}
//! [switch_pro example]
