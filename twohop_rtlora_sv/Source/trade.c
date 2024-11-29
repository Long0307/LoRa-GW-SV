/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include "trade.h"
#include <string.h>

/* clock and log file management */
time_t log_start_time;
FILE * log_file = NULL;
time_t log_time;

pthread_mutex_t mutexLogFile = PTHREAD_MUTEX_INITIALIZER;

_Bool open_log(void) {
    int i;
    char iso_date[20];
    
    char log_file_name[64];

    strftime(iso_date, ARRAY_SIZE(iso_date),"%Y%m%dT%H%M%SZ",gmtime(&log_time)); /* format yyyymmddThhmmssZ */
    log_start_time = log_time; /* keep track of when the log was started, for log rotation */

    sprintf(log_file_name, "twohoplora_%s.csv", iso_date);
    log_file = fopen(log_file_name, "a"); /* create log file, append if file already exist */
    if (log_file == NULL) {
        MSG("ERROR: impossible to create log file %s\n", log_file_name);
        return false;
    }

    i = fprintf(log_file, "START LOG\n");
    if (i < 0) {
        MSG("ERROR: impossible to write to log file %s\n", log_file_name);
        return false;
    }

    MSG("INFO: Now writing to log file %s\n", log_file_name);
    return true;
}

