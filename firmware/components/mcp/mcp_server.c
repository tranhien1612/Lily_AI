#include "mcp_server.h"
#include <string.h>
#include <stdlib.h>
#include "esp_log.h"

static const char *TAG = "MCP_SERVER";
#define MAX_MCP_TOOLS 16

static mcp_tool_t s_tools[MAX_MCP_TOOLS];
static size_t s_tool_count = 0;

esp_err_t mcp_server_init(void)
{
    s_tool_count = 0;
    ESP_LOGI(TAG, "MCP Server initialized (JSON-RPC 2.0)");
    return ESP_OK;
}

esp_err_t mcp_register_tool(const mcp_tool_t *tool)
{
    if (!tool || !tool->name || !tool->handler) return ESP_ERR_INVALID_ARG;
    if (s_tool_count >= MAX_MCP_TOOLS) return ESP_ERR_NO_MEM;

    s_tools[s_tool_count++] = *tool;
    ESP_LOGI(TAG, "Registered tool '%s': %s", tool->name, tool->description ? tool->description : "");
    return ESP_OK;
}

cJSON *mcp_get_tools_manifest(void)
{
    cJSON *list = cJSON_CreateArray();
    for (size_t i = 0; i < s_tool_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", s_tools[i].name);
        cJSON_AddStringToObject(item, "description", s_tools[i].description ? s_tools[i].description : "");
        if (s_tools[i].parameters_schema_json) {
            cJSON *schema = cJSON_Parse(s_tools[i].parameters_schema_json);
            if (schema) {
                cJSON_AddItemToObject(item, "inputSchema", schema);
            }
        }
        cJSON_AddItemToArray(list, item);
    }
    return list;
}

cJSON *mcp_handle_message(const cJSON *json_rpc)
{
    if (!json_rpc) return NULL;

    cJSON *id_obj = cJSON_GetObjectItem(json_rpc, "id");
    cJSON *method_obj = cJSON_GetObjectItem(json_rpc, "method");
    cJSON *params_obj = cJSON_GetObjectItem(json_rpc, "params");

    cJSON *response = cJSON_CreateObject();
    cJSON_AddStringToObject(response, "jsonrpc", "2.0");
    if (id_obj) {
        if (cJSON_IsNumber(id_obj)) cJSON_AddNumberToObject(response, "id", id_obj->valueint);
        else if (cJSON_IsString(id_obj)) cJSON_AddStringToObject(response, "id", id_obj->valuestring);
    }

    if (!method_obj || !cJSON_IsString(method_obj)) {
        cJSON *err = cJSON_CreateObject();
        cJSON_AddNumberToObject(err, "code", -32600);
        cJSON_AddStringToObject(err, "message", "Invalid Request: method is missing or not a string");
        cJSON_AddItemToObject(response, "error", err);
        return response;
    }

    const char *method = method_obj->valuestring;
    ESP_LOGI(TAG, "MCP Request method: %s", method);

    if (strcmp(method, "tools/list") == 0) {
        cJSON *result = cJSON_CreateObject();
        cJSON_AddItemToObject(result, "tools", mcp_get_tools_manifest());
        cJSON_AddItemToObject(response, "result", result);
        return response;
    } else if (strcmp(method, "tools/call") == 0) {
        if (!params_obj) {
            cJSON *err = cJSON_CreateObject();
            cJSON_AddNumberToObject(err, "code", -32602);
            cJSON_AddStringToObject(err, "message", "Invalid params");
            cJSON_AddItemToObject(response, "error", err);
            return response;
        }

        cJSON *tool_name_obj = cJSON_GetObjectItem(params_obj, "name");
        cJSON *args_obj = cJSON_GetObjectItem(params_obj, "arguments");
        if (!tool_name_obj || !cJSON_IsString(tool_name_obj)) {
            cJSON *err = cJSON_CreateObject();
            cJSON_AddNumberToObject(err, "code", -32602);
            cJSON_AddStringToObject(err, "message", "Tool name required");
            cJSON_AddItemToObject(response, "error", err);
            return response;
        }

        const char *t_name = tool_name_obj->valuestring;
        for (size_t i = 0; i < s_tool_count; i++) {
            if (strcmp(s_tools[i].name, t_name) == 0) {
                cJSON *tool_res = s_tools[i].handler(args_obj);
                cJSON *result = cJSON_CreateObject();
                cJSON_AddItemToObject(result, "content", tool_res);
                cJSON_AddItemToObject(response, "result", result);
                return response;
            }
        }

        cJSON *err = cJSON_CreateObject();
        cJSON_AddNumberToObject(err, "code", -32601);
        cJSON_AddStringToObject(err, "message", "Tool not found");
        cJSON_AddItemToObject(response, "error", err);
        return response;
    }

    cJSON *err = cJSON_CreateObject();
    cJSON_AddNumberToObject(err, "code", -32601);
    cJSON_AddStringToObject(err, "message", "Method not found");
    cJSON_AddItemToObject(response, "error", err);
    return response;
}

