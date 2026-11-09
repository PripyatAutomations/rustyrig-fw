/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef RRCLIENT_BACKUP_H
#define RRCLIENT_BACKUP_H
#include <stdint.h>
#include <stdbool.h>
#define RRCLIENT_BACKUP_REQUEST "client.backup.request"
#define RRCLIENT_BACKUP_RESULT "client.backup.result"
enum rrclient_backup_action { RR_BACKUP_SQL = 1, RR_BACKUP_ALL = 2, RR_BACKUP_IMPORT = 3 };
typedef struct {
   int action;
   char path[1024];
   char password[256];
} rrclient_backup_request_t;
typedef struct {
   bool ok;
   bool restore;
   char message[256];
} rrclient_backup_result_t;
void rrclient_backup_register(void);
#endif
