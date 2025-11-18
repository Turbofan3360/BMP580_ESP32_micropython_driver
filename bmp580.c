#include "bmp580.h"

mp_obj_t bmp580_make_new(size_t n_args, const mp_obj_t *pos_args, mp_map_t *kw_args){
	/**
	 * This function initialises a new driver instance. It creates an 
     * It then calls barometer_setup to configure the BMP580's config registers as required.
	*/
	uint8_t scl_pin, sda_pin, i2c_address;
	int8_t port;
	i2c_port_num_t i2c_port;
	i2c_master_bus_handle_t bus_handle;
	i2c_master_dev_handle_t device_handle;
	esp_err_t err;

	// Setting default values for I2C port/address. Can be modified as keyword arguments
	static const mp_arg_t allowed_args[] = {
		{ MP_QSTR_scl, MP_ARG_REQUIRED | MP_ARG_INT },
		{ MP_QSTR_sda, MP_ARG_REQUIRED | MP_ARG_INT },
		{ MP_QSTR_port, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = DEFAULT_I2C_PORT_NUM} },
		{ MP_QSTR_address, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = DEFAULT_I2C_ADDR} },
	};

	// Checking arguments
	mp_arg_check_num(n_args, n_kw, 2, 4, true);
	mp_arg_val_t parsed_args[MP_ARRAY_SIZE(allowed_args)];
	mp_arg_parse_all(n_args, pos_args, kw_args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed_args);

	// Extracting arguments
	scl_pin = parsed_args[0].u_int;
	sda_pin = parsed_args[1].u_int;
	port = parsed_args[2].u_int;
	i2c_address = parsed_args[3].u_int;

	// Selecting correct I2C address
	if (i2c_address == 0){
		i2c_address = BMP580_I2C_ADDRESS_0;
	}
	else if (i2c_address == 1){
		i2c_address = BMP580_I2C_ADDRESS_1;
	}
	else {
		mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("Invalid I2C address parameter: Input should be 0 for 0x46 or 1 for 0x47"));
	}

	// Ensuring I2C port number and pin numbers are valid - configured for ESP32-S3
	if ((scl_pin > 45) || (sda_pin > 45)){
		mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("Only 45 pins on ESP32-S3: Please enter valid pin number"));
	}

	if ((port < -1) || (port > 1)){
		mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("Invalid I2C port number"));
	}

	// If port is not set to autoselect:
	if (port != -1){
		// Trying to pull handle for the bus (if it's already been initialized)
		err = i2c_master_get_bus_handle(port, &bus_handle);
	}

	// If there's no already initialized bus handle or port is set to autoselect, then create a bus:
	if ((err == ESP_ERR_INVALID_STATE) || (port == -1)){
		// Setting I2C port value
		if (port == -1){
			i2c_port = -1;
		}
		else if (port == 0){
			i2c_port = I2C_NUM_0;
		}
		else if (port == 1){
			i2c_port = I2C_NUM_1;
		}

		// Configuring ESP-IDF I2C bus object
		i2c_master_bus_config_t i2c_mst_config = {
			.clk_source = I2C_CLK_SRC_DEFAULT,
			.i2c_port = i2c_port,
			.scl_io_num = scl_pin,
			.sda_io_num = sda_pin,
			.glitch_ignore_cnt = 7,
			.flags.enable_internal_pullup = true,
		};

		// Creating the bus
		err = i2c_new_master_bus(&i2c_mst_config, &bus_handle);

		if (err != ESP_OK){
			mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Error initialising I2C bus"));
		}
	}

	// Adding the BMP580 slave device to the bus
	i2c_device_config_t dev_cfg = {
		.dev_addr_length = I2C_ADDR_BIT_LEN_7,
		.device_address = i2c_address,
		.scl_speed_hz = 400000,
	};

	// Installing this to the bus
	err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &device_handle);

	if (err != ESP_OK){
		mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Error adding device to I2C bus"));
	}

	// Creating and allocating memory to the "self" instance of this module
	bmp580_obj_t *self = m_new_obj(bmp580_obj_t);

	// Initialising the required data in the "self" object
    self->base.type = &bmp580_type;
    self->bus_handle = bus_handle;
    self->device_handle = device_handle;
    self->i2c_address = i2c_address;

    // Setting up the barometer. Barometer takes 2ms to initialise after power-on
    mp_hal_delay_ms(2);
    barometer_setup(self);

	return MP_OBJ_FROM_PTR(self);
}

static void barometer_setup(bmp580_obj_t* self){
	/**
	 * Configures the BMP580 to the required settings:
	 * Normal power mode, 140Hz ODR, deep standby mode disabled, pressure measurements enabled, OSR_P x8, OSR_T x2, IIR coefficient = 7, FIFO in streaming mode with 0 decimation and 15 pressure + temperature output frames
	*/
	esp_err_t err;

	// Probing to check there's actually a device there
	err = i2c_master_probe(self->bus_handle, self->i2c_address, 100)

	if (err != ESP_OK){
		mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("BMP580 device not found on I2C bus"));
	}

	// Configuring the BMP580 into normal power mode, ODR of 140Hz, disabled deep standby mode
	const uint8_t write_data[2] = {BMP580_ODR_PWR_CONFIG, 0x99};
	err = i2c_master_transmit(self->device_handle, write_data, 2, 100);

	if (err != ESP_OK){
		mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Unable to write to sensor configuration registers"))
	}

	// Enabling pressure measurements, configuring OSR to x8 for pressure, x2 for temperature
	write_data[0] = BMP580_OSR_CONFIG;
	write_data[1] = 0x59;
	err = i2c_master_transmit(self->device_handle, write_data, 2, 100);

	if (err != ESP_OK){
		mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Unable to write to sensor configuration registers"))
	}

	// Configuring the IIR filter for pressure/temperature - set coefficient to 7 for both of them
	write_data[0] = BMP580_DSP_IIR_CONFIG;
	write_data[1] = 0x1B;
	err = i2c_master_transmit(self->device_handle, write_data, 2, 100);

	if (err != ESP_OK){
		mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Unable to write to sensor configuration registers"))
	}

	// Configuring the FIFO buffer - set FIFO to streaming mode, threshold set to 31 frames (15 given that both pressure and temperature will be output)
	write_data[0] = BMP580_FIFO_CONFIG;
	write_data[1] = 0x3F;
	err = i2c_master_transmit(self->device_handle, write_data, 2, 100);

	if (err != ESP_OK){
		mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Unable to write to sensor configuration registers"))
	}

	// Configuring the FIFO buffer - 0 decimation, pressure and temperature data enabled
	write_data[0] = BMP580_FIFO_SEL_CONFIG;
	write_data[1] = 0x03;
	err = i2c_master_transmit(self->device_handle, write_data, 2, 100);

	if (err != ESP_OK){
		mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("Unable to write to sensor configuration registers"))
	}
}

mp_obj_t get_press_temp(mp_obj_t* self){}
static MP_DEFINE_CONST_FUN_OBJ_1(bmp580_get_press_temp, get_press_temp);

mp_obj_t get_press_temp_alt(mp_obj_t* self){}
static MP_DEFINE_CONST_FUN_OBJ_1(bmp580_get_press_temp_alt, get_press_temp_alt);

/**
 * Code here exposes the module functions above to micropython as an object
*/

// Defining the functions that are exposed to micropython
static const mp_rom_map_elem_t bmp580_locals_dict_table[] = {
	{MP_ROM_QSTR(MP_QSTR_get_press_temp), MP_ROM_PTR(&bmp580_get_press_temp)},
	{MP_ROM_QSTR(MP_QSTR_get_press_temp_alt), MP_ROM_PTR(&bmp580_get_press_temp_alt)},
};
static MP_DEFINE_CONST_DICT(bmp580_locals_dict, bmp580_locals_dict_table);

// Overall module definition
MP_DEFINE_CONST_OBJ_TYPE(
    bmp580_type,
    MP_QSTR_bmp580,
    MP_TYPE_FLAG_NONE,
    make_new, bmp580_make_new,
    locals_dict, &bmp580_locals_dict
);

// Defining global constants
static const mp_rom_map_elem_t bmp580_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__) , MP_ROM_QSTR(MP_QSTR_bmp580) },
    { MP_ROM_QSTR(MP_QSTR_BMP580), MP_ROM_PTR(&bmp580_type) },
};
static MP_DEFINE_CONST_DICT(bmp580_globals_table, bmp580_module_globals_table);

// Creating module object
const mp_obj_module_t bmp580_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&bmp580_globals_table,
};

MP_REGISTER_MODULE(MP_QSTR_bmp580, bmp580_module);