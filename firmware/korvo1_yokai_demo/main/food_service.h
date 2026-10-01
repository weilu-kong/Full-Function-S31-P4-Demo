#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define FOOD_MAX_RECORDS 32
#define FOOD_NAME_BYTES 48
/* Calls are serialized by the LVGL task; pointers remain valid until a mutation. */
typedef struct { char name[FOOD_NAME_BYTES + 1]; char expiry[11]; } food_record_t;
typedef enum { FOOD_OK, FOOD_INVALID, FOOD_FULL, FOOD_STORAGE, FOOD_IO, FOOD_CORRUPT } food_result_t;
food_result_t food_service_init(void);
size_t food_service_count(void);
const food_record_t *food_service_get(size_t index);
food_result_t food_service_add(const char *name, const char *expiry);
food_result_t food_service_edit(size_t index, const char *name, const char *expiry);
food_result_t food_service_delete(size_t index);
bool food_date_ordinal(const char *date, int *ordinal);
const char *food_service_error(food_result_t result);
#ifdef __cplusplus
}
#endif
