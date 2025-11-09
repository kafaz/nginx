/*
 * CMake configuration wrapper for NGINX
 * This file provides compatibility with NGINX's build system
 */

#ifndef _NGX_CONFIG_H_INCLUDED_
#define _NGX_CONFIG_H_INCLUDED_

#include "ngx_auto_config.h"

#ifdef _WIN32
#include <ngx_win32_config.h>
#else
#include <ngx_auto_headers.h>
#endif

#endif /* _NGX_CONFIG_H_INCLUDED_ */
