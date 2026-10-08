# OLED System Monitor

A portrait system monitor on a 2.42" OLED, driven by an Arduino Nano. Temperatures sit on the top half. Fans sit on the bottom, and the blade with the dot spins faster as the percent goes up. At 0% it stays put.

The numbers on the panel right now are dummies in `nano-oled/src/main.cpp`. A Linux companion app is coming soon. That will feed real GPU, CPU, and system temperatures, plus fan speed, over the serial port.

![Portrait OLED showing GPU, CPU, and SYS temps above three fan readouts](images/oled-monitor.jpg)

## Parts

* [2.42" 128x64 SSD1309 OLED](https://s.click.aliexpress.com/e/_c3g9KWrj). Yellow on the temperature half, blue-green on the fans. I2C on A4/A5.
* [Arduino Nano](https://s.click.aliexpress.com/e/_c2RH7xt7). A CH340 clone is fine. This firmware expects the ATmega328P new bootloader.

You also want a short run of jumper wire. Seven connections.

## Wiring

These boards ship set up for SPI. The silkscreen says `SPI:R4` and `IIC:R3,R5`. For I2C, move the resistor on R4 over to R3, and fit a 0 ohm resistor on R5. CS has to sit on ground or the glass stays blank. Tie the CS pin to GND. You do not need to solder R18.

RES is not tied to VCC. It goes to D8 so the sketch can pulse it. Leave it on the supply and the panel stays black.

| OLED | Nano |
| --- | --- |
| VCC | 5V |
| GND | GND |
| SDA | A4 |
| SCL | A5 |
| CS | GND |
| DC | GND |
| RES | D8 |

DC to ground selects I2C address `0x3C`.

## Build

[PlatformIO](https://platformio.org/). From `nano-oled/`:

```bash
pio run -t upload
```

The board target is `nanoatmega328new`. If upload fails with `not in sync` and your clone is an older one, switch the board in `platformio.ini` to `nanoatmega328`.

## What's on the screen

`gpuTemp`, `cpuTemp`, and `sysTemp` are degrees. `gpuFan`, `cpuFan`, and `sysFan` are percents. A stopped fan is `0`. Anything from a crawl up to full speed is driven off that percent, with the fast end capped so the three identical blades don't look like they're turning backwards.
