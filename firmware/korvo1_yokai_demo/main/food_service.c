#include "food_service.h"
#include "storage_service.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef FOOD_SLOT_A
#define FOOD_SLOT_A "/storage/food_a.dat"
#define FOOD_SLOT_B "/storage/food_b.dat"
#endif
#define FOOD_MAGIC 0x464F4F44u

typedef struct {
    uint32_t magic, generation, count;
    food_record_t records[FOOD_MAX_RECORDS];
    uint32_t crc;
} food_file_t;
/* Two 1936-byte buffers; no full snapshot on the LVGL task's stack. */
static food_file_t s_food, s_work;
static int s_active_slot = -1;
static food_result_t s_status = FOOD_STORAGE;
static const char *const s_paths[] = {FOOD_SLOT_A, FOOD_SLOT_B};

static uint32_t crc32(const void *data, size_t len)
{
    const unsigned char *p = data;
    uint32_t crc = UINT32_MAX;
    while (len--) {
        crc ^= *p++;
        for (unsigned i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

bool food_date_ordinal(const char *date, int *ordinal)
{
    if (!date || !ordinal || strlen(date) != 10 || date[4] != '-' || date[7] != '-') return false;
    for (int i = 0; i < 10; ++i) if (i != 4 && i != 7 && (date[i] < '0' || date[i] > '9')) return false;
    int y = (date[0]-'0')*1000 + (date[1]-'0')*100 + (date[2]-'0')*10 + date[3]-'0';
    int m = (date[5]-'0')*10 + date[6]-'0', d = (date[8]-'0')*10 + date[9]-'0';
    static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (y < 2020 || y > 2099 || m < 1 || m > 12 || d < 1 || d > days[m-1] + (m == 2 && y % 4 == 0)) return false;
    int n = 0;
    for (int year = 2020; year < y; ++year) n += 365 + (year % 4 == 0);
    for (int month = 1; month < m; ++month) n += days[month-1] + (month == 2 && y % 4 == 0);
    *ordinal = n + d - 1;
    return true;
}

static bool make_record(food_record_t *out, const char *name, const char *expiry)
{
    int ordinal;
    if (!name || !food_date_ordinal(expiry, &ordinal)) return false;
    size_t n = 0;
    while (n <= FOOD_NAME_BYTES && name[n]) ++n;
    if (n > FOOD_NAME_BYTES) return false;
    while (n && name[n-1] == ' ') --n;
    while (n && *name == ' ') { ++name; --n; }
    if (!n) return false;
    /* Validate complete UTF-8, excluding controls, overlong encodings and surrogates. */
    for (size_t i = 0; i < n;) {
        unsigned char c = (unsigned char)name[i++];
        if (c < 0x80) { if (c < 0x20 || c == 0x7f) return false; continue; }
        unsigned extra; uint32_t cp, minimum;
        if (c >= 0xc2 && c <= 0xdf) { extra = 1; cp = c & 31; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { extra = 2; cp = c & 15; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { extra = 3; cp = c & 7; minimum = 0x10000; }
        else return false;
        if (i + extra > n) return false;
        while (extra--) { c = (unsigned char)name[i++]; if ((c & 0xc0) != 0x80) return false; cp = (cp << 6) | (c & 63); }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff) || (cp >= 0x80 && cp <= 0x9f)) return false;
    }
    memset(out, 0, sizeof(*out)); memcpy(out->name, name, n); memcpy(out->expiry, expiry, 10);
    return true;
}

static bool valid_file(const food_file_t *file)
{
    if (file->magic != FOOD_MAGIC || file->count > FOOD_MAX_RECORDS || file->crc != crc32(file, offsetof(food_file_t, crc))) return false;
    for (size_t i = 0; i < file->count; ++i) {
        const food_record_t *r = &file->records[i]; food_record_t checked;
        if (!memchr(r->name, 0, sizeof(r->name)) || !memchr(r->expiry, 0, sizeof(r->expiry)) || !make_record(&checked, r->name, r->expiry)) return false;
    }
    return true;
}

static food_result_t read_slot(int slot, bool *missing)
{
    *missing = false;
    FILE *f = fopen(s_paths[slot], "rb");
    if (!f) { *missing = errno == ENOENT; return *missing ? FOOD_OK : FOOD_IO; }
    bool ok = fread(&s_work, 1, sizeof(s_work), f) == sizeof(s_work);
    if (ok) ok = fgetc(f) == EOF && !ferror(f);
    if (fclose(f) != 0) ok = false;
    return ok && valid_file(&s_work) ? FOOD_OK : FOOD_CORRUPT;
}

food_result_t food_service_init(void)
{
    if (!app_storage_is_ready()) return s_status = FOOD_STORAGE;
    bool found = false, all_missing = true;
    food_result_t failure = FOOD_CORRUPT;
    for (int slot = 0; slot < 2; ++slot) {
        bool missing; food_result_t result = read_slot(slot, &missing);
        all_missing &= missing;
        if (result == FOOD_IO) failure = FOOD_IO;
        if (result == FOOD_OK && !missing && (!found || (int32_t)(s_work.generation - s_food.generation) > 0)) {
            s_food = s_work; s_active_slot = slot; found = true;
        }
    }
    if (!found && !all_missing) return s_status = failure;
    if (!found) { memset(&s_food, 0, sizeof(s_food)); s_food.magic = FOOD_MAGIC; s_active_slot = -1; }
    return s_status = FOOD_OK;
}

size_t food_service_count(void) { return s_food.count; }
const food_record_t *food_service_get(size_t index) { return index < s_food.count ? &s_food.records[index] : NULL; }

static food_result_t commit(void)
{
    if (!app_storage_is_ready()) return FOOD_STORAGE;
    s_work.generation = s_food.generation + 1;
    s_work.crc = crc32(&s_work, offsetof(food_file_t, crc));
    int target = s_active_slot == 0 ? 1 : 0;
    FILE *f = fopen(s_paths[target], "wb");
    if (!f) return FOOD_IO;
    bool ok = fwrite(&s_work, 1, sizeof(s_work), f) == sizeof(s_work);
    if (fflush(f) != 0) ok = false;
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(s_paths[target]); return FOOD_IO; }
    /* Verify without overwriting the candidate or the live snapshot. */
    f = fopen(s_paths[target], "rb");
    if (!f) { remove(s_paths[target]); return FOOD_IO; }
    unsigned char chunk[128]; size_t offset = 0;
    while (ok && offset < sizeof(s_work)) {
        size_t n = sizeof(s_work) - offset; if (n > sizeof(chunk)) n = sizeof(chunk);
        ok = fread(chunk, 1, n, f) == n && memcmp(chunk, (unsigned char *)&s_work + offset, n) == 0;
        offset += n;
    }
    if (ok) ok = fgetc(f) == EOF && !ferror(f);
    if (fclose(f) != 0) ok = false;
    if (!ok) { remove(s_paths[target]); return FOOD_IO; }
    s_food = s_work; s_active_slot = target;
    return FOOD_OK;
}

food_result_t food_service_edit(size_t index, const char *name, const char *expiry)
{
    if (s_status != FOOD_OK) return s_status;
    if (index >= s_food.count) return FOOD_INVALID;
    s_work = s_food;
    if (!make_record(&s_work.records[index], name, expiry)) return FOOD_INVALID;
    return commit();
}
food_result_t food_service_add(const char *name, const char *expiry)
{
    if (s_status != FOOD_OK) return s_status;
    if (s_food.count == FOOD_MAX_RECORDS) return FOOD_FULL;
    s_work = s_food;
    if (!make_record(&s_work.records[s_work.count], name, expiry)) return FOOD_INVALID;
    ++s_work.count;
    return commit();
}
food_result_t food_service_delete(size_t index)
{
    if (s_status != FOOD_OK) return s_status;
    if (index >= s_food.count) return FOOD_INVALID;
    s_work = s_food;
    memmove(&s_work.records[index], &s_work.records[index+1], (s_work.count-index-1)*sizeof(food_record_t));
    memset(&s_work.records[--s_work.count], 0, sizeof(food_record_t));
    return commit();
}
const char *food_service_error(food_result_t result)
{
    switch (result) {
    case FOOD_OK: return "";
    case FOOD_INVALID: return "名前は1〜48バイト、日付は2020〜2099年のYYYY-MM-DDで入力";
    case FOOD_FULL: return "登録上限は32件です";
    case FOOD_STORAGE: return "保存領域を使用できません";
    case FOOD_IO: return "読み込み・保存に失敗しました。保存領域を確認してください";
    case FOOD_CORRUPT: return "保存データを読み込めません。上書きは停止しています";
    }
    return "エラー";
}
