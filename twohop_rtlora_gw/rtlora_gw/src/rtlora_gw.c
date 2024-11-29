/*
 * Description: Two-hop RT-LoRa Gateway source file
 * Author: Quy Lam Hoang
 * Email: quylam925@gmail.com
 * Created: 2021/05/24
 */


/* -------------------------------------------------------------------------- */
/* --- DEPENDANCIES --------------------------------------------------------- */

/* fix an issue between POSIX and C99 */
#if __STDC_VERSION__ >= 199901L
#define _XOPEN_SOURCE 600
#else
#define _XOPEN_SOURCE 500
#endif

#include <stdint.h>     /* C99 types */
#include <stdbool.h>    /* bool type */
#include <stdio.h>      /* printf fprintf sprintf fopen fputs */

#include <string.h>     /* memset */
#include <signal.h>     /* sigaction */
#include <unistd.h>     /* getopt access */
#include <stdlib.h>     /* exit codes */
#include <getopt.h>     /* getopt_long */
#include <sys/time.h>
#include <math.h>

#include <sys/socket.h>     /* socket specific definitions */
#include <netinet/in.h>     /* INET constants and stuff */
#include <arpa/inet.h>      /* IP address conversion stuff */
#include <netdb.h>          /* gai_strerror */

#include <sys/types.h>

#include <pthread.h>

#include "loragw_hal.h"
#include "loragw_reg.h"
#include "loragw_aux.h"
#include "loragw_gps.h"
#include "parson.h"
#include "trace.h"
#include "base64.h"
#include "jitqueue.h"

/* -------------------------------------------------------------------------- */
/* --- PRIVATE MACROS ------------------------------------------------------- */

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
//#define MSG(args...) fprintf(stderr, args) /* message that is destined to the user */

/* -------------------------------------------------------------------------- */
/* --- PRIVATE CONSTANTS ---------------------------------------------------- */

#define VERSION_STRING      "2.0"
#ifndef VERSION_STRING
#define VERSION_STRING "undefined"
#endif

#define LOGGING_ENABLED     0

#define DEFAULT_SERVER      "127.0.0.1"   /* hostname also supported */
#define DEFAULT_PORT_UP     8000
#define DEFAULT_PORT_DW     8001
#define DEFAULT_KEEPALIVE   5     

/* default time interval for downstream keep-alive packet */
#define DEFAULT_STAT        10          /* default time interval for statistics */
#define PUSH_TIMEOUT_MS     100
#define PULL_TIMEOUT_MS     200
#define GPS_REF_MAX_AGE     30          /* maximum admitted delay in seconds of GPS loss before considering latest GPS sync unusable */
#define FETCH_SLEEP_MS      10          /* nb of ms waited when a fetch return no packets */
#define BEACON_POLL_MS      50          /* time in ms between polling of beacon TX status */

#define PROTOCOL_VERSION    2           /* v1.3 */

#define XERR_INIT_AVG       128         /* nb of measurements the XTAL correction is averaged on as initial value */
#define XERR_FILT_COEF      256         /* coefficient for low-pass XTAL error tracking */

//#define PKT_PUSH_DATA       0
//#define PKT_PUSH_ACK        1
//#define PKT_PULL_DATA       2
//#define PKT_PULL_RESP       3
//#define PKT_PULL_ACK        4
//#define PKT_TX_ACK          5

/* Gateway <-> Server packet types */
#define PKT_TIMESYNC_REQ        0 // gw -> sv
#define PKT_TIMESYNC_RES        1 // sv -> gw
#define PKT_DOWNLINK_DATA       2 // sv -> gw
#define PKT_DOWNLINK_ACK        3 // gw -> sv
#define PKT_UPLINK_DATA         4 // gw -> sv
#define PKT_UPLINK_ACK          5 // sv -> gw

#define DOWNSTREAM_BUF_SIZE     1024

#define NB_PKT_MAX      8 /* max number of packets per fetch/send cycle */

#define MIN_LORA_PREAMB 6 /* minimum Lora preamble length for this application */
#define STD_LORA_PREAMB 8
#define MIN_FSK_PREAMB  3 /* minimum FSK preamble length for this application */
#define STD_FSK_PREAMB  5

#define STATUS_SIZE     200
#define TX_BUFF_SIZE    ((540 * NB_PKT_MAX) + 30 + STATUS_SIZE)

#define UNIX_GPS_EPOCH_OFFSET 315964800 /* Number of seconds ellapsed between 01.Jan.1970 00:00:00
                                                                          and 06.Jan.1980 00:00:00 */

#define DEFAULT_BEACON_FREQ_HZ      869525000
#define DEFAULT_BEACON_FREQ_NB      1
#define DEFAULT_BEACON_FREQ_STEP    0
#define DEFAULT_BEACON_DATARATE     9
#define DEFAULT_BEACON_BW_HZ        125000
#define DEFAULT_BEACON_POWER        14
#define DEFAULT_BEACON_INFODESC     0

#define TIMESYNC_LONG_INTERVAL      60000 // in miliseconds
#define TIMESYNC_SHORT_INTERVAL     3000 // in miliseconds
#define TIMESYNC_MAX_ERROR_TOLERANT 1000 // in microseconds

#define SOCK_TIMEOUT_MS             20 /* non critical for throughput */

#define TIMERSUB(a, b, result)						      \
  do {                                                      \
    result.tv_sec = a.tv_sec - b.tv_sec;			      \
    result.tv_usec = a.tv_usec - b.tv_usec;			      \
    if (result.tv_usec < 0) {					      \
      --result.tv_sec;						      \
      result.tv_usec += 1000000;					      \
    }									      \
  } while (0)

/* -------------------------------------------------------------------------- */

/* --- PRIVATE VARIABLES (GLOBAL) ------------------------------------------- */

/* signal handling variables */
volatile bool exit_sig = false; /* 1 -> application terminates cleanly (shut down hardware, close open files, etc) */
volatile bool quit_sig = false; /* 1 -> application terminates without shutting down the hardware */

/* packets filtering configuration variables */
//static bool fwd_valid_pkt = true; /* packets with PAYLOAD CRC OK are forwarded */
//static bool fwd_error_pkt = false; /* packets with PAYLOAD CRC ERROR are NOT forwarded */
//static bool fwd_nocrc_pkt = false; /* packets with NO PAYLOAD CRC are NOT forwarded */

//static int keepalive_time = DEFAULT_KEEPALIVE; /* send a PULL_DATA request every X seconds, negative = disabled */

/* statistics collection configuration variables */
static unsigned stat_interval = DEFAULT_STAT; /* time interval (in sec) at which statistics are collected and displayed */

/* network configuration variables */
static uint64_t lgwm = 0; /* Lora gateway MAC address */

/* gateway <-> MAC protocol variables */
static uint32_t net_mac_h; /* Most Significant Nibble, network order */
static uint32_t net_mac_l; /* Least Significant Nibble, network order */

/* network sockets */
//static int sock_up; /* socket for upstream traffic */
static int sock_down; /* socket for downstream traffic */
//static struct sockaddr_in sock_up_address;
static struct sockaddr_in sock_down_address;
static struct timeval sock_timeout = {0, (SOCK_TIMEOUT_MS * 1000)}; /* non critical for throughput */

/* time synchronization variables */
enum timesync_flag_e {
    TIMESYNC_DONE = 0,
    TIMESYNC_WAITING,
    TIMESYNC_ERROR // Timeout or error occurs
};

/**
@struct timesync_var_s
@brief Structure defining the time sync variable
 */
struct timesync_var_s {
    uint8_t token_h; /* random token for acknowledgement matching */
    uint8_t token_l; /* random token for acknowledgement matching */
    struct timeval t0; // is the client's timestamp of the request packet transmission,
    struct timeval t1; // is the server's timestamp of the request packet reception,
    struct timeval t2; // is the server's timestamp of the response packet transmission and
    struct timeval t3; // is the client's timestamp of the response packet reception.
    int time_offset; // in micro second
    int offset_drift; // delta between current and previous offset, in micro second
    enum timesync_flag_e flag;
};

static struct timesync_var_s timesync_var;

/* network protocol variables */
//static struct timeval push_timeout_half = {0, (PUSH_TIMEOUT_MS * 500)}; /* cut in half, critical for throughput */
//static struct timeval pull_timeout = {0, (PULL_TIMEOUT_MS * 1000)}; /* non critical for throughput */

/* hardware access control and correction */
pthread_mutex_t mx_concent = PTHREAD_MUTEX_INITIALIZER; /* control access to the concentrator */
static pthread_mutex_t mx_xcorr = PTHREAD_MUTEX_INITIALIZER; /* control access to the XTAL correction */
static bool xtal_correct_ok = false; /* set true when XTAL correction is stable enough */
static double xtal_correct = 1.0;

/* GPS configuration and synchronization */
static char gps_tty_path[64] = "\0"; /* path of the TTY port GPS is connected on */
static int gps_tty_fd = -1; /* file descriptor of the GPS TTY port */
static bool gps_enabled = false; /* is GPS enabled on that gateway ? */

static pthread_mutex_t mx_stat_rep = PTHREAD_MUTEX_INITIALIZER; /* control access to the status report */
static bool report_ready = false; /* true when there is a new report to send to the server */
static char status_report[STATUS_SIZE]; /* status report as a JSON object */

/* auto-quit function */
static uint32_t autoquit_threshold = 0; /* enable auto-quit after a number of non-acknowledged PULL_DATA (0 = disabled)*/

/* Just In Time TX scheduling */
static struct jit_queue_s jit_queue;

/* Gateway specificities */
static int8_t antenna_gain = 0;

/* TX capabilities */
static struct lgw_tx_gain_lut_s txlut; /* TX gain table */
static uint32_t tx_freq_min[LGW_RF_CHAIN_NB]; /* lowest frequency supported by TX chain */
static uint32_t tx_freq_max[LGW_RF_CHAIN_NB]; /* highest frequency supported by TX chain */

#if LOGGING_ENABLED
static time_t log_time;
static time_t log_start_time;
static FILE * log_file = NULL;
#endif

/* -------------------------------------------------------------------------- */
/* --- PRIVATE FUNCTIONS DECLARATION ---------------------------------------- */

static void sig_handler(int sigio);

static int parse_SX1301_configuration(const char * conf_file);

static int parse_gateway_configuration(const char * conf_file);

/* threads */
void thread_up(void);
void thread_down(void);
void thread_jit(void);
void thread_timesync_to_server(void);
//void thread_timersync(void);

/* -------------------------------------------------------------------------- */
/* --- AUXILIARY FUNCTIONS DECLARATION ---------------------------------------- */
//static int ipow(int base, int exp);

static double difftimespec(struct timespec end, struct timespec beginning);

bool open_log(void);

void close_log(void);

/* -------------------------------------------------------------------------- */

/* --- PRIVATE FUNCTIONS DEFINITION ----------------------------------------- */

static void sig_handler(int sigio) {
    if (sigio == SIGQUIT) {
        quit_sig = 1;
    } else if ((sigio == SIGINT) || (sigio == SIGTERM)) {
        exit_sig = 1;
    }
}

static int parse_SX1301_configuration(const char * conf_file) {
    int i;
    const char conf_obj[] = "SX1301_conf";
    char param_name[32]; /* used to generate variable parameter names */
    const char *str; /* used to store string value from JSON object */
    struct lgw_conf_board_s boardconf;
    struct lgw_conf_rxrf_s rfconf;
    struct lgw_conf_rxif_s ifconf;
    JSON_Value *root_val;
    JSON_Object *root = NULL;
    JSON_Object *conf = NULL;
    JSON_Value *val;
    uint32_t sf, bw;

    /* try to parse JSON */
    root_val = json_parse_file_with_comments(conf_file);
    root = json_value_get_object(root_val);
    if (root == NULL) {
        MSG("ERROR: %s id not a valid JSON file\n", conf_file);
        exit(EXIT_FAILURE);
    }
    conf = json_object_get_object(root, conf_obj);
    if (conf == NULL) {
        MSG("INFO: %s does not contain a JSON object named %s\n", conf_file, conf_obj);
        return -1;
    } else {
        MSG("INFO: %s does contain a JSON object named %s, parsing SX1301 parameters\n", conf_file, conf_obj);
    }

    /* set board configuration */
    memset(&boardconf, 0, sizeof boardconf); /* initialize configuration structure */
    val = json_object_get_value(conf, "lorawan_public"); /* fetch value (if possible) */
    if (json_value_get_type(val) == JSONBoolean) {
        boardconf.lorawan_public = (bool) json_value_get_boolean(val);
    } else {
        MSG("WARNING: Data type for lorawan_public seems wrong, please check\n");
        boardconf.lorawan_public = false;
    }
    val = json_object_get_value(conf, "clksrc"); /* fetch value (if possible) */
    if (json_value_get_type(val) == JSONNumber) {
        boardconf.clksrc = (uint8_t) json_value_get_number(val);
    } else {
        MSG("WARNING: Data type for clksrc seems wrong, please check\n");
        boardconf.clksrc = 0;
    }
    MSG("INFO: lorawan_public %d, clksrc %d\n", boardconf.lorawan_public, boardconf.clksrc);
    /* all parameters parsed, submitting configuration to the HAL */
    if (lgw_board_setconf(boardconf) != LGW_HAL_SUCCESS) {
        MSG("ERROR: Failed to configure board\n");
        return -1;
    }

    /* set antenna gain configuration */
    val = json_object_get_value(conf, "antenna_gain"); /* fetch value (if possible) */
    if (val != NULL) {
        if (json_value_get_type(val) == JSONNumber) {
            antenna_gain = (int8_t) json_value_get_number(val);
        } else {
            MSG("WARNING: Data type for antenna_gain seems wrong, please check\n");
            antenna_gain = 0;
        }
    }
    MSG("INFO: antenna_gain %d dBi\n", antenna_gain);

    /* set configuration for tx gains */
    memset(&txlut, 0, sizeof txlut); /* initialize configuration structure */
    for (i = 0; i < TX_GAIN_LUT_SIZE_MAX; i++) {
        snprintf(param_name, sizeof param_name, "tx_lut_%i", i); /* compose parameter path inside JSON structure */
        val = json_object_get_value(conf, param_name); /* fetch value (if possible) */
        if (json_value_get_type(val) != JSONObject) {
            MSG("INFO: no configuration for tx gain lut %i\n", i);
            continue;
        }
        txlut.size++; /* update TX LUT size based on JSON object found in configuration file */
        /* there is an object to configure that TX gain index, let's parse it */
        snprintf(param_name, sizeof param_name, "tx_lut_%i.pa_gain", i);
        val = json_object_dotget_value(conf, param_name);
        if (json_value_get_type(val) == JSONNumber) {
            txlut.lut[i].pa_gain = (uint8_t) json_value_get_number(val);
        } else {
            MSG("WARNING: Data type for %s[%d] seems wrong, please check\n", param_name, i);
            txlut.lut[i].pa_gain = 0;
        }
        snprintf(param_name, sizeof param_name, "tx_lut_%i.dac_gain", i);
        val = json_object_dotget_value(conf, param_name);
        if (json_value_get_type(val) == JSONNumber) {
            txlut.lut[i].dac_gain = (uint8_t) json_value_get_number(val);
        } else {
            txlut.lut[i].dac_gain = 3; /* This is the only dac_gain supported for now */
        }
        snprintf(param_name, sizeof param_name, "tx_lut_%i.dig_gain", i);
        val = json_object_dotget_value(conf, param_name);
        if (json_value_get_type(val) == JSONNumber) {
            txlut.lut[i].dig_gain = (uint8_t) json_value_get_number(val);
        } else {
            MSG("WARNING: Data type for %s[%d] seems wrong, please check\n", param_name, i);
            txlut.lut[i].dig_gain = 0;
        }
        snprintf(param_name, sizeof param_name, "tx_lut_%i.mix_gain", i);
        val = json_object_dotget_value(conf, param_name);
        if (json_value_get_type(val) == JSONNumber) {
            txlut.lut[i].mix_gain = (uint8_t) json_value_get_number(val);
        } else {
            MSG("WARNING: Data type for %s[%d] seems wrong, please check\n", param_name, i);
            txlut.lut[i].mix_gain = 0;
        }
        snprintf(param_name, sizeof param_name, "tx_lut_%i.rf_power", i);
        val = json_object_dotget_value(conf, param_name);
        if (json_value_get_type(val) == JSONNumber) {
            txlut.lut[i].rf_power = (int8_t) json_value_get_number(val);
        } else {
            MSG("WARNING: Data type for %s[%d] seems wrong, please check\n", param_name, i);
            txlut.lut[i].rf_power = 0;
        }
    }
    /* all parameters parsed, submitting configuration to the HAL */
    if (txlut.size > 0) {
        MSG("INFO: Configuring TX LUT with %u indexes\n", txlut.size);
        if (lgw_txgain_setconf(&txlut) != LGW_HAL_SUCCESS) {
            MSG("ERROR: Failed to configure concentrator TX Gain LUT\n");
            return -1;
        }
    } else {
        MSG("WARNING: No TX gain LUT defined\n");
    }

    /* set configuration for RF chains */
    for (i = 0; i < LGW_RF_CHAIN_NB; ++i) {
        memset(&rfconf, 0, sizeof (rfconf)); /* initialize configuration structure */
        sprintf(param_name, "radio_%i", i); /* compose parameter path inside JSON structure */
        val = json_object_get_value(conf, param_name); /* fetch value (if possible) */
        if (json_value_get_type(val) != JSONObject) {
            MSG("INFO: no configuration for radio %i\n", i);
            continue;
        }
        /* there is an object to configure that radio, let's parse it */
        sprintf(param_name, "radio_%i.enable", i);
        val = json_object_dotget_value(conf, param_name);
        if (json_value_get_type(val) == JSONBoolean) {
            rfconf.enable = (bool) json_value_get_boolean(val);
        } else {
            rfconf.enable = false;
        }
        if (rfconf.enable == false) { /* radio disabled, nothing else to parse */
            MSG("INFO: radio %i disabled\n", i);
        } else { /* radio enabled, will parse the other parameters */
            snprintf(param_name, sizeof param_name, "radio_%i.freq", i);
            rfconf.freq_hz = (uint32_t) json_object_dotget_number(conf, param_name);
            snprintf(param_name, sizeof param_name, "radio_%i.rssi_offset", i);
            rfconf.rssi_offset = (float) json_object_dotget_number(conf, param_name);
            snprintf(param_name, sizeof param_name, "radio_%i.type", i);
            str = json_object_dotget_string(conf, param_name);
            if (!strncmp(str, "SX1255", 6)) {
                rfconf.type = LGW_RADIO_TYPE_SX1255;
            } else if (!strncmp(str, "SX1257", 6)) {
                rfconf.type = LGW_RADIO_TYPE_SX1257;
            } else {
                MSG("WARNING: invalid radio type: %s (should be SX1255 or SX1257)\n", str);
            }
            snprintf(param_name, sizeof param_name, "radio_%i.tx_enable", i);
            val = json_object_dotget_value(conf, param_name);
            if (json_value_get_type(val) == JSONBoolean) {
                rfconf.tx_enable = (bool) json_value_get_boolean(val);
                if (rfconf.tx_enable == true) {
                    /* tx notch filter frequency to be set */
                    snprintf(param_name, sizeof param_name, "radio_%i.tx_notch_freq", i);
                    rfconf.tx_notch_freq = (uint32_t) json_object_dotget_number(conf, param_name);
                }
            } else {
                rfconf.tx_enable = false;
            }
            MSG("INFO: radio %i enabled (type %s), center frequency %u, RSSI offset %f, tx enabled %d, tx_notch_freq %u\n", i, str, rfconf.freq_hz, rfconf.rssi_offset, rfconf.tx_enable, rfconf.tx_notch_freq);
        }
        /* all parameters parsed, submitting configuration to the HAL */
        if (lgw_rxrf_setconf(i, rfconf) != LGW_HAL_SUCCESS) {
            MSG("ERROR: invalid configuration for radio %i\n", i);
            return -1;
        }
    }

    /* set configuration for LoRa multi-SF channels (bandwidth cannot be set) */
    for (i = 0; i < LGW_MULTI_NB; ++i) {
        memset(&ifconf, 0, sizeof (ifconf)); /* initialize configuration structure */
        sprintf(param_name, "chan_multiSF_%i", i); /* compose parameter path inside JSON structure */
        val = json_object_get_value(conf, param_name); /* fetch value (if possible) */
        if (json_value_get_type(val) != JSONObject) {
            MSG("INFO: no configuration for LoRa multi-SF channel %i\n", i);
            continue;
        }
        /* there is an object to configure that LoRa multi-SF channel, let's parse it */
        sprintf(param_name, "chan_multiSF_%i.enable", i);
        val = json_object_dotget_value(conf, param_name);
        if (json_value_get_type(val) == JSONBoolean) {
            ifconf.enable = (bool) json_value_get_boolean(val);
        } else {
            ifconf.enable = false;
        }
        if (ifconf.enable == false) { /* LoRa multi-SF channel disabled, nothing else to parse */
            MSG("INFO: LoRa multi-SF channel %i disabled\n", i);
        } else { /* LoRa multi-SF channel enabled, will parse the other parameters */
            sprintf(param_name, "chan_multiSF_%i.radio", i);
            ifconf.rf_chain = (uint32_t) json_object_dotget_number(conf, param_name);
            sprintf(param_name, "chan_multiSF_%i.if", i);
            ifconf.freq_hz = (int32_t) json_object_dotget_number(conf, param_name);
            // TODO: handle individual SF enabling and disabling (spread_factor)
            MSG("INFO: LoRa multi-SF channel %i enabled, radio %i selected, IF %i Hz, 125 kHz bandwidth, SF 7 to 12\n", i, ifconf.rf_chain, ifconf.freq_hz);
        }
        /* all parameters parsed, submitting configuration to the HAL */
        if (lgw_rxif_setconf(i, ifconf) != LGW_HAL_SUCCESS) {
            MSG("ERROR: invalid configuration for Lora multi-SF channel %i\n", i);
            return -1;
        }
    }

    /* set configuration for LoRa standard channel */
    memset(&ifconf, 0, sizeof (ifconf)); /* initialize configuration structure */
    val = json_object_get_value(conf, "chan_Lora_std"); /* fetch value (if possible) */
    if (json_value_get_type(val) != JSONObject) {
        MSG("INFO: no configuration for LoRa standard channel\n");
    } else {
        val = json_object_dotget_value(conf, "chan_Lora_std.enable");
        if (json_value_get_type(val) == JSONBoolean) {
            ifconf.enable = (bool) json_value_get_boolean(val);
        } else {
            ifconf.enable = false;
        }
        if (ifconf.enable == false) {
            MSG("INFO: LoRa standard channel %i disabled\n", i);
        } else {
            ifconf.rf_chain = (uint32_t) json_object_dotget_number(conf, "chan_Lora_std.radio");
            ifconf.freq_hz = (int32_t) json_object_dotget_number(conf, "chan_Lora_std.if");
            bw = (uint32_t) json_object_dotget_number(conf, "chan_Lora_std.bandwidth");
            switch (bw) {
                case 500000: ifconf.bandwidth = BW_500KHZ;
                    break;
                case 250000: ifconf.bandwidth = BW_250KHZ;
                    break;
                case 125000: ifconf.bandwidth = BW_125KHZ;
                    break;
                default: ifconf.bandwidth = BW_UNDEFINED;
            }
            sf = (uint32_t) json_object_dotget_number(conf, "chan_Lora_std.spread_factor");
            switch (sf) {
                case 7: ifconf.datarate = DR_LORA_SF7;
                    break;
                case 8: ifconf.datarate = DR_LORA_SF8;
                    break;
                case 9: ifconf.datarate = DR_LORA_SF9;
                    break;
                case 10: ifconf.datarate = DR_LORA_SF10;
                    break;
                case 11: ifconf.datarate = DR_LORA_SF11;
                    break;
                case 12: ifconf.datarate = DR_LORA_SF12;
                    break;
                default: ifconf.datarate = DR_UNDEFINED;
            }
            MSG("INFO: LoRa standard channel enabled, radio %i selected, IF %i Hz, %u Hz bandwidth, SF %u\n", ifconf.rf_chain, ifconf.freq_hz, bw, sf);
        }
        if (lgw_rxif_setconf(8, ifconf) != LGW_HAL_SUCCESS) {
            MSG("ERROR: invalid configuration for Lora standard channel\n");
            return -1;
        }
    }

    /* set configuration for FSK channel */
    memset(&ifconf, 0, sizeof (ifconf)); /* initialize configuration structure */
    val = json_object_get_value(conf, "chan_FSK"); /* fetch value (if possible) */
    if (json_value_get_type(val) != JSONObject) {
        MSG("INFO: no configuration for FSK channel\n");
    } else {
        val = json_object_dotget_value(conf, "chan_FSK.enable");
        if (json_value_get_type(val) == JSONBoolean) {
            ifconf.enable = (bool) json_value_get_boolean(val);
        } else {
            ifconf.enable = false;
        }
        if (ifconf.enable == false) {
            MSG("INFO: FSK channel %i disabled\n", i);
        } else {
            ifconf.rf_chain = (uint32_t) json_object_dotget_number(conf, "chan_FSK.radio");
            ifconf.freq_hz = (int32_t) json_object_dotget_number(conf, "chan_FSK.if");
            bw = (uint32_t) json_object_dotget_number(conf, "chan_FSK.bandwidth");
            if (bw <= 7800) ifconf.bandwidth = BW_7K8HZ;
            else if (bw <= 15600) ifconf.bandwidth = BW_15K6HZ;
            else if (bw <= 31200) ifconf.bandwidth = BW_31K2HZ;
            else if (bw <= 62500) ifconf.bandwidth = BW_62K5HZ;
            else if (bw <= 125000) ifconf.bandwidth = BW_125KHZ;
            else if (bw <= 250000) ifconf.bandwidth = BW_250KHZ;
            else if (bw <= 500000) ifconf.bandwidth = BW_500KHZ;
            else ifconf.bandwidth = BW_UNDEFINED;
            ifconf.datarate = (uint32_t) json_object_dotget_number(conf, "chan_FSK.datarate");
            MSG("INFO: FSK channel enabled, radio %i selected, IF %i Hz, %u Hz bandwidth, %u bps datarate\n", ifconf.rf_chain, ifconf.freq_hz, bw, ifconf.datarate);
        }
        if (lgw_rxif_setconf(9, ifconf) != LGW_HAL_SUCCESS) {
            MSG("ERROR: invalid configuration for FSK channel\n");
            return -1;
        }
    }

    json_value_free(root_val);
    return 0;
}

static int parse_gateway_configuration(const char * conf_file) {
    const char conf_obj[] = "gateway_conf";
    JSON_Value *root_val;
    JSON_Object *root = NULL;
    JSON_Object *conf = NULL;
    const char *str; /* pointer to sub-strings in the JSON data */
    unsigned long long ull = 0;

    /* try to parse JSON */
    root_val = json_parse_file_with_comments(conf_file);
    root = json_value_get_object(root_val);
    if (root == NULL) {
        MSG("ERROR: %s id not a valid JSON file\n", conf_file);
        exit(EXIT_FAILURE);
    }
    conf = json_object_get_object(root, conf_obj);
    if (conf == NULL) {
        MSG("INFO: %s does not contain a JSON object named %s\n", conf_file, conf_obj);
        return -1;
    } else {
        MSG("INFO: %s does contain a JSON object named %s, parsing gateway parameters\n", conf_file, conf_obj);
    }

    /* getting network parameters (only those necessary for the packet logger) */
    str = json_object_get_string(conf, "gateway_ID");
    if (str != NULL) {
        sscanf(str, "%llx", &ull);
        lgwm = ull;
        MSG("INFO: gateway MAC address is configured to %016llX\n", ull);
    }
    
    /* GPS module TTY path (optional) */
    str = json_object_get_string(conf, "gps_tty_path");
    if (str != NULL) {
        strncpy(gps_tty_path, str, sizeof gps_tty_path);
        MSG("INFO: GPS serial port path is configured to \"%s\"\n", gps_tty_path);
    }
    
    json_value_free(root_val);
    return 0;
}

/*
 * Difference between end and beginning in microsecond 
 */
static double difftimespec(struct timespec end, struct timespec beginning) {
    double x;

    x = 1E6 * (double) (end.tv_sec - beginning.tv_sec);
    x += (1E-3 * (double) (end.tv_nsec - beginning.tv_nsec));

    return x;
}

bool open_log(void){
#if LOGGING_ENABLED
    int i;
    char iso_date[20];
    
    char log_file_name[64];
    time(&log_time);
    strftime(iso_date, sizeof(iso_date),"%Y%m%dT%H%M%SZ",localtime(&log_time)); /* format yyyymmddThhmmssZ */
    log_start_time = log_time; /* keep track of when the log was started, for log rotation */

    sprintf(log_file_name, "rtlora_gw_%s.csv", iso_date);
    log_file = fopen(log_file_name, "a"); /* create log file, append if file already exist */
    if (log_file == NULL) {
        MSG("GW_ERROR: impossible to create log file %s\n", log_file_name);
        return false;
    }

    i = fprintf(log_file, "START RT-LORA GW LOG\n");
    if (i < 0) {
        MSG("GW_ERROR: impossible to write to log file %s\n", log_file_name);
        return false;
    }

    MSG("GW_INFO: Now writing to rtlora_gw log file %s\n", log_file_name);
    return true;
#endif
}

void close_log(void){
#if LOGGING_ENABLED
    fclose(log_file);
#endif
}
/* --- MAIN FUNCTION -------------------------------------------------------- */

int main(int argc, char * argv[]) {

    struct sigaction sigact; /* SIGQUIT&SIGINT&SIGTERM signal handling */
    int i;
    int x = 0;
    int connect_attempts = 0;

    /* configuration file related */
    const char global_conf_fname[] = "global_conf.json"; /* contain global (typ. network-wide) configuration */
    const char local_conf_fname[] = "local_conf.json"; /* contain node specific configuration, overwrite global parameters for parameters that are defined in both */
    const char debug_conf_fname[] = "debug_conf.json"; /* if present, all other configuration files are ignored */

    /* network socket creation */
    //    struct addrinfo hints;
    //    struct addrinfo *result; /* store result of getaddrinfo */
    //    struct addrinfo *q; /* pointer to move into *result data */
    //    char host_name[64];
    //    char port_name[64];

    /* threads */
    pthread_t thrid_up;
    pthread_t thrid_down;
    pthread_t thrid_timesync_to_server;
    pthread_t thrid_jit;
//    pthread_t thrid_timersync;

    /* statistics variable */
    time_t t;
    char stat_timestamp[24];

    /* variables to get local copies of measurements */
    /* TODO: Add local copies of measurements for statistic */

    /* display version informations */
    MSG("*** RT Lora Gateway ***\nVersion: " VERSION_STRING "\n");
    MSG("*** Lora concentrator HAL library version info ***\n%s\n***\n", lgw_version_info());

    /* display host endianness */
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    MSG("INFO: Little endian host\n");
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    MSG("INFO: Big endian host\n");
#else
    MSG("INFO: Host endianness unknown\n");
#endif

    /* configuration files management */
    if (access(debug_conf_fname, R_OK) == 0) {
        /* if there is a debug conf, parse only the debug conf */
        MSG("INFO: found debug configuration file %s, other configuration files will be ignored\n", debug_conf_fname);
        x = parse_SX1301_configuration(debug_conf_fname);
        if (x != 0) {
            exit(EXIT_FAILURE);
        }
        x = parse_gateway_configuration(debug_conf_fname);
        if (x != 0) {
            exit(EXIT_FAILURE);
        }
    } else if (access(global_conf_fname, R_OK) == 0) {
        /* if there is a global conf, parse it and then try to parse local conf  */
        MSG("INFO: found global configuration file %s, trying to parse it\n", global_conf_fname);
        x = parse_SX1301_configuration(global_conf_fname);
        if (x != 0) {
            exit(EXIT_FAILURE);
        }
        x = parse_gateway_configuration(global_conf_fname);
        if (x != 0) {
            exit(EXIT_FAILURE);
        }
        if (access(local_conf_fname, R_OK) == 0) {
            MSG("INFO: found local configuration file %s, trying to parse it\n", local_conf_fname);
            parse_SX1301_configuration(local_conf_fname);
            parse_gateway_configuration(local_conf_fname);
        }
    } else if (access(local_conf_fname, R_OK) == 0) {
        /* if there is only a local conf, parse it and that's all */
        MSG("INFO: found local configuration file %s, trying to parse it\n", local_conf_fname);
        parse_SX1301_configuration(local_conf_fname);
        if (x != 0) {
            exit(EXIT_FAILURE);
        }
        parse_gateway_configuration(local_conf_fname);
        if (x != 0) {
            exit(EXIT_FAILURE);
        }
    } else {
        MSG("ERROR: failed to find any configuration file named %s, %s or %s\n", global_conf_fname, local_conf_fname, debug_conf_fname);
        return EXIT_FAILURE;
    }
    /* Start GPS a.s.a.p., to allow it to lock */
//    if (gps_tty_path[0] != '\0') { /* do not try to open GPS device if no path set */
//        i = lgw_gps_enable(gps_tty_path, "ubx7", 0, &gps_tty_fd); /* HAL only supports u-blox 7 for now */
//        if (i != LGW_GPS_SUCCESS) {
//            printf("WARNING: [main] impossible to open %s for GPS sync (check permissions)\n", gps_tty_path);
//            gps_enabled = false;
//            gps_ref_valid = false;
//        } else {
//            printf("INFO: [main] TTY port %s open for GPS synchronization\n", gps_tty_path);
//            gps_enabled = true;
//            gps_ref_valid = false;
//        }
//    }

    /* get timezone info */
    tzset();

    /* process some of the configuration variables */
    net_mac_h = htonl((uint32_t) (0xFFFFFFFF & (lgwm >> 32)));
    net_mac_l = htonl((uint32_t) (0xFFFFFFFF & lgwm));

    // 1. Generate LoRa Gateway socket
    sock_down = socket(PF_INET, SOCK_STREAM, 0);
    if (sock_down == -1) {
        printf("sock_down = socket(PF_INET, SOCK_STREAM, 0) error!\n");
        exit(EXIT_FAILURE);
    }
    
    /* set upstream socket RX timeout */
    i = setsockopt(sock_down, SOL_SOCKET, SO_RCVTIMEO, (struct timeval *)&sock_timeout, sizeof(struct timeval));
    if (i != 0) {
        MSG("ERROR: setsockopt returned %s\n");
        exit(EXIT_FAILURE);
    }
    
    memset(&sock_down_address, 0, sizeof (sock_down));
    if (argc == 1)
        sock_down_address.sin_addr.s_addr = inet_addr(DEFAULT_SERVER);
    else
        sock_down_address.sin_addr.s_addr = inet_addr(argv[1]);
    sock_down_address.sin_family = AF_INET;
    sock_down_address.sin_port = htons(DEFAULT_PORT_UP);

    /* connect to Network Server so we can send/receive packet with the server only */
    while (1) {
        MSG("Connecting to server...");
        i = connect(sock_down, (struct sockaddr *) &sock_down_address, sizeof (sock_down_address));
        if (i != 0) {
            if(connect_attempts > 200){
                MSG("ERROR: [up] connect returned. Exit\n");
                exit(EXIT_FAILURE);
            } else {
                MSG("Failed!\n");
                connect_attempts++;
                wait_ms(5000);
            }
        } else {
            printf("\nLoRa Network Server (%s) is connected.. start\n", inet_ntoa(sock_down_address.sin_addr));
            break;
        }
    }
    
    /* Start concentrator */
    i = lgw_start();

    if (i == LGW_HAL_SUCCESS) {
        MSG("INFO: concentrator started\n");
    } else {
        MSG("ERROR: failed to start the concentrator\n");
        return EXIT_FAILURE;
    }
        
    /* spawn threads to manage upstream and downstream */
    i = pthread_create(&thrid_up, NULL, (void * (*)(void *))thread_up, NULL);
    if (i != 0) {
        MSG("ERROR: [main] impossible to create upstream thread\n");
        exit(EXIT_FAILURE);
    }

    i = pthread_create(&thrid_down, NULL, (void * (*)(void *))thread_down, NULL);
    if (i != 0) {
        MSG("ERROR: [main] impossible to create downstream thread\n");
        exit(EXIT_FAILURE);
    }

    /* spawn threads to manage time synchronization with the server */
//    i = pthread_create(&thrid_timesync_to_server, NULL, (void * (*)(void *))thread_timesync_to_server, NULL);
//    if (i != 0) {
//        MSG("ERROR: [main] impossible to create Time Sync thread\n");
//        exit(EXIT_FAILURE);
//    }
    
    i = pthread_create( &thrid_jit, NULL, (void * (*)(void *))thread_jit, NULL);
    if (i != 0) {
        MSG("ERROR: [main] impossible to create JIT thread\n");
        exit(EXIT_FAILURE);
    }
    /* Time sync to concentrator */
//    i = pthread_create( &thrid_timersync, NULL, (void * (*)(void *))thread_timersync, NULL);
//    if (i != 0) {
//        MSG("ERROR: [main] impossible to create Timer Sync thread\n");
//        exit(EXIT_FAILURE);
//    }
    
    /* configure signal handling */
    sigemptyset(&sigact.sa_mask);
    sigact.sa_flags = 0;
    sigact.sa_handler = sig_handler;
    sigaction(SIGQUIT, &sigact, NULL); /* Ctrl-\ */
    sigaction(SIGINT, &sigact, NULL); /* Ctrl-C */
    sigaction(SIGTERM, &sigact, NULL); /* default "kill" command */
    
    open_log();
    
    /* main loop task: statistics collection */
    while (!exit_sig && !quit_sig) {
        sleep(1000 * stat_interval);
        /* get timestamp for statistics */
//        t = time(NULL);
//        strftime(stat_timestamp, sizeof stat_timestamp, "%F %T %Z", gmtime(&t));
//        printf("Statistic ... %s \n", stat_timestamp);
    }

    /* wait for upstream thread to finish (1 fetch cycle max) */
    pthread_cancel(thrid_up);
    pthread_cancel(thrid_down);
    pthread_cancel(thrid_jit); /* don't wait for jit thread */
    pthread_cancel(thrid_timesync_to_server); /* don't wait for timer sync thread */

    /* if an exit signal was received, try to quit properly */
    if (exit_sig) {
        /* shut down network sockets */
//        shutdown(sock_up, SHUT_RDWR);
        shutdown(sock_down, SHUT_RDWR);
        /* stop the hardware */
        i = lgw_stop();
        if (i == LGW_HAL_SUCCESS) {
            MSG("INFO: concentrator stopped successfully\n");
        } else {
            MSG("WARNING: failed to stop concentrator successfully\n");
        }
        close_log();
    }
    MSG("INFO: Exiting RT-LoRa Gateway program\n");
    exit(EXIT_SUCCESS);
}

static void print_tx_status(uint8_t tx_status) {
    switch (tx_status) {
        case TX_OFF:
            MSG("INFO: [thread_down] lgw_status returned TX_OFF\n");
            break;
        case TX_FREE:
            MSG("INFO: [thread_down] lgw_status returned TX_FREE\n");
            break;
        case TX_EMITTING:
            MSG("INFO: [thread_down] lgw_status returned TX_EMITTING\n");
            break;
        case TX_SCHEDULED:
            MSG("INFO: [thread_down] lgw_status returned TX_SCHEDULED\n");
            break;
        default:
            MSG("INFO: [thread_down] lgw_status returned UNKNOWN (%d)\n", tx_status);
            break;
    }
}
/* -------------------------------------------------------------------------- */

/* --- THREAD 1: RECEIVING PACKETS AND FORWARDING THEM ---------------------- */

void thread_up(void) {

    int i, j; /* loop variables */
    unsigned pkt_in_dgram; /* nb on Lora packet in the current datagram */

    /* allocate memory for packet fetching and processing */
    struct lgw_pkt_rx_s rxpkt[NB_PKT_MAX]; /* array containing inbound packets + metadata */
    struct lgw_pkt_rx_s *p; /* pointer on a RX packet */
    int nb_pkt;

    /* data buffers */
    uint8_t buff_up[TX_BUFF_SIZE]; /* buffer to compose the upstream packet */
    int buff_index;
    unsigned int msg_start_index, msg_end_index;   // These variables to keep track of the starting index 
                                                    // and end index of the current msg in buff_up
    uint8_t buff_ack[32]; /* buffer to receive acknowledges */

    /* protocol variables */
    uint8_t token_h; /* random token for acknowledgement matching */
    uint8_t token_l; /* random token for acknowledgement matching */

    /* ping measurement variables */
    struct timespec send_time;
    struct timespec recv_time;

    /* report management variable */
    bool send_report = false;

    /* mote info variables */
    uint16_t mote_addr = 0;
    uint16_t mote_fcnt = 0;

    /* set upstream socket RX timeout */
//    i = setsockopt(sock_up, SOL_SOCKET, SO_RCVTIMEO, (void *)&push_timeout_half, sizeof push_timeout_half);
//    if (i != 0) {
//        MSG("ERROR: [up] setsockopt returned %s\n", strerror(errno));
//        exit(EXIT_FAILURE);
//    }

    /* pre-fill the data buffer with fixed fields */
    buff_up[0] = PROTOCOL_VERSION;
    buff_up[3] = PKT_UPLINK_DATA;
    *(uint32_t *)(buff_up + 4) = net_mac_h;
    *(uint32_t *)(buff_up + 8) = net_mac_l;

    while (!exit_sig && !quit_sig) {
        /* fetch packets */
        pthread_mutex_lock(&mx_concent);
        nb_pkt = lgw_receive(NB_PKT_MAX, rxpkt);
        pthread_mutex_unlock(&mx_concent);
        if (nb_pkt == LGW_HAL_ERROR) {
            MSG("ERROR: [up] failed packet fetch, exiting\n");
            exit(EXIT_FAILURE);
        }

        /* check if there are status report to send */
        send_report = report_ready; /* copy the variable so it doesn't change mid-function */
        /* no mutex, we're only reading */

        /* wait a short time if no packets, nor status report */
        if ((nb_pkt == 0) && (send_report == false)) {
            wait_ms(FETCH_SLEEP_MS);
            continue;
        }

        /* start composing datagram with the header */
        token_h = (uint8_t)rand(); /* random token */
        token_l = (uint8_t)rand(); /* random token */
        buff_up[1] = token_h;
        buff_up[2] = token_l;
        buff_index = 12; /* 12-byte header */

        /* start of JSON structure */
        memcpy((void *)(buff_up + buff_index), (void *)"{\"rxpk\":[", 9);
        buff_index += 9;

        /* serialize Lora packets metadata and payload */
        pkt_in_dgram = 0;
        for (i=0; i < nb_pkt; ++i) {
            p = &rxpkt[i];

            /* Get mote information from current packet (addr, fcnt) */
            /* Device Address */
            mote_addr  = p->payload[1];
            mote_addr |= p->payload[2] << 8;

            /* Sequence number */
//            mote_fcnt  = p->payload[5];
//            mote_fcnt |= p->payload[6] << 8;

            /* basic packet filtering */
            switch(p->status) {
                case STAT_CRC_OK:
                    printf( "INFO: RCV UPLINK MSG (addr %u)\n", mote_addr);
                    break;
                case STAT_CRC_BAD:
                case STAT_NO_CRC:
                    printf( "INFO: RCV UPLINK MSG (addr %u) (CRC BAD). IGNORE!\n", mote_addr);
#if LOGGING_ENABLED
                    fprintf(log_file, "INFO: RCV UPLINK MSG (addr %u) (CRC BAD). IGNORE!\n", mote_addr);
#endif
                    continue; /* skip that packet */
                    break;
                default:
                    MSG("WARNING: [up] received packet with unknown status %u (size %u, modulation %u, BW %u, DR %u, RSSI %.1f)\n", p->status, p->size, p->modulation, p->bandwidth, p->datarate, p->rssi);
                    continue; /* skip that packet */
                    // exit(EXIT_FAILURE);
            }

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

//            /* Packet concentrator channel, RF chain & RX frequency, 34-36 useful chars */
//            j = snprintf((char *)(buff_up + buff_index), TX_BUFF_SIZE-buff_index, ",\"chan\":%1u,\"rfch\":%1u,\"freq\":%.6lf", p->if_chain, p->rf_chain, ((double)p->freq_hz / 1e6));
//            if (j > 0) {
//                buff_index += j;
//            } else {
//                MSG("ERROR: [up] snprintf failed line %u\n", (__LINE__ - 4));
//                exit(EXIT_FAILURE);
//            }

            /* Packet modulation, 13-14 useful chars */
            if (p->modulation == MOD_LORA) {
//                memcpy((void *)(buff_up + buff_index), (void *)",\"modu\":\"LORA\"", 14);
//                buff_index += 14;

                /* Lora datarate & bandwidth, 16-19 useful chars */
                switch (p->datarate) {
                    case DR_LORA_SF7:
                        memcpy((void *)(buff_up + buff_index), (void *)"\"datr\":\"SF7", 11);
                        buff_index += 11;
                        break;
                    case DR_LORA_SF8:
                        memcpy((void *)(buff_up + buff_index), (void *)"\"datr\":\"SF8", 11);
                        buff_index += 11;
                        break;
                    case DR_LORA_SF9:
                        memcpy((void *)(buff_up + buff_index), (void *)"\"datr\":\"SF9", 11);
                        buff_index += 11;
                        break;
                    case DR_LORA_SF10:
                        memcpy((void *)(buff_up + buff_index), (void *)"\"datr\":\"SF10", 12);
                        buff_index += 12;
                        break;
                    case DR_LORA_SF11:
                        memcpy((void *)(buff_up + buff_index), (void *)"\"datr\":\"SF11", 12);
                        buff_index += 12;
                        break;
                    case DR_LORA_SF12:
                        memcpy((void *)(buff_up + buff_index), (void *)"\"datr\":\"SF12", 12);
                        buff_index += 12;
                        break;
                    default:
                        MSG("ERROR: [up] lora packet with unknown datarate\n");
                        buff_index = msg_start_index;   // point to start index of the message
                        continue;
//                        memcpy((void *)(buff_up + buff_index), (void *)"\"datr\":\"SF?", 11);
//                        buff_index += 11;
//                        exit(EXIT_FAILURE);
                }
                switch (p->bandwidth) {
                    case BW_125KHZ:
                        memcpy((void *)(buff_up + buff_index), (void *)"BW125\"", 6);
                        buff_index += 6;
                        break;
                    case BW_250KHZ:
                        memcpy((void *)(buff_up + buff_index), (void *)"BW250\"", 6);
                        buff_index += 6;
                        break;
                    case BW_500KHZ:
                        memcpy((void *)(buff_up + buff_index), (void *)"BW500\"", 6);
                        buff_index += 6;
                        break;
                    default:
                        MSG("ERROR: [up] lora packet with unknown bandwidth\n");
                        buff_index = msg_start_index;   // point to start index of the message
                        continue;
//                        memcpy((void *)(buff_up + buff_index), (void *)"BW?\"", 4);
//                        buff_index += 4;
//                        exit(EXIT_FAILURE);
                }

                /* Lora SNR, 11-13 useful chars */
                j = snprintf((char *)(buff_up + buff_index), TX_BUFF_SIZE-buff_index, ",\"lsnr\":%.1f", p->snr);
                if (j > 0) {
                    buff_index += j;
                } else {
                    MSG("ERROR: [up] snprintf failed line %u\n", (__LINE__ - 4));
                    buff_index = msg_start_index;   // point to start index of the message
                    continue;
//                    exit(EXIT_FAILURE);
                }
            } else if (p->modulation == MOD_FSK) {
                memcpy((void *)(buff_up + buff_index), (void *)",\"modu\":\"FSK\"", 13);
                buff_index += 13;

                /* FSK datarate, 11-14 useful chars */
                j = snprintf((char *)(buff_up + buff_index), TX_BUFF_SIZE-buff_index, ",\"datr\":%u", p->datarate);
                if (j > 0) {
                    buff_index += j;
                } else {
                    MSG("ERROR: [up] snprintf failed line %u\n", (__LINE__ - 4));
                    buff_index = msg_start_index;   // point to start index of the message
                    continue;
//                    exit(EXIT_FAILURE);
                }
            } else {
                MSG("ERROR: [up] received packet with unknown modulation\n");
                buff_index = msg_start_index;   // point to start index of the message
                continue;
//                exit(EXIT_FAILURE);
            }

            /* Packet RSSI, payload size, 18-23 useful chars */
            j = snprintf((char *)(buff_up + buff_index), TX_BUFF_SIZE-buff_index, ",\"rssi\":%.0f,\"size\":%u", p->rssi, p->size);
            if (j > 0) {
                buff_index += j;
            } else {
                MSG("ERROR: [up] snprintf failed line %u\n", (__LINE__ - 4));
                buff_index = msg_start_index;   // point to start index of the message
                continue;
//                exit(EXIT_FAILURE);
            }

            /* Packet base64-encoded payload, 14-350 useful chars */
            memcpy((void *)(buff_up + buff_index), (void *)",\"data\":\"", 9);
            buff_index += 9;
            j = bin_to_b64(p->payload, p->size, (char *)(buff_up + buff_index), 341); /* 255 bytes = 340 chars in b64 + null char */
            if (j>=0) {
                buff_index += j;
            } else {
                MSG("ERROR: [up] bin_to_b64 failed line %u\n", (__LINE__ - 5));
                buff_index = msg_start_index;   // point to start index of the message
                continue;
//                exit(EXIT_FAILURE);
            }
            buff_up[buff_index] = '"';
            ++buff_index;

            /* End of packet serialization */
            buff_up[buff_index] = '}';
            ++buff_index;
            ++pkt_in_dgram;
        }
        
        /* restart fetch sequence without sending empty JSON if all packets have been filtered out */
        if (pkt_in_dgram == 0) {
            /* all packet have been filtered out and no report, restart loop */
            continue;
        } else {
            /* end of packet array */
            buff_up[buff_index] = ']';
            ++buff_index;
        }

        /* end of JSON datagram payload */
        buff_up[buff_index] = '}';
        ++buff_index;
        buff_up[buff_index] = 0; /* add string terminator, for safety */

//        printf("\nJSON up: %s\n", (char *)(buff_up + 12)); /* DEBUG: display JSON payload */

        /* send datagram to server */
        send(sock_down, (void *)buff_up, buff_index, 0);
//        clock_gettime(CLOCK_MONOTONIC, &send_time);
    }
    MSG("\nINFO: End of upstream thread\n");
    
    exit(EXIT_FAILURE);
}

/* -------------------------------------------------------------------------- */

/* --- THREAD 2: POLLING SERVER AND ENQUEUING PACKETS IN JIT QUEUE ---------- */

void thread_down(void) {
    int i; /* loop variables */

    /* configuration and metadata for an outbound packet */
    struct lgw_pkt_tx_s txpkt;
    bool sent_immediate = false; /* option to sent the packet immediately */
    
    /* data buffers */
    uint8_t buff_down[DOWNSTREAM_BUF_SIZE]; /* buffer to receive downstream packets */
    int msg_len;

    /* protocol variables */
//    uint8_t token_h; /* random token for acknowledgement matching */
//    uint8_t token_l; /* random token for acknowledgement matching */

    /* JSON parsing variables */
    JSON_Value *root_val = NULL;
    JSON_Object *txpk_obj = NULL;
    JSON_Value *val = NULL; /* needed to detect the absence of some fields */
    const char *str; /* pointer to sub-strings in the JSON data */
    short x0, x1;
    uint64_t x2;
    double x3, x4;
    
    struct timeval current_time;
    struct timeval unix_timeval = {0, 0};
    int offset_previous = 0; // save previous offset value to calculate time drift
    struct timeval time_temp_1 = {0,0}; // temporary variable to save operand value
    struct timeval time_temp_2 = {0,0}; // temporary variable to save operand value
    struct timeval time_temp_3 = {0,0}; // temporary variable to save operand value
    
    bool timesync_res = false; /* keep track of whether TIMESYNC_REQ was acknowledged or not */
    
    /* Just In Time downlink */
    struct timeval tx_unix_timestamp;
    struct timeval current_unix_time;
    struct timeval current_concentrator_time;
    enum jit_error_e jit_result = JIT_ERROR_OK;
    enum jit_pkt_type_e downlink_type;
    
#if LOGGING_ENABLED
    static int log_count = 0;   // for rotating log file if it is too long
#endif
    
    /* JIT queue initialization */
    jit_queue_init(&jit_queue);
    /* loop */
    while (!exit_sig && !quit_sig) {
        timesync_res = false;
        /* try to receive a datagram */
        msg_len = recv(sock_down, (void *) buff_down, (sizeof buff_down) - 1, 0);
        /* if no network message was received, got back to listening sock_down socket */
        if (msg_len == -1) {
            //MSG("WARNING: [down] recv returned %s\n", strerror(errno)); /* too verbose */
            continue;
        }
        
        if (msg_len == 0){
            // server is stopped
            MSG("Cannot connect to server. Stop program!\n");
            exit_sig = true;
            break;
        }
        
        /* if the datagram does not respect protocol, just ignore it */
        if ((msg_len < 12) || (buff_down[0] != PROTOCOL_VERSION)) {
            MSG("DOWN: [down] ignoring invalid packet len=%d, protocol_version=%d\n", msg_len, buff_down[0]);
            continue;
        }

        if(!((buff_down[3] == PKT_DOWNLINK_DATA) || (buff_down[3] == PKT_UPLINK_ACK) || (buff_down[3] == PKT_TIMESYNC_RES)))  {
            MSG("DOWN: [down] ignoring unknown packet type (%d)\n", buff_down[3]);
            continue;
        }
        
        switch (buff_down[3]) {
            case PKT_TIMESYNC_RES:
                if ((buff_down[1] == timesync_var.token_h) && (buff_down[2] == timesync_var.token_l)) {
                    if (timesync_res) {
                        MSG("DOWN duplicate RES received :)\n");
                    } else { /* if that packet was not already acknowledged */
                        timesync_res = true;
                        /* Get current unix time */
                        gettimeofday(&unix_timeval, NULL);
                        timesync_var.t3 = unix_timeval;
                        /* Retrieve timestamp t1 and t2 from response packet */
                        timesync_var.t1.tv_sec = *(uint32_t *) (buff_down + 4);
                        timesync_var.t1.tv_usec = *(uint32_t *) (buff_down + 8);
                        timesync_var.t2.tv_sec = *(uint32_t *) (buff_down + 12);
                        timesync_var.t2.tv_usec = *(uint32_t *) (buff_down + 16);

                        /* Save previous offset */
                        offset_previous = timesync_var.time_offset;

                        /* Compute new offset between server and gateway, with microsecond precision */
                        TIMERSUB(timesync_var.t1, timesync_var.t0, time_temp_1);
                        TIMERSUB(timesync_var.t2, timesync_var.t3, time_temp_2);
                        timesync_var.time_offset = time_temp_1.tv_sec * 1000000 + time_temp_1.tv_usec \
                                                    + time_temp_2.tv_sec *1000000 + time_temp_2.tv_usec;
                        timesync_var.time_offset = timesync_var.time_offset/2;
                        timesync_var.offset_drift = timesync_var.time_offset - offset_previous;

                        if(abs(timesync_var.time_offset) > TIMESYNC_MAX_ERROR_TOLERANT){
                            timesync_var.flag = TIMESYNC_ERROR;
                            
                            MSG("T0: %ld.%ld\n", timesync_var.t0.tv_sec, timesync_var.t0.tv_usec);
                            MSG("T1: %ld.%ld\n", timesync_var.t1.tv_sec, timesync_var.t1.tv_usec);
                            MSG("T2: %ld.%ld\n", timesync_var.t2.tv_sec, timesync_var.t2.tv_usec);
                            MSG("T3: %ld.%ld\n", timesync_var.t3.tv_sec, timesync_var.t3.tv_usec);
                        } else
                            timesync_var.flag = TIMESYNC_DONE;
                    }
                } else { /* out-of-sync token */
                    timesync_var.flag = TIMESYNC_ERROR;
                    MSG("INFO: [down] received out-of-sync ACK\n");
                }
                break;
            case PKT_DOWNLINK_DATA:
                buff_down[msg_len] = 0; /* add string terminator, just to be safe */
                gettimeofday(&current_time, NULL);
//                MSG("\nJSON down: %s\n", (char *)(buff_down + 4)); /* DEBUG: display JSON payload */
                /* initialize TX struct and try to parse JSON */
                memset(&txpkt, 0, sizeof txpkt);
                root_val = json_parse_string_with_comments((const char *)(buff_down + 4)); /* JSON offset */
                if (root_val == NULL) {
                    MSG("WARNING: [down] invalid JSON, TX aborted\n");
                    continue;
                }
                
                /* look for JSON sub-object 'txpk' */
                txpk_obj = json_object_get_object(json_value_get_object(root_val), "txpk");
                if (txpk_obj == NULL) {
                    MSG("WARNING: [down] no \"txpk\" object in JSON, TX aborted\n");
                    json_value_free(root_val);
                    continue;
                }
                
                /* Parse Tx UNIX timestamp value (mandatory) */
                val = json_object_get_value(txpk_obj,"tm_s");
                if (val == NULL) {
                    MSG("WARNING: [down] no mandatory \"tm_s\" object in JSON, TX aborted\n");
                    json_value_free(root_val);
                    continue;
                } else {
                    tx_unix_timestamp.tv_sec = (uint32_t)json_value_get_number(val);
                }
                
                val = json_object_get_value(txpk_obj,"tm_us");
                if (val == NULL) {
                    MSG("WARNING: [down] no mandatory \"tm_us\" object in JSON, TX aborted\n");
                    json_value_free(root_val);
                    continue;
                } else {
                    tx_unix_timestamp.tv_usec = (uint32_t)json_value_get_number(val);
                }
                
                /* Parse "immediate" tag, or target timestamp, or UTC time to be converted by GPS (mandatory) */
                i = json_object_get_boolean(txpk_obj,"imme"); /* can be 1 if true, 0 if false, or -1 if not a JSON boolean */
                if (i == 1) {
                    /* TX procedure: send immediately */
                    sent_immediate = true;
//                    downlink_type = JIT_PKT_TYPE_DOWNLINK_CLASS_C;
//                    MSG("INFO: [down] a packet will be sent in \"immediate\" mode\n");
                } else {
                    sent_immediate = false;
                    // TODO: implement send on timestamp
                }
                
                /* parse target frequency (mandatory) */
                val = json_object_get_value(txpk_obj,"freq");
                if (val == NULL) {
                    MSG("WARNING: [down] no mandatory \"txpk.freq\" object in JSON, TX aborted\n");
                    json_value_free(root_val);
                    continue;
                }
                txpkt.freq_hz = (uint32_t)((double)(1.0e6) * json_value_get_number(val));
                
                /* parse RF chain used for TX (mandatory) */
                val = json_object_get_value(txpk_obj,"rfch");
                if (val == NULL) {
                    MSG("WARNING: [down] no mandatory \"txpk.rfch\" object in JSON, TX aborted\n");
                    json_value_free(root_val);
                    continue;
                }
                txpkt.rf_chain = (uint8_t)json_value_get_number(val);

                /* parse TX power (optional field) */
                val = json_object_get_value(txpk_obj,"powe");
                if (val != NULL) {
                    txpkt.rf_power = (int8_t)json_value_get_number(val) - antenna_gain;
                }
                
                /* Parse modulation (mandatory) */
                str = json_object_get_string(txpk_obj, "modu");
                if (str == NULL) {
                    MSG("WARNING: [down] no mandatory \"txpk.modu\" object in JSON, TX aborted\n");
                    json_value_free(root_val);
                    continue;
                }
                if (strcmp(str, "LORA") == 0) {
                    /* Lora modulation */
                    txpkt.modulation = MOD_LORA;

                    /* Parse Lora spreading-factor and modulation bandwidth (mandatory) */
                    str = json_object_get_string(txpk_obj, "datr");
                    if (str == NULL) {
                        MSG("WARNING: [down] no mandatory \"txpk.datr\" object in JSON, TX aborted\n");
                        json_value_free(root_val);
                        continue;
                    }
                    i = sscanf(str, "SF%2hdBW%3hd", &x0, &x1);
                    if (i != 2) {
                        MSG("WARNING: [down] format error in \"txpk.datr\", TX aborted\n");
                        json_value_free(root_val);
                        continue;
                    }
                    switch (x0) {
                        case  7: txpkt.datarate = DR_LORA_SF7;  break;
                        case  8: txpkt.datarate = DR_LORA_SF8;  break;
                        case  9: txpkt.datarate = DR_LORA_SF9;  break;
                        case 10: txpkt.datarate = DR_LORA_SF10; break;
                        case 11: txpkt.datarate = DR_LORA_SF11; break;
                        case 12: txpkt.datarate = DR_LORA_SF12; break;
                        default:
                            MSG("WARNING: [down] format error in \"txpk.datr\", invalid SF, TX aborted\n");
                            json_value_free(root_val);
                            continue;
                    }
                    switch (x1) {
                        case 125: txpkt.bandwidth = BW_125KHZ; break;
                        case 250: txpkt.bandwidth = BW_250KHZ; break;
                        case 500: txpkt.bandwidth = BW_500KHZ; break;
                        default:
                            MSG("WARNING: [down] format error in \"txpk.datr\", invalid BW, TX aborted\n");
                            json_value_free(root_val);
                            continue;
                    }

                    /* Parse ECC coding rate (optional field) */
                    str = json_object_get_string(txpk_obj, "codr");
                    if (str == NULL) {
                        MSG("WARNING: [down] no mandatory \"txpk.codr\" object in json, TX aborted\n");
                        json_value_free(root_val);
                        continue;
                    }
                    if      (strcmp(str, "4/5") == 0) txpkt.coderate = CR_LORA_4_5;
                    else if (strcmp(str, "4/6") == 0) txpkt.coderate = CR_LORA_4_6;
                    else if (strcmp(str, "2/3") == 0) txpkt.coderate = CR_LORA_4_6;
                    else if (strcmp(str, "4/7") == 0) txpkt.coderate = CR_LORA_4_7;
                    else if (strcmp(str, "4/8") == 0) txpkt.coderate = CR_LORA_4_8;
                    else if (strcmp(str, "1/2") == 0) txpkt.coderate = CR_LORA_4_8;
                    else {
                        MSG("WARNING: [down] format error in \"txpk.codr\", TX aborted\n");
                        json_value_free(root_val);
                        continue;
                    }

                    /* Parse signal polarity switch (optional field) */
                    val = json_object_get_value(txpk_obj,"ipol");
                    if (val != NULL) {
                        txpkt.invert_pol = (bool)json_value_get_boolean(val);
                    }

                    /* parse Lora preamble length (optional field, optimum min value enforced) */
                    val = json_object_get_value(txpk_obj,"prea");
                    if (val != NULL) {
                        i = (int)json_value_get_number(val);
                        if (i >= MIN_LORA_PREAMB) {
                            txpkt.preamble = (uint16_t)i;
                        } else {
                            txpkt.preamble = (uint16_t)MIN_LORA_PREAMB;
                        }
                    } else {
                        txpkt.preamble = (uint16_t)STD_LORA_PREAMB;
                    }

                } else if (strcmp(str, "FSK") == 0) {
                    /* TODO FSK modulation */
                    MSG("WARNING: [down] FSK implementation is required. TX aborted\n");
                    continue;
                } else {
                    MSG("WARNING: [down] invalid modulation in \"txpk.modu\", TX aborted\n");
                    json_value_free(root_val);
                    continue;
                }
                
                /* Parse payload length (mandatory) */
                val = json_object_get_value(txpk_obj,"size");
                if (val == NULL) {
                    MSG("WARNING: [down] no mandatory \"txpk.size\" object in JSON, TX aborted\n");
                    json_value_free(root_val);
                    continue;
                }
                txpkt.size = (uint16_t)json_value_get_number(val);
                
                /* Parse payload data (mandatory) */
                str = json_object_get_string(txpk_obj, "data");
                if (str == NULL) {
                    MSG("WARNING: [down] no mandatory \"txpk.data\" object in JSON, TX aborted\n");
                    json_value_free(root_val);
                    continue;
                }
                i = b64_to_bin(str, strlen(str), txpkt.payload, sizeof txpkt.payload);
                if (i != txpkt.size) {
                    MSG("WARNING: [down] mismatch between .size and .data size once converter to binary\n");
                }

                /* free the JSON parse tree from memory */
                json_value_free(root_val);
                /* select TX mode */
                if (sent_immediate) {
                    txpkt.tx_mode = IMMEDIATE;
                } else {
                    txpkt.tx_mode = TIMESTAMPED;
                }
 
                /* TODO: record measurement data */
                
                /* check TX parameter before trying to queue packet */
                jit_result = JIT_ERROR_OK;
//                if ((txpkt.freq_hz < tx_freq_min[txpkt.rf_chain]) || (txpkt.freq_hz > tx_freq_max[txpkt.rf_chain])) {
//                    jit_result = JIT_ERROR_TX_FREQ;
//                    MSG("ERROR: Packet REJECTED, unsupported frequency - %u (min:%u,max:%u)\n", txpkt.freq_hz, tx_freq_min[txpkt.rf_chain], tx_freq_max[txpkt.rf_chain]);
//                }
                
                for (i=0; i<txlut.size; i++) {
                    if (txlut.lut[i].rf_power == txpkt.rf_power) {
                        /* this RF power is supported, we can continue */
                        break;
                    }
                }
                if (i == txlut.size) {
                    /* this RF power is not supported */
                    jit_result = JIT_ERROR_TX_POWER;
                    MSG("ERROR: Packet REJECTED, unsupported RF power for TX - %d\n", txpkt.rf_power);
                }
                
                /* insert packet to be sent into JIT queue */
                if (jit_result == JIT_ERROR_OK) {
                    jit_result = jit_enqueue(&jit_queue, tx_unix_timestamp, &txpkt, JIT_PKT_TYPE_DOWNLINK);
                    if (jit_result != JIT_ERROR_OK) {
                        printf("\nTimestamp: %ld.%06ld\n", tx_unix_timestamp.tv_sec, tx_unix_timestamp.tv_usec);
                        printf("ERROR: Packet REJECTED (jit error=%d)\n", jit_result);
#if LOGGING_ENABLED
                        fprintf(log_file, "\nTimestamp: %ld.%06ld\n", tx_unix_timestamp.tv_sec, tx_unix_timestamp.tv_usec);
                        fprintf(log_file, "ERROR: Packet REJECTED (jit error=%d)\n", jit_result);
#endif                        
                    }
                }
                /* Send acknoledge datagram to server */
//                send_tx_ack(buff_down[1], buff_down[2], jit_result);
                /* Enqueue downlink packet to JIT */
#if LOGGING_ENABLED
                // After forwarding one DL message, increase log count
                log_count++;
#endif
                break;
            case PKT_UPLINK_ACK:
                MSG("INFO: Receive UPLINK ACK\n");
                break;
            default:
                MSG("INFO: ignoring invalid packet len=%d, id=%d\n", msg_len, buff_down[3]);
                break;
        }

#if LOGGING_ENABLED        
        if(log_count == 2000){
            fclose(log_file);
            open_log();
            log_count = 0;
        }
#endif
    }

    MSG("\nINFO: End of downstream thread\n");
}

/* -------------------------------------------------------------------------- */
/* --- THREAD 3: CHECKING PACKETS TO BE SENT FROM JIT QUEUE AND SEND THEM --- */

void thread_jit(void) {
    int result = LGW_HAL_SUCCESS;
    struct lgw_pkt_tx_s pkt;
    int pkt_index = -1;
    struct timeval current_unix_time;
    enum jit_error_e jit_result;
    enum jit_pkt_type_e pkt_type;
    uint8_t tx_status;
    
    struct timeval time_stamp;
    time_t local_current_time;
    struct tm* ptime;

    while (!exit_sig && !quit_sig) {
        /* transfer data and metadata to the concentrator, and schedule TX */
        jit_result = jit_peek(&jit_queue, NULL, &pkt_index);
        if (jit_result == JIT_ERROR_OK) {
            if (pkt_index > -1) {
                jit_result = jit_dequeue(&jit_queue, pkt_index, &pkt, &pkt_type);
                if (jit_result == JIT_ERROR_OK) {
                    /* check if concentrator is free for sending new packet */
                    pthread_mutex_lock(&mx_concent); /* may have to wait for a fetch to finish */
                    result = lgw_status(TX_STATUS, &tx_status);
                    pthread_mutex_unlock(&mx_concent); /* free concentrator ASAP */
                    if (result == LGW_HAL_ERROR) {
                        MSG("WARNING: [jit] lgw_status failed\n");
                    } else {
                        if (tx_status == TX_EMITTING) {
                            MSG("ERROR: concentrator is currently emitting\n");
                            print_tx_status(tx_status);
                            continue;
                        } else if (tx_status == TX_SCHEDULED) {
                            MSG("WARNING: a downlink was already scheduled, overwritting it...\n");
                            print_tx_status(tx_status);
                        } else {
                            /* Nothing to do */
                        }
                    }
                    
                    /* Check timesync before transmit a downlink packet*/
//                    if((timesync_var.flag != TIMESYNC_DONE)) {
//                        MSG("Time sync error (offset=%d). Tx aborted!\n", timesync_var.time_offset);
//                        continue;
//                    }
                           
                    /* send packet to concentrator */
                    pthread_mutex_lock(&mx_concent); /* may have to wait for a fetch to finish */
                    result = lgw_send(pkt);
                    pthread_mutex_unlock(&mx_concent); /* free concentrator ASAP */
                    if (result == LGW_HAL_ERROR) {
                        MSG("WARNING: [jit] lgw_send failed\n");
                        continue;
                    } else {
                        gettimeofday(&current_unix_time, NULL);
                        time(&local_current_time);
                        ptime = localtime(&local_current_time);
                        
                        MSG("\nFORWARD DOWNLINK MSG at %ld.%06ld", current_unix_time.tv_sec, current_unix_time.tv_usec);
                        MSG(" (%d-%02d-%02d %2d:%02d:%02d)\n",ptime->tm_year + 1900, ptime->tm_mon + 1, ptime->tm_mday, \
                                (ptime->tm_hour) % 24, ptime->tm_min, ptime->tm_sec);
#if LOGGING_ENABLED                        
                        fprintf(log_file, "\nFORWARD DOWNLINK MSG at %ld.%06ld", current_unix_time.tv_sec, current_unix_time.tv_usec);
                        fprintf(log_file, " (%d-%02d-%02d %2d:%02d:%02d)\n",ptime->tm_year + 1900, ptime->tm_mon + 1, ptime->tm_mday, \
                                (ptime->tm_hour) % 24, ptime->tm_min, ptime->tm_sec);
#endif                        
                    }
                } else {
                    MSG("ERROR: jit_dequeue failed with %d\n", jit_result);
                }
            }
        } else if (jit_result == JIT_ERROR_EMPTY) {
            /* Do nothing, it can happen */
            usleep(10000);
        } else {
            MSG("ERROR: jit_peek failed with %d\n", jit_result);
        }
    }
}

/* -------------------------------------------------------------------------- */

/* --- THREAD 3: TIME SYNCHRONIZATION WITH THE SERVER ----------------------- */

void thread_timesync_to_server(void) {
    
    static unsigned int sync_interval;
    struct timeval unix_timeval = {0, 0};
    uint8_t buff_req[20]; /* buffer to compose time sync requests */
    uint8_t buff_req_size = 0;

    /* pre-fill the time sync request buffer with fixed fields */
    buff_req[0] = PROTOCOL_VERSION;
    buff_req[3] = PKT_TIMESYNC_REQ;
    *(uint32_t *) (buff_req + 4) = net_mac_h;
    *(uint32_t *) (buff_req + 8) = net_mac_l;

    /* reset timing variables */
    timesync_var.token_h = 0;
    timesync_var.token_l = 0;
    timesync_var.t0 = (struct timeval){0};
    timesync_var.t1 = (struct timeval){0};
    timesync_var.t2 = (struct timeval){0};
    timesync_var.t3 = (struct timeval){0};
    timesync_var.time_offset = 0;
    timesync_var.offset_drift = 0;
    timesync_var.flag = TIMESYNC_DONE;
    sync_interval = TIMESYNC_SHORT_INTERVAL;
    while (!exit_sig && !quit_sig) {
        /* Send TIMESYNC_REQUEST packet*/
        /* generate random token for request */
        timesync_var.token_h = (uint8_t) rand(); /* random token */
        timesync_var.token_l = (uint8_t) rand(); /* random token */
        buff_req[1] = timesync_var.token_h;
        buff_req[2] = timesync_var.token_l;
        buff_req_size = 12;
        /* Get current unix time */
        gettimeofday(&unix_timeval, NULL);

        /* send time sync request and record time */
        send(sock_down, (void *) buff_req, buff_req_size, 0);

        /* record t0 timestamp */
        timesync_var.t0 = unix_timeval;
        timesync_var.flag = TIMESYNC_WAITING;

        wait_ms(100);
        MSG("TIME SYNC TO SERVER INFO:\n");
        MSG("    offset = %dus\n", timesync_var.time_offset);
        MSG("    drift = %dus\n", timesync_var.offset_drift);
        if (timesync_var.flag != TIMESYNC_DONE) {
            timesync_var.flag = TIMESYNC_ERROR;
            MSG("    offset > %d us. Timesync ERROR\n", TIMESYNC_MAX_ERROR_TOLERANT);
            /* delay for short time */
            wait_ms(TIMESYNC_SHORT_INTERVAL);
        } else {
            /* delay for long time */
            wait_ms(TIMESYNC_LONG_INTERVAL);
        }
    }
}



/* --- EOF ------------------------------------------------------------------ */
