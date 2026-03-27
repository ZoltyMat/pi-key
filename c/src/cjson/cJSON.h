/*
 * cJSON — minimal JSON parser for embedded C projects
 * Based on cJSON by Dave Gammon. Stripped to essentials for PiKey.
 */
#ifndef CJSON_H
#define CJSON_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* cJSON Types */
#define cJSON_Invalid  (0)
#define cJSON_False    (1 << 0)
#define cJSON_True     (1 << 1)
#define cJSON_NULL     (1 << 2)
#define cJSON_Number   (1 << 3)
#define cJSON_String   (1 << 4)
#define cJSON_Array    (1 << 5)
#define cJSON_Object   (1 << 6)
#define cJSON_Raw      (1 << 7)

#define cJSON_IsReference  256
#define cJSON_StringIsConst 512

typedef struct cJSON {
    struct cJSON *next;
    struct cJSON *prev;
    struct cJSON *child;
    int type;
    char *valuestring;
    int valueint;
    double valuedouble;
    char *string;  /* key name */
} cJSON;

/* Parse JSON string, returns root node. Caller must cJSON_Delete(). */
cJSON *cJSON_Parse(const char *value);

/* Free a cJSON tree */
void cJSON_Delete(cJSON *item);

/* Get item from object by key (case-sensitive) */
cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object, const char *string);

/* Get array size and item */
int cJSON_GetArraySize(const cJSON *array);
cJSON *cJSON_GetArrayItem(const cJSON *array, int index);

/* Print JSON to string. Caller must free(). */
char *cJSON_Print(const cJSON *item);
char *cJSON_PrintUnformatted(const cJSON *item);

/* Creation */
cJSON *cJSON_CreateObject(void);
cJSON *cJSON_CreateArray(void);
cJSON *cJSON_CreateString(const char *string);
cJSON *cJSON_CreateNumber(double num);
cJSON *cJSON_CreateBool(int boolean);
cJSON *cJSON_CreateNull(void);

/* Add to object/array */
void cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item);
void cJSON_AddItemToArray(cJSON *array, cJSON *item);

/* Convenience macros */
#define cJSON_AddStringToObject(object, name, s) \
    cJSON_AddItemToObject(object, name, cJSON_CreateString(s))
#define cJSON_AddNumberToObject(object, name, n) \
    cJSON_AddItemToObject(object, name, cJSON_CreateNumber(n))
#define cJSON_AddBoolToObject(object, name, b) \
    cJSON_AddItemToObject(object, name, cJSON_CreateBool(b))

/* Type checks */
#define cJSON_IsString(item) ((item) != NULL && ((item)->type & 0xFF) == cJSON_String)
#define cJSON_IsNumber(item) ((item) != NULL && ((item)->type & 0xFF) == cJSON_Number)
#define cJSON_IsObject(item) ((item) != NULL && ((item)->type & 0xFF) == cJSON_Object)
#define cJSON_IsArray(item)  ((item) != NULL && ((item)->type & 0xFF) == cJSON_Array)
#define cJSON_IsBool(item)   ((item) != NULL && (((item)->type & 0xFF) == cJSON_True || ((item)->type & 0xFF) == cJSON_False))
#define cJSON_IsNull(item)   ((item) != NULL && ((item)->type & 0xFF) == cJSON_NULL)

#ifdef __cplusplus
}
#endif

#endif /* CJSON_H */
