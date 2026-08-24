#pragma once

#if __has_include("ServerConfig.local.h")
#include "ServerConfig.local.h"
#else
#error "Missing Config/ServerConfig.local.h. Copy Config/ServerConfig.example.h to ServerConfig.local.h and set the server base URL."
#endif
