#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    const char* src;
    int pos;
    int len;
} JSONReader;

static void skip_ws(JSONReader* r) {
    while (r->pos < r->len && (r->src[r->pos] == ' ' || r->src[r->pos] == '\t' ||
                               r->src[r->pos] == '\n' || r->src[r->pos] == '\r')) {
        r->pos++;
    }
}

static char peek(JSONReader* r) {
    skip_ws(r);
    if (r->pos >= r->len) return '\0';
    return r->src[r->pos];
}

static char advance(JSONReader* r) {
    skip_ws(r);
    if (r->pos >= r->len) return '\0';
    return r->src[r->pos++];
}

static Obj* parse_value(JSONReader* r);

static char* parse_string_raw(JSONReader* r) {
    if (advance(r) != '"') return NULL;
    int cap = 32;
    int len = 0;
    char* buf = (char*)malloc(cap);

    while (r->pos < r->len) {
        char ch = r->src[r->pos++];
        if (ch == '"') {
            buf[len] = '\0';
            return buf;
        }
        if (ch == '\\' && r->pos < r->len) {
            char esc = r->src[r->pos++];
            if (esc == 'n') ch = '\n';
            else if (esc == 't') ch = '\t';
            else if (esc == 'r') ch = '\r';
            else if (esc == '"') ch = '"';
            else if (esc == '\\') ch = '\\';
            else ch = esc;
        }
        if (len + 2 >= cap) {
            cap *= 2;
            buf = (char*)realloc(buf, cap);
        }
        buf[len++] = ch;
    }
    buf[len] = '\0';
    return buf;
}

static Obj* parse_number(JSONReader* r) {
    int start = r->pos;
    bool is_float = false;
    if (r->src[r->pos] == '-') r->pos++;
    while (r->pos < r->len && isdigit(r->src[r->pos])) r->pos++;
    if (r->pos < r->len && r->src[r->pos] == '.') {
        is_float = true;
        r->pos++;
        while (r->pos < r->len && isdigit(r->src[r->pos])) r->pos++;
    }
    if (r->pos < r->len && (r->src[r->pos] == 'e' || r->src[r->pos] == 'E')) {
        is_float = true;
        r->pos++;
        if (r->pos < r->len && (r->src[r->pos] == '+' || r->src[r->pos] == '-')) r->pos++;
        while (r->pos < r->len && isdigit(r->src[r->pos])) r->pos++;
    }
    int num_len = r->pos - start;
    char* num_str = (char*)malloc(num_len + 1);
    memcpy(num_str, r->src + start, num_len);
    num_str[num_len] = '\0';

    Obj* res = NULL;
    if (is_float) {
        res = obj_new_float(atof(num_str));
    } else {
        res = obj_new_int(atoll(num_str));
    }
    free(num_str);
    return res;
}

static Obj* parse_array(JSONReader* r) {
    advance(r); // consume '['
    Obj* arr = obj_new_array();
    skip_ws(r);
    if (peek(r) == ']') {
        advance(r);
        return arr;
    }

    while (r->pos < r->len) {
        Obj* item = parse_value(r);
        if (item) {
            if (arr->as.array.count >= arr->as.array.capacity) {
                arr->as.array.capacity *= 2;
                arr->as.array.items = (Obj**)realloc(arr->as.array.items, sizeof(Obj*) * arr->as.array.capacity);
            }
            arr->as.array.items[arr->as.array.count++] = item;
        }
        skip_ws(r);
        char ch = advance(r);
        if (ch == ']') break;
        if (ch != ',') break;
    }
    return arr;
}

static Obj* parse_object(JSONReader* r) {
    advance(r); // consume '{'
    Obj* dict = obj_new_dict();
    skip_ws(r);
    if (peek(r) == '}') {
        advance(r);
        return dict;
    }

    while (r->pos < r->len) {
        skip_ws(r);
        char* key = parse_string_raw(r);
        if (!key) break;
        skip_ws(r);
        if (advance(r) != ':') {
            free(key);
            break;
        }
        Obj* val = parse_value(r);

        if (dict->as.dict.count >= dict->as.dict.capacity) {
            dict->as.dict.capacity *= 2;
            dict->as.dict.entries = (DictEntry*)realloc(dict->as.dict.entries, sizeof(DictEntry) * dict->as.dict.capacity);
        }
        dict->as.dict.entries[dict->as.dict.count].key = key;
        dict->as.dict.entries[dict->as.dict.count].value = val ? val : obj_new_nil();
        dict->as.dict.count++;

        skip_ws(r);
        char ch = advance(r);
        if (ch == '}') break;
        if (ch != ',') break;
    }
    return dict;
}

static Obj* parse_value(JSONReader* r) {
    skip_ws(r);
    char ch = peek(r);
    if (ch == '"') {
        char* s = parse_string_raw(r);
        Obj* o = obj_new_string(s);
        free(s);
        return o;
    }
    if (ch == '{') return parse_object(r);
    if (ch == '[') return parse_array(r);
    if (isdigit(ch) || ch == '-') return parse_number(r);
    if (strncmp(r->src + r->pos, "true", 4) == 0) {
        r->pos += 4;
        return obj_new_bool(true);
    }
    if (strncmp(r->src + r->pos, "false", 5) == 0) {
        r->pos += 5;
        return obj_new_bool(false);
    }
    if (strncmp(r->src + r->pos, "null", 4) == 0) {
        r->pos += 4;
        return obj_new_nil();
    }
    return obj_new_nil();
}

Obj* json_parse(const char* str) {
    JSONReader r;
    r.src = str;
    r.pos = 0;
    r.len = (int)strlen(str);
    return parse_value(&r);
}

// Stringification
static void stringify_value(Obj* obj, char** out, int* len, int* cap);

static void append_str(char** out, int* len, int* cap, const char* str) {
    int slen = (int)strlen(str);
    while (*len + slen + 1 >= *cap) {
        *cap *= 2;
        *out = (char*)realloc(*out, *cap);
    }
    memcpy(*out + *len, str, slen);
    *len += slen;
    (*out)[*len] = '\0';
}

static void append_escaped_string(char** out, int* len, int* cap, const char* s) {
    append_str(out, len, cap, "\"");
    int slen = (int)strlen(s);
    for (int i = 0; i < slen; i++) {
        char ch = s[i];
        if (ch == '"') append_str(out, len, cap, "\\\"");
        else if (ch == '\\') append_str(out, len, cap, "\\\\");
        else if (ch == '\n') append_str(out, len, cap, "\\n");
        else if (ch == '\t') append_str(out, len, cap, "\\t");
        else if (ch == '\r') append_str(out, len, cap, "\\r");
        else {
            char tmp[2] = {ch, '\0'};
            append_str(out, len, cap, tmp);
        }
    }
    append_str(out, len, cap, "\"");
}

static void stringify_value(Obj* obj, char** out, int* len, int* cap) {
    if (!obj || obj->type == OBJ_NIL) {
        append_str(out, len, cap, "null");
        return;
    }
    char buf[64];
    switch (obj->type) {
        case OBJ_NIL:
            append_str(out, len, cap, "null");
            break;
        case OBJ_BOOL:
            append_str(out, len, cap, obj->as.boolean ? "true" : "false");
            break;
        case OBJ_INT:
            sprintf(buf, "%lld", (long long)obj->as.integer);
            append_str(out, len, cap, buf);
            break;
        case OBJ_FLOAT:
            sprintf(buf, "%g", obj->as.number);
            append_str(out, len, cap, buf);
            break;
        case OBJ_STRING:
            append_escaped_string(out, len, cap, obj->as.string.chars);
            break;
        case OBJ_ARRAY: {
            append_str(out, len, cap, "[");
            for (int i = 0; i < obj->as.array.count; i++) {
                if (i > 0) append_str(out, len, cap, ",");
                stringify_value(obj->as.array.items[i], out, len, cap);
            }
            append_str(out, len, cap, "]");
            break;
        }
        case OBJ_DICT: {
            append_str(out, len, cap, "{");
            for (int i = 0; i < obj->as.dict.count; i++) {
                if (i > 0) append_str(out, len, cap, ",");
                append_escaped_string(out, len, cap, obj->as.dict.entries[i].key);
                append_str(out, len, cap, ":");
                stringify_value(obj->as.dict.entries[i].value, out, len, cap);
            }
            append_str(out, len, cap, "}");
            break;
        }
        case OBJ_INSTANCE: {
            append_str(out, len, cap, "{");
            for (int i = 0; i < obj->as.instance.count; i++) {
                if (i > 0) append_str(out, len, cap, ",");
                append_escaped_string(out, len, cap, obj->as.instance.fields[i].name);
                append_str(out, len, cap, ":");
                stringify_value(obj->as.instance.fields[i].value, out, len, cap);
            }
            append_str(out, len, cap, "}");
            break;
        }
        default: {
            char* s = obj_to_string(obj);
            append_escaped_string(out, len, cap, s);
            free(s);
            break;
        }
    }
}

char* json_stringify(Obj* obj) {
    int cap = 64;
    int len = 0;
    char* out = (char*)malloc(cap);
    out[0] = '\0';
    stringify_value(obj, &out, &len, &cap);
    return out;
}
