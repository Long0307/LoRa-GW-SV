/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   time_conversion.h
 * Author:
 *
 * Created on October 14, 2020, 9:22 PM
 */

#ifndef TIME_CONVERSION_H
#define TIME_CONVERSION_H

#define TIMESPEC_ADD(A,B) /* A += B */ \
do {                                   \
    (A).tv_sec += (B).tv_sec;          \
    (A).tv_nsec += (B).tv_nsec;        \
    if ( (A).tv_nsec >= 1000000000 ) { \
        (A).tv_sec++;                  \
        (A).tv_nsec -= 1000000000;     \
    }                                  \
} while (0)

#define TIMESPEC_SUB(A,B) /* A -= B */ \
do {                                   \
    (A).tv_sec -= (B).tv_sec;          \
    (A).tv_nsec -= (B).tv_nsec;        \
    if ( (A).tv_nsec < 0 ) {           \
        (A).tv_sec--;                  \
        (A).tv_nsec += 1000000000;     \
    }                                  \
} while (0)

#define	TIMESPEC_TO_TIMEVAL(tv, ts)       \
do {                                      \
    (tv).tv_sec = (ts).tv_sec;            \
    (tv).tv_usec = (ts).tv_nsec / 1000;   \
} while (0)
    
#define TIMESPEC_TO_MILLISECONDS(ts, ml)     \
do {                                        \
    ml = ((ts).tv_sec * (uint64_t)1000);    \
    ml += (ts).tv_nsec / 1000000;           \
} while (0)

#define TIMESPEC_TO_MICROSECONDS(ts, us)     \
do {                                        \
    us = ((ts).tv_sec * (uint64_t)1000000);    \
    us += (ts).tv_nsec / 1000;           \
} while (0)

#define TIMEVAL_ADD(A,B) /* A += B */ \
do {                                   \
    (A).tv_sec += (B).tv_sec;          \
    (A).tv_usec += (B).tv_usec;        \
    if ( (A).tv_usec >= 1000000 ) {    \
        (A).tv_sec++;                  \
        (A).tv_usec -= 1000000;        \
    }                                  \
} while (0)
    
#define TIMEVAL_SUB(A,B) /* A -= B */  \
do {                                   \
    (A).tv_sec -= (B).tv_sec;          \
    (A).tv_usec -= (B).tv_usec;        \
    if ( (A).tv_usec < 0 ) {           \
        (A).tv_sec--;                  \
        (A).tv_usec += 1000000;        \
    }                                  \
} while (0)

#define TIMEVAL_TO_TIMESPEC(tv, ts)       \
do {                                      \
    (ts).tv_sec = (tv).tv_sec;            \
    (ts).tv_nsec = (tv).tv_usec * 1000;   \
} while (0)

#define TIMEVAL_TO_MILLISECONDS(tv, ml)     \
do {                                        \
    ml = ((tv).tv_sec * (uint64_t)1000);    \
    ml += (tv).tv_usec / 1000;            \
} while (0)

#define TIMEVAL_TO_MICROSECONDS(tv, mc)     \
do {                                        \
    mc = ((tv).tv_sec * (uint64_t)1000000); \
    mc += (tv).tv_usec;                     \
} while (0)
    
#endif /* TIME_CONVERSION_H */

