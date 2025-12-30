#include "bmp580.h"

mp_obj_t bmp580_make_new(const mp_obj_type_t *type, size_t n_args, size_t n_kw, const mp_obj_t *args){
	/**
	 * This function initialises a new driver instance. It creates an I2C bus, and adds the barometer to it
     * It then calls barometer_setup to configure the BMP580's config registers as required.
	*/
	gpio_num_t scl_pin, sda_pin;
	uint8_t i2c_address;
	int8_t port;
	i2c_port_num_t i2c_port;
	i2c_master_bus_handle_t bus_handle;
	i2c_master_dev_handle_t device_handle;
	esp_err_t err = ESP_ERR_INVALID_STATE;

	// Defining the allowed arguments, and setting default values for I2C port/address. Can be modified as keyword arguments
	static const mp_arg_t allowed_args[] = {
		{ MP_QSTR_scl, MP_ARG_REQUIRED | MP_ARG_INT },
		{ MP_QSTR_sda, MP_ARG_REQUIRED | MP_ARG_INT },
		{ MP_QSTR_i2c_port, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = DEFAULT_I2C_PORT_NUM} },
		{ MP_QSTR_address, MP_ARG_KW_ONLY | MP_ARG_INT, {.u_int = DEFAULT_I2C_ADDR} },
	};

	// Checking arguments
	mp_arg_val_t parsed_args[MP_ARRAY_SIZE(allowed_args)];
    mp_arg_parse_all_kw_array(n_args, n_kw, args, MP_ARRAY_SIZE(allowed_args), allowed_args, parsed_args);

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

	// Ensuring I2C port number and pin numbers are valid
	if (!GPIO_IS_VALID_OUTPUT_GPIO(scl_pin) || !GPIO_IS_VALID_OUTPUT_GPIO(sda_pin)){
		mp_raise_msg(&mp_type_ValueError, MP_ERROR_TEXT("Invalid SCL or SDA pin number"));
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
	if (err == ESP_ERR_INVALID_STATE){
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
			mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("Error initialising I2C bus: %s"), esp_err_to_name(err));
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
		mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("Error adding device to I2C bus: %s"), esp_err_to_name(err));
	}

	// Creating and allocating memory to the "self" instance of this module
	bmp580_obj_t *self = m_new_obj(bmp580_obj_t);

	// Initialising the required data in the "self" object
    self->base.type = &bmp580_type;
    self->bus_handle = bus_handle;
    self->device_handle = device_handle;
    self->i2c_address = i2c_address;

    // Setting up the barometer. Barometer takes 2ms to initialise after power-on, so it delays for that long to be 100% sure of no issues before configuring the sensor
    wait_micro_s(2000);
    barometer_setup(self);
	log_func("Sensor configured\n");

    // Reading initial pressure reading to save to self struct
    float data[2];
    read_bmp580_data(self, data);

    self->initial_pressure = data[0];
    self->initial_temperature = data[1]+273.15f;

	return MP_OBJ_FROM_PTR(self);
}

static void barometer_setup(bmp580_obj_t* self){
	/**
	 * Configures the BMP580 to the required settings:
	 * Normal power mode, 140Hz ODR, deep standby mode disabled, pressure measurements enabled, OSR_P x8, OSR_T x2, IIR coefficient = 7, FIFO in streaming mode with 0 decimation and 15 pressure + temperature output frames
	*/
	uint8_t write_data[2];
	esp_err_t err;

	// Probing to check there's actually a device there
	err = i2c_master_probe(self->bus_handle, self->i2c_address, 100);

	if (err != ESP_OK){
		mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("BMP580 device not found on I2C bus: %s"), esp_err_to_name(err));
	}

	// Configuring the BMP580 into normal power mode, ODR of 140Hz, disabled deep standby mode
	write_data[0] = BMP580_ODR_PWR_CONFIG;
	write_data[1] = 0x99;
	err = i2c_master_transmit(self->device_handle, write_data, 2, 100);

	if (err != ESP_OK){
		mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("Unable to write to sensor configuration registers: %s"), esp_err_to_name(err));
	}

	// Enabling pressure measurements, configuring OSR to x8 for pressure, x2 for temperature
	write_data[0] = BMP580_OSR_CONFIG;
	write_data[1] = 0x59;
	err = i2c_master_transmit(self->device_handle, write_data, 2, 100);

	if (err != ESP_OK){
		mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("Unable to write to sensor configuration registers: %s"), esp_err_to_name(err));
	}

	// Configuring the IIR filter for pressure/temperature - set coefficient to 7 for both of them
	write_data[0] = BMP580_DSP_IIR_CONFIG;
	write_data[1] = 0x1B;
	err = i2c_master_transmit(self->device_handle, write_data, 2, 100);

	if (err != ESP_OK){
		mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("Unable to write to sensor configuration registers: %s"), esp_err_to_name(err));
	}

	// Configuring the FIFO buffer - set FIFO to streaming mode, threshold set to 31 frames (15 given that both pressure and temperature will be output)
	write_data[0] = BMP580_FIFO_CONFIG;
	write_data[1] = 0x3F;
	err = i2c_master_transmit(self->device_handle, write_data, 2, 100);

	if (err != ESP_OK){
		mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("Unable to write to sensor configuration registers: %s"), esp_err_to_name(err));
	}

	// Configuring the FIFO buffer - 0 decimation, pressure and temperature data enabled
	write_data[0] = BMP580_FIFO_SEL_CONFIG;
	write_data[1] = 0x03;
	err = i2c_master_transmit(self->device_handle, write_data, 2, 100);

	if (err != ESP_OK){
		mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("Unable to write to sensor configuration registers: %s"), esp_err_to_name(err));
	}
}

static void wait_micro_s(uint32_t micro_s_delay){
	/**
	 * Function to delay by a certain number of microseconds
	*/
	uint64_t start = esp_timer_get_time();

	while (esp_timer_get_time() - start < micro_s_delay){}

	return;
}

static float* read_bmp580_data(bmp580_obj_t* self, float* output){
	/**
	 * Internal driver function to get and process data from the BMP580
	*/
    uint8_t write_data[1], read_data[6], attempts = 0;
	int32_t data;
	esp_err_t err;

	write_data[0] = BMP580_NUM_FIFO_FRAMES;

	// Checking to see if there's any data frames in the FIFO
    while (attempts < MAX_FIFO_ATTEMPTS){
        attempts ++;

		// Reading the register with the number of FIFO frames
		err = i2c_master_transmit_receive(self->device_handle, write_data, 1, read_data, 1, 10);

		if (err != ESP_OK){
			mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("Unable to read BMP580 register: %s"), esp_err_to_name(err));
		}

		// Checking the number of frames
		if (read_data[0] != 0){
			break;
		}
		// Checking the timeout condition - this function will time out after 1 second
        if (attempts == MAX_FIFO_ATTEMPTS){
            mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("No BMP580 data available in the FIFO buffer: %s"), esp_err_to_name(err));
		}

		// 0.5ms delay
		wait_micro_s(500);
	}

	// Reading a data frame from the FIFO
	write_data[0] = BMP580_FIFO_OUT;
	err = i2c_master_transmit_receive(self->device_handle, write_data, 1, read_data, 6, 10);

	if (err != ESP_OK){
		mp_raise_msg_varg(&mp_type_RuntimeError, MP_ERROR_TEXT("Error reading data from BMP580 FIFO: %s"), esp_err_to_name(err));
	}

	// Processing the data
	// Pressure:
	data = (read_data[5]<<16) | (read_data[4]<<8) | read_data[3];
	output[0] = data/6400.0f;
	// Temperature:
	data = (read_data[2]<<16) | (read_data[1]<<8) | read_data[0];

	// 32-bit sign extension for temperature:
	data = (data ^ 0x800000) - 0x800000;

	output[1] = data/65536.0f;

	return output;
}

static void log_func(const char *log_string){
    /**
     * Basic logging function - currently just prints to the REPL, but can be adapted to log to other places (e.g. log to a file) if needed
    */
    mp_printf(&mp_plat_print, "%s", log_string);
}

mp_obj_t get_press_temp(mp_obj_t self_in){
	/**
	 * Micropython-exposed function to return pressure and temperature data from the BMP580
	*/
	float data[2];
	mp_obj_t retvals[2];

	bmp580_obj_t *self = MP_OBJ_TO_PTR(self_in);

	// Getting the data
	read_bmp580_data(self, data);

	// Converting the integer pressure/temperature values into float values in degrees celcius and hPa
	retvals[0] = mp_obj_new_float(data[0]);
	retvals[1] = mp_obj_new_float(data[1]);

	return mp_obj_new_list(2, retvals);
}
static MP_DEFINE_CONST_FUN_OBJ_1(bmp580_get_press_temp, get_press_temp);

mp_obj_t get_press_temp_alt(mp_obj_t self_in){
	/**
	 * Micropython-exposed function to return pressure and temperature data from the BMP580 and convert that into an altitude reading
	 * Altitude measured from the location at which the driver was initialised
	*/
	float data[2];
	float alt;
	mp_obj_t retvals[3];

	bmp580_obj_t *self = MP_OBJ_TO_PTR(self_in);

	// Getting the data
	read_bmp580_data(self, data);

	alt = (self->initial_temperature/0.0065f)*(1.0f-powf(data[0]/self->initial_pressure, BAROMETRIC_EQ_COEFFICIENT));

	retvals[0] = mp_obj_new_float(data[0]);
	retvals[1] = mp_obj_new_float(data[1]);
	retvals[2] = mp_obj_new_float(alt);

	return mp_obj_new_list(3, retvals);
}
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
