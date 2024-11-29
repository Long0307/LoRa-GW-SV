/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   weather_device.h
 * Author: LAM-HOANG
 *
 * Created on July 20, 2021, 9:34 PM
 */



#ifndef WEATHER_DEVICE_H
#define WEATHER_DEVICE_H

#ifdef __cplusplus
extern "C" {
#endif

    
/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */
    
/* -------------------------------------------------------------------------- */
/* --- DEPENDANCIES --------------------------------------------------------- */
#include <stdint.h>     /* C99 types */
#include <stdbool.h>    /* bool type */
    
/* -------------------------------------------------------------------------- */
/* --- PUBLIC CONSTANTS ----------------------------------------------------- */
    
#define WS_PUSH_DATA_TO_SERVER          1
#define WS_PRINT_META_DATA              1
    
#define WS_NUMBER_OF_DEVICE             65
#define WS_MAX_DATA_SIZE                30

/* -------------------------------------------------------------------------- */
/* --- PUBLIC TYPES AND VARIABLES ------------------------------------------- */
enum ws_error_e {
    WS_OK,
    WS_CONNECT_TO_SERVER_FAILED,
    WS_UPDATE_DATA_FAILED
};

/* -------------------------------------------------------------------------- */
/* --- PUBLIC FUNCTIONS PROTOTYPES ------------------------------------------ */
void ws_app_init(void);

//enum ws_error_e ws_update_raw_data(struct ws_data_buffer_t *buff);
enum ws_error_e ws_update_raw_data(uint16_t nodeAddr, uint8_t *buff, uint8_t buffLen) ;

enum ws_error_e ws_parse_data(time_t parse_time);

void ws_print_meta_data(void);

enum ws_error_e ws_push_data_to_server(void);

#ifdef __cplusplus
}
#endif

#endif /* WEATHER_DEVICE_H */

