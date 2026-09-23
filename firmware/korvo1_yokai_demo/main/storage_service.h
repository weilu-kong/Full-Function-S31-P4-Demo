#pragma once
#include <stdbool.h>

#ifdef HOST_TEST
#ifndef _ESP_ERR_T_DEFINED_
#define _ESP_ERR_T_DEFINED_
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#endif
#else
#include "esp_err.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STORAGE_STATE_UNINITIALIZED = 0,
    STORAGE_STATE_READY,
    STORAGE_STATE_MOUNT_FAILED,
} storage_state_t;

esp_err_t app_storage_init(void);
bool app_storage_is_ready(void);
storage_state_t app_storage_state(void);
esp_err_t app_storage_format(void);

#ifdef __cplusplus
}
#endif
