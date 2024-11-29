/*
 * Description: Two-hop RT-LoRa server source file
 * Author: Quy Lam Hoang
 * Email: quylam925@gmail.com
 * Created: 2021/05/24
 * Git long-run branch: master, develope
 */

#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <string.h>
#include <memory.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <fcntl.h>

#include <pthread.h>
#include <sys/time.h>

#include "rtlora_mac.h"
#include "device_management.h"
#include "application.h"
#include "conf.h"
#include "time_conversion.h"
#include "base64.h"
#include "parson.h"
// For Debug..
#include "debug.h"
#include "device_mngt.h"
#include "packet_queue.h"
#include "trade.h"

#define PKT_TIMESYNC_REQ        0 // gw -> sv
#define PKT_TIMESYNC_RES        1 // sv -> gw
#define PKT_DOWNLINK_DATA       2 // sv -> gw
#define PKT_DOWNLINK_ACK        3 // gw -> sv
#define PKT_UPLINK_DATA         4 // gw -> sv
#define PKT_UPLINK_ACK          5 // sv -> gw

#define DOWNSTREAM_BUF_SIZE     1024

#if defined (LOG)
int logfd; // LOG File Descriptor
char logmsg[512];
#endif

/* signal handling variables */
volatile bool exit_sig = false; /* 1 -> application terminates cleanly (shut down hardware, close open files, etc) */
volatile bool quit_sig = false; /* 1 -> application terminates without shutting down the hardware */

extern pthread_cond_t condTxMsg;
extern pthread_cond_t condRxMsg;
extern pthread_mutex_t mutexTxMsg;
extern pthread_mutex_t mutexRxMsg;
extern _Bool flagTxMsg;   // flag indicates it has a packet to transmit
extern _Bool flagRxMsg;   // flag indicates it receives a packet

extern struct PktQueue inboundMsgQueue;
extern struct PktQueue outboundMsgQueue;

extern pthread_mutex_t mutexPhaseTrans;
extern bool phaseTransRequest;

/* clock and log file management */
extern time_t log_start_time;
extern FILE * log_file;
extern time_t log_time;
extern pthread_mutex_t mutexLogFile;

int guwbsocket;

static int fdmax, fdnum;
static fd_set reads, cp_reads;
    
static int server_socket; // LoRa network server socket

/* gateway <-> MAC protocol variables */
static uint32_t net_mac_h; /* Most Significant Nibble, network order */
static uint32_t net_mac_l; /* Least Significant Nibble, network order */

/* MAC remotely configurable parameters */
extern int mac_frame_factor;
extern int mac_ul_slot_size_ms;
extern int mac_dl_slot_size_ms;
extern int mac_nbo_channels;

/* threads */
void thread_inputstream(void); // process input data from terminal or from socket
void thread_downstream(void); // beaconing and downstream messages 

void set_signal(void);

/* --- PRIVATE FUNCTIONS DEFINITION ----------------------------------------- */

/* This implementation is POSIX-pecific and require a fix to be compatible with C99 */
void wait_ms(unsigned long a) {
    struct timespec dly;
    struct timespec rem;

    dly.tv_sec = a / 1000;
    dly.tv_nsec = ((long) a % 1000) * 1000000;

    if ((dly.tv_sec > 0) || ((dly.tv_sec == 0) && (dly.tv_nsec > 100000))) {
        clock_nanosleep(CLOCK_MONOTONIC, 0, &dly, &rem);
    }
    return;
}

/******************************************************************************
 * Function Name        : main
 * Input Parameters     : Network Parameters ..
 * Return Value         : none
 * Function Description : Signal handling function
 ******************************************************************************/
int main(int argc, char* argv[]) {

    int i;
    /* clock and log rotation management */
    int log_rotate_interval = 7200; /* by default, rotation every hour */
    int time_check = 0; /* variable used to limit the number of calls to time() function */
    
    /* threads */
    pthread_t thrid_input;
    pthread_t thrid_downstream;

    // 0. Initialization .. Handling SIGPIPE
    set_signal();
    
    printf("[ LoRa Network Server ]\n");
    
    /* Parse command line options */   
    int c;
    while((c = getopt(argc, argv, "n:u:d:c:h")) != -1){
    	switch(c){
            case 'n': // frame factor N
                mac_frame_factor = atoi(optarg);
                if(mac_frame_factor < 1 || mac_frame_factor > 7){
                    printf("Frame factor 'n' must greater than 0 and less than 8!\n");
                    exit(0);
                }
                break;
            case 'u': // Uplink slot size
                mac_ul_slot_size_ms = atoi(optarg);
                if(mac_ul_slot_size_ms < 30 || mac_ul_slot_size_ms > 310 || (mac_ul_slot_size_ms%10 != 0)){
                    printf("Uplink slot size 'u' must greater than 20 and less than 320.\n");
                    printf("It should be divisible by 10.\n");
                    exit(0);
                }
                break;
            case 'd': // Downlink slot size
                mac_dl_slot_size_ms = atoi(optarg);
                if(mac_dl_slot_size_ms < 30 || mac_dl_slot_size_ms > 310 || (mac_dl_slot_size_ms%10 != 0)){
                    printf("Downlink slot size 'd' must greater than 20 and less than 320.\n");
                    printf("It should be divisible by 10.\n");
                    exit(0);
                }
                break;
            case 'c':
                mac_nbo_channels = atoi(optarg);
                if(mac_nbo_channels < 1 || mac_nbo_channels > 7){
                    printf("Number of channels 'c' must greater than 0 and less than 8!\n");
                    exit(0);
                }
                break;
            case 'h':
                printf("\n");
                printf("***********************************************************\n");
                printf("* This is Private LoRa Network Server Program (Infomation)*\n");
                printf("*  - Current working port : %4d                          *\n", LORA_NETWORK_WELCOME_SERVER_PORT);
                printf("*  - Version              : %-10s                    *\n", LORA_NETWORK_SERVER_VERSION);
                printf("***********************************************************\n");
                printf("\n");
                
                printf("\n--- PARAMETERS REMOTE CONFIGURATION OPTIONS  ---\n");
                printf("\nSynopsis: ./lora_network_server [OPTION] [VALUE] ...\n");
                printf("\nOPTIONS:\n");
                printf("\t-n\tMAC frame factor. VALUE ranges from 1 to 7.\n");
                printf("\t\tDefault value is %u.\n\n", mac_frame_factor);
                
                printf("\t-u\tMAC uplink slot size in milliseconds. VALUE ranges from 30 to 310.\n");
                printf("\t\tMust be divisible by 10.\n");
                printf("\t\tDefault value is %u.\n\n", mac_ul_slot_size_ms);
                
                printf("\t-d\tMAC downlink slot size in milliseconds. VALUE ranges from 30 to 310.\n");
                printf("\t\tMust be divisible by 10.\n");
                printf("\t\tDefault value is %u.\n\n", mac_dl_slot_size_ms);
                
                printf("\t-c\tMAC number of channels. VALUE ranges from 1 to 7.\n");
                printf("\t\tNOTE: It must be compatible with the number of enabled channels in the gateway,\n");
                printf("\t\tconfigured in the 'global_conf.json' file.\n");
                printf("\t\tDefault value is %u.\n\n", mac_nbo_channels);
                
                printf("\nEXAMPLES:\n");
                printf("\t./lora_network_server -n 6 -u 150 -d 300 -c 2\n\n");
                printf("\tWill set the MAC parameters as follows:\n");
                printf("\t\tFrame factor: 6\n");
                printf("\t\tUplink slot size: 150 ms\n");
                printf("\t\tDownlink slot size: 300 ms\n");
                printf("\t\tNumber of channels: 2\n");
                exit(0);
                break;
            default:
                abort();
    	}
    }
    
    printf("\nRTLoRa MAC configuration parameters:\n");
    printf("\tFrame factor N: %d\n", mac_frame_factor);
    printf("\tUplink slot size: %d ms\n", mac_ul_slot_size_ms);
    printf("\tDownlink slot size: %d ms\n", mac_dl_slot_size_ms);
    printf("\tNumber of channels: %d\n", mac_nbo_channels);
    printf("\n\n");
    
    /* opening log file and writing CSV header*/
    pthread_mutex_lock(&mutexLogFile);
    time(&log_time);
    if(open_log() == false){
        MSG("Unable to open log file. Exit\n");
        pthread_mutex_unlock(&mutexLogFile);
        exit(EXIT_FAILURE);
    }
    pthread_mutex_unlock(&mutexLogFile);
    
    // Init some variables
    flagTxMsg = false;
    flagRxMsg = false;
    
    twohopLoRaMacInit();
    InitGateWayInfo();
    InitEndDeviceInfo();
    InitApplication();

    FD_ZERO(&reads);
    FD_SET(0, &reads); // STDIN
    
    /* process some of the configuration variables */
    net_mac_h = htonl((uint32_t)(0xFFFFFFFF));
    net_mac_l = htonl((uint32_t)(0xFFFFFFFF));
    
    /* spawn threads to manage upstream and downstream */
    i = pthread_create(&thrid_input, NULL, (void * (*)(void *))thread_inputstream, NULL);
    if (i != 0) {
        printf("ERROR: [main] impossible to create upstream thread\n");
        exit(EXIT_FAILURE);
    }

    i = pthread_create(&thrid_downstream, NULL, (void * (*)(void *))thread_downstream, NULL);
    if (i != 0) {
        printf("ERROR: [main] impossible to create downstream thread\n");
        exit(EXIT_FAILURE);
    }

#if defined (LOG)
    logfd = open("./log.txt", O_RDWR | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH);
    if (logfd == -1) {
        printf("Can not open log file..\n");
        exit(1);
    }
#endif
    
    // main while loop
    while (!exit_sig && !quit_sig) {
        sleep(1);
        ++time_check;
        // Handle application data
//        if((time_check % 8) == 0){
//            AppParseData();
////            AppPushDataToAppServer();
//        }
//        
//        if((time_check % 20) == 0){
//            AppDisplayData();
//        }
        
        /* check time and rotate log file if necessary */
        if (time_check >= 60) {
            pthread_mutex_lock(&mutexLogFile);
            time_check = 0;
            time(&log_time);
            if (difftime(log_time, log_start_time) > log_rotate_interval) {
                fclose(log_file);
                open_log();
            }
            pthread_mutex_unlock(&mutexLogFile);
        }
    }
    /* wait for upstream thread to finish (1 fetch cycle max) */
    twohopLoRaMacDeInit();
    DeInitApplication();
//    pthread_join(thrid_input, NULL);
//    pthread_join(thrid_downstream, NULL); /* don't wait for downstream thread */
    pthread_cancel(thrid_input);
    pthread_cancel(thrid_downstream);
    
    printf("End of program\n");
    fprintf(log_file, "End of program\n");
    
    pthread_mutex_lock(&mutexLogFile);
    fclose(log_file);
    pthread_mutex_unlock(&mutexLogFile);
   
    return 1;
}

void terminal_input_handle(int fd, uint8_t* buff, int buff_len) {
    dprintf("%s", buff);
    if (buff[0] == 'x') {
        DeinitGateWayInfo();
        DeinitEndDeviceInfo();
        DeInitApplication();
        close(server_socket);
#if defined (LOG)
        close(logfd);
#endif
        exit(0);
    } else if (buff[0] == 'd') {
        ShowEndDevices();
    } else if (buff[0] == 'g') {
        ShowGateWays();
    } else if ((buff[0] == 'P') && (buff[1] == 'T')) {   // Receive phase transition request
        MSG("[SERVER] Receive Phase Transition Request from user\n");
        pthread_mutex_lock(&mutexPhaseTrans);
        phaseTransRequest = true;
        pthread_mutex_unlock(&mutexPhaseTrans);
    }

    return;
}

int new_gw_connection_handle(int server_socket) {
    struct sockaddr_in client_address; // Gateway address
    static int client_socket; // LoRa network server socket
    unsigned int client_size;

    memset(&client_address, 0, sizeof (client_address));
    client_size = sizeof (client_address);
    client_socket = accept(server_socket, (struct sockaddr*) &client_address, &client_size);
    printf("Connecting New Gateway(%d): %s\n", client_socket, inet_ntoa(client_address.sin_addr));
    //    printf("Server (%d): %s\n", server_socket, inet_ntoa(server_address.sin_addr));

    // Call LoRa management function here for first connection..
    // Register Lora Gateway to Management List
    AddGateWay(client_socket, client_address);
#if defined (LOG)
    sprintf(logmsg, "ADD NEW GATEWAY = %d, %s\n", clntsocket, inet_ntoa(clntAddress.sin_addr));
    write(logfd, logmsg, strlen(logmsg));
#endif
    return client_socket;
}

/******************************************************************************
 * Function Name        : ReceiveFrameFromLoRaGW
 * Input Parameters     : uint8_t *rxFrame - Received Frame Data
 *                      : int size         - Received Frame Size
 *                      : int gwSocket     - Received GW socket
 * Return Value         : None
 * Function Description : Receive LoRa Frame from Gateway
 ******************************************************************************/
void receive_frame_from_gateway(uint8_t *rxFrame, int size, int gwSocket) {
    uint32_t loopCnt;
    uint32_t findStartOfFrame = 0;
    uint32_t remainingBufferSize = 0;
    uint32_t rxFrameSize = 0;
    GateWayInfo_t *gatewayInfo;
    gatewayInfo = FindGateWay(gwSocket);

    if (gatewayInfo == NULL) {
        dprintf("Gateway information is not vaild.. Ingnoring rx frame\n");
        return;
    }

    // Check Buffer overflow..
    if ((gatewayInfo->currentRxBufferSize + (uint32_t) size) > TCP_STREAM_BUFFER_SIZE) {
        dprintf("Gateway Rx Frame Buffer is overflowed..Ignoring old buffer data\n");
        // clear old buffer..
        gatewayInfo->currentRxBufferSize = 0;
    }

    // concatenate previous remaining data and new arrived data
    memcpy(&(gatewayInfo->rxBuffer[gatewayInfo->currentRxBufferSize]), rxFrame, size);
    gatewayInfo->currentRxBufferSize += size;

    // We need 2 bytes at least .. SOF, SIZE
    while (gatewayInfo->currentRxBufferSize > 2) {
        // Check Start of Frame..
        if (gatewayInfo->rxBuffer[0] != START_OF_FRAME) {
            // Ignored rx data until meeting SOF
            for (loopCnt = 0; loopCnt < gatewayInfo->currentRxBufferSize; loopCnt++) {
                if (gatewayInfo->rxBuffer[loopCnt] == START_OF_FRAME) {
                    findStartOfFrame = loopCnt;
                    remainingBufferSize = gatewayInfo->currentRxBufferSize - loopCnt;
                    break;
                }
            }
            // Move remaining data to start of buffer
            if (findStartOfFrame != 0) {
                memmove(gatewayInfo->rxBuffer, &(gatewayInfo->rxBuffer[findStartOfFrame]), remainingBufferSize);
                gatewayInfo->currentRxBufferSize = remainingBufferSize;
                findStartOfFrame = 0;
            } else {
                // Cannot find START_OF_FRAME.. clear Buffer
                gatewayInfo->currentRxBufferSize = 0;
            }
        } else {

            rxFrameSize = gatewayInfo->rxBuffer[1] + RX_TUNNELING_OVERHEAD;
            // Check Size..
            if (rxFrameSize <= gatewayInfo->currentRxBufferSize) {
                // Check End of Frame
                //if (gatewayInfo->rxBuffer[gatewayInfo->currentRxBufferSize-1] == END_OF_FRAME)
                if (gatewayInfo->rxBuffer[rxFrameSize - 1] == END_OF_FRAME) {

                    // Parsing LoRa Frame.. Here..
                    NetworkServerReceiveFrame(gatewayInfo->rxBuffer, gwSocket);

                    // Remaining Part..
                    remainingBufferSize = gatewayInfo->currentRxBufferSize - rxFrameSize;
                    memmove(gatewayInfo->rxBuffer, &(gatewayInfo->rxBuffer[gatewayInfo->currentRxBufferSize]), remainingBufferSize);
                    gatewayInfo->currentRxBufferSize = remainingBufferSize;
                } else {
                    // Find new SOF
                    gatewayInfo->rxBuffer[0] = 0;
                }
            } else {
                // need more data stream..
                return;
            }
        }
    }

    // Too small rx buffer..
    return;
}

void upstream_data_handle(int sock, uint8_t* buff, int buff_len) {
    int i; /* loop variables */
    int payload_len;
    
    /* protocol variables */
    uint8_t token_h; /* random token for acknowledgement matching */
    uint8_t token_l; /* random token for acknowledgement matching */
    int j;
    uint8_t buff_out[512];
    int buff_out_len;
    struct timeval buff_timeval = {0, 0};  
    struct MsgInfo_ ulMsg;
    
    /* JSON parsing variables */
    JSON_Value *root_val = NULL;
    JSON_Object *rxpk_obj = NULL;
    JSON_Array *rxpk_arr = NULL;
    int arr_len;
    JSON_Value *val = NULL; /* needed to detect the absence of some fields */
    const char *str; /* pointer to sub-strings in the JSON data */
    short x0, x1;
    
//    dprintf("[%d/%d] : ", sock, buff_len);
//    for (j = 0; j < buff_len; j++)
//        dprintfc("%02x ", buff[j]);
//    dprintfc("\n");

    /* if the datagram does not respect protocol, just ignore it */
    if ((buff_len < 12) || (buff[0] != PROTOCOL_VERSION) || ((buff[3] != PKT_TIMESYNC_REQ) \
                                            && (buff[3] != PKT_UPLINK_DATA))) {
        printf("WARNING: ignoring invalid packet len=%d, protocol_version=%hhu, id=%hhu\n",
                buff_len, buff[0], buff[3]);
        fprintf(log_file, "WARNING: ignoring invalid packet len=%d, protocol_version=%hhu, id=%hhu\n",
                buff_len, buff[0], buff[3]);
        return;
    }

    memset(buff_out, 0, sizeof (buff_out));

    /* pre-fill the out buffer with fixed fields */
    buff_out[0] = PROTOCOL_VERSION;
    buff_out_len = 0;

    switch (buff[3]) {
        case PKT_TIMESYNC_REQ:
            printf("Received TIMESYNC_REQ from GW (sock %d)\n", sock);
            /* Get receiving timestamp */
            gettimeofday(&buff_timeval, NULL);
            buff_len = 0;
            /* respond timesync */
            buff_out[1] = buff[1];
            buff_out[2] = buff[2];
            buff_out[3] = PKT_TIMESYNC_RES;
            buff_out_len += 4;
            *(uint32_t *) (buff_out + 4) = buff_timeval.tv_sec;
            *(uint32_t *) (buff_out + 8) = buff_timeval.tv_usec;
            buff_out_len += 8;
            /* Get transmitting timestamp */
            gettimeofday(&buff_timeval, NULL);
            *(uint32_t *) (buff_out + 12) = buff_timeval.tv_sec;
            *(uint32_t *) (buff_out + 16) = buff_timeval.tv_usec;
            buff_out_len += 8;

            /* send the response message to the gateway*/
            write(sock, buff_out, buff_out_len);
            printf("Sent TIMESYNC_RES\n");
            break;
        case PKT_UPLINK_DATA:
            /* Parse JSON data*/
            buff[buff_len] = 0; /* add string terminator, just to be safe */
//            printf("\nJSON up: %s\n", (char *)(buff + 12)); /* DEBUG: display JSON payload */
            /* initialize TX struct and try to parse JSON */
            memset(&ulMsg, 0, sizeof ulMsg);
            root_val = json_parse_string_with_comments((const char *)(buff + 12)); /* JSON offset */
            if (root_val == NULL) {
                printf("WARNING: [down] invalid JSON, TX aborted\n");
                return;
            }
            
            /* look for JSON sub-object 'rxpk' */
            rxpk_arr = json_object_get_array(json_value_get_object(root_val), "rxpk");
            if (rxpk_arr == NULL) {
                printf("WARNING: [down] no \"rxpk\" array in JSON, TX aborted\n");
                json_value_free(root_val);
                return;
            }
            
            arr_len = json_array_get_count(rxpk_arr);
            if(arr_len == 0){
                printf("Rx JSON array has no member\n");
                return;
            } 
            for( i = 0; i < arr_len ; i++){
                // get the i-th object in rx array
                rxpk_obj = json_array_get_object(rxpk_arr, i);
                /* parse rssi value (mandatory) */
                val = json_object_get_value(rxpk_obj,"rssi");
                if (val == NULL) {
                    printf("WARNING: [down] no mandatory \"rxpk.rssi\" object in JSON, TX aborted\n");
                    json_value_free(root_val);
                    continue;
                }
                ulMsg.rssi = (float)json_value_get_number(val);

                /* parse rssi value (mandatory) */
                val = json_object_get_value(rxpk_obj,"lsnr");
                if (val == NULL) {
                    printf("WARNING: [down] no mandatory \"rxpk.lsnr\" object in JSON, TX aborted\n");
                    json_value_free(root_val);
                    continue;
                }
                ulMsg.snr = (float)json_value_get_number(val);

                /* Parse payload length (mandatory) */
                val = json_object_get_value(rxpk_obj,"size");
                if (val == NULL) {
                    printf("WARNING: [down] no mandatory \"rxpk.size\" object in JSON, TX aborted\n");
                    json_value_free(root_val);
                    continue;
                }
                ulMsg.size = (uint16_t)json_value_get_number(val);

                /* Parse payload data (mandatory) */
                str = json_object_get_string(rxpk_obj, "data");
                if (str == NULL) {
                    printf("WARNING: no mandatory \"rxpk.data\" object in JSON, TX aborted\n");
                    json_value_free(root_val);
                    continue;
                }
                if (b64_to_bin(str, strlen(str), ulMsg.payload, sizeof ulMsg.payload) != ulMsg.size) {
                    printf("WARNING: mismatch between .size and .data size once converter to binary\n");
                    continue;
                }
                
                ulMsg.sock = sock;
                
//                MSG_DEBUG(DEBUG_LOG,"Parse pkt %d from JSON done\n", i);
                // Enqueue msg and notify MAC thread that a packet has been input to the RX queue
                pthread_mutex_lock(&mutexRxMsg);
                pktEnqueue(&inboundMsgQueue, &ulMsg);
                flagRxMsg = true;
                pthread_cond_signal(&condRxMsg);
                pthread_mutex_unlock(&mutexRxMsg);
//                MSG_DEBUG(DEBUG_LOG,"Receive UP_DATA from sock: %d\n", sock);
            }
            
            /* free the JSON parse tree from memory */
            json_value_free(root_val);
            break;
        default:
            printf("WARNING: ignoring weird packet len=%d, id=%hhu\n", buff_len, buff[3]);
            fprintf(log_file, "WARNING: ignoring weird packet len=%d, id=%hhu\n", buff_len, buff[3]);
            break;
    }

#if defined(LOG)
    WriteEndDevices(logfd, i);
#endif
    return;
}
// process input data from terminal or from socket

void thread_inputstream(void) {
    static struct sockaddr_in server_address; // LoRa network server
    static int client_socket; // LoRa network server socket
    int option = 1; // address reusable option
    struct timeval timeout = {0, 0};

    uint8_t buff_in[512];
    int buff_in_len;

    int i;

    // 1. Generate LoRa Network Server welcome socket
    server_socket = socket(PF_INET, SOCK_STREAM, 0);
    if (server_socket == -1) {
        printf("TCP socket() error!\n");
        exit(0);
    }
    printf("  Server socket is %d\n", server_socket);

    // Option for LoRa Network Server address reusable
    setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &option, sizeof (option));

    memset(&server_address, 0, sizeof (server_address));
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(LORA_NETWORK_WELCOME_SERVER_PORT);
    printf("  Server listen port is %d\n", LORA_NETWORK_WELCOME_SERVER_PORT);

    if (i = bind(server_socket, (struct sockaddr *) &server_address, sizeof (server_address))) {
        printf("TCP bind() error!. ERR(%d)\n", i);
        exit(1);
    }

    // 2. Wait for LoRa Gateway connection
    if (listen(server_socket, 30) == -1) {
        printf("TCP listen() error!\n");
        exit(1);
    }

    FD_SET(server_socket, &reads);
    fdmax = server_socket;

    while (!exit_sig && !quit_sig) {
        cp_reads = reads;
        timeout.tv_sec = 5;
        timeout.tv_usec = 0;

        if ((fdnum = select(fdmax + 1, &cp_reads, 0, 0, &timeout)) < 0) {
            // Select returns error..
            if (errno == EINTR) {
                continue;
            }
            //            printf("select() error!\n");
            break;
        }
        if (fdnum == 0) {
            // Select Timeout
            //            dprintf("select() timeout retry\n");
            continue;
        }
        // 3. LoRa network server received data
        for (i = 0; i < fdmax + 1; i++) {
            if (FD_ISSET(i, &cp_reads)) {
                if (i == 0) {
                    // Receive input data from terminal
                    buff_in_len = read(i, buff_in, sizeof (buff_in));
                    buff_in[buff_in_len] = 0;

                    terminal_input_handle(i, buff_in, buff_in_len);

                } else if (i == server_socket) {
                    // 3-1. New connection from LoRa gateway
                    client_socket = new_gw_connection_handle(server_socket);
                    FD_SET(client_socket, &reads);
                    if (fdmax < client_socket)
                        fdmax = client_socket;
#if defined (LOG)
                } else if (i == logfd) {
                    // TODO: 
#endif
                } else {
                    // 3-3. New encapsulated Pkt arrived from LoRa gateway
                    if (FindGateWay(i) != NULL) {
                        buff_in_len = read(i, buff_in, sizeof (buff_in) - 1);
                        // Just test..
                        if (buff_in_len == 0) {
                            dprintf("LoRa Gateway connection is closed..\n");
                            // Clear LoRa Gateway from Management List
                            RemoveGateWay(i);
                            FD_CLR(i, &reads);
                            close(i);
                        } else if (buff_in_len == -1) {
                            /* if no message was received, got back to listening socket */
                            continue;
                        } else {
                            // Rx from LoRa Gateway..
                            upstream_data_handle(i, buff_in, buff_in_len);
                        }
                        //write(i, msg, strlen(msg) + 1);
                    } // 3-4. New encapsulated Pkt arrived from Application Server
                }
            }
        }
    }
    printf("\nINFO: End of input thread\n");
}

// downstream messages 

void thread_downstream(void) {
    int j;
    struct timeval current_time;
    struct timeval temp_time;
    struct timeval tx_timestamp;
    uint32_t time_to_SF_mil;
    struct MsgInfo_ dlMsg;
//    int downlink_ready;
    
    /* protocol variables */
    uint8_t token_h; /* random token for acknowledgement matching */
    uint8_t token_l; /* random token for acknowledgement matching */
    
    /* data buffers */
    uint8_t buff_down[DOWNSTREAM_BUF_SIZE]; /* buffer to compose the downstream packet */
    int buff_index;
    
    memset(buff_down, 0, sizeof (buff_down));
    /* pre-fill the data buffer with fixed fields */
    buff_down[0] = PROTOCOL_VERSION;
    buff_down[3] = PKT_DOWNLINK_DATA;
    
//    wait_ms(1000);

    while (!exit_sig && !quit_sig) {
        // Get next start of the next superframe time, named next_SF_time from RT-LoRa MAC
//        downlink_ready = RtLoRaGetReadyDownlinkPacket(&pkt);
        pthread_mutex_lock(&mutexTxMsg);
        flagTxMsg = false;
        while(flagTxMsg == false)
            pthread_cond_wait(&condTxMsg, &mutexTxMsg);
        pthread_mutex_unlock(&mutexTxMsg);
        
        if (pktDequeue(&outboundMsgQueue, &dlMsg) != PKT_ERROR_OK){
            continue;
        }
       
        // Prepare downstream packet to send to gw
        /* start composing datagram with the header */
        token_h = (uint8_t)rand(); /* random token */
        token_l = (uint8_t)rand(); /* random token */
        buff_down[1] = token_h;
        buff_down[2] = token_l;
        buff_index = 4; /* 4-byte header */
        
        /* start of JSON structure */
        memcpy((void *)(buff_down + buff_index), (void *)"{\"txpk\":", 8);
        buff_index += 8;
        
        /* Start of packet, add inter-packet separator if necessary */
        buff_down[buff_index] = '{';
        ++buff_index;
        
        /* Network protocol metadata */
        /* TX Time stamp */
        j = snprintf((char *)(buff_down + buff_index), DOWNSTREAM_BUF_SIZE-buff_index, "\"tm_s\":%u,\"tm_us\":%6u", \
                dlMsg.msg_tx_time.tv_sec, dlMsg.msg_tx_time.tv_usec);
        if (j > 0) {
            buff_index += j;
        } else {
            printf("ERROR: [down] snprintf failed line %u\n", (__LINE__ - 4));
            exit(EXIT_FAILURE);
        }
        
        // Tx packet metadata
        switch (dlMsg.tx_mode) {
            case IMMEDIATE:
            default:
            memcpy((void *)(buff_down + buff_index), (void *)",\"imme\":true", 12);
            buff_index += 12;
            break;
        }
        
        /* RF chain & RX frequency, 34-36 useful chars */
        j = snprintf((char *)(buff_down + buff_index), DOWNSTREAM_BUF_SIZE-buff_index, ",\"rfch\":%1u,\"freq\":%.6lf", dlMsg.rf_chain, (double)dlMsg.freq/1e6);
        if (j > 0) {
            buff_index += j;
        } else {
            printf("ERROR: [down] snprintf failed line %u\n", (__LINE__ - 4));
            exit(EXIT_FAILURE);
        }
        
        /* TX power */
        j = snprintf((char *)(buff_down + buff_index), DOWNSTREAM_BUF_SIZE-buff_index, ",\"powe\":%1u", dlMsg.rf_power);
        if (j > 0) {
            buff_index += j;
        } else {
            printf("ERROR: [down] snprintf failed line %u\n", (__LINE__ - 4));
            exit(EXIT_FAILURE);
        }
        
        if (dlMsg.modulation == MOD_LORA) {
            memcpy((void *)(buff_down + buff_index), (void *)",\"modu\":\"LORA\"", 14);
            buff_index += 14;

            /* Lora datarate & bandwidth, 16-19 useful chars */
            switch (dlMsg.datarate) {
                case DR_LORA_SF7:
                    memcpy((void *)(buff_down + buff_index), (void *)",\"datr\":\"SF7", 12);
                    buff_index += 12;
                    break;
                case DR_LORA_SF8:
                    memcpy((void *)(buff_down + buff_index), (void *)",\"datr\":\"SF8", 12);
                    buff_index += 12;
                    break;
                case DR_LORA_SF9:
                    memcpy((void *)(buff_down + buff_index), (void *)",\"datr\":\"SF9", 12);
                    buff_index += 12;
                    break;
                case DR_LORA_SF10:
                    memcpy((void *)(buff_down + buff_index), (void *)",\"datr\":\"SF10", 13);
                    buff_index += 13;
                    break;
                case DR_LORA_SF11:
                    memcpy((void *)(buff_down + buff_index), (void *)",\"datr\":\"SF11", 13);
                    buff_index += 13;
                    break;
                case DR_LORA_SF12:
                    memcpy((void *)(buff_down + buff_index), (void *)",\"datr\":\"SF12", 13);
                    buff_index += 13;
                    break;
                default:
                    printf("ERROR: [down] lora packet with unknown datarate\n");
                    memcpy((void *)(buff_down + buff_index), (void *)",\"datr\":\"SF?", 12);
                    buff_index += 12;
                    exit(EXIT_FAILURE);
            }
            
            switch (dlMsg.bandwidth) {
                case BW_125KHZ:
                    memcpy((void *)(buff_down + buff_index), (void *)"BW125\"", 6);
                    buff_index += 6;
                    break;
                case BW_250KHZ:
                    memcpy((void *)(buff_down + buff_index), (void *)"BW250\"", 6);
                    buff_index += 6;
                    break;
                case BW_500KHZ:
                    memcpy((void *)(buff_down + buff_index), (void *)"BW500\"", 6);
                    buff_index += 6;
                    break;
                default:
                    printf("ERROR: [up] lora packet with unknown bandwidth\n");
                    memcpy((void *)(buff_down + buff_index), (void *)"BW?\"", 4);
                    buff_index += 4;
                    exit(EXIT_FAILURE);
            }

            /* Packet ECC coding rate, 11-13 useful chars */
            switch (dlMsg.coderate) {
                case CR_LORA_4_5:
                    memcpy((void *)(buff_down + buff_index), (void *)",\"codr\":\"4/5\"", 13);
                    buff_index += 13;
                    break;
                case CR_LORA_4_6:
                    memcpy((void *)(buff_down + buff_index), (void *)",\"codr\":\"4/6\"", 13);
                    buff_index += 13;
                    break;
                case CR_LORA_4_7:
                    memcpy((void *)(buff_down + buff_index), (void *)",\"codr\":\"4/7\"", 13);
                    buff_index += 13;
                    break;
                case CR_LORA_4_8:
                    memcpy((void *)(buff_down + buff_index), (void *)",\"codr\":\"4/8\"", 13);
                    buff_index += 13;
                    break;
                case 0: /* treat the CR0 case (mostly false sync) */
                    memcpy((void *)(buff_down + buff_index), (void *)",\"codr\":\"OFF\"", 13);
                    buff_index += 13;
                    break;
                default:
                    printf("ERROR: [up] lora packet with unknown coderate\n");
                    memcpy((void *)(buff_down + buff_index), (void *)",\"codr\":\"?\"", 11);
                    buff_index += 11;
                    exit(EXIT_FAILURE);
            }
        } else if (dlMsg.modulation == MOD_FSK) {
            memcpy((void *)(buff_down + buff_index), (void *)",\"modu\":\"FSK\"", 13);
            buff_index += 13;

            /* FSK datarate, 11-14 useful chars */
            j = snprintf((char *)(buff_down + buff_index), DOWNSTREAM_BUF_SIZE-buff_index, ",\"datr\":%u", dlMsg.datarate);
            if (j > 0) {
                buff_index += j;
            } else {
                printf("ERROR: [up] snprintf failed line %u\n", (__LINE__ - 4));
                exit(EXIT_FAILURE);
            }
        } else {
            printf("ERROR: [up] received packet with unknown modulation\n");
            exit(EXIT_FAILURE);
        }
        
        switch (dlMsg.invert_pol) {
            case true:
                memcpy((void *)(buff_down + buff_index), (void *)",\"ipol\":true", 12);
                buff_index += 13;
            case false:
            default:
                memcpy((void *)(buff_down + buff_index), (void *)",\"ipol\":false", 13);
                buff_index += 13;
            break;
        }
        
        /* Preamble size, payload size */
        j = snprintf((char *)(buff_down + buff_index), DOWNSTREAM_BUF_SIZE-buff_index, ",\"prea\":%u,\"size\":%u", dlMsg.preamble, dlMsg.size);
        if (j > 0) {
            buff_index += j;
        } else {
            printf("ERROR: [down] snprintf failed line %u\n", (__LINE__ - 4));
            exit(EXIT_FAILURE);
        }

        /* Packet payload */
        memcpy((void *)(buff_down + buff_index), (void *)",\"data\":\"", 9);
        buff_index += 9;
        
//        printf("MSG: ");
//        for(int k = 0; k < dlMsg.size; k++){
//            printf("%x ", dlMsg.payload[k]);
//        }
//        printf("\n");
                
        j = bin_to_b64(dlMsg.payload, dlMsg.size, (char *)(buff_down + buff_index), 341); /* 255 bytes = 340 chars in b64 + null char */
        if (j>=0) {
            buff_index += j;
        } else {
            printf("ERROR: [up] bin_to_b64 failed line %u\n", (__LINE__ - 5));
            exit(EXIT_FAILURE);
        }

        buff_down[buff_index] = '"';
        ++buff_index;
        
        buff_down[buff_index] = '}';
        ++buff_index;
        
        /* End of packet serialization */
        buff_down[buff_index] = '}';
        ++buff_index;

//        printf("JSON: %s\n", buff_down);
//        for(j = 0; j < buff_index; j++ ){
//            printf("%x.",buff_down[j]);
//        }
//        printf("\n");
        
        /* Send JSON data to all gateways in the networks */
        for (j = 0; j < fdmax + 1; j++){
            if (FindGateWay(j) != NULL) {
                write(j, buff_down, buff_index);
//                printf("Send DOWNLINK msg to GW with sock %d\n", j);
            }
        }
        
    }
    printf("\nINFO: End of downstream thread\n");
}

/******************************************************************************
 * Function Name        : sig_handler
 * Input Parameters     : Signal Number
 * Return Value         : none
 * Function Description : Signal handling function
 ******************************************************************************/
static void sig_handler(int sigio) {
    if (sigio == SIGQUIT) {
        quit_sig = 1;
    } else if ((sigio == SIGINT) || (sigio == SIGTERM)) {
        exit_sig = 1;
    }
}

/******************************************************************************
 * Function Name        : sig_logging
 * Input Parameters     : Signal Number
 * Return Value         : none
 * Function Description : Signal handling function - monitoring with periodic interval
 ******************************************************************************/
static void sig_logging(int signo) {
    //
    printf("\n");
    printf("**************************************************************\n");
    printf("*                Device Informations                         *\n");
    printf("**************************************************************\n");
    ShowEndDevices();

    alarm(MONITORING_INTERVAL);
}

/******************************************************************************
 * Function Name        : set_signal
 * Input Parameters     : none
 * Return Value         : none
 * Function Description : Register SIGPIPE handler to Linux kernel
 ******************************************************************************/
void set_signal(void) {
    struct sigaction sigact;

    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigact.sa_handler = sig_handler;
    sigaction(SIGQUIT, &sigact, NULL); /* Ctrl-\ */
    sigaction(SIGINT, &sigact, NULL); /* Ctrl-C */
    sigaction(SIGTERM, &sigact, NULL); /* default "kill" command */
}
