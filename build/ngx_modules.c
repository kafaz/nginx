
#include <ngx_config.h>
#include <ngx_core.h>

extern ngx_module_t ngx_core_module;
extern ngx_module_t ngx_errlog_module;
extern ngx_module_t ngx_conf_module;
extern ngx_module_t ngx_events_module;
extern ngx_module_t ngx_event_core_module;
extern ngx_module_t ngx_http_module;
extern ngx_module_t ngx_http_core_module;
extern ngx_module_t ngx_http_log_module;
extern ngx_module_t ngx_http_upstream_module;
extern ngx_module_t ngx_http_static_module;
extern ngx_module_t ngx_http_index_module;
extern ngx_module_t ngx_http_access_module;
extern ngx_module_t ngx_http_write_filter_module;
extern ngx_module_t ngx_http_header_filter_module;

ngx_module_t *ngx_modules[] = {
    &ngx_core_module,
    &ngx_errlog_module,
    &ngx_conf_module,
    &ngx_events_module,
    &ngx_event_core_module,
    &ngx_http_module,
    &ngx_http_core_module,
    &ngx_http_log_module,
    &ngx_http_upstream_module,
    &ngx_http_static_module,
    &ngx_http_index_module,
    &ngx_http_access_module,
    &ngx_http_write_filter_module,
    &ngx_http_header_filter_module,
    NULL
};

char *ngx_module_names[] = {
    "ngx_core_module",
    "ngx_errlog_module",
    "ngx_conf_module",
    "ngx_events_module",
    "ngx_event_core_module",
    "ngx_http_module",
    "ngx_http_core_module",
    "ngx_http_log_module",
    "ngx_http_upstream_module",
    "ngx_http_static_module",
    "ngx_http_index_module",
    "ngx_http_access_module",
    "ngx_http_write_filter_module",
    "ngx_http_header_filter_module",
    NULL
};
