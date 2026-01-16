#include "include/settings.h"

#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_err.h>
#include <nvs_flash.h>
#include <cJSON.h>

static const char *TAG = "SETTINGS";
static const char *NVS_STORAGE = "settings_nvs";

static settings_handler_t settings_handler;
static void              *handler_arg;

#ifdef CONFIG_SETTINGS_DATETIME_SUPPORT
static void datetime_gettimeofday(setting_datetime_t *setting)
{
    time_t    t;
    struct tm lt;

    time(&t);
    localtime_r(&t, &lt);

    setting->time.hh = lt.tm_hour;
    setting->time.mm = lt.tm_min;

    setting->date.day = lt.tm_mday;
    setting->date.month = lt.tm_mon + 1;
    setting->date.year = lt.tm_year + 1900;
}

/* set system date and time from setting */
static int datetime_settimeofday(setting_datetime_t *setting)
{
    struct tm      tm;
    struct timeval timeval;

    tm.tm_year = setting->date.year - 1900;
    tm.tm_mon = setting->date.month - 1;
    tm.tm_mday = setting->date.day;

    tm.tm_hour = setting->time.hh;
    tm.tm_min = setting->time.mm;
    tm.tm_sec = 0;
    tm.tm_isdst = -1; //use timezone data to determine if DST is used

    timeval.tv_sec = mktime(&tm);
    timeval.tv_usec = 0;

    return settimeofday(&timeval, NULL);
}
#endif

esp_err_t settings_pack_update_nvs_ids(const settings_group_t *pack)
{
    char   nvs_id[128];
    size_t id_len;
    esp_err_t rc;

    for (const settings_group_t *gr = pack; gr->id; gr++) {
        for (setting_t *setting = gr->settings; setting->id; setting++) {
            snprintf(nvs_id, SETTINGS_NVS_ID_LEN, "%s:%s", gr->id, setting->id);
            id_len = strnlen(nvs_id, sizeof(nvs_id));
            if (id_len >= NVS_KEY_NAME_MAX_SIZE - 1) {
                ESP_LOGE(TAG, "NVS key too long (%u >= %d): %s", id_len, NVS_KEY_NAME_MAX_SIZE - 1, nvs_id);
                rc = ESP_ERR_INVALID_ARG;
                return rc;
            }
            strncpy(setting->nvs_id, nvs_id, SETTINGS_NVS_ID_LEN);
        }
    }
    return ESP_OK;  
}

void settings_pack_print(const settings_group_t *settings_pack)
{
    printf("Settings:\n");
    for (const settings_group_t *gr = settings_pack; gr->label; gr++) {
        printf("gr %s\n", gr->label);
        for (setting_t *setting = gr->settings; setting->label; setting++) {
            printf("- %s: ", setting->label);
            switch (setting->type) {
            case SETTING_TYPE_BOOL:
                printf("%s\n", setting->boolean.val ? "ENABLED" : "DISABLED");
                break;
            case SETTING_TYPE_NUM:
                printf("%d\n", setting->num.val);
                break;
            case SETTING_TYPE_ONEOF:
                printf("%s\n", setting->oneof.options[setting->oneof.val]);
                break;
            case SETTING_TYPE_TEXT:
                printf("%s\n", setting->text.val);
                break;
#ifdef CONFIG_SETTINGS_DATETIME_SUPPORT
            case SETTING_TYPE_TIME:
                printf("%02d:%02d\n", setting->time.hh, setting->time.mm);
                break;
            case SETTING_TYPE_DATE:
                printf("%02d-%02d-%04d\n", setting->date.day, setting->date.month, setting->date.year);
                break;
            case SETTING_TYPE_DATETIME:
                datetime_gettimeofday(&setting->datetime);
                printf("%02d:%02d %02d-%02d-%04d\n",
                       setting->datetime.time.hh, //
                       setting->datetime.time.mm, //
                       setting->datetime.date.day, setting->datetime.date.month, setting->datetime.date.year);
                break;
#endif
#ifdef CONFIG_SETTINGS_TIMEZONE_SUPPORT
            case SETTING_TYPE_TIMEZONE:
                printf("%s\n", setting->timezone.val);
                break;
#endif
#ifdef CONFIG_SETTINGS_COLOR_SUPPORT
            case SETTING_TYPE_COLOR:
                printf("#%02x%02x%02x\n", setting->color.r, setting->color.g, setting->color.b);
                break;
#endif
            default:
                break;
            }
        }
    }
}

setting_t *settings_pack_find(const settings_group_t *pack, const char *gr_id, const char *id)
{
    for (const settings_group_t *gr = pack; gr->id; gr++) {
        if (strcmp(gr_id, gr->id))
            continue;
        for (setting_t *setting = gr->settings; setting->id; setting++) {
            if (!strcmp(id, setting->id))
                return setting;
        }
    }
    return NULL;
}

void setting_set_defaults(setting_t *setting)
{
    switch (setting->type) {
    case SETTING_TYPE_BOOL:
        setting->boolean.val = setting->boolean.def;
        break;
    case SETTING_TYPE_NUM:
        setting->num.val = setting->num.def;
        break;
    case SETTING_TYPE_ONEOF:
        setting->oneof.val = setting->oneof.def;
        break;
    case SETTING_TYPE_TEXT:
        strncpy(setting->text.val, setting->text.def, setting->text.len);
        break;
#ifdef CONFIG_SETTINGS_DATETIME_SUPPORT
    case SETTING_TYPE_TIME:
        memset(&setting->time, 0, sizeof(setting_time_t));
        break;
    case SETTING_TYPE_DATE:
        memset(&setting->date, 0, sizeof(setting_date_t));
        break;
    case SETTING_TYPE_DATETIME:
        datetime_gettimeofday(&setting->datetime);
        break;
#endif
#ifdef CONFIG_SETTINGS_TIMEZONE_SUPPORT
    case SETTING_TYPE_TIMEZONE:
        strncpy(setting->timezone.val, setting->timezone.def, setting->timezone.len);
        break;
#endif
#ifdef CONFIG_SETTINGS_COLOR_SUPPORT
    case SETTING_TYPE_COLOR:
        break;
#endif
    default:
        break;
    }
}

void settings_pack_set_defaults(const settings_group_t *settings_pack)
{
    for (const settings_group_t *gr = settings_pack; gr->label; gr++) {
        for (setting_t *setting = gr->settings; setting->label; setting++)
            setting_set_defaults(setting);
    }
}

void setting_set_bool(setting_t *setting, const bool value)
{
    setting->boolean.val = value;
    #ifdef CONFIG_SETTINGS_CALLBACK_SUPPORT
    if(setting->on_set_callback)
        setting->on_set_callback(setting);
    #endif
}

void setting_set_num(setting_t *setting, const int value)
{
    if (value < setting->num.range[0] || value > setting->num.range[1])
        return;

    setting->num.val = value;
    #ifdef CONFIG_SETTINGS_CALLBACK_SUPPORT
    if(setting->on_set_callback)
        setting->on_set_callback(setting);
    #endif
}

void setting_set_oneof(setting_t *setting, const int index)
{
    int labels_count = 0;
    for (const char **label = setting->oneof.options; *label != NULL; label++)
        labels_count++;
           
    if (index < 0 || index >= labels_count)
        return;

    setting->oneof.val = index;
    #ifdef CONFIG_SETTINGS_CALLBACK_SUPPORT
    if(setting->on_set_callback)
        setting->on_set_callback(setting);
    #endif
}

void setting_set_text(setting_t *setting, const char *text)
{
    strncpy(setting->text.val, text, setting->text.len);
    #ifdef CONFIG_SETTINGS_CALLBACK_SUPPORT
    if(setting->on_set_callback)
        setting->on_set_callback(setting);
    #endif
}

#ifdef CONFIG_SETTINGS_DATETIME_SUPPORT
void setting_set_time(setting_t *setting, const setting_time_t *time)   
{
    setting->time.hh = time->hh;
    setting->time.mm = time->mm;
    #ifdef CONFIG_SETTINGS_CALLBACK_SUPPORT
    if(setting->on_set_callback)
        setting->on_set_callback(setting);
    #endif
}
void setting_set_date(setting_t *setting, const setting_date_t *date)
{
    setting->date.day = date->day;
    setting->date.month = date->month;
    setting->date.year = date->year;
    #ifdef CONFIG_SETTINGS_CALLBACK_SUPPORT
    if(setting->on_set_callback)
        setting->on_set_callback(setting);
    #endif
}
void setting_set_datetime(setting_t *setting, const setting_datetime_t *datetime)
{
    setting->datetime.date.day = datetime->date.day;
    setting->datetime.date.month = datetime->date.month;
    setting->datetime.date.year = datetime->date.year;
    setting->datetime.time.hh = datetime->time.hh;
    setting->datetime.time.mm = datetime->time.mm;
    #ifdef CONFIG_SETTINGS_CALLBACK_SUPPORT
    if(setting->on_set_callback)
        setting->on_set_callback(setting);
    #endif
}
#endif

#ifdef CONFIG_SETTINGS_TIMEZONE_SUPPORT
void setting_set_timezone(setting_t *setting, const char *timezone)
{
    strncpy(setting->timezone.val, timezone, setting->timezone.len);
    #ifdef CONFIG_SETTINGS_CALLBACK_SUPPORT
    if(setting->on_set_callback)
        setting->on_set_callback(setting);
    #endif
}
#endif

#ifdef CONFIG_SETTINGS_COLOR_SUPPORT
void setting_set_color(setting_t *setting, const setting_color_t *color)
{
    setting->color = *color;
    #ifdef CONFIG_SETTINGS_CALLBACK_SUPPORT
    if(setting->on_set_callback)
        setting->on_set_callback(setting);
    #endif
}
#endif


esp_err_t settings_nvs_read(const settings_group_t *settings_pack)
{
    nvs_handle nvs;
    esp_err_t  rc;

    ESP_LOGI(TAG, "NVS init");
    nvs_flash_init();

    settings_pack_set_defaults(settings_pack);
    settings_pack_update_nvs_ids(settings_pack);

    rc = nvs_open(NVS_STORAGE, NVS_READONLY, &nvs);
    if (rc == ESP_OK) {
        for (const settings_group_t *gr = settings_pack; gr->id; gr++) {
            for (setting_t *setting = gr->settings; setting->id; setting++) {
                switch (setting->type) {
                case SETTING_TYPE_BOOL: {
                    bool val;
                    if(nvs_get_i8(nvs, setting->nvs_id, (int8_t *)&val) == ESP_OK)
                        setting_set_bool(setting, val);
                } break;
                case SETTING_TYPE_NUM: {
                    int32_t val;
                    if(nvs_get_i32(nvs, setting->nvs_id, (int32_t *)&val) == ESP_OK)
                        setting_set_num(setting, val);
                } break;
                case SETTING_TYPE_ONEOF: {
                    int8_t val;
                    if(nvs_get_i8(nvs, setting->nvs_id, &val) == ESP_OK)
                        setting_set_oneof(setting, val);
                } break;
                case SETTING_TYPE_TEXT: {
                    char buf[1024];
                    size_t len = setting->text.len;
      
                    if(nvs_get_str(nvs, setting->nvs_id, buf, &len) == ESP_OK)
                        setting_set_text(setting, buf);
                } break;
#ifdef CONFIG_SETTINGS_DATETIME_SUPPORT
                case SETTING_TYPE_TIME: {
                    uint16_t val;
                    setting_time_t time;

                    if(nvs_get_u16(nvs, setting->nvs_id, &val) == ESP_OK){
                        time.hh = (val >> 8);
                        time.mm = (val & 0xFF);
                        setting_set_time(setting, &time);
                    }
                } break;
                case SETTING_TYPE_DATE: {
                    uint32_t val;
                    setting_date_t date;

                    if(nvs_get_u32(nvs, setting->nvs_id, &val) == ESP_OK){
                        date.day = (val >> 24 & 0xFF);
                        date.month = (val >> 16 & 0xFF);
                        date.year = (val & 0xFFFF);
                        setting_set_date(setting, &date);
                    }
                } break;
                case SETTING_TYPE_DATETIME:
                    /* this is current date and time on device not from nvs */
                    datetime_gettimeofday(&setting->datetime);
                    break;
#endif
#ifdef CONFIG_SETTINGS_TIMEZONE_SUPPORT
                case SETTING_TYPE_TIMEZONE: {
                    size_t len = setting->timezone.len;
                    if(nvs_get_str(nvs, setting->nvs_id, setting->timezone.val, &len) == ESP_OK)
                        setting_set_timezone(setting, setting->timezone.val);
                } break;
#endif
#ifdef CONFIG_SETTINGS_COLOR_SUPPORT
                case SETTING_TYPE_COLOR: {
                    setting_color_t color;
                    if(nvs_get_u32(nvs, setting->nvs_id, &color.combined) == ESP_OK)
                        setting_set_color(setting, &color);
                } break;
#endif
                default:
                    break;
                }
            }
        }
        nvs_close(nvs);
    } else {
        ESP_LOGW(TAG, "nvs open error %s", esp_err_to_name(rc));
    }
    return ESP_OK;
}

static esp_err_t setting_nvs_write(setting_t *setting,  nvs_handle_t nvs)
{
    esp_err_t rc;

    rc = nvs_open(NVS_STORAGE, NVS_READWRITE, &nvs);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nvs open error %s", esp_err_to_name(rc));
        return rc;
    }

    switch (setting->type) {
    case SETTING_TYPE_BOOL:
        rc = nvs_set_i8(nvs, setting->nvs_id, setting->boolean.val);
        break;
    case SETTING_TYPE_NUM:
        rc = nvs_set_i32(nvs, setting->nvs_id, setting->num.val);
        break;
    case SETTING_TYPE_ONEOF:
        rc = nvs_set_i8(nvs, setting->nvs_id, setting->oneof.val);
        break;
    case SETTING_TYPE_TEXT:
        rc = nvs_set_str(nvs, setting->nvs_id, setting->text.val);
        break;
#ifdef CONFIG_SETTINGS_DATETIME_SUPPORT
    case SETTING_TYPE_TIME: {
        uint16_t val = (setting->time.hh << 8) | setting->time.mm;
        rc = nvs_set_u16(nvs, setting->nvs_id, val);
    } break;
    case SETTING_TYPE_DATE: {
        uint32_t val = 0;
        val |= ((uint32_t)(setting->date.day & 0xFF) << 24);
        val |= ((uint32_t)(setting->date.month & 0xFF) << 16);
        val |= ((uint32_t)(setting->date.year & 0xFFFF));
        rc = nvs_set_u32(nvs, setting->nvs_id, val);
    } break;
    case SETTING_TYPE_DATETIME:
        /* set date and time on device - do not store in nvs */
        rc = datetime_settimeofday(&setting->datetime);
        break;
#endif
#ifdef CONFIG_SETTINGS_TIMEZONE_SUPPORT
    case SETTING_TYPE_TIMEZONE:
        rc = nvs_set_str(nvs, setting->nvs_id, setting->timezone.val);
        break;
#endif
#ifdef CONFIG_SETTINGS_COLOR_SUPPORT
    case SETTING_TYPE_COLOR: {
        rc = nvs_set_u32(nvs, setting->nvs_id, setting->color.combined);
    } break;
#endif
    default:
        rc = ESP_ERR_NOT_SUPPORTED;
        break;
    }
    return rc;
}

esp_err_t setting_nvs_write_single(setting_t *setting)
{
    nvs_handle_t nvs;
    esp_err_t    rc;

    rc = nvs_open(NVS_STORAGE, NVS_READWRITE, &nvs);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nvs open error %s", esp_err_to_name(rc));
        return rc;
    }

    rc = setting_nvs_write(setting, nvs);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "nvs set: %s", esp_err_to_name(rc));
        nvs_close(nvs);
        return rc;
    }
    nvs_commit(nvs);
    nvs_close(nvs);
    return ESP_OK;
}

esp_err_t settings_nvs_write(const settings_group_t *settings_pack)
{
    nvs_handle nvs;
    esp_err_t  rc;

    settings_pack_update_nvs_ids(settings_pack);
    rc = nvs_open(NVS_STORAGE, NVS_READWRITE, &nvs);
    if (rc == ESP_OK) {
        for (const settings_group_t *gr = settings_pack; gr->id; gr++) {
            for (setting_t *setting = gr->settings; setting->id; setting++) {
                rc = setting_nvs_write(setting,nvs);
                if(rc != ESP_OK)
                    break;
            }
        }
        if(rc == ESP_OK){
            nvs_commit(nvs);
        }
        nvs_close(nvs);
    } else {
        ESP_LOGE(TAG, "nvs open error %s", esp_err_to_name(rc));
    }
    return rc;
}

esp_err_t settings_nvs_erase(settings_group_t *settings_pack)
{
    nvs_handle nvs;
    esp_err_t  rc;
    rc = nvs_open(NVS_STORAGE, NVS_READWRITE, &nvs);
    if (rc == ESP_OK) {
        nvs_erase_all(nvs);
        ESP_LOGW(TAG, "nvs erased");
        if (settings_handler != NULL)
            settings_handler(settings_pack, handler_arg);
    } else {
        ESP_LOGE(TAG, "nvs open error %s", esp_err_to_name(rc));
    }
    return rc;
}

esp_err_t settings_handler_register(settings_handler_t handler, void *arg)
{
    settings_handler = handler;
    handler_arg = arg;
    return ESP_OK;
}

static esp_err_t send_json_response(cJSON *js, httpd_req_t *req)
{
    char *js_txt = cJSON_Print(js);
    cJSON_Delete(js);

    httpd_resp_set_type(req, HTTPD_TYPE_JSON);
    httpd_resp_send(req, js_txt, -1);
    free(js_txt);
    return ESP_OK;
}

static cJSON *settings_pack_to_json(settings_group_t *settings_pack)
{
    cJSON *js_groups;
    cJSON *js_group;
    cJSON *js_settings;
    cJSON *js_setting;
    cJSON *js_labels;
    cJSON *js;

    const char *types[] = {
        [SETTING_TYPE_BOOL] = "BOOL",         [SETTING_TYPE_NUM] = "NUM",   [SETTING_TYPE_ONEOF] = "ONEOF",
        [SETTING_TYPE_TEXT] = "TEXT",
#ifdef CONFIG_SETTINGS_DATETIME_SUPPORT
        [SETTING_TYPE_TIME] = "TIME",         [SETTING_TYPE_DATE] = "DATE", [SETTING_TYPE_DATETIME] = "DATETIME",
#endif
#ifdef CONFIG_SETTINGS_TIMEZONE_SUPPORT
        [SETTING_TYPE_TIMEZONE] = "TIMEZONE",
#endif
#ifdef CONFIG_SETTINGS_COLOR_SUPPORT
        [SETTING_TYPE_COLOR] = "COLOR",
#endif
    };

    if (!settings_pack)
        return NULL;

    js_groups = cJSON_CreateArray();
    for (settings_group_t *gr = settings_pack; gr->label; gr++) {
        js_group = cJSON_CreateObject();
        cJSON_AddStringToObject(js_group, "label", gr->label);
        cJSON_AddStringToObject(js_group, "id", gr->id);
        js_settings = cJSON_CreateArray();
        for (setting_t *setting = gr->settings; setting->label; setting++) {
            js_setting = cJSON_CreateObject();
            cJSON_AddStringToObject(js_setting, "label", setting->label);
            cJSON_AddStringToObject(js_setting, "id", setting->id);
            cJSON_AddStringToObject(js_setting, "type", types[setting->type]);
            switch (setting->type) {
            case SETTING_TYPE_BOOL:
                cJSON_AddBoolToObject(js_setting, "val", setting->boolean.val);
                cJSON_AddBoolToObject(js_setting, "def", setting->boolean.def);
                break;
            case SETTING_TYPE_NUM:
                cJSON_AddNumberToObject(js_setting, "val", setting->num.val);
                cJSON_AddNumberToObject(js_setting, "def", setting->num.def);
                cJSON_AddNumberToObject(js_setting, "min", setting->num.range[0]);
                cJSON_AddNumberToObject(js_setting, "max", setting->num.range[1]);
                break;
            case SETTING_TYPE_ONEOF:
                cJSON_AddNumberToObject(js_setting, "val", setting->oneof.val);
                cJSON_AddNumberToObject(js_setting, "def", setting->oneof.def);
                js_labels = cJSON_CreateArray();
                cJSON_AddItemToObject(js_setting, "options", js_labels);
                for (const char **opt = setting->oneof.options; *opt != NULL; opt++)
                    cJSON_AddItemToArray(js_labels, cJSON_CreateString(*opt));
                break;
            case SETTING_TYPE_TEXT:
                cJSON_AddStringToObject(js_setting, "val", setting->text.val);
                cJSON_AddStringToObject(js_setting, "def", setting->text.def);
                cJSON_AddNumberToObject(js_setting, "len", setting->text.len);
                break;
#ifdef CONFIG_SETTINGS_DATETIME_SUPPORT
            case SETTING_TYPE_TIME:
                cJSON_AddNumberToObject(js_setting, "hh", setting->time.hh);
                cJSON_AddNumberToObject(js_setting, "mm", setting->time.mm);
                cJSON_AddNumberToObject(js_setting, "ss", setting->time.ss);
                break;
            case SETTING_TYPE_DATE:
                cJSON_AddNumberToObject(js_setting, "day", setting->date.day);
                cJSON_AddNumberToObject(js_setting, "month", setting->date.month);
                cJSON_AddNumberToObject(js_setting, "year", setting->date.year);
                break;
            case SETTING_TYPE_DATETIME:
                datetime_gettimeofday(&setting->datetime);
                cJSON_AddNumberToObject(js_setting, "hh", setting->datetime.time.hh);
                cJSON_AddNumberToObject(js_setting, "mm", setting->datetime.time.mm);
                cJSON_AddNumberToObject(js_setting, "ss", setting->datetime.time.ss);
                cJSON_AddNumberToObject(js_setting, "day", setting->datetime.date.day);
                cJSON_AddNumberToObject(js_setting, "month", setting->datetime.date.month);
                cJSON_AddNumberToObject(js_setting, "year", setting->datetime.date.year);
                break;
#endif
#ifdef CONFIG_SETTINGS_TIMEZONE_SUPPORT
            case SETTING_TYPE_TIMEZONE:
                cJSON_AddStringToObject(js_setting, "val", setting->timezone.val);
                cJSON_AddStringToObject(js_setting, "def", setting->timezone.def);
                cJSON_AddNumberToObject(js_setting, "len", setting->timezone.len);
                break;
#endif
#ifdef CONFIG_SETTINGS_COLOR_SUPPORT
            case SETTING_TYPE_COLOR: {
                char buf[8];
                snprintf(buf, sizeof(buf), "#%02x%02x%02x", setting->color.r, setting->color.g, setting->color.b);
                cJSON_AddStringToObject(js_setting, "val", buf);
            } break;
#endif
            default:
                break;
            }
            cJSON_AddItemToArray(js_settings, js_setting);
        }
        cJSON_AddItemToObject(js_group, "settings", js_settings);
        cJSON_AddItemToArray(js_groups, js_group);
    }
    js = cJSON_CreateObject();
    cJSON_AddItemToObject(js, "groups", js_groups);
    return js;
}

static esp_err_t set_req_handle(httpd_req_t *req)
{
    char *req_data;
    char  srch_id[128];
    char  value[128];
    int   bytes_recv = 0;
    int   rc;

    settings_group_t *settings_pack = req->user_ctx;
    if (req->content_len) {
        req_data = calloc(1, req->content_len + 1);
        if (!req_data)
            return ESP_ERR_NO_MEM;

        for (int bytes_left = req->content_len; bytes_left > 0;) {
            if ((rc = httpd_req_recv(req, req_data + bytes_recv, bytes_left)) <= 0) {
                if (rc == HTTPD_SOCK_ERR_TIMEOUT)
                    continue;
                else
                    return ESP_FAIL;
            }
            bytes_recv += rc;
            bytes_left -= rc;
        }

        for (settings_group_t *gr = settings_pack; gr->label; gr++) {
            for (setting_t *setting = gr->settings; setting->label; setting++) {
                sprintf(srch_id, "%s:%s", gr->id, setting->id);
                /* Set bool settings to false by default(if false then not in request)*/
                if (httpd_query_key_value(req_data, srch_id, value, sizeof(value)) != ESP_OK) {
                    if (setting->type == SETTING_TYPE_BOOL)
                        setting->boolean.val = false;
                    continue;
                }

                switch (setting->type) {
                case SETTING_TYPE_BOOL: {
                    setting_set_bool(setting, !strcmp("on", value));
                } break;
                case SETTING_TYPE_NUM: {
                    setting_set_num(setting,  atoi(value));
                } break;
                case SETTING_TYPE_ONEOF: {
                    setting_set_oneof(setting, atoi(value));
                } break;
                case SETTING_TYPE_TEXT: {
                    setting_set_text(setting, value);
                } break;
#ifdef CONFIG_SETTINGS_DATETIME_SUPPORT
                case SETTING_TYPE_TIME: {
                    setting_time_t time;
                    sscanf(value, "%d:%d", &time.hh, &time.mm);
                    setting_set_time(setting, &time);
                } break;
                case SETTING_TYPE_DATE: {
                    setting_date_t date;
                    sscanf(value, "%d-%d-%d", &date.year, &date.month, &date.day);
                    setting_set_date(setting, &date);   
                } break;
                case SETTING_TYPE_DATETIME: {
                    setting_date_t date;
                    setting_time_t time;
                    setting_datetime_t combined;

                    sscanf(value, "%d-%d-%dT%d:%d", &date.year, &date.month, &date.day, &time.hh, &time.mm);
                    combined.date = date;
                    combined.time = time;
                    setting_set_datetime(setting, &combined);
                } break;
#endif
#ifdef CONFIG_SETTINGS_TIMEZONE_SUPPORT
                case SETTING_TYPE_TIMEZONE: {
                    setting_set_timezone(setting, value);
                } break;
#endif
#ifdef CONFIG_SETTINGS_COLOR_SUPPORT
                case SETTING_TYPE_COLOR: {
                    setting_color_t color = { .combined = strtol(value + 1, NULL, 16) };
                    setting_set_color(setting, &color);
                } break;
#endif
                default:
                    break;
                }
            }
        }
        free(req_data);
    }

    if (settings_handler != NULL)
        settings_handler(settings_pack, handler_arg);

    rc = settings_nvs_write(settings_pack);
    if (rc == 0) {
        ESP_LOGI(TAG, "nvs write OK");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "nvs write ERR:%s(%d)", esp_err_to_name(rc), rc);
        return rc;
    }
}

esp_err_t settings_httpd_handler(httpd_req_t *req)
{
    cJSON *js;
    char  *url_query;
    size_t qlen;
    char   value[128];

    js = cJSON_CreateObject();

    settings_group_t *settings_pack = req->user_ctx;

    //parse URL query
    qlen = httpd_req_get_url_query_len(req) + 1;
    if (qlen > 1) {
        url_query = malloc(qlen);
        if (httpd_req_get_url_query_str(req, url_query, qlen) == ESP_OK) {
            if (httpd_query_key_value(url_query, "action", value, sizeof(value)) == ESP_OK) {
                if (!strcmp(value, "set")) {
                    set_req_handle(req);
                } else if (!strcmp(value, "erase")) {
                    settings_pack_set_defaults(settings_pack);
                    settings_nvs_erase(settings_pack);
                } else if (!strcmp(value, "restart")) {
                    send_json_response(js, req);
                    esp_restart();
                    return ESP_OK;
                }
            }
        }
        free(url_query);
    }
    cJSON_AddItemToObject(js, "data", settings_pack_to_json(settings_pack));
    return send_json_response(js, req);
}
