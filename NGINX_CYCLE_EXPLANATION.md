# NGINX Cycle详解

## 1. Cycle概述

### 1.1 什么是Cycle

**Cycle（ngx_cycle_t）**是NGINX最核心的数据结构，它代表NGINX的一个完整的运行时周期（configuration cycle）。Cycle包含了NGINX运行所需的所有信息，包括配置、模块、连接、事件、监听socket、共享内存等。

### 1.2 Cycle的定义

```c
// src/core/ngx_cycle.h:39-86
struct ngx_cycle_s {
    void                  ****conf_ctx;        // 配置上下文数组
    ngx_pool_t               *pool;            // 内存池
    ngx_log_t                *log;             // 日志对象
    ngx_log_t                 new_log;         // 新日志对象（重载时使用）
    ngx_uint_t                log_use_stderr;  // 是否使用标准错误输出日志
    ngx_connection_t        **files;           // 文件连接数组
    ngx_connection_t         *free_connections; // 空闲连接链表
    ngx_uint_t                free_connection_n; // 空闲连接数量
    ngx_module_t            **modules;         // 模块数组
    ngx_uint_t                modules_n;       // 模块数量
    ngx_uint_t                modules_used;    // 模块是否已使用
    ngx_queue_t               reusable_connections_queue; // 可重用连接队列
    ngx_uint_t                reusable_connections_n;     // 可重用连接数量
    time_t                    connections_reuse_time;     // 连接重用时间
    ngx_array_t               listening;       // 监听socket数组
    ngx_array_t               paths;           // 路径数组
    ngx_array_t               config_dump;     // 配置转储数组
    ngx_rbtree_t              config_dump_rbtree; // 配置转储红黑树
    ngx_rbtree_node_t         config_dump_sentinel; // 配置转储哨兵节点
    ngx_list_t                open_files;      // 打开文件链表
    ngx_list_t                shared_memory;   // 共享内存链表
    ngx_uint_t                connection_n;    // 连接数量
    ngx_uint_t                files_n;         // 文件数量
    ngx_connection_t         *connections;     // 连接数组
    ngx_event_t              *read_events;     // 读事件数组
    ngx_event_t              *write_events;    // 写事件数组
    ngx_cycle_t              *old_cycle;       // 旧cycle引用（重载时使用）
    ngx_str_t                 conf_file;       // 配置文件路径
    ngx_str_t                 conf_param;      // 配置参数
    ngx_str_t                 conf_prefix;     // 配置前缀
    ngx_str_t                 prefix;          // 安装前缀
    ngx_str_t                 error_log;       // 错误日志路径
    ngx_str_t                 lock_file;       // 锁文件路径
    ngx_str_t                 hostname;        // 主机名
};
```

---

## 2. Cycle的核心组成部分

### 2.1 配置信息（conf_ctx）

**作用：**存储所有模块的配置结构体，是NGINX配置系统的核心。

**数据结构：**
```c
void ****conf_ctx;  // 四维指针数组
```

**访问方式：**
```c
// 获取核心模块配置
ngx_core_conf_t *ccf = ngx_get_conf(cycle->conf_ctx, ngx_core_module);

// 获取HTTP模块配置
ngx_http_conf_ctx_t *hctx = ngx_get_conf(cycle->conf_ctx, ngx_http_module);
```

**特点：**
- 每个模块通过索引访问自己的配置
- 配置结构体在解析配置文件时创建
- 配置信息在每个进程中都有独立的副本

### 2.2 内存池（pool）

**作用：**管理cycle生命周期内的内存分配，避免内存泄漏。

**特点：**
- 默认大小为16KB（NGX_CYCLE_POOL_SIZE）
- cycle销毁时，pool自动释放所有内存
- 所有cycle相关的内存都从pool分配

**使用示例：**
```c
// 创建cycle时创建pool
pool = ngx_create_pool(NGX_CYCLE_POOL_SIZE, log);
cycle->pool = pool;

// 从pool分配内存
cycle->conf_prefix.data = ngx_pstrdup(pool, &old_cycle->conf_prefix);

// cycle销毁时，pool自动释放所有内存
ngx_destroy_pool(cycle->pool);
```

### 2.3 日志对象（log）

**作用：**全局日志对象，所有模块使用它记录日志。

**特点：**
- 支持多级日志（error、warn、info、debug）
- 支持日志文件轮转
- 支持同时输出到文件和标准错误

**使用示例：**
```c
// 记录错误日志
ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "error message");

// 记录调试日志
ngx_log_debug1(NGX_LOG_DEBUG_CORE, cycle->log, 0, "debug message: %s", str);
```

### 2.4 模块数组（modules）

**作用：**存储所有已加载的模块，包括核心模块、事件模块、HTTP模块等。

**特点：**
- 模块按类型和索引组织
- 模块顺序决定配置解析顺序
- 每个模块都有唯一的索引

**使用示例：**
```c
// 遍历所有模块
for (i = 0; cycle->modules[i]; i++) {
    ngx_module_t *module = cycle->modules[i];
    // 处理模块
}

// 访问特定模块
ngx_module_t *core_module = cycle->modules[ngx_core_module.index];
```

### 2.5 连接和事件（connections、read_events、write_events）

**作用：**高效处理大量并发连接，使用连接池和事件池。

**数据结构：**
```c
ngx_connection_t *connections;   // 连接数组
ngx_event_t      *read_events;   // 读事件数组
ngx_event_t      *write_events;  // 写事件数组
```

**特点：**
- 连接池：预分配的连接对象数组，避免频繁分配
- 事件池：每个连接对应一个读事件和一个写事件
- 空闲连接：使用free_connections链表管理空闲连接

**使用示例：**
```c
// 获取空闲连接
ngx_connection_t *c = cycle->free_connections;
cycle->free_connections = c->data;
cycle->free_connection_n--;

// 归还连接
c->data = cycle->free_connections;
cycle->free_connections = c;
cycle->free_connection_n++;
```

### 2.6 监听socket（listening）

**作用：**存储所有监听的socket（HTTP、HTTPS、Stream等）。

**数据结构：**
```c
ngx_array_t listening;  // 监听socket数组
```

**特点：**
- 每个listen指令对应一个ngx_listening_t对象
- Worker进程从这些socket接受连接
- 支持热重载：新cycle继承旧cycle的监听socket

**使用示例：**
```c
// 获取监听socket数组
ngx_listening_t *ls = cycle->listening.elts;

// 遍历所有监听socket
for (i = 0; i < cycle->listening.nelts; i++) {
    ngx_listening_t *ls = &cycle->listening.elts[i];
    // 处理监听socket
}
```

### 2.7 共享内存（shared_memory）

**作用：**Worker进程间共享的内存区域，用于存储限速计数器、会话信息、缓存元数据等。

**数据结构：**
```c
ngx_list_t shared_memory;  // 共享内存链表
```

**特点：**
- 通过mmap()在进程间共享
- 使用slab分配器管理共享内存
- 支持热重载：新cycle继承旧cycle的共享内存

**使用示例：**
```c
// 添加共享内存区域
ngx_shm_zone_t *zone = ngx_shared_memory_add(cf, &name, size, tag);

// 初始化共享内存
if (zone->init(zone, NULL) != NGX_OK) {
    return NGX_ERROR;
}
```

### 2.8 打开的文件（open_files）

**作用：**跟踪所有打开的文件描述符，用于日志文件轮转和文件描述符管理。

**数据结构：**
```c
ngx_list_t open_files;  // 打开文件链表
```

**特点：**
- Worker进程继承这些文件描述符
- 支持日志文件轮转：重新打开日志文件
- 支持文件描述符管理：跟踪所有打开的文件

**使用示例：**
```c
// 添加打开的文件
ngx_open_file_t *file = ngx_list_push(&cycle->open_files);
file->name = name;
file->fd = fd;

// 重新打开文件
ngx_reopen_files(cycle, user);
```

### 2.9 路径信息（paths）

**作用：**存储所有路径配置（日志路径、临时文件路径等），用于文件管理和路径解析。

**数据结构：**
```c
ngx_array_t paths;  // 路径数组
```

**特点：**
- 存储所有路径配置
- 支持路径管理和解析
- 支持路径清理和创建

### 2.10 旧cycle引用（old_cycle）

**作用：**指向上一个cycle（配置重载时），用于平滑过渡和资源清理。

**特点：**
- 配置重载时，新cycle的old_cycle指向旧cycle
- 旧cycle在新cycle稳定后销毁
- 支持平滑过渡：旧连接继续使用旧cycle，新连接使用新cycle

---

## 3. Cycle的生命周期

### 3.1 创建阶段

**位置：**`src/core/ngx_cycle.c:ngx_init_cycle()`

**流程：**
1. 创建内存池（16KB）
2. 创建cycle对象
3. 初始化核心数据结构数组
4. 复制模块数组
5. 创建配置上下文数组
6. 解析配置文件
7. 打开监听socket
8. 创建共享内存区域
9. 调用模块初始化钩子

**代码示例：**
```c
// 创建内存池
pool = ngx_create_pool(NGX_CYCLE_POOL_SIZE, log);

// 创建cycle对象
cycle = ngx_pcalloc(pool, sizeof(ngx_cycle_t));
cycle->pool = pool;
cycle->log = log;
cycle->old_cycle = old_cycle;

// 初始化核心数据结构
ngx_array_init(&cycle->listening, pool, n, sizeof(ngx_listening_t));
ngx_list_init(&cycle->open_files, pool, n, sizeof(ngx_open_file_t));
ngx_list_init(&cycle->shared_memory, pool, n, sizeof(ngx_shm_zone_t));

// 创建配置上下文数组
cycle->conf_ctx = ngx_pcalloc(pool, ngx_max_module * sizeof(void *));

// 解析配置文件
ngx_conf_parse(&conf, &cycle->conf_file);

// 打开监听socket
ngx_open_listening_sockets(cycle);

// 创建共享内存区域
ngx_init_zone_pool(cycle, shm_zone);
```

### 3.2 使用阶段

**特点：**
- Master进程和Worker进程都使用同一个cycle
- 配置信息在每个进程中都有独立的副本
- 共享内存区域在进程间共享
- 连接和事件对象在每个Worker进程中独立

**使用方式：**
```c
// Master进程
ngx_master_process_cycle(cycle);

// Worker进程
ngx_worker_process_cycle(cycle, data);

// 单进程模式
ngx_single_process_cycle(cycle);
```

### 3.3 重载阶段

**流程：**
1. 收到SIGHUP信号
2. 创建新cycle（old_cycle指向旧cycle）
3. 解析新配置文件
4. 打开新监听socket
5. 创建新共享内存区域
6. 启动新Worker进程
7. 旧Worker进程优雅退出
8. 新cycle稳定后，销毁旧cycle

**代码示例：**
```c
// 收到SIGHUP信号
if (ngx_reconfigure) {
    // 创建新cycle
    cycle = ngx_init_cycle(old_cycle);
    
    // 启动新Worker进程
    ngx_start_worker_processes(cycle, ccf->worker_processes, NGX_PROCESS_RESPAWN);
    
    // 旧Worker进程优雅退出
    ngx_signal_worker_processes(cycle, ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
    
    // 新cycle稳定后，销毁旧cycle
    ngx_destroy_cycle_pools(&conf);
}
```

### 3.4 销毁阶段

**流程：**
1. 关闭监听socket
2. 关闭打开的文件
3. 销毁共享内存区域
4. 销毁内存池（自动释放所有内存）

**代码示例：**
```c
// 关闭监听socket
ngx_close_listening_sockets(cycle);

// 关闭打开的文件
ngx_reopen_files(cycle, user);

// 销毁共享内存区域
ngx_destroy_cycle_pools(&conf);

// 销毁内存池（自动释放所有内存）
ngx_destroy_pool(cycle->pool);
```

---

## 4. Cycle的共享方式

### 4.1 进程间共享

**Master进程和Worker进程：**
- 通过fork()共享cycle
- 配置信息在每个进程中都有独立的副本
- 共享内存区域通过mmap()在进程间共享
- 连接和事件对象在每个Worker进程中独立

### 4.2 共享内存

**特点：**
- 使用mmap()在进程间共享
- 使用slab分配器管理共享内存
- 支持热重载：新cycle继承旧cycle的共享内存

**使用示例：**
```c
// 创建共享内存区域
ngx_shm_zone_t *zone = ngx_shared_memory_add(cf, &name, size, tag);

// 初始化共享内存
if (zone->init(zone, NULL) != NGX_OK) {
    return NGX_ERROR;
}

// 访问共享内存
void *data = zone->data;
```

### 4.3 配置信息

**特点：**
- 配置信息在每个进程中都有独立的副本
- 配置结构体从cycle->pool分配
- 配置重载时，新cycle创建新的配置结构体

---

## 5. Cycle的作用

### 5.1 配置管理

**作用：**存储和管理NGINX的所有配置信息。

**特点：**
- 配置信息存储在conf_ctx中
- 每个模块通过索引访问自己的配置
- 支持配置热重载

### 5.2 资源管理

**作用：**管理NGINX的所有资源，包括内存、文件、socket等。

**特点：**
- 使用内存池管理内存
- 跟踪所有打开的文件
- 管理所有监听socket

### 5.3 模块管理

**作用：**管理所有已加载的模块。

**特点：**
- 模块数组存储在modules中
- 模块按类型和索引组织
- 支持模块动态加载

### 5.4 进程管理

**作用：**支持Master-Worker进程模型。

**特点：**
- Master进程和Worker进程都使用同一个cycle
- 支持配置热重载
- 支持平滑升级

---

## 6. Cycle的使用示例

### 6.1 获取配置信息

```c
// 获取核心模块配置
ngx_core_conf_t *ccf = ngx_get_conf(cycle->conf_ctx, ngx_core_module);

// 获取HTTP模块配置
ngx_http_conf_ctx_t *hctx = ngx_get_conf(cycle->conf_ctx, ngx_http_module);

// 访问配置项
ngx_int_t worker_processes = ccf->worker_processes;
ngx_str_t pid_file = ccf->pid;
```

### 6.2 记录日志

```c
// 记录错误日志
ngx_log_error(NGX_LOG_ERR, cycle->log, 0, "error message");

// 记录调试日志
ngx_log_debug1(NGX_LOG_DEBUG_CORE, cycle->log, 0, "debug message: %s", str);

// 记录警告日志
ngx_log_error(NGX_LOG_WARN, cycle->log, 0, "warning message");
```

### 6.3 访问监听socket

```c
// 获取监听socket数组
ngx_listening_t *ls = cycle->listening.elts;

// 遍历所有监听socket
for (i = 0; i < cycle->listening.nelts; i++) {
    ngx_listening_t *ls = &cycle->listening.elts[i];
    // 处理监听socket
}
```

### 6.4 访问共享内存

```c
// 遍历共享内存区域
ngx_list_part_t *part = &cycle->shared_memory.part;
ngx_shm_zone_t *shm_zone = part->elts;

for (i = 0; /* void */; i++) {
    if (i >= part->nelts) {
        if (part->next == NULL) {
            break;
        }
        part = part->next;
        shm_zone = part->elts;
        i = 0;
    }
    // 处理共享内存区域
}
```

---

## 7. Cycle的设计优势

### 7.1 统一管理

**优势：**所有NGINX的运行信息都集中在一个数据结构中，便于管理。

**好处：**
- 简化代码结构
- 便于调试和维护
- 支持配置热重载

### 7.2 内存管理

**优势：**使用内存池管理cycle相关的所有内存，避免内存泄漏。

**好处：**
- 自动释放内存
- 减少内存碎片
- 提高内存使用效率

### 7.3 进程间共享

**优势：**支持Master-Worker进程模型，共享内存区域在进程间共享。

**好处：**
- 支持多进程架构
- 提高并发处理能力
- 支持配置热重载

### 7.4 配置热重载

**优势：**支持配置热重载，无需重启服务。

**好处：**
- 零停机时间
- 平滑升级
- 提高可用性

---

## 8. 总结

Cycle是NGINX最核心的数据结构，它代表NGINX的一个完整的运行时周期。Cycle包含了NGINX运行所需的所有信息，包括配置、模块、连接、事件、监听socket、共享内存等。

**Cycle的核心特点：**
1. **统一管理**：所有NGINX的运行信息都集中在一个数据结构中
2. **内存管理**：使用内存池管理cycle相关的所有内存
3. **进程间共享**：支持Master-Worker进程模型
4. **配置热重载**：支持配置热重载，无需重启服务

**Cycle的生命周期：**
1. **创建**：在ngx_init_cycle()中创建新的cycle
2. **初始化**：解析配置文件，创建监听socket，初始化共享内存
3. **使用**：Master和Worker进程都使用同一个cycle
4. **重载**：配置重载时创建新cycle，旧cycle保留一段时间
5. **销毁**：新cycle稳定后，旧cycle被销毁

Cycle是NGINX架构的核心，理解Cycle对于理解NGINX的工作原理非常重要。
