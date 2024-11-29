/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <stdint.h>     /* C99 types */
#include <stdbool.h>    /* bool type */
#include <stdio.h>      /* printf fprintf sprintf fopen fputs */
#include <stdlib.h>

#include <string.h>
#include <mysql.h>
#include <pthread.h>
#include <math.h>
#include <sys/socket.h>     /* socket specific definitions */
#include <netinet/in.h>     /* INET constants and stuff */
#include <arpa/inet.h>      /* IP address conversion stuff */
#include "weather_device.h"

#define MSG(args...) fprintf(stderr, args) /* message that is destined to the user */

#define DATA_TO_LOG_FILE_ENABLE             0

#define WS_DATA_OBSOLETE_LEVEL_MIN           0
#define WS_DATA_OBSOLETE_LEVEL_MAX           10

#define WS_TX_BUFF_SIZE                      10000

#define SOCK_TIMEOUT_MS                     20 /* non critical for throughput */

#define DATABASE_SERVER                    "203.250.78.211"   /* hostname also supported */
#define DATABASE_PORT                      8888

struct ws_data_buffer_t {
    uint8_t     loraNodeID;         // ID of the weather station device
    uint8_t     data_ptr[WS_MAX_DATA_SIZE];   // raw data buffer
    uint8_t     data_len;
    uint8_t     data_obsolete_level;    // = 0 means data has been updated
                                        // = x (0 < x <=  WS_MAX_DATA_OBSOLETE_LEVEL) means data has not been updated for x parsing interval 
};

struct ws_meta_data_s {
    uint16_t    loraNodeId ;
    uint8_t     wdID;       // Weather device ID
    int         winddir;    // wind direction
    float       windspd_f;  // wind speed
    float       temp;       // temporature
    float       humi;       // humidity
    float       gas;
    float       gps_lat;
    float       gps_alt;
    
    // for temperature tracking
    int         temp_cnt;
    float       temp_past;
    int         data_obsolete_level; // indicate how much the data is obsoleted
                                     // This variable is reset to 0 when the ws_update_raw_data is called
    bool        data_valid;
} ;

typedef union {
    float f;
    struct
    {
        // Order is important.
        // Here the members of the union data structure
        // use the same memory (32 bits).
        // The ordering is taken
        // from the LSB to the MSB.
        unsigned int mantissa : 23;
        unsigned int exponent : 8;
        unsigned int sign : 1;
    } raw;
} myfloat;

extern FILE * log_file;

enum ws_error_e open_connection_to_app_server(void);

enum ws_error_e close_connection_to_app_server(void);

bool ws_open_data_log_file(void);

void ws_close_data_log_file(void);

static pthread_mutex_t mx_raw_data_access = PTHREAD_MUTEX_INITIALIZER; /* control access to the raw data table*/

/* raw data buffer */
static struct ws_data_buffer_t ws_data_buf[WS_NUMBER_OF_DEVICE];

static pthread_mutex_t mx_meta_data_access = PTHREAD_MUTEX_INITIALIZER; /* control access to the WS meta data */
/* WS meta data */
static struct ws_meta_data_s ws_meta_data[WS_NUMBER_OF_DEVICE];

static MYSQL * connector;

static unsigned int sql_connection_timeout = 7;

static int sock_db_server; /* socket for downstream traffic */

static struct sockaddr_in sock_db_address;

static struct timeval sock_timeout = {0, (SOCK_TIMEOUT_MS * 100)}; /* non critical for throughput */

#if DATA_TO_LOG_FILE_ENABLE
static time_t ws_data_log_time;
static time_t ws_data_log_start_time;
static FILE * ws_data_log_file = NULL;
#endif

static float gps_location[WS_NUMBER_OF_DEVICE][2] = {0}; // 0 is latitude, 1 is longitude

extern bool exit_sig;
extern bool quit_sig;

void ws_app_init(void) {
    int i;
    /* device data buffer init */
    pthread_mutex_lock(&mx_raw_data_access);
    memset(ws_data_buf, 0, sizeof(ws_data_buf));
    pthread_mutex_unlock(&mx_raw_data_access);
    
    pthread_mutex_lock(&mx_meta_data_access);
    for (i = 0; i < WS_NUMBER_OF_DEVICE; i++) {
        ws_meta_data[i].wdID = 0;
        ws_meta_data[i].winddir = 0;
        ws_meta_data[i].windspd_f = 0;
        ws_meta_data[i].temp = 0.0;
        ws_meta_data[i].humi = 0.0;
        ws_meta_data[i].temp_cnt = 0;
        ws_meta_data[i].temp_past = 0.0;
        ws_meta_data[i].data_obsolete_level = 0;
        ws_meta_data[i].data_valid = false;
    }
    pthread_mutex_unlock(&mx_meta_data_access);
    
    // Add gps value manually
    gps_location[1][0] = 35.542925;
    gps_location[1][1] = 129.254590;
    gps_location[2][0] = 35.542978;
    gps_location[2][1] = 129.255347;
    gps_location[3][0] = 35.542912;
    gps_location[3][1] = 129.255888;
    gps_location[4][0] = 35.545618;
    gps_location[4][1] = 129.254853;
    gps_location[5][0] = 35.543165;
    gps_location[5][1] = 129.256811;
    gps_location[6][0] = 35.544776;
    gps_location[6][1] = 129.256055;
    gps_location[7][0] = 35.543331;
    gps_location[7][1] = 129.257444;
    gps_location[8][0] = 35.543519;
    gps_location[8][1] = 129.257718;
    gps_location[9][0] = 35.543536;
    gps_location[9][1] = 129.254547;
    gps_location[10][0] = 35.543685;
    gps_location[10][1] = 129.255121;
    gps_location[11][0] = 35.543209;
    gps_location[11][1] = 129.255749;
    gps_location[12][0] = 35.543244;
    gps_location[12][1] = 129.256232;
    gps_location[13][0] = 35.546483;
    gps_location[13][1] = 129.255921;
    gps_location[14][0] = 35.543414;
    gps_location[14][1] = 129.257036;
    gps_location[15][0] = 35.543523;
    gps_location[15][1] = 129.257364;
    gps_location[16][0] = 35.543667;
    gps_location[16][1] = 129.257712;
    gps_location[17][0] = 35.544418;
    gps_location[17][1] = 129.254386;
    gps_location[18][0] = 35.544213;
    gps_location[18][1] = 129.255078;
    gps_location[19][0] = 35.544318;
    gps_location[19][1] = 129.255588;
    gps_location[20][0] = 35.544431;
    gps_location[20][1] = 129.256049;
    gps_location[21][0] = 35.544392;
    gps_location[21][1] = 129.256645;
    gps_location[22][0] = 35.544209;
    gps_location[22][1] = 129.257047;
    gps_location[23][0] = 35.544078;
    gps_location[23][1] = 129.257509;
    gps_location[24][0] = 35.544148;
    gps_location[24][1] = 129.257836;
    gps_location[25][0] = 35.544776;
    gps_location[25][1] = 129.254628;
    gps_location[26][0] = 35.544894;
    gps_location[26][1] = 129.255025;
    gps_location[27][0] = 35.544972;
    gps_location[27][1] = 129.255422;
    gps_location[28][0] = 35.544885;
    gps_location[28][1] = 129.255990;
    gps_location[29][0] = 35.544859;
    gps_location[29][1] = 129.256709;
    gps_location[30][0] = 35.544876;
    gps_location[30][1] = 129.257074;
    gps_location[31][0] = 35.544890;
    gps_location[31][1] = 129.257433;
    gps_location[32][0] = 35.544951;
    gps_location[32][1] = 129.257702;
    gps_location[33][0] = 35.545536;
    gps_location[33][1] = 129.254708;
    gps_location[34][0] = 35.545300;
    gps_location[34][1] = 129.254783;
    gps_location[35][0] = 35.545435;
    gps_location[35][1] = 129.255143;
    gps_location[36][0] = 35.545295;
    gps_location[36][1] = 129.255486;
    gps_location[37][0] = 35.545330;
    gps_location[37][1] = 129.256666;
    gps_location[38][0] = 35.545343;
    gps_location[38][1] = 129.257085;
    gps_location[39][0] = 35.545352;
    gps_location[39][1] = 129.257471;
    gps_location[40][0] = 35.545618;
    gps_location[40][1] = 129.257187;
    
    for(i = 41; i < WS_NUMBER_OF_DEVICE; i++){
        gps_location[i][0] = 0;
        gps_location[i][1] = 0;
    }
    
    ws_open_data_log_file();
    
    // testing
//    if(open_connection_to_app_server() != WS_OK){
//        exit_sig = 1;
//        //exit(EXIT_FAILURE);
//    }
    //close_connection_to_app_server();
}

void ws_app_deinit(void){
    close_connection_to_app_server();
    ws_close_data_log_file();
    //close_connection_to_app_server();
}

enum ws_error_e open_connection_to_app_server(void){
    int i;
    
    // Generate App socket
    sock_db_server = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_db_server == -1) {
        MSG("ERROR: open socket error!\n");
        fprintf(log_file, "ERROR: open socket error!\n");
        return WS_CONNECT_TO_SERVER_FAILED;
    }
    
    /* set upstream socket RX timeout */
    i = setsockopt(sock_db_server, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *)&sock_timeout, sizeof(struct timeval));
    if (i != 0) {
        MSG("ERROR: setsockopt error\n");
        fprintf(log_file, "ERROR: setsockopt error\n");
        return WS_CONNECT_TO_SERVER_FAILED;
    }
    
    /* connect to Database Server so we can send/receive packet with the server only */
    memset(&sock_db_address, 0, sizeof (sock_db_server));
    sock_db_address.sin_addr.s_addr = inet_addr(DATABASE_SERVER);
    sock_db_address.sin_family = AF_INET;
    sock_db_address.sin_port = htons(DATABASE_PORT);
    
    MSG("Connecting to the DB server (%s)...", inet_ntoa(sock_db_address.sin_addr));
    fprintf(log_file, "Connecting to the DB server (%s)...", inet_ntoa(sock_db_address.sin_addr));
    i = connect(sock_db_server, (struct sockaddr *) &sock_db_address, sizeof (sock_db_address));
    if (i != 0) {
        MSG("Failed!\n");
        fprintf(log_file, "Failed!\n");
        return WS_CONNECT_TO_SERVER_FAILED;
    } else {
        printf("Succeed!\n");
        fprintf(log_file, "Succeed!\n");
        return WS_OK;
    }
    
//    connector = mysql_init(NULL);
//    mysql_options(connector, MYSQL_OPT_CONNECT_TIMEOUT, (const char *) &sql_connection_timeout);
//    if (!mysql_real_connect(connector, "219.249.140.29", "lny", "tsei1234", "weather", 4000, NULL, 0)) 
//    {
//        fprintf(stderr, "%s\n", mysql_error(connector));
//        MSG("CONNECT TO APP SERVER FAILED\n");
//        return WS_CONNECT_TO_SERVER_FAILED;
//    } else {
//        MSG("CONNECT TO APP SERVER SUCCESSED\n");
//        return WS_OK;
//    }
}

enum ws_error_e close_connection_to_app_server(void){
    //mysql_close(connector);
    shutdown(sock_db_server, SHUT_RDWR);
    //close(sock_db_server);
    return WS_OK;
}

bool ws_open_data_log_file(void){
#if DATA_TO_LOG_FILE_ENABLE
    int i;
    char iso_date[20];
    
    char log_file_name[64];
    time(&ws_data_log_time);
    strftime(iso_date, sizeof(iso_date),"%Y%m%dT%H%M%SZ",localtime(&ws_data_log_time)); /* format yyyymmddThhmmssZ */
    ws_data_log_start_time = ws_data_log_time; /* keep track of when the log was started, for log rotation */

    sprintf(log_file_name, "data_%s.csv", iso_date);
    ws_data_log_file = fopen(log_file_name, "a"); /* create log file, append if file already exist */
    if (ws_data_log_file == NULL) {
        MSG("WS_ERROR: impossible to create log file %s\n", log_file_name);
        return false;
    }

    i = fprintf(ws_data_log_file, "START WS DATA LOG\n");
    if (i < 0) {
        MSG("WS_ERROR: impossible to write to log file %s\n", log_file_name);
        return false;
    }

    MSG("WS_INFO: Now writing to ws data log file %s\n", log_file_name);
    fprintf(ws_data_log_file, "TS Weather system v1.0\n");
    fprintf(ws_data_log_file, "UBICOM - UOU\n\n");
  
    fprintf(ws_data_log_file, "ID,LATI,LONGI,WINDIR,WINSPD\n");
//    fprintf(ws_data_log_file, "WsID\t\t\t(m/s)\t(`C)\t(%RH)\t(ppm)\t-\t-\n");
    return true;
#else
    return false;
#endif
    
}

void ws_close_data_log_file(void){
#if DATA_TO_LOG_FILE_ENABLE    
    fclose(ws_data_log_file);
#endif
}

enum ws_error_e ws_update_raw_data(uint16_t nodeAddr, uint8_t *buff, uint8_t buffLen) {
    uint16_t ws_index;

    if((buff == NULL))
        return WS_UPDATE_DATA_FAILED;
    
    ws_index = nodeAddr; // use nodeAddr as the index to save the raw ws data
    
    if (ws_index >= WS_NUMBER_OF_DEVICE)
        return WS_UPDATE_DATA_FAILED;
    
    pthread_mutex_lock(&mx_raw_data_access);
    ws_data_buf[ws_index].loraNodeID = nodeAddr;
    memset(ws_data_buf[ws_index].data_ptr, 0, sizeof(ws_data_buf[ws_index].data_ptr));
    memcpy(ws_data_buf[ws_index].data_ptr, buff, buffLen);
    ws_data_buf[ws_index].data_len = buffLen;
    ws_data_buf[ws_index].data_obsolete_level = WS_DATA_OBSOLETE_LEVEL_MIN; // indicating that data has been updated
    pthread_mutex_unlock(&mx_raw_data_access);
    
    return WS_OK;
}

// Function to convert a binary array
// to the corresponding integer
unsigned int convertToInt(int* arr, int low, int high)
{
    unsigned f = 0, i;
    for (i = high; i >= low; i--) {
        f = f + arr[i] * pow(2, high - i);
    }
    return f;
}

float convertToFloatingPoint(unsigned int value){
    // Get the 32-bit floating point number
    unsigned int ieee[32];
    
    for(int i = 31; i >= 0; i--){
        ieee[i] = value & 0x00000001;
        value = value >> 1;
    }
    
    myfloat var;
 
    // Convert the least significant
    // mantissa part (23 bits)
    // to corresponding decimal integer
    unsigned f = convertToInt(ieee, 9, 31);
 
    // Assign integer representation of mantissa
    var.raw.mantissa = f;
 
    // Convert the exponent part (8 bits)
    // to a corresponding decimal integer
    f = convertToInt(ieee, 1, 8);
 
    // Assign integer representation
    // of the exponent
    var.raw.exponent = f;
 
    // Assign sign bit
    var.raw.sign = ieee[0];
 
    return var.f;
}

enum ws_error_e ws_parse_data(time_t parse_time){
    uint8_t i;
    
    uint8_t temp1;
    uint8_t temp2;
    uint8_t temp3;
    uint8_t temp4;
    int result_int;
    float result_float;
    struct ws_meta_data_s temp_ws_meta_data;
    struct ws_data_buffer_t temp_ws_data_buf[WS_NUMBER_OF_DEVICE];

#if DATA_TO_LOG_FILE_ENABLE
    static int log_count = 0;   // for rotating log file if it is too long
    
    // Add time stamp to the data log file
    char iso_date[20];
    strftime(iso_date, sizeof(iso_date),"%Y%m%dT%H%M%S",localtime(&parse_time)); /* format yyyymmddThhmmssZ */
    fprintf(ws_data_log_file, "\nD%s\n", iso_date);
#endif
    
    // Copy raw data to a temp memory and unlock the mutex. Then the copied data will be processed
    memset(temp_ws_data_buf, 0, sizeof(temp_ws_data_buf));
    pthread_mutex_lock(&mx_raw_data_access);
    for (i = 0; i < WS_NUMBER_OF_DEVICE; i++){
        if((ws_data_buf[i].loraNodeID != 0) && (ws_data_buf[i].data_ptr[0] == 0x24)){
            temp_ws_data_buf[i].loraNodeID = ws_data_buf[i].loraNodeID;
            temp_ws_data_buf[i].data_len = ws_data_buf[i].data_len;
            
            memcpy(&temp_ws_data_buf[i].data_ptr, &ws_data_buf[i].data_ptr, WS_MAX_DATA_SIZE);
            
            temp_ws_data_buf[i].data_obsolete_level = ws_data_buf[i].data_obsolete_level;
            if(ws_data_buf[i].data_obsolete_level < WS_DATA_OBSOLETE_LEVEL_MAX)
                ws_data_buf[i].data_obsolete_level++; // up to this time, the data has been outdated
            
        }
    }
    pthread_mutex_unlock(&mx_raw_data_access);

    // parse data in the copied buffer
    for (i = 0; i < WS_NUMBER_OF_DEVICE; i++){
        if((temp_ws_data_buf[i].loraNodeID != 0) && (temp_ws_data_buf[i].data_ptr[0] == 0x24)){
            temp_ws_meta_data.loraNodeId = temp_ws_data_buf[i].loraNodeID;
            
            // do not parse the virtual weather data
            if(temp_ws_data_buf[i].data_ptr[1] == 255){
                //printf("NODE %u: Failed to communicate with control board\n", temp_ws_meta_data.loraNodeId);
                //fprintf(log_file, "NODE %u: Failed to communicate with control board\n", temp_ws_meta_data.loraNodeId);
                pthread_mutex_lock(&mx_meta_data_access);
                ws_meta_data[i].data_valid = false;
                pthread_mutex_unlock(&mx_meta_data_access);
                continue;
            }
                
            if(temp_ws_data_buf[i].data_ptr[1] > (WS_NUMBER_OF_DEVICE + 1))
                continue;
            
            temp_ws_meta_data.wdID = temp_ws_data_buf[i].data_ptr[1];
            
            temp1 = temp_ws_data_buf[i].data_ptr[2];
            temp2 = temp_ws_data_buf[i].data_ptr[3];
            
            result_int = (temp1 >> 4) * 4096 + (temp1 & 0x0F) * 256 + (temp2 >> 4) * 16 + (temp2 & 0x0F); 
            temp_ws_meta_data.winddir = result_int;
            
            temp1 = temp_ws_data_buf[i].data_ptr[4];
            temp2 = temp_ws_data_buf[i].data_ptr[5];
            result_int = (temp1 >> 4) * 4096 + (temp1 & 0x0F) * 256 + (temp2 >> 4) * 16 + (temp2 & 0x0F);
            result_float = result_int/10.0;
            temp_ws_meta_data.windspd_f = result_float;
            
            temp1 = temp_ws_data_buf[i].data_ptr[6];
            temp2 = temp_ws_data_buf[i].data_ptr[7];
            result_int = (temp1 >> 4) * 4096 + (temp1 & 0x0F) * 256 + (temp2 >> 4) * 16 + (temp2 & 0x0F);
            result_int -= 300;
            result_float = result_int/10.0;
            temp_ws_meta_data.temp = result_float;
            
            temp1 = temp_ws_data_buf[i].data_ptr[8];
            temp2 = temp_ws_data_buf[i].data_ptr[9];
            result_int = (temp1 >> 4) * 4096 + (temp1 & 0x0F) * 256 + (temp2 >> 4) * 16 + (temp2 & 0x0F);
            result_float = result_int/10.0;
            temp_ws_meta_data.humi = result_float;
            
            temp1 = temp_ws_data_buf[i].data_ptr[10];
            temp2 = temp_ws_data_buf[i].data_ptr[11];
            temp3 = temp_ws_data_buf[i].data_ptr[12];
            temp4 = temp_ws_data_buf[i].data_ptr[13];
            
            unsigned int temp_convert = (temp1 | (temp2 << 8) | (temp3 << 16) | (temp4 << 24));
            temp_ws_meta_data.gas = convertToFloatingPoint(temp_convert);         
            
            temp_ws_meta_data.gps_lat = gps_location[temp_ws_meta_data.wdID][0];
            temp_ws_meta_data.gps_alt = gps_location[temp_ws_meta_data.wdID][1];
            
            temp_ws_meta_data.data_valid = true;
            
            temp_ws_meta_data.data_obsolete_level = temp_ws_data_buf[i].data_obsolete_level;
            
            // Now copy the parse data to the meta data buffer
            pthread_mutex_lock(&mx_meta_data_access);
            memcpy(&ws_meta_data[i], &temp_ws_meta_data, sizeof(struct ws_meta_data_s));
            pthread_mutex_unlock(&mx_meta_data_access);
        }
    }
    
#if DATA_TO_LOG_FILE_ENABLE
    pthread_mutex_lock(&mx_meta_data_access);
    for(i = 0; i < WS_NUMBER_OF_DEVICE; i++){
        if((ws_meta_data[i].loraNodeId != 0) && (ws_meta_data[i].wdID != 255)){
            if(ws_meta_data[i].data_valid == true && ws_meta_data[i].data_obsolete_level < WS_DATA_OBSOLETE_LEVEL_MAX){
                fprintf(ws_data_log_file,"%u,",ws_meta_data[i].wdID);
                fprintf(ws_data_log_file,"%.6f,%.6f,", ws_meta_data[i].gps_lat, ws_meta_data[i].gps_alt);
                fprintf(ws_data_log_file,"%d,%.2f\n", ws_meta_data[i].winddir, ws_meta_data[i].windspd_f);
                
            }
        }
    }
    pthread_mutex_unlock(&mx_meta_data_access);

    log_count++;
    if(log_count == 1000){
        fclose(ws_data_log_file);
        ws_open_data_log_file();
        log_count = 0;
    }
#endif
    
    return WS_OK;
}

void ws_print_meta_data(void) {
#if WS_PRINT_META_DATA == 1
    int i, count;
    struct ws_meta_data_s temp_ws_meta_data[WS_NUMBER_OF_DEVICE];
    
    memset(temp_ws_meta_data, 0, sizeof(temp_ws_meta_data));
    pthread_mutex_lock(&mx_meta_data_access);
    for (i = 0; i < WS_NUMBER_OF_DEVICE; i++){
        if (ws_meta_data[i].loraNodeId != 0){
            memcpy(&temp_ws_meta_data[i], &ws_meta_data[i], sizeof(struct ws_meta_data_s));
        }
    }
    pthread_mutex_unlock(&mx_meta_data_access);
    
    printf("\n\n#################################################\n");
    printf("                       TS Weather system v1.0     \n");
    printf("                           UBICOM - UOU         \n\n");

    printf("\t\tWINDIR\tWINSPD\tTEMP\tHUMI\tGAS\tLAT\tALT\n");
    printf("LoRaID\tWsID\t(`N)\t(m/s)\t(`C)\t(RH)\t(ppm)\t-\t-\n");
    count = 0;
    for (i = 0; i < WS_NUMBER_OF_DEVICE; i++) {
        if(temp_ws_meta_data[i].loraNodeId != 0){
            count++;
            if(temp_ws_meta_data[i].data_valid == true){
                if(temp_ws_meta_data[i].data_obsolete_level < WS_DATA_OBSOLETE_LEVEL_MAX){
                    printf("%hu\t%hhu", temp_ws_meta_data[i].loraNodeId, temp_ws_meta_data[i].wdID);
                    printf("\t%d\t%.2f", temp_ws_meta_data[i].winddir, temp_ws_meta_data[i].windspd_f);
                    printf("\t%.2f\t%.2f",temp_ws_meta_data[i].temp, temp_ws_meta_data[i].humi);
                    printf("\t%.2f\t%.2f\t%.2f\n", temp_ws_meta_data[i].gas, temp_ws_meta_data[i].gps_lat, temp_ws_meta_data[i].gps_alt);
                } else
                    printf("%hu\t%hhu - Data outdated\n", temp_ws_meta_data[i].loraNodeId, temp_ws_meta_data[i].wdID);
            } else {
                printf("%hu\t%hhu - Data invalid\n", temp_ws_meta_data[i].loraNodeId, temp_ws_meta_data[i].wdID);
            }
        }
    }
    printf("\n######################## TOTAL DEVICES: %d ########\n", count);
    system("date");
#endif
}

enum ws_error_e ws_push_data_to_server(void){
#if WS_PUSH_DATA_TO_SERVER == 1
    int i, j;
    struct ws_meta_data_s temp_ws_meta_data[WS_NUMBER_OF_DEVICE];
    
    /* data buffers */
    uint8_t buff_up[WS_TX_BUFF_SIZE]; /* buffer to compose the upstream packet */
    int buff_index;
    unsigned int msg_start_index;   // These variables to keep track of the starting index 
                                    // of the current msg in buff_up
    
    int pkt_in_dgram;
    
    //if(open_connection_to_app_server() != WS_OK){
    //    return WS_CONNECT_TO_SERVER_FAILED;
    //}
    
//    if(connector == NULL){
//        MSG("Failed to connect to APP server...\n");
//        return WS_CONNECT_TO_SERVER_FAILED;
//    }
    memset(temp_ws_meta_data, 0, sizeof(temp_ws_meta_data));
    pthread_mutex_lock(&mx_meta_data_access);
    for (i = 0; i < WS_NUMBER_OF_DEVICE; i++){
        if ((ws_meta_data[i].loraNodeId != 0) && (ws_meta_data[i].wdID != 255)){
            if((ws_meta_data[i].data_valid == true) && (ws_meta_data[i].data_obsolete_level < WS_DATA_OBSOLETE_LEVEL_MAX)){
                memcpy(&temp_ws_meta_data[i], &ws_meta_data[i], sizeof(struct ws_meta_data_s));
            }
        }
    }
    pthread_mutex_unlock(&mx_meta_data_access);
    
    buff_index = 0;
    /* start of JSON structure */
//    memcpy((void *)(buff_up + buff_index), (void *)"{\"rxdt\":[", 9);
//    buff_index += 9;
    memcpy((void *)(buff_up + buff_index), (void *)"[", 1);
    buff_index += 1;
    /* Push sensor data to the server */
    /* Serialize data packets */
    pkt_in_dgram = 0;
    for (i = 0; i < WS_NUMBER_OF_DEVICE; i++) {
        if ((temp_ws_meta_data[i].loraNodeId != 0) && (temp_ws_meta_data[i].wdID != 255)){
            // This is the new implementation, the data is sent to the server via socket connection
            // The data is packed as a JSON object
            /* Start of packet, add inter-packet separator if necessary */
            msg_start_index = buff_index;      
            if (pkt_in_dgram == 0) {
                buff_up[buff_index] = '{';
                ++buff_index;
            } else {
                buff_up[buff_index] = ',';
                buff_up[buff_index+1] = '{';
                buff_index += 2;
            }
            
            /* Weather station ID */
            j = snprintf((char *)(buff_up + buff_index), WS_TX_BUFF_SIZE-buff_index, "\"id\":%u", temp_ws_meta_data[i].wdID);
            if (j > 0) {
                buff_index += j;
            } else {
                MSG("ERROR: [up] snprintf failed line %u\n", (__LINE__ - 4));
                buff_index = msg_start_index;   // point to start index of the message
                continue;
            }
            
            /* Wind direction, wind speed */
            j = snprintf((char *)(buff_up + buff_index), WS_TX_BUFF_SIZE-buff_index, ",\"wd\":%d,\"ws\":%.2f", \
                    temp_ws_meta_data[i].winddir, temp_ws_meta_data[i].windspd_f);
            if (j > 0) {
                buff_index += j;
            } else {
                MSG("ERROR: [up] snprintf failed line %u\n", (__LINE__ - 4));
                buff_index = msg_start_index;   // point to start index of the message
                continue;
            }
            
            /* Temp and humi*/
            j = snprintf((char *)(buff_up + buff_index), WS_TX_BUFF_SIZE-buff_index, ",\"tp\":%.2f,\"hm\":%.2f", \
                    temp_ws_meta_data[i].temp, temp_ws_meta_data[i].humi);
            if (j > 0) {
                buff_index += j;
            } else {
                MSG("ERROR: [up] snprintf failed line %u\n", (__LINE__ - 4));
                buff_index = msg_start_index;   // point to start index of the message
                continue;
            }
            
            /* GPS lat, lon */
            j = snprintf((char *)(buff_up + buff_index), WS_TX_BUFF_SIZE-buff_index, ",\"lat\":%.6f,\"lon\":%.6f", \
                    temp_ws_meta_data[i].gps_lat, temp_ws_meta_data[i].gps_alt);
            if (j > 0) {
                buff_index += j;
            } else {
                MSG("ERROR: [up] snprintf failed line %u\n", (__LINE__ - 4));
                buff_index = msg_start_index;   // point to start index of the message
                continue;
            }
            
            /* Ou*/
            j = snprintf((char *)(buff_up + buff_index), WS_TX_BUFF_SIZE-buff_index, ",\"ou\":%d", 0);
            if (j > 0) {
                buff_index += j;
            } else {
                MSG("ERROR: [up] snprintf failed line %u\n", (__LINE__ - 4));
                buff_index = msg_start_index;   // point to start index of the message
                continue;
            }

            /* End of packet serialization */
            buff_up[buff_index] = '}';
            ++buff_index;
            ++pkt_in_dgram;
            
            // This is the old implementation, the data is inserted to the mysql database directly
//            char Data[1024] = {'\0',};
//            sprintf(Data, "insert into weather.weather_sensor2 values ( (select ifnull(max(m_id),0)+1 from weather_sensor2 as a),'WT_%02d',%d,%.2f,%.1f,%.2f,%.2f, 0.0,0.0,now())",\
//            temp_ws_meta_data[i].wdID, temp_ws_meta_data[i].winddir, temp_ws_meta_data[i].windspd_f, temp_ws_meta_data[i].temp, \
//                    temp_ws_meta_data[i].humi, temp_ws_meta_data[i].gas);
//            //            MSG("%s\n", Data);
//            if (mysql_query(connector, Data)){
//                fprintf(stderr, "%s\n", mysql_error(connector));
//            }
        }
    }
    /* restart fetch sequence without sending empty JSON if all packets have been filtered out */
    if (pkt_in_dgram == 0) {
       /* all packet have been filtered out and no report, restart loop */
       return WS_UPDATE_DATA_FAILED;
    } else {
       /* end of packet array */
       buff_up[buff_index] = ']';
       ++buff_index;
    }
    
    /* end of JSON datagram payload */
//    buff_up[buff_index] = '}';
//    ++buff_index;
    
    buff_up[buff_index] = '\n';
    ++buff_index;
    
    buff_up[buff_index] = 0; /* add string terminator, for safety */
    
    //printf("\nJSON up: %s\n", (char *)(buff_up)); /* DEBUG: display JSON payload */
    
    /* send datagram to server */
    send(sock_db_server, (void *)buff_up, buff_index, 0);
    MSG("DATA -> SEVER: OK\n");
    fprintf(log_file, "DATA -> SEVER: OK\n");
    
    //close_connection_to_app_server();
    
    return WS_OK;
#else
    MSG("INFO: PUSH_WS_DATA_TO_SERVER undefined\n");
    return WS_OK;
#endif
}

