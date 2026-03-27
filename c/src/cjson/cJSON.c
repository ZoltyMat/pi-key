/*
 * cJSON — minimal JSON parser for embedded C projects
 * Based on cJSON by Dave Gammon. Stripped to essentials for PiKey.
 */
#include "cJSON.h"

#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <limits.h>
#include <ctype.h>
#include <float.h>

/* ── Internal helpers ──────────────────────────────────────────────────────── */

static char *cJSON_strdup(const char *str) {
    size_t len;
    char *copy;
    if (!str) return NULL;
    len = strlen(str) + 1;
    copy = (char *)malloc(len);
    if (!copy) return NULL;
    memcpy(copy, str, len);
    return copy;
}

static cJSON *cJSON_New_Item(void) {
    cJSON *node = (cJSON *)calloc(1, sizeof(cJSON));
    return node;
}

void cJSON_Delete(cJSON *item) {
    cJSON *next;
    while (item) {
        next = item->next;
        if (!(item->type & cJSON_IsReference) && item->child) {
            cJSON_Delete(item->child);
        }
        if (!(item->type & cJSON_IsReference) && item->valuestring) {
            free(item->valuestring);
        }
        if (!(item->type & cJSON_StringIsConst) && item->string) {
            free(item->string);
        }
        free(item);
        item = next;
    }
}

/* ── Parser ────────────────────────────────────────────────────────────────── */

typedef struct {
    const char *content;
    size_t offset;
    size_t length;
} parse_buffer;

static int can_read(const parse_buffer *buf, size_t count) {
    return buf && (buf->offset + count <= buf->length);
}

static void skip_whitespace(parse_buffer *buf) {
    if (!buf || !buf->content) return;
    while (buf->offset < buf->length) {
        unsigned char c = (unsigned char)buf->content[buf->offset];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            buf->offset++;
        } else {
            break;
        }
    }
}

static int parse_string(cJSON *item, parse_buffer *buf) {
    const char *start;
    char *out;
    size_t len = 0;

    if (!can_read(buf, 1) || buf->content[buf->offset] != '\"') return 0;
    buf->offset++;
    start = buf->content + buf->offset;

    /* find end quote */
    while (buf->offset < buf->length) {
        if (buf->content[buf->offset] == '\\') {
            buf->offset += 2;
            continue;
        }
        if (buf->content[buf->offset] == '\"') break;
        buf->offset++;
    }
    if (buf->offset >= buf->length) return 0;

    len = (size_t)(buf->content + buf->offset - start);
    out = (char *)malloc(len + 1);
    if (!out) return 0;

    /* copy with basic escape handling */
    {
        size_t i = 0, o = 0;
        while (i < len) {
            if (start[i] == '\\' && i + 1 < len) {
                i++;
                switch (start[i]) {
                    case '\"': out[o++] = '\"'; break;
                    case '\\': out[o++] = '\\'; break;
                    case '/':  out[o++] = '/';  break;
                    case 'b':  out[o++] = '\b'; break;
                    case 'f':  out[o++] = '\f'; break;
                    case 'n':  out[o++] = '\n'; break;
                    case 'r':  out[o++] = '\r'; break;
                    case 't':  out[o++] = '\t'; break;
                    default:   out[o++] = start[i]; break;
                }
                i++;
            } else {
                out[o++] = start[i++];
            }
        }
        out[o] = '\0';
    }

    buf->offset++; /* skip closing quote */
    item->type = cJSON_String;
    item->valuestring = out;
    return 1;
}

static int parse_number(cJSON *item, parse_buffer *buf) {
    double number = 0;
    char *after = NULL;
    const char *start = buf->content + buf->offset;

    number = strtod(start, &after);
    if (start == after) return 0;

    item->valuedouble = number;
    if (number >= INT_MIN && number <= INT_MAX) {
        item->valueint = (int)number;
    } else {
        item->valueint = (number >= 0) ? INT_MAX : INT_MIN;
    }
    item->type = cJSON_Number;
    buf->offset += (size_t)(after - start);
    return 1;
}

/* Forward declaration */
static int parse_value(cJSON *item, parse_buffer *buf);

static int parse_array(cJSON *item, parse_buffer *buf) {
    cJSON *head = NULL;
    cJSON *current = NULL;

    if (!can_read(buf, 1) || buf->content[buf->offset] != '[') return 0;
    buf->offset++;
    skip_whitespace(buf);

    item->type = cJSON_Array;

    if (can_read(buf, 1) && buf->content[buf->offset] == ']') {
        buf->offset++;
        return 1;
    }

    head = cJSON_New_Item();
    if (!head) return 0;

    if (!parse_value(head, buf)) {
        cJSON_Delete(head);
        return 0;
    }

    item->child = head;
    current = head;

    while (can_read(buf, 1) && buf->content[buf->offset] == ',') {
        cJSON *newitem;
        buf->offset++;
        skip_whitespace(buf);

        newitem = cJSON_New_Item();
        if (!newitem) return 0;
        current->next = newitem;
        newitem->prev = current;
        current = newitem;

        if (!parse_value(current, buf)) return 0;
    }

    skip_whitespace(buf);
    if (!can_read(buf, 1) || buf->content[buf->offset] != ']') return 0;
    buf->offset++;
    return 1;
}

static int parse_object(cJSON *item, parse_buffer *buf) {
    cJSON *head = NULL;
    cJSON *current = NULL;

    if (!can_read(buf, 1) || buf->content[buf->offset] != '{') return 0;
    buf->offset++;
    skip_whitespace(buf);

    item->type = cJSON_Object;

    if (can_read(buf, 1) && buf->content[buf->offset] == '}') {
        buf->offset++;
        return 1;
    }

    head = cJSON_New_Item();
    if (!head) return 0;

    /* parse key */
    if (!parse_string(head, buf)) { cJSON_Delete(head); return 0; }
    head->string = head->valuestring;
    head->valuestring = NULL;

    skip_whitespace(buf);
    if (!can_read(buf, 1) || buf->content[buf->offset] != ':') { cJSON_Delete(head); return 0; }
    buf->offset++;
    skip_whitespace(buf);

    if (!parse_value(head, buf)) { cJSON_Delete(head); return 0; }

    item->child = head;
    current = head;

    while (can_read(buf, 1) && buf->content[buf->offset] == ',') {
        cJSON *newitem;
        buf->offset++;
        skip_whitespace(buf);

        newitem = cJSON_New_Item();
        if (!newitem) return 0;

        /* parse key */
        if (!parse_string(newitem, buf)) { cJSON_Delete(newitem); return 0; }
        newitem->string = newitem->valuestring;
        newitem->valuestring = NULL;

        skip_whitespace(buf);
        if (!can_read(buf, 1) || buf->content[buf->offset] != ':') return 0;
        buf->offset++;
        skip_whitespace(buf);

        if (!parse_value(newitem, buf)) return 0;

        current->next = newitem;
        newitem->prev = current;
        current = newitem;
    }

    skip_whitespace(buf);
    if (!can_read(buf, 1) || buf->content[buf->offset] != '}') return 0;
    buf->offset++;
    return 1;
}

static int parse_value(cJSON *item, parse_buffer *buf) {
    if (!buf || !buf->content || buf->offset >= buf->length) return 0;

    skip_whitespace(buf);

    /* null */
    if (can_read(buf, 4) && strncmp(buf->content + buf->offset, "null", 4) == 0) {
        item->type = cJSON_NULL;
        buf->offset += 4;
        return 1;
    }
    /* false */
    if (can_read(buf, 5) && strncmp(buf->content + buf->offset, "false", 5) == 0) {
        item->type = cJSON_False;
        item->valueint = 0;
        buf->offset += 5;
        return 1;
    }
    /* true */
    if (can_read(buf, 4) && strncmp(buf->content + buf->offset, "true", 4) == 0) {
        item->type = cJSON_True;
        item->valueint = 1;
        buf->offset += 4;
        return 1;
    }
    /* string */
    if (can_read(buf, 1) && buf->content[buf->offset] == '\"') {
        return parse_string(item, buf);
    }
    /* number */
    if (can_read(buf, 1) && (buf->content[buf->offset] == '-' || isdigit((unsigned char)buf->content[buf->offset]))) {
        return parse_number(item, buf);
    }
    /* array */
    if (can_read(buf, 1) && buf->content[buf->offset] == '[') {
        return parse_array(item, buf);
    }
    /* object */
    if (can_read(buf, 1) && buf->content[buf->offset] == '{') {
        return parse_object(item, buf);
    }

    return 0;
}

cJSON *cJSON_Parse(const char *value) {
    parse_buffer buf;
    cJSON *item;

    if (!value) return NULL;

    buf.content = value;
    buf.length = strlen(value);
    buf.offset = 0;

    item = cJSON_New_Item();
    if (!item) return NULL;

    if (!parse_value(item, &buf)) {
        cJSON_Delete(item);
        return NULL;
    }

    return item;
}

/* ── Accessors ─────────────────────────────────────────────────────────────── */

cJSON *cJSON_GetObjectItemCaseSensitive(const cJSON *object, const char *string) {
    cJSON *current;
    if (!object || !string) return NULL;
    current = object->child;
    while (current) {
        if (current->string && strcmp(current->string, string) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

int cJSON_GetArraySize(const cJSON *array) {
    cJSON *child;
    int size = 0;
    if (!array) return 0;
    child = array->child;
    while (child) {
        size++;
        child = child->next;
    }
    return size;
}

cJSON *cJSON_GetArrayItem(const cJSON *array, int index) {
    cJSON *child;
    if (!array) return NULL;
    child = array->child;
    while (child && index > 0) {
        child = child->next;
        index--;
    }
    return child;
}

/* ── Printer (unformatted) ─────────────────────────────────────────────────── */

typedef struct {
    char *buffer;
    size_t length;
    size_t offset;
} printbuffer;

static int ensure_space(printbuffer *p, size_t needed) {
    if (p->offset + needed >= p->length) {
        size_t newsize = (p->length + needed) * 2;
        char *newbuf = (char *)realloc(p->buffer, newsize);
        if (!newbuf) return 0;
        p->buffer = newbuf;
        p->length = newsize;
    }
    return 1;
}

static int print_string_ptr(const char *str, printbuffer *p) {
    size_t len;
    if (!str) str = "";
    len = strlen(str);
    if (!ensure_space(p, len * 2 + 3)) return 0;

    p->buffer[p->offset++] = '\"';
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)str[i];
        if (c == '\"' || c == '\\') {
            p->buffer[p->offset++] = '\\';
            p->buffer[p->offset++] = (char)c;
        } else if (c == '\n') {
            p->buffer[p->offset++] = '\\';
            p->buffer[p->offset++] = 'n';
        } else if (c == '\r') {
            p->buffer[p->offset++] = '\\';
            p->buffer[p->offset++] = 'r';
        } else if (c == '\t') {
            p->buffer[p->offset++] = '\\';
            p->buffer[p->offset++] = 't';
        } else if (c < 0x20) {
            p->offset += (size_t)sprintf(p->buffer + p->offset, "\\u%04x", c);
        } else {
            p->buffer[p->offset++] = (char)c;
        }
    }
    p->buffer[p->offset++] = '\"';
    p->buffer[p->offset] = '\0';
    return 1;
}

static int print_value(const cJSON *item, printbuffer *p);

static int print_number(const cJSON *item, printbuffer *p) {
    if (!ensure_space(p, 64)) return 0;
    if (fabs(item->valuedouble - (double)item->valueint) < DBL_EPSILON && item->valuedouble <= INT_MAX && item->valuedouble >= INT_MIN) {
        p->offset += (size_t)sprintf(p->buffer + p->offset, "%d", item->valueint);
    } else {
        p->offset += (size_t)sprintf(p->buffer + p->offset, "%g", item->valuedouble);
    }
    return 1;
}

static int print_array(const cJSON *item, printbuffer *p) {
    cJSON *child = item->child;
    if (!ensure_space(p, 1)) return 0;
    p->buffer[p->offset++] = '[';

    while (child) {
        if (!print_value(child, p)) return 0;
        if (child->next) {
            if (!ensure_space(p, 1)) return 0;
            p->buffer[p->offset++] = ',';
        }
        child = child->next;
    }

    if (!ensure_space(p, 2)) return 0;
    p->buffer[p->offset++] = ']';
    p->buffer[p->offset] = '\0';
    return 1;
}

static int print_object(const cJSON *item, printbuffer *p) {
    cJSON *child = item->child;
    if (!ensure_space(p, 1)) return 0;
    p->buffer[p->offset++] = '{';

    while (child) {
        if (!print_string_ptr(child->string, p)) return 0;
        if (!ensure_space(p, 1)) return 0;
        p->buffer[p->offset++] = ':';
        if (!print_value(child, p)) return 0;
        if (child->next) {
            if (!ensure_space(p, 1)) return 0;
            p->buffer[p->offset++] = ',';
        }
        child = child->next;
    }

    if (!ensure_space(p, 2)) return 0;
    p->buffer[p->offset++] = '}';
    p->buffer[p->offset] = '\0';
    return 1;
}

static int print_value(const cJSON *item, printbuffer *p) {
    if (!item) return 0;

    switch (item->type & 0xFF) {
        case cJSON_NULL:
            if (!ensure_space(p, 5)) return 0;
            strcpy(p->buffer + p->offset, "null");
            p->offset += 4;
            return 1;
        case cJSON_False:
            if (!ensure_space(p, 6)) return 0;
            strcpy(p->buffer + p->offset, "false");
            p->offset += 5;
            return 1;
        case cJSON_True:
            if (!ensure_space(p, 5)) return 0;
            strcpy(p->buffer + p->offset, "true");
            p->offset += 4;
            return 1;
        case cJSON_Number:
            return print_number(item, p);
        case cJSON_String:
            return print_string_ptr(item->valuestring, p);
        case cJSON_Array:
            return print_array(item, p);
        case cJSON_Object:
            return print_object(item, p);
        default:
            return 0;
    }
}

char *cJSON_PrintUnformatted(const cJSON *item) {
    printbuffer p;
    p.buffer = (char *)malloc(256);
    if (!p.buffer) return NULL;
    p.length = 256;
    p.offset = 0;
    p.buffer[0] = '\0';

    if (!print_value(item, &p)) {
        free(p.buffer);
        return NULL;
    }
    return p.buffer;
}

char *cJSON_Print(const cJSON *item) {
    /* For simplicity, same as unformatted */
    return cJSON_PrintUnformatted(item);
}

/* ── Creators ──────────────────────────────────────────────────────────────── */

cJSON *cJSON_CreateNull(void) {
    cJSON *item = cJSON_New_Item();
    if (item) item->type = cJSON_NULL;
    return item;
}

cJSON *cJSON_CreateBool(int boolean) {
    cJSON *item = cJSON_New_Item();
    if (item) {
        item->type = boolean ? cJSON_True : cJSON_False;
        item->valueint = boolean ? 1 : 0;
    }
    return item;
}

cJSON *cJSON_CreateNumber(double num) {
    cJSON *item = cJSON_New_Item();
    if (item) {
        item->type = cJSON_Number;
        item->valuedouble = num;
        item->valueint = (int)num;
    }
    return item;
}

cJSON *cJSON_CreateString(const char *string) {
    cJSON *item = cJSON_New_Item();
    if (item) {
        item->type = cJSON_String;
        item->valuestring = cJSON_strdup(string ? string : "");
    }
    return item;
}

cJSON *cJSON_CreateArray(void) {
    cJSON *item = cJSON_New_Item();
    if (item) item->type = cJSON_Array;
    return item;
}

cJSON *cJSON_CreateObject(void) {
    cJSON *item = cJSON_New_Item();
    if (item) item->type = cJSON_Object;
    return item;
}

static void suffix_object(cJSON *prev, cJSON *item) {
    prev->next = item;
    item->prev = prev;
}

void cJSON_AddItemToArray(cJSON *array, cJSON *item) {
    cJSON *child;
    if (!array || !item) return;
    child = array->child;
    if (!child) {
        array->child = item;
    } else {
        while (child->next) child = child->next;
        suffix_object(child, item);
    }
}

void cJSON_AddItemToObject(cJSON *object, const char *string, cJSON *item) {
    if (!object || !item || !string) return;
    if (!(item->type & cJSON_StringIsConst) && item->string) {
        free(item->string);
    }
    item->string = cJSON_strdup(string);
    cJSON_AddItemToArray(object, item);
}
