/*
 * custom_sensors.c
 *
 *  Created on: Jul 23, 2012
 *      Author: nabercro
 *
 *  Edit: Jul 22, 2014
 *  	Simplified and fixed current calculations and "interesting" detections.
 *  	Wattage values not accurate below 30W, disabling noise removal code reduced the issue.
 *  Author: mgsit
 */
//TODO REMOVE DEBUG INCLUDES
#include <msp430.h>
#include "wattage_sensor.h"
//#include "temperature_sensor.h"

#define VOL_CUR_PERIOD (12000/2400) // 5
#define VOL_CUR_SIZE	(12000/(VOL_CUR_PERIOD*60) * 5) //5 * 60hz cycles, = 200 Samples
#define WATTAGE_SIZE	(12) //1 second

#define CURRENT_INTERESTING_THRESHOLD 1000 // 3172 corresponds to 0.5A AC over 5 cycles. 24.8 raw adc = 0.5A.
#define VOLTAGE_INTERESTING_THRESHOLD 9800 // 3800 corresponds to 5V AC over 5 cycles?
#define WATTAGE_THRESHOLD_BITS 4
#define CURRENT_NOISE 0 //  8 = 0.16A
#define VOLTAGE_NOISE 0 //	30 = 5V
#define WATTAGE_NOISE 0 //  30

uint8_t calculate_wattage();

sensor_ref current_sensor;
sensor_ref voltage_sensor;
sensor_ref wattage_sensor;

uint8_t current_is_ready;
uint8_t voltage_is_ready;

uint8_t should_transmit_current;
uint8_t should_transmit_voltage;
uint8_t should_transmit_wattage;

uint8_t voltage_is_interesting;
uint8_t send_next_voltage;
uint8_t current_is_interesting;

int32_t power_sum;
int32_t power_counter;

int32_t power_average;
int32_t power_low_passed;

int32_t current_reference;
int32_t voltage_reference;

int32_t new_voltage;
int32_t new_current;

int32_t stable_voltage;
int32_t stable_current;
int32_t stable_wattage;

uint16_t wattage_settling_counter;
uint16_t voltage_settling_counter;
uint16_t current_settling_counter;

#define SETTLING_TIME 12 //one second

// Number of interesting currents to send. Each interesting current is 200 samples.
#define NUM_MSGS 60
uint8_t msg_count_current;
uint8_t msg_count_voltage;

/* Initialize global variables
 *
 */
void init_internal_wattage_sensor() {
	// Set time_increment to 84 ms (message time length)
	msg_count_current = NUM_MSGS;
	msg_count_voltage = NUM_MSGS;

	/* init some variables */
	voltage_sensor = 0;
	current_sensor = 0;
	wattage_sensor = 0;

	should_transmit_voltage = FALSE;
	should_transmit_current = FALSE;
	should_transmit_wattage = FALSE;

	voltage_is_interesting = FALSE;
	send_next_voltage = FALSE;
	current_is_interesting = FALSE;

	current_reference = 2036; // experimentally (close to half of 3.3 V)
	voltage_reference = 2036;

	stable_current = 0;
	stable_voltage = 0;
	stable_wattage = 0;

	voltage_is_ready = FALSE;
	current_is_ready = FALSE;

	power_average = 0;
	power_low_passed = 0;

	wattage_settling_counter = SETTLING_TIME;
	current_settling_counter = SETTLING_TIME;
	voltage_settling_counter = SETTLING_TIME;
}

/* Clear current, voltage, wattage sensor structs
 * Members of these structs cleared are data, count, trigger
 */
uint8_t sync_wattage_sensors() {
	stop_sampling();

	sensor_clear_state(current_sensor);
	sensor_clear_state(voltage_sensor);
	sensor_clear_state(wattage_sensor);

	voltage_is_ready = FALSE;
	current_is_ready = FALSE;
	wattage_settling_counter = SETTLING_TIME;
	voltage_settling_counter = SETTLING_TIME;
	voltage_settling_counter = SETTLING_TIME;

	start_sampling();
	return SUCCESS;
}

/*
 * This is were current values are put into array.
 * Interesting detection is here.
 * Current packet transmit flag is set here.
 */
uint8_t current_on_full(node_ref args) {
	int16_t * current_array = 0;
	int16_t current_value = 0;
	int16_t previous_current_value = 0;
	int16_t current_reading = 0;

	new_current = 0; // For "interesting" detection

	current_array = sensor_get_data_array(current_sensor); // get from sensor struct

	// With the data acquired, now we can loop through it and fix it for the reference
	int32_t current_sum = 0;
	uint16_t itor;

	//Sum up current array and calculat average current
	for(itor = 0; itor < VOL_CUR_SIZE; itor++) {
		current_reading = current_array[itor];
		current_sum += current_reading; // Since it is a sine wave, it is centered at 0, so lets sum them up
	}
	int32_t current_average = (int32_t)current_sum/(int32_t)VOL_CUR_SIZE;

	// Could low pass it right here // offset compensation?
	int32_t current_difference = (int32_t)current_average-(int32_t)current_reference;
	current_reference += (int32_t)current_difference>>(int32_t)1; // Current difference from reference divide by 2 and add to new ref.

	// Put current minus reference (current value) into current_array
	for(itor = 0; itor < VOL_CUR_SIZE; itor++) {
		current_reading = current_array[itor];
		current_value = (int16_t)(current_reading-(int32_t)current_reference); // Calculate Current

		// Check our threshold.  If it is under 10, then it must be noise - No load
		if(current_value <= CURRENT_NOISE && current_value >= -CURRENT_NOISE) {
			if(previous_current_value == 0) {
				current_value = 0;
			} else {
				previous_current_value = 0;
			}
		} else {
			previous_current_value = current_value;
		}

		current_array[itor] = current_value;
		// Accumulate current values into new_current = sum(abs(current_value))
		new_current = (current_value > 0) ? new_current + (int32_t)current_value : new_current - (int32_t)current_value;
	}

	// After 12 new_currents set it to stable_current
	if(current_settling_counter > 0) {
		current_settling_counter--;
		stable_current = new_current;
		current_is_ready = FALSE;
	} else {
		current_is_ready = TRUE;
	}


	if(should_transmit_current) { //check if current is enabled
		// Find the difference in current levels
		uint32_t diff_current;
		diff_current = (stable_current > new_current) ? (stable_current - new_current) : (new_current - stable_current);
		if(diff_current > CURRENT_INTERESTING_THRESHOLD) {
			current_is_interesting = TRUE;
			stable_current = new_current;
		}

		if (msg_count_current < NUM_MSGS) {
			current_is_interesting = FALSE;
			if (encode_data_for_transmit(args) == FAILURE) { // Buffer overflow.
				msg_count_current = NUM_MSGS; //stop sequence.
			} else {
				msg_count_current += 1;
			}
		}
		if (current_is_interesting) {
			encode_old_data_for_transmit(args);
			msg_count_current = 0;
		}
	}

	if(should_transmit_wattage && voltage_is_ready && current_is_ready) {
		calculate_wattage();
		current_is_ready = FALSE;
		voltage_is_ready = FALSE;
	}
	return SUCCESS;
}





uint8_t voltage_on_full(node_ref args) {

	//For "interesting" detection
	new_voltage = 0;

	int16_t * voltage_array = 0;
	int16_t voltage_value = 0;
	uint16_t voltage_reading = 0;

	voltage_array = sensor_get_data_array(voltage_sensor);

	// With the data acquired, now we can loop through it and fix it for the reference
	int32_t voltage_sum = 0;

	uint16_t itor;
	for(itor = 0; itor < VOL_CUR_SIZE; itor++) {
		voltage_reading = voltage_array[itor];
		// Since it is a sine wave, it is centered at 0, so lets sum them up
		voltage_sum += voltage_reading;
	}

	int32_t voltage_average = (int32_t)voltage_sum/(int32_t)VOL_CUR_SIZE;

	// Could low pass it right here
	int32_t voltage_difference = (int32_t)voltage_average-(int32_t)voltage_reference;
	voltage_reference += (int32_t)voltage_difference>>(int32_t)1;

	for(itor = 0; itor < VOL_CUR_SIZE; itor++) {
		voltage_reading = voltage_array[itor];
		// Current is the voltage reading minus its reference (centered at 3.3/2)
		voltage_value = (int16_t)(voltage_reading-voltage_reference); // Calculate Voltage

		 //Check our threshold.  If it is under 30, then it must be under 5 volts and must be noise
		if(voltage_value < VOLTAGE_NOISE && voltage_value > VOLTAGE_NOISE) {// TODO magic number
		  voltage_value = 0;
		}

		voltage_array[itor] = voltage_value;

		// Accumulate Voltage values into new_voltage, sum(abs(current_val))
		new_voltage = (voltage_value > 0) ? new_voltage + (int32_t)voltage_value : new_voltage - (int32_t)voltage_value;
	}
	// After 12 new_voltages set it to stable_voltage
	if(voltage_settling_counter > 0) {
		voltage_settling_counter--;
		stable_voltage = new_voltage;
		voltage_is_ready = FALSE;
	} else {
		voltage_is_ready = TRUE;
	}

	if(should_transmit_voltage) {
		// Find the difference in voltage levels
		uint32_t diff_voltage;
		diff_voltage = (stable_voltage > new_voltage) ? (stable_voltage - new_voltage) : (new_voltage - stable_voltage);

		if((uint32_t)diff_voltage > VOLTAGE_INTERESTING_THRESHOLD) {
			voltage_is_interesting = TRUE;
			stable_voltage = new_voltage;
		}
		if (msg_count_voltage < NUM_MSGS) {
			voltage_is_interesting = FALSE;
			if (encode_data_for_transmit(args) == FAILURE) { // Buffer overflow.
				msg_count_voltage = NUM_MSGS; //stop sequence.
			} else {
				msg_count_voltage += 1;
			}
		}
		if (voltage_is_interesting) {
			msg_count_voltage = 0;
		}
	}


	if(should_transmit_wattage && current_is_ready && voltage_is_ready) {
		calculate_wattage();
		current_is_ready = FALSE;
		voltage_is_ready = FALSE;
	}
	return SUCCESS;
}


uint8_t calculate_wattage() {

	int16_t * current_array = 0;
	int16_t * voltage_array = 0;

	int16_t cur_current = 0;
	int16_t cur_voltage = 0;

	current_array = sensor_get_data_array(current_sensor);
	voltage_array = sensor_get_data_array(voltage_sensor);

	power_sum = 0;

	// With the data acquired, now we can loop through it and calculate wattage
	uint16_t itor;
	for(itor = 0; itor < VOL_CUR_SIZE; itor++) {
		cur_current = current_array[itor];
		cur_voltage = voltage_array[itor];

		// Calculate Instantaneous Wattage
		// Multiply current and voltage together.
		// Eventually we will add them up and divide, so, we might as well add now
		MPYS = cur_current;
		OP2 = cur_voltage;
		uint32_t current_power = 0;
		current_power |= RES0;
		current_power |= (int32_t)RES1<<(int32_t)16;
		power_sum += (int32_t)current_power;

	}


	power_average = (int32_t)power_sum / (int32_t)VOL_CUR_SIZE; //todo get rid of this division

	//low pass the average
	power_low_passed = (int32_t)3*(int32_t)power_low_passed + (int32_t)power_average;
	power_low_passed = (int32_t)power_low_passed>>(int32_t)2;

	int16_t result_wattage = power_low_passed >>5;

	//noise thresholds
	// 3 watts = 5 / 0.165 = 5 * 6 = 30 )
	if(result_wattage < WATTAGE_NOISE && result_wattage > -WATTAGE_NOISE) result_wattage = 0;

	// give some settling time
	if(wattage_settling_counter > 0) wattage_settling_counter--;

	if(check_time(sensor_get_period(wattage_sensor), sensor_get_trigger_time(wattage_sensor))) {
		if(wattage_settling_counter == 0) {
			store_data_point(wattage_sensor, result_wattage);
		}
	}
	return SUCCESS;
}




/// *********************** CREATE SENSORS

/* Create current sensor struct if they do not already exist
 * Param period and size of sensor from inbound packet
 */
uint8_t create_internal_current_sensor(time_ref period, uint16_t size) {
	if(current_sensor) {
		// We already have one
		enable_internal_current_sensor();
		return SUCCESS;
	}

	action_ref full_action = 0;
	node_ref sensor_node = 0;

	for(;;) {

		time_clear(period);
		time_set_clock_time(period, VOL_CUR_PERIOD);

		current_sensor = new_sensor('I', CURRENT_ADC, period, VOL_CUR_SIZE);
		if(!current_sensor) break;

		// Create the transmit action
		full_action = new_action();
		if(!full_action) break;
		action_set_func(full_action, &current_on_full);

		sensor_node = new_node(current_sensor, 0);
		if(!sensor_node) break;
		action_set_args(full_action, sensor_node);

		sensor_add_action_on_data_full(current_sensor, full_action);

		should_transmit_current = TRUE;
		sync_wattage_sensors();
		return SUCCESS;
	}

	sensor_delete(&current_sensor);
	action_delete(&full_action);

	return FAILURE;
}

/* Create voltage sensor struct if they do not already exist
 * Param period and size of sensor from inbound packet
 */
uint8_t create_internal_voltage_sensor(time_ref period, uint16_t size) {
	if(voltage_sensor) {
		// We already have one
		enable_internal_voltage_sensor();
		return SUCCESS;
	}

	action_ref full_action = 0;
	node_ref sensor_node = 0;

	for(;;) {

		time_clear(period);
		time_set_clock_time(period, VOL_CUR_PERIOD);

		voltage_sensor = new_sensor('V', VOLTAGE_ADC, period, VOL_CUR_SIZE);
		if(!voltage_sensor) break;

		// Create the transmit action
		full_action = new_action();
		if(!full_action) break;
		action_set_func(full_action, &voltage_on_full);

		sensor_node = new_node(voltage_sensor, 0);
		if(!sensor_node) break;
		action_set_args(full_action, sensor_node);

		sensor_add_action_on_data_full(voltage_sensor, full_action);

		should_transmit_voltage = TRUE;
		sync_wattage_sensors();
		return SUCCESS;
	}

	sensor_delete(&voltage_sensor);
	action_delete(&full_action);

	return FAILURE;
}


/* Create wattage sensor struct if they do not already exist
 * Param period and size of sensor from inbound packet
 */
uint8_t create_internal_wattage_sensor(time_ref period, uint16_t size) {
	if(wattage_sensor) {
		// We already have one
		if((time_cmp(period, sensor_get_period(wattage_sensor)) == 0) && size == sensor_get_size(wattage_sensor)) {
			// If they are the same...do nothing
			enable_internal_wattage_sensor();
			return SUCCESS;
		} else {
			// They are different, so delete the old one
			delete_internal_wattage_sensor();
		}
	}

	action_ref transmit_action = 0;

	//Use a for loop so we can do a break statement for error checking
	for(;;) {
		// Create all the sensors
		wattage_sensor = new_sensor('W', 0, period, size);
		if(!wattage_sensor) break;

		// We give period to the internal sensors, because they need to use
		// the struct temporarily.  Could get around this by allocating a new
		// time and then freeing it
		if(!voltage_sensor)	{
			create_internal_voltage_sensor(period, size);
			should_transmit_voltage = FALSE;
		}
		if(!voltage_sensor) break;

		if(!current_sensor) {
			create_internal_current_sensor(period,size);
			should_transmit_current = FALSE;
		}
		if(!current_sensor) break;


		// Create the transmit action
		transmit_action = new_transmit_action(wattage_sensor);
		if(!transmit_action) break;
		sensor_add_action_on_data_full(wattage_sensor, transmit_action);

		should_transmit_wattage = TRUE;
		return SUCCESS;
	}


	//CLEAN UP
	delete_internal_wattage_sensor();

	action_delete(&transmit_action);

	return FAILURE;
}


// *********  ENABLE SENSORS

uint8_t enable_internal_current_sensor() {
	if(!current_sensor) return FAILURE;
	should_transmit_current = TRUE;
	enable_sensor(current_sensor);
	current_settling_counter = SETTLING_TIME;
	return SUCCESS;
}
uint8_t enable_internal_voltage_sensor() {
	if(!voltage_sensor) return FAILURE;
	should_transmit_voltage = TRUE;
	enable_sensor(voltage_sensor);
	voltage_settling_counter = SETTLING_TIME;
	return SUCCESS;
}
uint8_t enable_internal_wattage_sensor() {
	if(!should_transmit_current) {
		enable_internal_current_sensor();
		should_transmit_current = FALSE;
	}
	if(!should_transmit_voltage) {
		enable_internal_voltage_sensor();
		should_transmit_voltage = FALSE;
	}

	sync_wattage_sensors();

	should_transmit_wattage = TRUE;
	// dont need to enable wattage because it doesn't use a sensor directly

	return SUCCESS;
}


// ************** DISABLE SENSORS

uint8_t disable_internal_current_sensor() {
	if(!current_sensor) return FAILURE;
	should_transmit_current = FALSE;

	if(!should_transmit_wattage) {
		disable_sensor(current_sensor);
	}

	return SUCCESS;
}
uint8_t disable_internal_voltage_sensor() {
	if(!voltage_sensor) return FAILURE;
	should_transmit_voltage = FALSE;

	if(!should_transmit_wattage) {
		disable_sensor(voltage_sensor);
	}

	return SUCCESS;
}
uint8_t disable_internal_wattage_sensor() {
	should_transmit_wattage = FALSE;

	if(!should_transmit_current) disable_internal_current_sensor();
	if(!should_transmit_voltage) disable_internal_voltage_sensor();

	//don't need to disable wattage, because it was never enabled, it doesn't use a sensor
	return SUCCESS;
}


// ***************** DELETE SENSORS

uint8_t delete_internal_current_sensor() {
	if(!current_sensor) return FAILURE;
	should_transmit_current = FALSE;
	if(!should_transmit_wattage) sensor_delete(&current_sensor);
	return SUCCESS;
}
uint8_t delete_internal_voltage_sensor() {
	if(!voltage_sensor) return FAILURE;
	should_transmit_voltage = FALSE;
	if(!should_transmit_wattage) sensor_delete(&voltage_sensor);
	return SUCCESS;
}
uint8_t delete_internal_wattage_sensor() {
	should_transmit_wattage = FALSE;

	if(!should_transmit_current) delete_internal_current_sensor();
	if(!should_transmit_voltage) delete_internal_voltage_sensor();

	if(!wattage_sensor) return FAILURE;
	sensor_delete(&wattage_sensor);
	return SUCCESS;
}
