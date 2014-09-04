/*
 * shempserver.c
 *
 *  Created on: Jul 25, 2012
 *      Author: nabercro
 */

#include "shempserver.h"

//#define DEBUG_ENCODE

const uint8_t SERIAL_NUMBER[] = "111111"; // This is hardcoded serial number

#ifdef DEBUG_ENCODE
uint16_t test_buffer_encode[100] = {
		0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,
		0,0,0,0,0,0,0,0,0,0,
};
uint16_t test_itor_encode = 0;
#endif

uint8_t output_buffer[OUTPUT_BUFFER_SIZE]; //These are not initialized to zero on Ti compilers
static uint16_t write_index;
static uint16_t read_index;
uint8_t reset_output_flag;
uint8_t hold_transmit_flag;

void init_transmits() {
	int i;
	for (i=0; i<OUTPUT_BUFFER_SIZE; i++) {
		output_buffer[i] = 0;
	}
	//d//debug_push(d_init_transmits);
	write_index = 0;
	read_index = 0;
	reset_output_flag = FALSE;

	hold_transmit_flag = FALSE;
	//d//debug_pop();
}


void reset_output_buffer() {
	write_index = 0;
	read_index = 0;
}


// Size Calculation
// Lxx	3 for length
// Tx	2 for type
// lx	2 for loc
// t+14	15 for time
// P+14	15 for period
// Cxx		3 for count
// Dxx...	1 + count*2 for data
// X		1 for end
#define HEADER_SIZE 42

uint8_t encode_data_for_transmit(node_ref args) {
	uint8_t ret = FAILURE;

	for(;;) {
		if(!args) break;
		sensor_ref s = (sensor_ref)node_get_val(args);

		if(!s) break;

		uint16_t cur_size = 0;
		int16_t * array = sensor_get_data_array(s);
		uint16_t temp_ptr = write_index;

		cur_size = sensor_get_size(s)*sizeof(uint16_t) + HEADER_SIZE;

		if(cur_size > OUTPUT_BUFFER_SIZE) return FAILURE; // Not ever going to fit

		if(temp_ptr + cur_size > OUTPUT_BUFFER_SIZE - write_index) { //If the output_buffer has no space for msg then return failure.
			// Cannot fit it
//			temp_ptr = 0;
//			read_index = 0;
//			write_index = 0;
			return FAILURE;
		}

#ifdef DEBUG_ENCODE
		test_buffer_encode[test_itor_encode] = s->end_time->milliseconds;
		test_itor_encode++;
		if (test_itor_encode == 99) {
			return FAILURE;
		}
#endif

		output_buffer[temp_ptr++] = 'L';
		write_2_bytes_to_string(&output_buffer[temp_ptr], cur_size);
		temp_ptr += 2;

		output_buffer[temp_ptr++] = 'T';
		output_buffer[temp_ptr++] = sensor_get_type(s);

		output_buffer[temp_ptr++] = 'l';
		output_buffer[temp_ptr++] = sensor_get_loc(s);

		output_buffer[temp_ptr++] = 't'; //time
		write_time_to_string(&output_buffer[temp_ptr], sensor_get_end_time(s));
//		write_count_to_string(&output_buffer[temp_ptr]);
		temp_ptr += STRING_TO_TIME_LENGTH;

		output_buffer[temp_ptr++] = 'P'; //period
		write_time_to_string(&output_buffer[temp_ptr], sensor_get_period(s));
		temp_ptr += STRING_TO_TIME_LENGTH;

		output_buffer[temp_ptr++] = 'C';
		write_2_bytes_to_string(&output_buffer[temp_ptr], sensor_get_size(s));
		temp_ptr += 2;

		output_buffer[temp_ptr++] = 'D';
		//data
		memcpy(&output_buffer[temp_ptr], array, sensor_get_size(s)*sizeof(uint16_t));
		temp_ptr += sensor_get_size(s) * 2;

		output_buffer[temp_ptr++] = 'X'; // end

		write_index = temp_ptr;

		ret = SUCCESS;
		break;
	}
	return ret;
}

uint8_t encode_old_data_for_transmit(node_ref args) {
	uint8_t ret = FAILURE;

	for(;;) {
		if(!args) break;
		sensor_ref s = (sensor_ref)node_get_val(args);

		if(!s) break;

		uint16_t cur_size = 0;
		int16_t * array = sensor_get_old_array(s);
		uint16_t temp_ptr = write_index;

		cur_size = sensor_get_size(s)*sizeof(uint16_t) + HEADER_SIZE;

		if(cur_size > OUTPUT_BUFFER_SIZE) return FAILURE; // Not ever going to fit

		if(temp_ptr + cur_size > OUTPUT_BUFFER_SIZE - write_index) { //If the output_buffer has no space for msg then return failure.
			return FAILURE;
		}

		output_buffer[temp_ptr++] = 'L';
		write_2_bytes_to_string(&output_buffer[temp_ptr], cur_size);
		temp_ptr += 2;

		output_buffer[temp_ptr++] = 'T';
		output_buffer[temp_ptr++] = sensor_get_type(s);

		output_buffer[temp_ptr++] = 'l';
		output_buffer[temp_ptr++] = sensor_get_loc(s);

		output_buffer[temp_ptr++] = 't'; //time
		write_time_to_string(&output_buffer[temp_ptr], sensor_get_end_time(s));
		temp_ptr += STRING_TO_TIME_LENGTH;

		output_buffer[temp_ptr++] = 'P'; //period
		write_time_to_string(&output_buffer[temp_ptr], sensor_get_period(s));
		temp_ptr += STRING_TO_TIME_LENGTH;

		output_buffer[temp_ptr++] = 'C';
		write_2_bytes_to_string(&output_buffer[temp_ptr], sensor_get_size(s));
		temp_ptr += 2;

		output_buffer[temp_ptr++] = 'D';
		//data
		memcpy(&output_buffer[temp_ptr], array, sensor_get_size(s)*sizeof(uint16_t));
		temp_ptr += sensor_get_size(s) * 2;

		output_buffer[temp_ptr++] = 'X'; // end

		write_index = temp_ptr;

		ret = SUCCESS;
		break;
	}
	return ret;
}


uint8_t have_data_to_transmit() {
	if(reset_output_flag) return FALSE;
	if (read_index != write_index) return TRUE;
	return FALSE;
}

void hold_transmits() {
	hold_transmit_flag = TRUE;
}

void continue_transmits() {
	hold_transmit_flag = FALSE;
}

uint8_t transmit_data() {
	if(!have_data_to_transmit()) {
		return SUCCESS;
	}
	if(hold_transmit_flag) return SUCCESS;
	reset_ack();

	uint16_t end_ptr = write_index;
	while (have_data_to_transmit()) {
		uart_send_array(&output_buffer[read_index], end_ptr-read_index);
		if(wait_for(&have_ack, 500)) {
			read_index = end_ptr;
		}
		else {
			return FAILURE;
		}
		end_ptr = write_index;
	}

	reset_output_buffer();
	return SUCCESS;
}

#define HEADER_LENGTH 28 // 0x1C00
#define HEADER_LENGTH_PTR 1

#define HEADER_TIME_PTR 13

#define HEADER_SERIAL_PTR 6
#define HEADER_SERIAL_LENGTH 6

uint8_t transmit_header() {
	uint8_t header[] = "L00THSxxxxxxt00000000000000X";
	// Set up header
	write_2_bytes_to_string(&header[HEADER_LENGTH_PTR], HEADER_LENGTH);
	memcpy(&header[HEADER_SERIAL_PTR], SERIAL_NUMBER, HEADER_SERIAL_LENGTH);

	reset_ack();
	while(!have_ack()) {
		write_time_to_string(&header[HEADER_TIME_PTR], global_time());
		uart_send_array((uint8_t *)header, HEADER_LENGTH);
		wait_for(&have_ack, 500);
	}

	return SUCCESS;
}


action_ref new_transmit_action(sensor_ref s) {
	action_ref tx = new_action();
	action_set_func(tx, &encode_data_for_transmit);
	node_ref arg1 = new_node(s, 0);
	action_set_args(tx, arg1);
	return tx;
}

