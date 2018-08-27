/**
 * \file devices/vdml_adi.c
 *
 * Contains functions for interacting with the V5 ADI.
 *
 * Copyright (c) 2017-2018, Purdue University ACM SIGBots.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>

#include "ifi/v5_api.h"
#include "kapi.h"
#include "vdml/registry.h"
#include "vdml/vdml.h"

#define INTERNAL_ADI_PORT 21

#define ADI_MOTOR_MAX_SPEED 127
#define ADI_MOTOR_MIN_SPEED -128

#define NUM_MAX_TWOWIRE 4

typedef union adi_data {
	struct {
		int32_t calib;
	} analog_data;
	struct {
		bool was_pressed;
	} digital_data;
	struct {
		bool reversed;
	} encoder_data;
	uint8_t raw[4];
} adi_data_s_t;

static int32_t get_analog_calib(uint8_t port) {
	adi_data_s_t data;
	data.raw[0] = registry_get_device(port)->pad[port * 4];
	data.raw[1] = registry_get_device(port)->pad[port * 4 + 1];
	data.raw[2] = registry_get_device(port)->pad[port * 4 + 2];
	data.raw[3] = registry_get_device(port)->pad[port * 4 + 3];
	return data.analog_data.calib;
}

static void set_analog_calib(uint8_t port, int32_t calib) {
	adi_data_s_t data;
	data.analog_data.calib = calib;
	registry_get_device(port)->pad[port * 4] = data.raw[0];
	registry_get_device(port)->pad[port * 4 + 1] = data.raw[1];
	registry_get_device(port)->pad[port * 4 + 2] = data.raw[2];
	registry_get_device(port)->pad[port * 4 + 3] = data.raw[3];
}

static bool get_digital_pressed(uint8_t port) {
	adi_data_s_t data;
	data.raw[0] = registry_get_device(port)->pad[port * 4];
	return data.digital_data.was_pressed;
}

static void set_digital_pressed(uint8_t port, bool val) {
	adi_data_s_t data;
	data.digital_data.was_pressed = val;
	registry_get_device(port)->pad[port * 4] = data.raw[0];
}

static bool get_encoder_reversed(uint8_t port) {
	adi_data_s_t data;
	data.raw[0] = registry_get_device(port)->pad[port * 4];
	return data.encoder_data.reversed;
}

static void set_encoder_reversed(uint8_t port, bool val) {
	adi_data_s_t data;
	data.encoder_data.reversed = val;
	registry_get_device(port)->pad[port * 4] = data.raw[0];
}

#define transform_adi_port(port)       \
	if (port >= 'a' && port <= 'h')      \
		port -= 'a';                       \
	else if (port >= 'A' && port <= 'H') \
		port -= 'A';                       \
	else                                 \
		port--;                            \
	if (port > 7 || port < 0) {          \
		errno = EINVAL;                    \
		return PROS_ERR;                   \
	}

#define validate_type(port, type)                          \
	adi_port_config_e_t config = _adi_port_get_config(port); \
	if (config != type) {                                    \
		return PROS_ERR;                                       \
	}

#define validate_analog(port)                                                                                     \
	adi_port_config_e_t config = _adi_port_get_config(port);                                                        \
	if (config != E_ADI_ANALOG_IN && config != E_ADI_LEGACY_POT && config != E_ADI_LEGACY_LINE_SENSOR &&            \
	    config != E_ADI_LEGACY_LIGHT_SENSOR && config != E_ADI_LEGACY_ACCELEROMETER && config != E_ADI_SMART_POT) { \
		errno = EINVAL;                                                                                               \
		return PROS_ERR;                                                                                              \
	}

#define validate_digital_in(port)                                                                    \
	adi_port_config_e_t config = _adi_port_get_config(port);                                           \
	if (config != E_ADI_DIGITAL_IN && config != E_ADI_LEGACY_BUTTON && config != E_ADI_SMART_BUTTON) { \
		errno = EINVAL;                                                                                  \
		return PROS_ERR;                                                                                 \
	}

#define validate_motor(port)                                        \
	adi_port_config_e_t config = _adi_port_get_config(port);          \
	if (config != E_ADI_LEGACY_PWM && config != E_ADI_LEGACY_SERVO) { \
		errno = EINVAL;                                                 \
		return PROS_ERR;                                                \
	}

#define validate_twowire(port_top, port_bottom) \
	if (abs(port_top - port_bottom) > 1) {        \
		errno = EINVAL;                             \
		return PROS_ERR;                            \
	}                                             \
	int port;                                     \
	if (port_top < port_bottom)                   \
		port = port_top;                            \
	else if (port_bottom < port_top)              \
		port = port_bottom;                         \
	else                                          \
		return PROS_ERR;                            \
	if (port % 2 == 1) {                          \
		return PROS_ERR;                            \
	}

static inline int32_t _adi_port_set_config(uint8_t port, adi_port_config_e_t type) {
	claim_port(INTERNAL_ADI_PORT, E_DEVICE_ADI);
	vexDeviceAdiPortConfigSet(device->device_info, port, type);
	return_port(INTERNAL_ADI_PORT, 1);
}

static inline adi_port_config_e_t _adi_port_get_config(uint8_t port) {
	claim_port(INTERNAL_ADI_PORT, E_DEVICE_ADI);
	adi_port_config_e_t rtn = (adi_port_config_e_t)vexDeviceAdiPortConfigGet(device->device_info, port);
	return_port(INTERNAL_ADI_PORT, rtn);
}

static inline int32_t _adi_port_set_value(uint8_t port, int32_t value) {
	claim_port(INTERNAL_ADI_PORT, E_DEVICE_ADI);
	vexDeviceAdiValueSet(device->device_info, port, value);
	return_port(INTERNAL_ADI_PORT, 1);
}

static inline int32_t _adi_port_get_value(uint8_t port) {
	claim_port(INTERNAL_ADI_PORT, E_DEVICE_ADI);
	int32_t rtn = vexDeviceAdiValueGet(device->device_info, port);
	return_port(INTERNAL_ADI_PORT, rtn);
}

int32_t adi_port_set_config(uint8_t port, adi_port_config_e_t type) {
	transform_adi_port(port);
	return _adi_port_set_config(port, type);
}

adi_port_config_e_t adi_port_get_config(uint8_t port) {
	transform_adi_port(port);
	return _adi_port_get_config(port);
}

int32_t adi_port_set_value(uint8_t port, int32_t value) {
	transform_adi_port(port);
	return _adi_port_set_value(port, value);
}

int32_t adi_port_get_value(uint8_t port) {
	transform_adi_port(port);
	return _adi_port_get_value(port);
}

int32_t adi_analog_calibrate(uint8_t port) {
	transform_adi_port(port);
	validate_analog(port);
	uint32_t total = 0, i;
	for (i = 0; i < 512; i++) {
		total += _adi_port_get_value(port);
		task_delay(1);
	}
	set_analog_calib(port, (int32_t)((total + 16) >> 5));
	return ((int32_t)((total + 256) >> 9));
}

int32_t adi_analog_read(uint8_t port) {
	transform_adi_port(port);
	validate_analog(port);
	return _adi_port_get_value(port);
}

int32_t adi_analog_read_calibrated(uint8_t port) {
	transform_adi_port(port);
	validate_analog(port);
	return (_adi_port_get_value(port) - (get_analog_calib(port) >> 4));
}

int32_t adi_analog_read_calibrated_HR(uint8_t port) {
	transform_adi_port(port);
	validate_analog(port);
	return ((_adi_port_get_value(port) << 4) - get_analog_calib(port));
}

int32_t adi_digital_read(uint8_t port) {
	transform_adi_port(port);
	validate_digital_in(port);
	return _adi_port_get_value(port);
}

int32_t adi_digital_get_new_press(uint8_t port) {
	transform_adi_port(port);
	int32_t pressed = _adi_port_get_value(port);

	if (!pressed)  // buttons is not currently pressed
		set_digital_pressed(port, false);

	if (pressed && !get_digital_pressed(port)) {
		// button is currently pressed and was not detected as being pressed during last check
		set_digital_pressed(port, true);
		return true;
	} else
		return false;  // button is not pressed or was already detected
}

int32_t adi_digital_write(uint8_t port, const bool value) {
	transform_adi_port(port);
	validate_type(port, E_ADI_DIGITAL_OUT);
	return _adi_port_set_value(port, (int32_t)value);
}

int32_t adi_pin_mode(uint8_t port, uint8_t mode) {
	switch (mode) {
		case INPUT:
			adi_port_set_config(port, E_ADI_DIGITAL_IN);
			break;
		case OUTPUT:
			adi_port_set_config(port, E_ADI_DIGITAL_OUT);
			break;
		case INPUT_ANALOG:
			adi_port_set_config(port, E_ADI_ANALOG_IN);
			break;
		case OUTPUT_ANALOG:
			adi_port_set_config(port, E_ADI_ANALOG_OUT);
			break;
		default:
			errno = EINVAL;
			return PROS_ERR;
	};
	return 1;
}

int32_t adi_motor_set(uint8_t port, int8_t speed) {
	transform_adi_port(port);
	validate_motor(port);
	if (speed > ADI_MOTOR_MAX_SPEED)
		speed = ADI_MOTOR_MAX_SPEED;
	else if (speed < ADI_MOTOR_MIN_SPEED)
		speed = ADI_MOTOR_MIN_SPEED;

	return _adi_port_set_value(port, speed);
}

int32_t adi_motor_get(uint8_t port) {
	transform_adi_port(port);
	validate_motor(port);
	return (_adi_port_get_value(port) - ADI_MOTOR_MAX_SPEED);
}

int32_t adi_motor_stop(uint8_t port) {
	validate_motor(port);
	return _adi_port_set_value(port, 0);
}

adi_encoder_t adi_encoder_init(uint8_t port_top, uint8_t port_bottom, const bool reverse) {
	transform_adi_port(port_top);
	transform_adi_port(port_bottom);
	validate_twowire(port_top, port_bottom);
	// encoder_reversed[(port - 1) / 2] = reverse;
	set_encoder_reversed(port, reverse);

	if (_adi_port_set_config(port, E_ADI_LEGACY_ENCODER)) {
		return port;
	} else {
		return PROS_ERR;
	}
}

int32_t adi_encoder_get(adi_encoder_t enc) {
	validate_type(enc, E_ADI_LEGACY_ENCODER);
	if (get_encoder_reversed(enc)) return (-_adi_port_get_value(enc));
	return _adi_port_get_value(enc);
}

int32_t adi_encoder_reset(adi_encoder_t enc) {
	validate_type(enc, E_ADI_LEGACY_ENCODER);
	return _adi_port_set_value(enc, 0);
}

int32_t adi_encoder_shutdown(adi_encoder_t enc) {
	validate_type(enc, E_ADI_LEGACY_ENCODER);
	return _adi_port_set_config(enc, E_ADI_TYPE_UNDEFINED);
}

adi_ultrasonic_t adi_ultrasonic_init(uint8_t port_echo, uint8_t port_ping) {
	transform_adi_port(port_echo);
	transform_adi_port(port_ping);
	validate_twowire(port_echo, port_ping);
	if (port != port_echo) return PROS_ERR;

	if (_adi_port_set_config(port, E_ADI_LEGACY_ULTRASONIC)) {
		return port;
	} else {
		return PROS_ERR;
	}
}

int32_t adi_ultrasonic_get(adi_ultrasonic_t ult) {
	validate_type(ult, E_ADI_LEGACY_ULTRASONIC);
	return _adi_port_get_value(ult);
}

int32_t adi_ultrasonic_shutdown(adi_ultrasonic_t ult) {
	validate_type(ult, E_ADI_LEGACY_ULTRASONIC);
	return _adi_port_set_config(ult, E_ADI_TYPE_UNDEFINED);
}
