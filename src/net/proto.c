#include "proto.h"
#include "../log.h"
#include "../../deps/cJSON.h"

#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>

static const char *type_str(msg_type_t t)
{
    switch (t) {
    case MSG_HELLO:        return "hello";
    case MSG_HELLO_OK:     return "hello_ok";
    case MSG_HELLO_ERR:    return "hello_err";
    case MSG_FOCUS_ENTER:  return "focus_enter";
    case MSG_FOCUS_ACK:    return "focus_ack";
    case MSG_PING:         return "ping";
    case MSG_PONG:         return "pong";
    case MSG_INPUT_START:  return "input_start";
    case MSG_INPUT_STOP:   return "input_stop";
    case MSG_INPUT:        return "input";
    case MSG_FOCUS:        return "focus";
    case MSG_FOCUS_RESULT: return "focus_result";
    default:               return "unknown";
    }
}

static msg_type_t type_from_str(const char *s)
{
    if (!s) return MSG_UNKNOWN;
    if (strcmp(s, "hello") == 0)        return MSG_HELLO;
    if (strcmp(s, "hello_ok") == 0)     return MSG_HELLO_OK;
    if (strcmp(s, "hello_err") == 0)    return MSG_HELLO_ERR;
    if (strcmp(s, "focus_enter") == 0)  return MSG_FOCUS_ENTER;
    if (strcmp(s, "focus_ack") == 0)    return MSG_FOCUS_ACK;
    if (strcmp(s, "input_start") == 0)  return MSG_INPUT_START;
    if (strcmp(s, "input_stop") == 0)   return MSG_INPUT_STOP;
    if (strcmp(s, "input") == 0)        return MSG_INPUT;
    if (strcmp(s, "ping") == 0)         return MSG_PING;
    if (strcmp(s, "pong") == 0)         return MSG_PONG;
    if (strcmp(s, "focus") == 0)        return MSG_FOCUS;
    if (strcmp(s, "focus_result") == 0) return MSG_FOCUS_RESULT;
    return MSG_UNKNOWN;
}

size_t msg_serialize(const glew_msg_t *msg, char **out)
{
    cJSON *json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "type", type_str(msg->type));

    switch (msg->type) {
    case MSG_HELLO:
        cJSON_AddStringToObject(json, "name", msg->hello.name);
        cJSON_AddStringToObject(json, "secret", msg->hello.secret);
        break;
    case MSG_HELLO_ERR:
        cJSON_AddStringToObject(json, "reason", msg->hello_err.reason);
        break;
    case MSG_FOCUS_ENTER:
        cJSON_AddStringToObject(json, "from_direction", msg->focus_enter.from_direction);
        cJSON_AddStringToObject(json, "source", msg->focus_enter.source);
        cJSON_AddNumberToObject(json, "cursor_y", msg->focus_enter.cursor_y);
        break;
    case MSG_FOCUS_ACK:
        cJSON_AddBoolToObject(json, "success", msg->focus_ack.success);
        break;
    case MSG_INPUT_START:
        cJSON_AddStringToObject(json, "source", msg->input_start.source);
        cJSON_AddNumberToObject(json, "mods", msg->input_start.mods);
        cJSON_AddNumberToObject(json, "seq", msg->input_start.seq);
        break;
    case MSG_INPUT_STOP:
        break;
    case MSG_INPUT:
        cJSON_AddNumberToObject(json, "event", msg->input.type);
        cJSON_AddNumberToObject(json, "keysym", msg->input.keysym);
        cJSON_AddNumberToObject(json, "x", msg->input.x);
        cJSON_AddNumberToObject(json, "y", msg->input.y);
        cJSON_AddNumberToObject(json, "button", msg->input.button);
        cJSON_AddNumberToObject(json, "scroll_x", msg->input.scroll_x);
        cJSON_AddNumberToObject(json, "scroll_y", msg->input.scroll_y);
        cJSON_AddNumberToObject(json, "mods", msg->input.mods);
        cJSON_AddNumberToObject(json, "seq", msg->input.seq);
        cJSON_AddNumberToObject(json, "ts", msg->input.ts_ms);
        break;
    case MSG_FOCUS:
        cJSON_AddStringToObject(json, "direction", msg->focus.direction);
        break;
    case MSG_FOCUS_RESULT:
        cJSON_AddBoolToObject(json, "crossed", msg->focus_result.crossed);
        cJSON_AddStringToObject(json, "target", msg->focus_result.target);
        break;
    default:
        break;
    }

    char *payload = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);
    if (!payload) return 0;

    uint32_t plen = (uint32_t)strlen(payload);
    uint32_t net_len = htonl(plen);

    size_t total = PROTO_HEADER_SIZE + plen;
    *out = malloc(total);
    if (!*out) { free(payload); return 0; }

    memcpy(*out, &net_len, PROTO_HEADER_SIZE);
    memcpy(*out + PROTO_HEADER_SIZE, payload, plen);
    free(payload);

    return total;
}

static void copy_json_str(char *dst, size_t dstsz, cJSON *obj, const char *key)
{
    cJSON *v = cJSON_GetObjectItem(obj, key);
    if (v && cJSON_IsString(v))
        snprintf(dst, dstsz, "%s", v->valuestring);
}

int msg_parse(const char *data, size_t len, glew_msg_t *out)
{
    memset(out, 0, sizeof(*out));

    cJSON *json = cJSON_ParseWithLength(data, len);
    if (!json) return -1;

    cJSON *type_field = cJSON_GetObjectItem(json, "type");
    if (!type_field || !cJSON_IsString(type_field)) {
        cJSON_Delete(json);
        return -1;
    }

    out->type = type_from_str(type_field->valuestring);

    switch (out->type) {
    case MSG_HELLO:
        copy_json_str(out->hello.name, sizeof(out->hello.name), json, "name");
        copy_json_str(out->hello.secret, sizeof(out->hello.secret), json, "secret");
        break;
    case MSG_HELLO_ERR:
        copy_json_str(out->hello_err.reason, sizeof(out->hello_err.reason), json, "reason");
        break;
    case MSG_FOCUS_ENTER:
        copy_json_str(out->focus_enter.from_direction, sizeof(out->focus_enter.from_direction), json, "from_direction");
        copy_json_str(out->focus_enter.source, sizeof(out->focus_enter.source), json, "source");
        {
            cJSON *cy = cJSON_GetObjectItem(json, "cursor_y");
            out->focus_enter.cursor_y = (cy && cJSON_IsNumber(cy)) ? cJSON_GetNumberValue(cy) : 0.5;
        }
        break;
    case MSG_FOCUS_ACK: {
        cJSON *s = cJSON_GetObjectItem(json, "success");
        out->focus_ack.success = (s && cJSON_IsTrue(s)) ? 1 : 0;
        break;
    }
    case MSG_INPUT_START:
        copy_json_str(out->input_start.source, sizeof(out->input_start.source), json, "source");
        out->input_start.mods = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(json, "mods"));
        out->input_start.seq = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(json, "seq"));
        break;
    case MSG_INPUT_STOP:
        break;
    case MSG_INPUT:
        out->input.type = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(json, "event"));
        out->input.keysym = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(json, "keysym"));
        out->input.x = cJSON_GetNumberValue(cJSON_GetObjectItem(json, "x"));
        out->input.y = cJSON_GetNumberValue(cJSON_GetObjectItem(json, "y"));
        out->input.button = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(json, "button"));
        out->input.scroll_x = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(json, "scroll_x"));
        out->input.scroll_y = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(json, "scroll_y"));
        out->input.mods = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(json, "mods"));
        out->input.seq = (uint32_t)cJSON_GetNumberValue(cJSON_GetObjectItem(json, "seq"));
        out->input.ts_ms = cJSON_GetNumberValue(cJSON_GetObjectItem(json, "ts"));
        break;
    case MSG_FOCUS:
        copy_json_str(out->focus.direction, sizeof(out->focus.direction), json, "direction");
        break;
    case MSG_FOCUS_RESULT: {
        cJSON *c = cJSON_GetObjectItem(json, "crossed");
        out->focus_result.crossed = (c && cJSON_IsTrue(c)) ? 1 : 0;
        copy_json_str(out->focus_result.target, sizeof(out->focus_result.target), json, "target");
        break;
    }
    default:
        break;
    }

    cJSON_Delete(json);
    return 0;
}

int32_t proto_read_len(const char *buf, size_t avail)
{
    if (avail < PROTO_HEADER_SIZE)
        return -1;
    uint32_t net_len;
    memcpy(&net_len, buf, PROTO_HEADER_SIZE);
    return (int32_t)ntohl(net_len);
}
