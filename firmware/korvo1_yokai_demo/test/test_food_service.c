/* Run: cc -std=c11 -Wall -Wextra -Werror test/test_food_service.c -o /tmp/test_food && /tmp/test_food */
#define HOST_TEST 1
#define FOOD_SLOT_A "/tmp/yokai_food_test_a"
#define FOOD_SLOT_B "/tmp/yokai_food_test_b"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "../main/storage_service.c"
#include "../main/food_service.c"

int main(void)
{
    remove(FOOD_SLOT_A); remove(FOOD_SLOT_B);
    assert(food_service_init() == FOOD_OK);
    assert(food_service_count() == 0);
    int d;
    assert(food_date_ordinal("2024-02-29", &d));
    int next; assert(food_date_ordinal("2024-03-01", &next) && next == d + 1);
    assert(!food_date_ordinal("2023-02-29", &d));
    assert(!food_date_ordinal("2100-01-01", &d));
    assert(!food_date_ordinal("2019-12-31", &d));
    assert(food_date_ordinal("2020-02-29", &d));
    assert(food_date_ordinal("2099-12-31", &d));
    assert(!food_date_ordinal("2024-04-31", &d));
    assert(!food_date_ordinal("2024-2-29", &d));
    assert(food_service_add("", "2024-01-01") == FOOD_INVALID);
    assert(food_service_add("   ", "2024-01-01") == FOOD_INVALID);
    assert(food_service_add("\xc0\xaf", "2024-01-01") == FOOD_INVALID);
    assert(food_service_add("bad\nname", "2024-01-01") == FOOD_INVALID);
    assert(food_service_add("\xed\xa0\x80", "2024-01-01") == FOOD_INVALID);
    assert(food_service_add("\xf4\x90\x80\x80", "2024-01-01") == FOOD_INVALID);
    assert(food_service_add("\xe3\x81", "2024-01-01") == FOOD_INVALID);
    char longname[50]; memset(longname, 'a', sizeof(longname)); longname[49] = 0;
    assert(food_service_add(longname, "2024-01-01") == FOOD_INVALID);
    assert(food_service_add("Milk", "2023-02-29") == FOOD_INVALID);
    assert(food_service_add("  Milk  ", "2024-02-29") == FOOD_OK);
    assert(strcmp(food_service_get(0)->name, "Milk") == 0);
    longname[48] = 0;
    assert(food_service_edit(0, longname, "2024-01-01") == FOOD_OK);
    assert(strlen(food_service_get(0)->name) == FOOD_NAME_BYTES);
    assert(food_service_edit(0, "豆腐", "2024-03-01") == FOOD_OK);
    assert(food_service_init() == FOOD_OK);
    assert(strcmp(food_service_get(0)->name, "豆腐") == 0);
    assert(food_service_edit(1, "Missing", "2024-01-01") == FOOD_INVALID);
    for (unsigned i = 1; i < FOOD_MAX_RECORDS; ++i) assert(food_service_add("Rice", "2025-01-01") == FOOD_OK);
    assert(food_service_add("Too many", "2025-01-01") == FOOD_FULL);
    assert(food_service_delete(0) == FOOD_OK);
    assert(food_service_count() == FOOD_MAX_RECORDS - 1);
    assert(food_service_init() == FOOD_OK);
    /* Fail writing the inactive slot; live memory and latest disk must survive. */
    const char *inactive = s_active_slot == 0 ? FOOD_SLOT_B : FOOD_SLOT_A;
    remove(inactive); assert(mkdir(inactive, 0700) == 0);
    assert(food_service_edit(0, "Changed", "2025-01-01") == FOOD_IO);
    assert(strcmp(food_service_get(0)->name, "Rice") == 0);
    assert(food_service_init() == FOOD_OK);
    assert(strcmp(food_service_get(0)->name, "Rice") == 0);
    assert(rmdir(inactive) == 0);
    /* A corrupt newest slot falls back to previous complete snapshot. */
    assert(food_service_edit(0, "Newest", "2025-01-01") == FOOD_OK);
    const char *active = s_active_slot == 0 ? FOOD_SLOT_A : FOOD_SLOT_B;
    FILE *f = fopen(active, "r+b"); assert(f);
    assert(fseek(f, offsetof(food_file_t, records), SEEK_SET) == 0);
    assert(fputc('X', f) != EOF); assert(fclose(f) == 0);
    assert(food_service_init() == FOOD_OK);
    assert(strcmp(food_service_get(0)->name, "Rice") == 0);
    f = fopen(FOOD_SLOT_A, "wb"); assert(f); fputs("bad", f); fclose(f);
    f = fopen(FOOD_SLOT_B, "wb"); assert(f); fputs("bad", f); fclose(f);
    assert(food_service_init() == FOOD_CORRUPT);
    assert(food_service_add("Never overwrite", "2025-01-01") == FOOD_CORRUPT);
    f = fopen(FOOD_SLOT_A, "rb"); assert(f); char buf[4] = {0}; assert(fread(buf, 1, 3, f) == 3); fclose(f);
    assert(strcmp(buf, "bad") == 0);
    s_storage_state = STORAGE_STATE_MOUNT_FAILED;
    assert(food_service_init() == FOOD_STORAGE);
    assert(food_service_add("No mount", "2025-01-01") == FOOD_STORAGE);
    remove(FOOD_SLOT_A); remove(FOOD_SLOT_B);
    puts("Food service tests passed");
    return 0;
}
