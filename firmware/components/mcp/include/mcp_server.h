#pragma once

#include "cJSON.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef cJSON *(*mcp_tool_fn_t)(const cJSON *arguments);

typedef struct {
    const char *name;
    const char *description;
    const char *parameters_schema_json;
    mcp_tool_fn_t handler;
} mcp_tool_t;

esp_err_t mcp_server_init(void);
esp_err_t mcp_register_tool(const mcp_tool_t *tool);

/**
 * @brief Handle incoming MCP JSON-RPC 2.0 message
 * @param json_rpc_msg Parsed cJSON message
 * @return Response cJSON object (caller must free with cJSON_Delete)
 */
cJSON *mcp_handle_message(const cJSON *json_rpc_msg);

/**
 * @brief Get tool definitions array for LLM initialization
 */
cJSON *mcp_get_tools_manifest(void);

#ifdef __cplusplus
}
#endif

