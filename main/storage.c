/* SD pins from the official example (Arduino/examples/01_audio_out, ESP-IDF esp_sdcard_port.cpp):
 * SDMMC 1-bit, CLK=GPIO11 CMD=GPIO10 D0=GPIO9. */
#include "storage.h"
#include "settings.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "wear_levelling.h"
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "storage";

#define SD_MOUNT    "/sdcard"
#define FLASH_MOUNT "/flash"

static sdmmc_card_t *s_card;
static wl_handle_t s_wl = WL_INVALID_HANDLE;
static char s_dir[32];
static bool s_is_sd;

static bool s_sd_probed;   /* probe the SD slot once per boot (rescan on request): a missing card must not spam the log */

static esp_err_t try_sd(void)
{
    if (s_card) return ESP_OK;
    if (s_sd_probed) return ESP_ERR_NOT_FOUND;
    s_sd_probed = true;
    esp_vfs_fat_sdmmc_mount_config_t mount = { .format_if_mount_failed = false, .max_files = 4, .allocation_unit_size = 16 * 1024 };
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = GPIO_NUM_11;
    slot.cmd = GPIO_NUM_10;
    slot.d0 = GPIO_NUM_9;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_err_t err = esp_vfs_fat_sdmmc_mount(SD_MOUNT, &host, &slot, &mount, &s_card);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "SD card mounted: %s, %llu MB", s_card->cid.name,
                 ((uint64_t)s_card->csd.capacity * s_card->csd.sector_size) >> 20);
    } else {
        ESP_LOGW(TAG, "no SD card (%s)", esp_err_to_name(err));
        s_card = NULL;
    }
    return err;
}

static esp_err_t try_flash(void)
{
    if (s_wl != WL_INVALID_HANDLE) return ESP_OK;
    esp_vfs_fat_mount_config_t mount = { .format_if_mount_failed = true, .max_files = 4, .allocation_unit_size = 4096 };
    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(FLASH_MOUNT, "storage", &mount, &s_wl);
    if (err != ESP_OK) ESP_LOGE(TAG, "flash storage mount failed: %s", esp_err_to_name(err));
    else ESP_LOGI(TAG, "flash storage mounted at %s", FLASH_MOUNT);
    return err;
}

esp_err_t storage_rescan(void)
{
    if (!s_card) s_sd_probed = false;
    return storage_init();
}

esp_err_t storage_init(void)
{
    if (try_sd() == ESP_OK) {
        s_is_sd = true;
        snprintf(s_dir, sizeof s_dir, "%s/DCIM", SD_MOUNT);
    } else if (try_flash() == ESP_OK) {
        s_is_sd = false;
        snprintf(s_dir, sizeof s_dir, "%s/DCIM", FLASH_MOUNT);
    } else {
        s_dir[0] = 0;
        return ESP_FAIL;
    }
    mkdir(s_dir, 0777);
    return ESP_OK;
}

bool storage_is_sd(void) { return s_is_sd; }
const char *storage_dir(void) { return s_dir[0] ? s_dir : NULL; }

static int cmp_names(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }

int storage_list(char (*names)[STORAGE_NAME_LEN])
{
    if (!s_dir[0]) return 0;
    DIR *d = opendir(s_dir);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL && n < STORAGE_MAX_FILES) {
        size_t l = strlen(e->d_name);
        if (l < 5 || l >= STORAGE_NAME_LEN) continue;
        const char *ext = e->d_name + l - 4;
        if (strcasecmp(ext, ".jpg") != 0) continue;
        strcpy(names[n++], e->d_name);
    }
    closedir(d);
    qsort(names, n, STORAGE_NAME_LEN, cmp_names);
    return n;
}

esp_err_t storage_save_jpeg(const uint8_t *jpeg, size_t len, char *name, size_t name_len)
{
    if (!s_dir[0]) return ESP_ERR_INVALID_STATE;
    if (!s_is_sd) {
        char (*names)[STORAGE_NAME_LEN] = heap_caps_malloc(STORAGE_MAX_FILES * STORAGE_NAME_LEN, MALLOC_CAP_SPIRAM);
        int n = names ? storage_list(names) : 0;
        free(names);
        if (n >= STORAGE_FLASH_MAX) return ESP_ERR_NO_MEM;
    }
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    if (t.tm_year + 1900 >= 2024) {
        strftime(name, name_len, "%Y%m%d-%H%M%S.jpg", &t);
    } else {
        char buf[12];
        int seq = settings_get_str("photo_seq", buf, sizeof buf) ? atoi(buf) : 0;
        seq++;
        snprintf(buf, sizeof buf, "%d", seq);
        settings_set_str("photo_seq", buf);
        snprintf(name, name_len, "IMG_%05d.jpg", seq);
    }
    char path[80];
    snprintf(path, sizeof path, "%s/%s", s_dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "cannot create %s", path);
        return ESP_FAIL;
    }
    size_t w = fwrite(jpeg, 1, len, f);
    fclose(f);
    if (w != len) {
        unlink(path);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "saved %s (%u bytes)", path, (unsigned)len);
    return ESP_OK;
}

esp_err_t storage_read(const char *name, uint8_t **data, size_t *len)
{
    if (!s_dir[0]) return ESP_ERR_INVALID_STATE;
    char path[80];
    snprintf(path, sizeof path, "%s/%s", s_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return ESP_ERR_NOT_FOUND;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 2 * 1024 * 1024) { fclose(f); return ESP_ERR_INVALID_SIZE; }
    uint8_t *buf = heap_caps_malloc((size_t)sz, MALLOC_CAP_SPIRAM);
    if (!buf) { fclose(f); return ESP_ERR_NO_MEM; }
    size_t r = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (r != (size_t)sz) { free(buf); return ESP_FAIL; }
    *data = buf;
    *len = r;
    return ESP_OK;
}

esp_err_t storage_delete(const char *name)
{
    if (!s_dir[0]) return ESP_ERR_INVALID_STATE;
    char path[80];
    snprintf(path, sizeof path, "%s/%s", s_dir, name);
    return unlink(path) == 0 ? ESP_OK : ESP_FAIL;
}
