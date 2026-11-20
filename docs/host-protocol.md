# Host COM protocol

Liminal (on the Orange Pi) drives this firmware over the board’s **COM** serial port.
The **USB** port stays the Switch Pro HID link.

## Ports

| Port on board | Connects to | Role |
|---|---|---|
| **COM** | Orange Pi USB | Host commands @ 19200 8N1 |
| **USB** | Switch dock | Nintendo Pro Controller HID |

## Handshake

After nine `0xFF` bytes (to clear a partial packet), the host sends and the firmware answers:

| Host sends | Firmware answers |
|---|---|
| `0xFF` | `0xFF` |
| `0x44` | `0xEE` |
| `0xEE` | `0x03` |

Then every press/hold is a **9-byte** packet.

## Packet

```
[buttons_hi, buttons_lo, hat, lx, ly, rx, ry, unused, crc8]
```

- **Buttons** (big-endian mask): Y=0x0001, B=0x0002, A=0x0004, X=0x0008, L=0x0010, R=0x0020,
  ZL=0x0040, ZR=0x0080, MINUS=0x0100, PLUS=0x0200, LCLICK=0x0400, RCLICK=0x0800,
  HOME=0x1000, CAPTURE=0x2000
- **Hat**: 0=UP … 7=UPLEFT, `0x08`=centered
- **Sticks**: 1–255, 128=center; Y positive is down (screen coords)
- **CRC-8**: poly `0x07` over the first 8 bytes

Firmware maps each valid packet onto `espp::SwitchPro::InputReport` (replacing the old A/B/X/Y demo loop).
