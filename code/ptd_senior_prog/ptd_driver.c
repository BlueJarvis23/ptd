//*****************************************************************************
//
// uart_echo.c - Example for reading data from and writing data to the UART in
//               an interrupt driven fashion.
//
// Copyright (c) 2012-2014 Texas Instruments Incorporated.  All rights reserved.
// Software License Agreement
// 
// Texas Instruments (TI) is supplying this software for use solely and
// exclusively on TI's microcontroller products. The software is owned by
// TI and/or its suppliers, and is protected under applicable copyright
// laws. You may not combine this software with "viral" open-source
// software in order to form a larger program.
// 
// THIS SOFTWARE IS PROVIDED "AS IS" AND WITH ALL FAULTS.
// NO WARRANTIES, WHETHER EXPRESS, IMPLIED OR STATUTORY, INCLUDING, BUT
// NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE APPLY TO THIS SOFTWARE. TI SHALL NOT, UNDER ANY
// CIRCUMSTANCES, BE LIABLE FOR SPECIAL, INCIDENTAL, OR CONSEQUENTIAL
// DAMAGES, FOR ANY REASON WHATSOEVER.
// 
// This is part of revision 2.1.0.12573 of the EK-TM4C123GXL Firmware Package.
//
//*****************************************************************************

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "driverlib/debug.h"
#include "driverlib/fpu.h"
#include "driverlib/gpio.h"
#include "driverlib/interrupt.h"
#include "driverlib/pin_map.h"
#include "driverlib/rom.h"
#include "driverlib/sysctl.h"
#include "driverlib/uart.h"

//*****************************************************************************
//
//! \addtogroup example_list
//! <h1>UART Echo (uart_echo)</h1>
//!
//! This example application utilizes the UART to echo text.  The first UART
//! (connected to the USB debug virtual serial port on the evaluation board)
//! will be configured in 4800 baud, 8-n-1 mode.  All characters received on
//! the UART are transmitted back to the UART.
//
//*****************************************************************************

//*****************************************************************************
// Global Constants Section
//*****************************************************************************

char gps_buffer_glob[100];
char* next_letter_loc = gps_buffer_glob;
char line_start = 0;

struct gps_point point;
struct char_list split_fields;
char point_str_buffer[300];

char cell_return_buffer[100];
char cell_return_buffer_loc;

char uart_0_buffer[12] = "\0\0\0\0\0\0\0\0\0\0\0\0";
int uart_0_pos = 0;
int uart_0_flag = 0;

char cell_num_target[12] = "\0\0\0\0\0\0\0\0\0\0\0\0";

//*****************************************************************************
//
// The error routine that is called if the driver library encounters an error.
//
//*****************************************************************************
#ifdef DEBUG
void
__error__(char *pcFilename, uint32_t ui32Line)
{
}
#endif

void clear_point_str_buffer(){
	int i = 0;
	for (i=0;i<300;++i) point_str_buffer[i] = 0;
}

void clear_gps_buffer_glob(){
	int i = 0;
	for (i=0;i<100;++i) gps_buffer_glob[i] = 0;
}


//*****************************************************************************
// INIT_MEM.h
//*****************************************************************************


void init_memory(void){
	unsigned volatile int * SYSCTL = (unsigned int *) 0x400FE000;
	unsigned volatile int * EEPROM = (unsigned int *) 0x400AF000;
	SYSCTL[0x658/4] = 0x1;
	__nop();
	__nop();
	__nop();
	while(EEPROM[0x18/4] & 0x1);
	EEPROM[0x00/4] = 0x10005;
	while(EEPROM[0x18/4] & 0x1);
	EEPROM[0x4/4] = 0x1;
}

void writeMemory(int * values){
	unsigned volatile int * EEPROM = (unsigned int *) 0x400AF000;
	int d;
	//values[0] = 1;
	while(EEPROM[0x18/4] & 0x1);
	EEPROM[0x18/4] = 0x1;
	while(EEPROM[0x18/4] & 0x1);
	EEPROM[0x00/4] = 0x10005;
	for(d=0; d<3; d++){
		while(EEPROM[0x18/4] & 0x1);
		EEPROM[0x8/4] = d;
		while(EEPROM[0x18/4] & 0x1);
		EEPROM[0x10/4] = values[d];
	}
}

void readMemory(int * values){
	unsigned volatile int * EEPROM = (unsigned int *) 0x400AF000;
	int d;
	while(EEPROM[0x18/4] & 0x1);
	EEPROM[0x4/4] = 0x1;
	while(EEPROM[0x18/4] & 0x1);
	EEPROM[0x00/4] = 0x10005;
	while(EEPROM[0x18/4] & 0x1);
	EEPROM[0x8/4] = 0x0;
	while(EEPROM[0x18/4] & 0x1);
	values[0] = EEPROM[0X10];
	for(d=0; d<3; d++){
		EEPROM[0x8/4] = d;
		while(EEPROM[0x18] & 0x1);
		values[d] = EEPROM[0x10/4];
	}
}

void resetValues(int *values){
	int d;
	for(d=0; d<5; d++){
		values[d] = 0;
	}
}


//*****************************************************************************
// CELL_CTL.H
//*****************************************************************************

//////////////////////////////////////////////
// This header is to be used with the
// Adafruit Cell Module -- PTD
//
// Written by Dallin Marshall -- Oct 2015
//
//////////////////////////////////////////////


#define CTRL_Z 26

char cell_return_buffer[100];
char cell_return_buffer_loc = 0;

int get_cell_status(){
	char cell_response[10] = "OK\n";
	cell_return_buffer_loc = 0;
	int temp = strcmp(cell_response, cell_return_buffer);
	return 1 ? temp : 0;
}

int send_to_cell_module(char * msg){
    int i = 0;
    while(msg[i] != '\0')
    {
        if(UARTSpaceAvail(UART7_BASE))
        {
            ROM_UARTCharPutNonBlocking(UART7_BASE, msg[i]);
            ++i;
        }
    }

    for(i=0;i<500;++i){
    	SysCtlDelay(SysCtlClockGet() / (1000 * 3));
    }

    return 1 ? get_cell_status(): 0;
}

void read_cell_module(char * buffer){
	strcpy(buffer, cell_return_buffer);
	cell_return_buffer[0] = 0;
	cell_return_buffer_loc = 0;
}

int cell_power_toggle(){
    // Cycle Power Pin 2 Sec
	int i;

	GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, 0);

	for(i=0;i<5000;++i){
	    	SysCtlDelay(SysCtlClockGet() / (1000 * 3));
	}

	GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, GPIO_PIN_1);

	for(i=0;i<20000;++i){
		    	SysCtlDelay(SysCtlClockGet() / (1000 * 3));
	}

	return 1;
}

void cell_power_off(){
	int i;
	volatile int power_stat = 0;

	for(i=0;i<1000;++i){
		SysCtlDelay(SysCtlClockGet() / (1000 * 3));
	}

	power_stat = GPIOPinRead(GPIO_PORTB_BASE, GPIO_PIN_0);

    for(i=0;i<20000;++i){
       	SysCtlDelay(SysCtlClockGet() / (1000 * 3));
    }

    if(power_stat){ // turn cell off in case it turned on on power up
    	cell_power_toggle();
    }
}

int set_text_mode(){
    // "AT+CMGF=1"
    char str_temp[100];
    sprintf(str_temp, "AT+CMGF=1\r\n");
    send_to_cell_module(str_temp);
    return 1 ? get_cell_status() : 0;
}

int send_sms_msg(char msg[200]){
    // AT+CMGS="number"\n
    // msg + control+Z

	int i;
	cell_power_off();
	cell_power_toggle();

    char str_temp[200];
    for(i=0;i<200;++i){
    	str_temp[i] = 0;
    }

    set_text_mode();

    //char cell_number[12] = "14359949335\0";
    //char temp_char[12] = "14359949335\0";

    char cell_number[12] = "\0\0\0\0\0\0\0\0\0\0\0\0";
    readMemory((int *)cell_number);

    sprintf(str_temp, "AT+CMGS=\"%s\"\r\n", cell_number);
    send_to_cell_module(str_temp);

    sprintf(str_temp, "%s%c\r\n", msg, CTRL_Z);
    send_to_cell_module(str_temp);

    for(i=0;i<20000;++i){
    	SysCtlDelay(SysCtlClockGet() / (1000 * 3));
    }

    cell_power_toggle();

    return 1;
}


//*****************************************************************************
// GPS_POINT.H
//*****************************************************************************

//////////////////////////////////////////////
// This header is to be used with the 
// Trimble GPS module of the PTD
//
// Written by Dallin Marshall -- May 2015
//
//////////////////////////////////////////////


#define NUM_FIELDS_GPGGA 14 // Num Fields in GPGGA data
#define LEN_FIELDS_GPGGA 15 // Len Fields in GPGGA data

char m1[LEN_FIELDS_GPGGA];
char m2[LEN_FIELDS_GPGGA];
char m3[LEN_FIELDS_GPGGA];
char m4[LEN_FIELDS_GPGGA];
char m5[LEN_FIELDS_GPGGA];
char m6[LEN_FIELDS_GPGGA];
char m7[LEN_FIELDS_GPGGA];
char m8[LEN_FIELDS_GPGGA];
char m9[LEN_FIELDS_GPGGA];
char m10[LEN_FIELDS_GPGGA];
char m11[LEN_FIELDS_GPGGA];
char m12[LEN_FIELDS_GPGGA];
char m13[LEN_FIELDS_GPGGA];
char m14[LEN_FIELDS_GPGGA];

static char * strtok_single (char * str, char const * delims)
{
    static char  *src = NULL;
    char  *p,  *ret = 0;

    if (str != NULL)
        src = str;

    if (src == NULL || *src == '\0')
        return NULL;

    ret = src;
    if ((p = strpbrk(src, delims)) != NULL)
    {
        *p  = 0;
        src = ++p;
    }
    else
        src += strlen(src);

    return ret;
}

struct gps_point
{
    char* m_code;
    char* utc_time;
    char* lat_val;
    char* lat_dir;
    char* long_val;
    char* long_dir;
    char* qual_code;
    char* num_sats;
    char* hor_dilu_pos;
    char* alt_val;
    char* alt_unit;
    char* geoid_val;
    char* geoid_unit;
    char* check_sum;
};


struct char_list
{
    char array[NUM_FIELDS_GPGGA][LEN_FIELDS_GPGGA];
};


void gps_point_modify(struct gps_point* point, const char* str_gps, struct char_list *split_fields)
{
    char active_string[80];
    strcpy(active_string,str_gps);

    int i = 0;
    char* pch = strtok_single(active_string,",");
    while(pch != NULL && i<NUM_FIELDS_GPGGA)
    {
        strcpy(split_fields->array[i], pch);
        //printf("%s\n",pch);
        pch = strtok_single(NULL, ",");
        ++i;
    }

    // might need to use this spot to check if GPS output is long enough

    strcpy(point->m_code, split_fields->array[0]);
    strcpy(point->utc_time, split_fields->array[1]);
    strcpy(point->lat_val, split_fields->array[2]);
    strcpy(point->lat_dir, split_fields->array[3]);
    strcpy(point->long_val, split_fields->array[4]);
    strcpy(point->long_dir, split_fields->array[5]);
    strcpy(point->qual_code, split_fields->array[6]);
    strcpy(point->num_sats, split_fields->array[7]);
    strcpy(point->hor_dilu_pos, split_fields->array[8]);
    strcpy(point->alt_val, split_fields->array[9]);
    strcpy(point->alt_unit, split_fields->array[10]);
    strcpy(point->geoid_val, split_fields->array[11]);
    strcpy(point->geoid_unit, split_fields->array[12]);
    strcpy(point->check_sum, split_fields->array[13]);

}

void gps_point_member_clear(struct gps_point* point)
{
	char temp[LEN_FIELDS_GPGGA];
	int i =0;
	for (i=0; i<LEN_FIELDS_GPGGA; ++i)
	{
		temp[i] = 0;
	}

	 strcpy(point->m_code, temp);
	 strcpy(point->utc_time, temp);
	 strcpy(point->lat_val, temp);
	 strcpy(point->lat_dir, temp);
	 strcpy(point->long_val, temp);
	 strcpy(point->long_dir, temp);
	 strcpy(point->qual_code, temp);
	 strcpy(point->num_sats, temp);
	 strcpy(point->hor_dilu_pos, temp);
     strcpy(point->alt_val, temp);
     strcpy(point->alt_unit, temp);
	 strcpy(point->geoid_val, temp);
	 strcpy(point->geoid_unit, temp);
	 strcpy(point->check_sum, temp);

}


void gps_point_create(struct gps_point* point)
{

    point->m_code = m1;//malloc(LEN_FIELDS_GPGGA);
    point->utc_time = m2;//malloc(LEN_FIELDS_GPGGA);
    point->lat_val = m3;//malloc(LEN_FIELDS_GPGGA);
    point->lat_dir = m4;//malloc(LEN_FIELDS_GPGGA);
    point->long_val = m5;//malloc(LEN_FIELDS_GPGGA);
    point->long_dir = m6;//malloc(LEN_FIELDS_GPGGA);
    point->qual_code = m7;//malloc(LEN_FIELDS_GPGGA);
    point->num_sats = m8;//malloc(LEN_FIELDS_GPGGA);
    point->hor_dilu_pos = m9;//malloc(LEN_FIELDS_GPGGA);
    point->alt_val = m10;//malloc(LEN_FIELDS_GPGGA);
    point->alt_unit = m11;//malloc(LEN_FIELDS_GPGGA);
    point->geoid_val = m12;//malloc(LEN_FIELDS_GPGGA);
    point->geoid_unit = m13;//malloc(LEN_FIELDS_GPGGA);
    point->check_sum = m14;//malloc(LEN_FIELDS_GPGGA);

    gps_point_member_clear(point);

}


void gps_point_free(struct gps_point* point)
{

    free(point->m_code);
    free(point->utc_time);
    free(point->lat_val);
    free(point->lat_dir);
    free(point->long_val);
    free(point->long_dir);
    free(point->qual_code);
    free(point->num_sats);
    free(point->hor_dilu_pos);
    free(point->alt_val);
    free(point->alt_unit);
    free(point->geoid_val);
    free(point->geoid_unit);
    free(point->check_sum);

    free(point);
}

void get_point_str_debug(struct gps_point* point, char *point_str_dest)
{
    char point_str_src[200];

    sprintf(point_str_src, "\r\nm_code: \t\t%s\r\n",point->m_code);
    strcpy(point_str_dest, point_str_src);

    sprintf(point_str_src, "\rutc_time: \t\t%s\r\n",point->utc_time);
    strcat(point_str_dest, point_str_src);

    sprintf(point_str_src, "\rlat_val: \t\t%s\r\n",point->lat_val);
    strcat(point_str_dest, point_str_src);

    sprintf(point_str_src, "\rlat_dir: \t\t%s\r\n",point->lat_dir);
    strcat(point_str_dest, point_str_src);

    sprintf(point_str_src, "\rlong_val: \t\t%s\r\n",point->long_val);
    strcat(point_str_dest, point_str_src);

    sprintf(point_str_src, "\rlong_dir: \t\t%s\r\n",point->long_dir);
    strcat(point_str_dest, point_str_src);

    sprintf(point_str_src, "\rqual_code: \t\t%s\r\n",point->qual_code);
    strcat(point_str_dest, point_str_src);

    sprintf(point_str_src, "\rnum_sats: \t\t%s\r\n",point->num_sats);
    strcat(point_str_dest, point_str_src);

    sprintf(point_str_src, "\rhor_dilu_pos: \t\t%s\r\n",point->hor_dilu_pos);
    strcat(point_str_dest, point_str_src);

    sprintf(point_str_src, "\ralt_val: \t\t%s\r\n",point->alt_val);
    strcat(point_str_dest, point_str_src);

    sprintf(point_str_src, "\ralt_unit: \t\t%s\r\n",point->alt_unit);
    strcat(point_str_dest, point_str_src);

    sprintf(point_str_src, "\rgeoid_val: \t\t%s\r\n",point->geoid_val);
    strcat(point_str_dest, point_str_src);

    sprintf(point_str_src, "\rgeoid_unit: \t\t%s\r\n",point->geoid_unit);
    strcat(point_str_dest, point_str_src);

    sprintf(point_str_src, "\rcheck_sum: \t\t%s\r\n\00",point->check_sum);
    strcat(point_str_dest, point_str_src);
}

void get_point_str(struct gps_point* point, char *point_str_dest)
{
    char point_str_src[200];

    sprintf(point_str_src, "Time: %s UTC\r\n",point->utc_time);
    strcpy(point_str_dest, point_str_src);

    sprintf(point_str_src, "Latitude: %s %s\r\nLongitude: %s %s\r\n", point->lat_val, point->lat_dir, point->long_val, point->long_dir);
    strcat(point_str_dest, point_str_src);

    sprintf(point_str_src, "Altitude: %s %s\r\n\00", point->alt_val, point->alt_unit);
    strcat(point_str_dest, point_str_src);
}

void gps_point_print(struct gps_point* point)
{
    //
    // Function should probably call get_point_str(point) to reduce code duplication
    //
    //assert(point != NULL);

    printf("\r\nTime: \t\t%s UTC\r\n",point->utc_time);
    printf("Latitude: \t%s %s \nLongitude: \t%s %s\r\n", point->lat_val, point->lat_dir, point->long_val, point->long_dir);
    printf("Altitude: \t%s %s\r\n\r\n", point->alt_val, point->alt_unit);
}

char gps_point_valid(struct gps_point* point)
{
    if((*point->qual_code == '1' || *point->qual_code == '2' || *point->qual_code == '3') && *point->utc_time && *point->lat_val && *point->long_val && (*point->lat_dir == 'N' || *point->lat_dir == 'S') && (*point->long_dir == 'E' || *point->long_dir == 'W')){
    //if(*point->lat_val && *point->long_val && (*point->lat_dir == 'N' || *point->lat_dir == 'S') && (*point->long_dir == 'E' || *point->long_dir == 'W')){
    	return 1;
    }
    else
        return 0;
}




//*****************************************************************************
// PTD Driver
//*****************************************************************************

//*****************************************************************************
//
// Send a string to the UART.
//
//*****************************************************************************
void UARTSend0(const uint8_t *pui8Buffer, uint32_t ui32Count)
{
	for(;ui32Count>0;){
		if(UARTSpaceAvail(UART0_BASE)){
			ROM_UARTCharPutNonBlocking(UART0_BASE, *pui8Buffer++);
			ui32Count--;
		}
	}

    //
    // Loop while there are more characters to send.
    //
    /*while(ui32Count--)
    {
        //
        // Write the next character to the UART.
        //
        ROM_UARTCharPutNonBlocking(UART0_BASE, *pui8Buffer++);
    }*/
}

//*****************************************************************************
//
// The UART interrupt handler.
//
//*****************************************************************************
void UART0IntHandler(void)
{
    uint32_t ui32Status;

    //
    // Get the interrrupt status.
    //
    ui32Status = ROM_UARTIntStatus(UART0_BASE, true);

    //
    // Clear the asserted interrupts.
    //
    ROM_UARTIntClear(UART0_BASE, ui32Status);

    //
    // Loop while there are characters in the receive FIFO.
    //
    while(ROM_UARTCharsAvail(UART0_BASE))
    {
    	char temp_char = ROM_UARTCharGetNonBlocking(UART0_BASE);
    	if (uart_0_flag){
    		if(temp_char == '$'){
    			uart_0_flag = 0;
    			writeMemory(uart_0_buffer);
    			uart_0_pos = 0;
    			ROM_UARTCharPutNonBlocking(UART0_BASE, temp_char);
    			UARTSend0((uint8_t *)"\r\nCell Number Changed\r\nEnter Target Cell Number: ", 50);
    		}
    		else{
    		uart_0_buffer[uart_0_pos] = temp_char;
    		uart_0_pos = uart_0_pos + 1;
    		ROM_UARTCharPutNonBlocking(UART0_BASE, temp_char);
    		}
    	}
    	else if (temp_char == '$'){
    		uart_0_flag = 1;
    		ROM_UARTCharPutNonBlocking(UART0_BASE, temp_char);
    	}


        //
        // Read the next character from the UART and write it back to the UART.
        //
        //ROM_UARTCharPutNonBlocking(UART0_BASE, ROM_UARTCharGetNonBlocking(UART0_BASE));

        //
        // Blink the LED to show a character transfer is occuring.
        //
        //GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_2, GPIO_PIN_2);

        //
        // Delay for 1 millisecond.  Each SysCtlDelay is about 3 clocks.
        //
        //SysCtlDelay(SysCtlClockGet() / (1000 * 3));

        //
        // Turn off the LED
        //
        //GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_2, 0);

    }
}

void UART5IntHandler(void) // GPS MODULE connected to UART5
{
    //
    // function must fill gps_buffer_glob and send, and check if string is ready for gps point opperations.
    //
    uint32_t ui32Status;

    //
    // Get the interrrupt status.
    //
    ui32Status = ROM_UARTIntStatus(UART5_BASE, true);

    //
    // Clear the asserted interrupts.
    //

    ROM_UARTIntClear(UART5_BASE, ui32Status);
    ROM_IntDisable(INT_UART5); // disable interupts

    //
    // Loop while there are characters in the receive FIFO.
    //

    while(ROM_UARTCharsAvail(UART5_BASE))
    {
        //
        // Read the next character from the UART and write it back to the UART.
        //
        char letter = ROM_UARTCharGetNonBlocking(UART5_BASE);
        if(letter == '$')
        {
            if(gps_buffer_glob[2] == 'G'){ //Look for GPGGA
                gps_point_modify(&point, gps_buffer_glob, &split_fields);

                //get_point_str(&point, point_str_buffer);
                //get_point_str_debug(&point, point_str_buffer);

                if(gps_point_valid(&point))
                {
                	get_point_str(&point, point_str_buffer);
                	if( send_sms_msg(point_str_buffer) )
                	{
                		//while(1);
                		clear_point_str_buffer();
                		//gps_point_create(&point);
                		//gps_point_member_clear(&point);
                		ROM_IntDisable(INT_UART5);
                		ROM_UARTIntDisable(UART5_BASE, UART_INT_RX | UART_INT_RT);
                	}
                    /*char i = 0;
                    while(point_str_buffer[i] != '\0')
                    {
                        if(UARTSpaceAvail(UART7_BASE))
                        {
                            ROM_UARTCharPutNonBlocking(UART0_BASE, point_str_buffer[i]);
                            //ROM_UARTCharPutNonBlocking(UART7_BASE, point_str_buffer[i]);
                            ++i;
                        }
                    }*/
                }
                gps_point_create(&point);
                //gps_point_member_clear(&point);
                clear_gps_buffer_glob();
            }
            next_letter_loc = gps_buffer_glob;
        }
        else
        {
            *next_letter_loc = letter;
            ++next_letter_loc;
            ROM_IntEnable(INT_UART5); // re-enable interupts
            //SysCtlDelay(SysCtlClockGet() / (3000 * 3));
        }

        //ROM_UARTCharPutNonBlocking(UART5_BASE, letter);

        //
        // Blink the LED to show a character transfer is occuring.
        //
        //GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_2, GPIO_PIN_2);

        //
        // Delay for 1 millisecond.  Each SysCtlDelay is about 3 clocks.
        //
        //SysCtlDelay(SysCtlClockGet() / (1000 * 3));

        //
        // Turn off the LED
        //
        //GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_2, 0);
    }

    //ROM_IntEnable(INT_UART5); // re-enable interupts
}

void UART7IntHandler(void)
{
    uint32_t ui32Status;

    //
    // Get the interrrupt status.
    //
    ui32Status = ROM_UARTIntStatus(UART7_BASE, true);

    //
    // Clear the asserted interrupts.
    //
    ROM_UARTIntClear(UART7_BASE, ui32Status);

    //
    // Loop while there are characters in the receive FIFO.
    //
    while(ROM_UARTCharsAvail(UART7_BASE))
    {
        //
        // Read the next character from the UART and write it back to the UART.
        //
        //ROM_UARTCharPutNonBlocking(UART7_BASE, ROM_UARTCharGetNonBlocking(UART7_BASE));

    	cell_return_buffer[cell_return_buffer_loc] = ROM_UARTCharGetNonBlocking(UART7_BASE);

        //
        // Delay for 1 millisecond.  Each SysCtlDelay is about 3 clocks.
        //
        SysCtlDelay(SysCtlClockGet() / (1000 * 3));
    }
}


//*****************************************************************************
//
// This example demonstrates how to send a string of data to the UART.
//
//*****************************************************************************
int main(void){
    //
    // Enable lazy stacking for interrupt handlers.  This allows floating-point
    // instructions to be used within interrupt handlers, but at the expense of
    // extra stack usage.
    //
	int i,j,k,l;

    ROM_FPUEnable();
    ROM_FPULazyStackingEnable();

    //
    // Set the clocking to run directly from the crystal.
    //
    ROM_SysCtlClockSet(SYSCTL_SYSDIV_1 | SYSCTL_USE_OSC | SYSCTL_OSC_MAIN |
                       SYSCTL_XTAL_16MHZ);

    //
    // Enable the GPIO port that is used for the on-board LED.
    //
    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);

    //
    // Enable the GPIO pins for the LED (PF2).
    //
    ROM_GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_2);

    ROM_GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_1);
    ROM_GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_3);

    GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1, GPIO_PIN_1);

    //
    // Enable the peripherals used by this example.
    //
    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);


        ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_UART5);
        ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);

        ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_UART7);
        ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);

    //
    // Enable processor interrupts.
    //
    ROM_IntMasterEnable();

    //
    // Set GPIO A0 and A1 as UART pins.
    //
    GPIOPinConfigure(GPIO_PA0_U0RX);
    GPIOPinConfigure(GPIO_PA1_U0TX);
    ROM_GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);

        GPIOPinConfigure(GPIO_PE4_U5RX);
        GPIOPinConfigure(GPIO_PE5_U5TX);
        ROM_GPIOPinTypeUART(GPIO_PORTE_BASE, GPIO_PIN_4 | GPIO_PIN_5);

        GPIOPinConfigure(GPIO_PE0_U7RX);
        GPIOPinConfigure(GPIO_PE1_U7TX);
        ROM_GPIOPinTypeUART(GPIO_PORTE_BASE, GPIO_PIN_0 | GPIO_PIN_1);

    //
    // Configure the UART for 4800, 8-N-1 operation.
    //
    ROM_UARTConfigSetExpClk(UART0_BASE, ROM_SysCtlClockGet(), 4800,
                            (UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE |
                             UART_CONFIG_PAR_NONE));

    ROM_UARTConfigSetExpClk(UART5_BASE, ROM_SysCtlClockGet(), 4800,
                            (UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE |
                             UART_CONFIG_PAR_NONE));

    ROM_UARTConfigSetExpClk(UART7_BASE, ROM_SysCtlClockGet(), 4800,
                            (UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE |
                             UART_CONFIG_PAR_NONE));


    ROM_SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    ROM_GPIOPinTypeGPIOInput(GPIO_PORTB_BASE, GPIO_PIN_0);

    init_memory();

    cell_power_off();

    //
    // Prompt for text to be entered.
    //
    UARTSend0((uint8_t *)"\rEnter Target Cell Number: ", 28);

    // Setup Point
    gps_point_create(&point);
    gps_point_member_clear(&point);
    clear_point_str_buffer();

    //char temp1[12] = "14359949335\0";
    //writeMemory((int*)temp1);
    //readMemory((int*)cell_num_target);


    //
    // Enable the UART interrupt.
    //
    ROM_IntEnable(INT_UART0);
    //ROM_IntDisable(INT_UART0);
    ROM_UARTIntEnable(UART0_BASE, UART_INT_RX | UART_INT_RT);

    //ROM_IntEnable(INT_UART5);
    //ROM_UARTIntEnable(UART5_BASE, UART_INT_RX | UART_INT_RT);

    ROM_IntEnable(INT_UART7);
    ROM_UARTIntEnable(UART7_BASE, UART_INT_RX | UART_INT_RT);

    //
    // Loop forever echoing data through the UART.
    //
    while(1)
    {
    	//for(k=0;k<20;++k){
    		for(j=0;j<60;++j){ // Minute
    			for(i=0;i<1309022;++i); // One Second
    		}
    	//}

		//for(i=0;i<20000;++i){
		//	SysCtlDelay(SysCtlClockGet() / (1000 * 3));
		//}

    	gps_point_member_clear(&point);
    	ROM_IntEnable(INT_UART5);
    	ROM_UARTIntEnable(UART5_BASE, UART_INT_RX | UART_INT_RT);

    	//for(k=0;k<20;++k){
    		for(j=0;j<60;++j){ // Minute
    			for(i=0;i<1309022;++i); // One Second
    		}
    	//}

		//for(i=0;i<20000;++i){
		//	SysCtlDelay(SysCtlClockGet() / (1000 * 3));
		//}

    	//for(j=0;j<180;++j){ //1440 for 8 hours
    		//for(i=0;i<20000;++i){
    			//SysCtlDelay(SysCtlClockGet() / (1000 * 3));
    		//}
    	//}
    }
}
