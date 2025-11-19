#ifndef BMP580_H
#define BMP580_H

#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#include "py/runtime.h"
#include "py/obj.h"
#include "py/objstr.h"
#include "py/mphal.h"

#include "driver/i2c_master.h"
#include "driver/gpio.h"

// Register address definitions
#define BMP580_ODR_PWR_CONFIG 0x37
#define BMP580_OSR_CONFIG 0x36
#define BMP580_DSP_IIR_CONFIG 0x31
#define BMP580_FIFO_CONFIG 0x16
#define BMP580_FIFO_SEL_CONFIG 0x18
#define BMP580_FIFO_OUT 0x29
#define BMP580_NUM_FIFO_FRAMES 0x17

// Constant definitions
#define BMP580_I2C_ADDRESS_0 0x46
#define BMP580_I2C_ADDRESS_1 0x47
#define DEFAULT_I2C_PORT_NUM -1
#define DEFAULT_I2C_ADDR 0
#define BAROMETRIC_EQ_COEFFICIENT ((8.314*0.0065)/(9.80665*0.028964)) // Coefficient required for barometric equation: Rg*L/gM

// Object definition
typedef struct {
	mp_obj_base_t base;

	uint8_t i2c_address;
	i2c_master_bus_handle_t bus_handle;
	i2c_master_dev_handle_t device_handle;

	float initial_pressure;
	float initial_temperature;
}bmp580_obj_t;

// Function definitions
static void barometer_setup(bmp580_obj_t* self);
static int32_t* read_bmp580_data(bmp580_obj_t* self, int32_t* output);
static void log_func(const char *log_string);

extern const mp_obj_type_t bmp580_type;

#endif