// Minimal stand-in for the ESP-IDF cJSON header, used only by tools/syntax-check.sh
// to type check the provider files on a machine without ESP-IDF. It mirrors the
// signatures the project uses; it is not a JSON implementation.
#pragma once
#include <stddef.h>
typedef int cJSON_bool;
typedef struct cJSON {
    struct cJSON *next, *prev, *child;
    int type; char *valuestring; int valueint; double valuedouble; char *string;
} cJSON;
cJSON *cJSON_CreateObject(void);
cJSON *cJSON_CreateArray(void);
cJSON *cJSON_CreateString(const char *string);
cJSON *cJSON_AddObjectToObject(cJSON *object, const char *name);
cJSON *cJSON_AddArrayToObject(cJSON *object, const char *name);
cJSON *cJSON_AddStringToObject(cJSON *object, const char *name, const char *string);
cJSON *cJSON_AddNumberToObject(cJSON *object, const char *name, double number);
cJSON *cJSON_AddNullToObject(cJSON *object, const char *name);
cJSON *cJSON_AddBoolToObject(cJSON *object, const char *name, cJSON_bool boolean);
void cJSON_AddItemToArray(cJSON *array, cJSON *item);
void cJSON_AddItemToObject(cJSON *object, const char *name, cJSON *item);
cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object, const char *name);
cJSON_bool cJSON_IsString(const cJSON *item);
cJSON_bool cJSON_IsNumber(const cJSON *item);
cJSON_bool cJSON_IsObject(const cJSON *item);
cJSON_bool cJSON_IsTrue(const cJSON *item);
cJSON *cJSON_Parse(const char *value);
void cJSON_Delete(cJSON *item);
char *cJSON_PrintUnformatted(const cJSON *item);
void cJSON_free(void *object);
#define cJSON_ArrayForEach(element, array) \
    for (element = ((array) != NULL ? (array)->child : NULL); element != NULL; element = element->next)
