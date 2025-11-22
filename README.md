# ESP32 Micropython BMP580 Barometer Driver #

### The Code: ###

This code is a driver for the Bosch Sensortec BMP580 barometric pressure/temperature sensor. This sensor is a significant upgrade over the older BMP180/280 sensors. This code should, in theory, work with other sensors in the BMP58x line (potentially needing minimal modifications), but it hasn't been tested with any sensors other than the BMP580.

There are two potential methods you can call: get_press_temp() or get_press_temp_alt()

get_press_temp() returns the array [pressure, temperature], with pressure being in hPa (equivalent to mbar) and temperature being in degrees celcius. Both values are floats.
get_press_temp_alt)() returns the array [pressure, temperature, altitude]. This is the same as get_press_temp(), with the addition of the altitude calculation. This uses the standard barometric formula, and returns an altitude in meters (again a float value) from the location where the sensor driver was initialised. This altitude can vary over time with weather patterns changing the local pressure, but this isn't something you need to worry about over timeframes of less than a few hours. 

This module is designed to be compiled into the micropython firmware for your ESP32 board, so you have the .c, .h, and .cmake files to be able to do this. 

### Embedded C Module Example Usage: ###

```python3
import bmp580

scl_pin = 1
sda_pin = 2
# Port can be either 0 or 1, or set to -1 to automatically choose an I2C port. Default value is -1
port = 0
# Set address to 0 for 0x46, or to 1 for 0x47 (the BMP580 I2C address changes depending on whether the SDO pin is pulled high/low). Default address is 0x46
address = 1

sensor = bmp580.BMP580(scl_pin, sda_pin, i2c_port=port, address=address)

sensor.get_press_temp()
sensor.get_press_temp_alt()
```
SCL/SDA pins are required, i2c_port and address are optional parameters.

### Compiling the module into firmware: ###

To do this, you will need:
 - ESP-IDF cloned from github
 - Micropython cloned from github

1. Enter your esp-idf directory, and run ./install.sh (only needs to be run the first time)
2. Enter your esp-idf directory and run . ./export.sh (needs to be run every new terminal session)
3. Download the files from this repository
4. Enter your directory ~/micropython/ports/esp32
5. Run the make command, specifying USER_C_MODULES=/path/to/BMP580/micropython.cmake (replace with your file path)

For me, with an ESP32-S3 that has octal SPIRAM, the full make command is:
```
make BOARD=ESP32_GENERIC_S3 BOARD_VARIANT=SPIRAM_OCT USER_C_MODULES=/path/to/BMP580/micropython.cmake
```

### Module Configuration Settings: ###

The BMP580 chip is configured to the following settings:
 - Normal power mode
 - 140Hz ODR
 - Deep standby mode disabled
 - Pressure measurements enabled
 - OSR_P x8, OSR_T x2
 - IIR coefficient = 7
 - FIFO in streaming mode with 0 decimation and 15 pressure + temperature output frames

### References: ###

 - Datasheet: <https://cdn-shop.adafruit.com/product-files/6411/BMP580.pdf>