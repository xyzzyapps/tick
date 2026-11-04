#ifndef TCLTALK_JSON_H
#define TCLTALK_JSON_H

#include "object.h"

Obj* json_parse(const char* str);
char* json_stringify(Obj* obj);

#endif // TCLTALK_JSON_H
