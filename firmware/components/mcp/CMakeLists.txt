idf_component_register(SRCS "mcp_server.c" "tools/gpio.c"
                        INCLUDE_DIRS "include"
                        REQUIRES json driver audio ui)
