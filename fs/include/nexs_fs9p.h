/*
 * fs/include/nexs_fs9p.h — 9P2000 server
 */
#ifndef NEXS_FS9P_H
#define NEXS_FS9P_H
#pragma once
#include <stdint.h>
#include <stddef.h>

/* 9P2000 message types */
#define P9_TVERSION 100
#define P9_RVERSION 101
#define P9_TATTACH  104
#define P9_RATTACH  105
#define P9_TWALK    110
#define P9_RWALK    111
#define P9_TOPEN    112
#define P9_ROPEN    113
#define P9_TREAD    116
#define P9_RREAD    117
#define P9_TWRITE   118
#define P9_RWRITE   119
#define P9_TCLUNK   120
#define P9_RCLUNK   121
#define P9_TSTAT    124
#define P9_RSTAT    125

void p9_server_init(void);
int  p9_server_handle(const uint8_t *req, size_t req_len,
                      uint8_t *resp, size_t resp_max);

#endif
