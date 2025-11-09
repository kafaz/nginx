# NGINX Master进程和Worker进程分裂详解

## 1. Master进程和Worker进程的分裂点

### 1.1 分裂时机

Master进程和Worker进程的分裂发生在**main函数的最后阶段**，具体流程如下：

```
main() 函数流程：
│
├─ 1. 基础系统初始化
├─ 2. 创建临时Cycle
├─ 3. 系统初始化
├─ 4. 模块和Socket准备
├─ 5. 初始化主Cycle（解析配置）
├─ 6. 进程环境准备
│   ├─ 确定进程模式（单进程或Master-Worker）
│   ├─ 初始化信号处理
│   ├─ 守护进程化
│   └─ 创建PID文件
│
└─ 7. 启动工作循环 ← **分裂点**
    ├─ 单进程模式：ngx_single_process_cycle()
    └─ Master-Worker模式：ngx_master_process_cycle()
        └─ ngx_start_worker_processes() ← **Worker进程创建**
            └─ ngx_spawn_process() ← **fork()系统调用**
                └─ ngx_worker_process_cycle() ← **Worker进程执行**
```

### 1.2 关键代码位置

#### 1.2.1 进程模式确定

```c
// src/core/nginx.c:409-412
/* 如果配置要求使用master进程且当前是单进程模式，则切换到master模式 */
if (ccf->master && ngx_process == NGX_PROCESS_SINGLE) {
    ngx_process = NGX_PROCESS_MASTER;
}
```

#### 1.2.2 工作循环启动（分裂点）

```c
// src/core/nginx.c:457-465
/* 根据进程模式启动相应的工作循环 */
if (ngx_process == NGX_PROCESS_SINGLE) {
    /* 单进程模式，直接处理请求 */
    ngx_single_process_cycle(cycle);
} else {
    /* master进程模式，管理worker进程 */
    ngx_master_process_cycle(cycle);  // ← 进入Master进程循环
}
```

#### 1.2.3 Master进程启动Worker进程

```c
// src/os/unix/ngx_process_cycle.c:73-131
void
ngx_master_process_cycle(ngx_cycle_t *cycle)
{
    // ... 信号处理设置 ...
    
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
    
    // ← **关键：启动Worker进程**
    ngx_start_worker_processes(cycle, ccf->worker_processes,
                               NGX_PROCESS_RESPAWN);
    ngx_start_cache_manager_processes(cycle, 0);
    
    // Master进程进入事件循环，管理Worker进程
    for ( ;; ) {
        // 处理信号、监控Worker进程等
    }
}
```

#### 1.2.4 创建Worker进程

```c
// src/os/unix/ngx_process_cycle.c:335-349
static void
ngx_start_worker_processes(ngx_cycle_t *cycle, ngx_int_t n, ngx_int_t type)
{
    ngx_int_t  i;
    
    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "start worker processes");
    
    // 循环创建n个Worker进程
    for (i = 0; i < n; i++) {
        // ← **关键：spawn Worker进程**
        ngx_spawn_process(cycle, ngx_worker_process_cycle,
                          (void *) (intptr_t) i, "worker process", type);
        
        ngx_pass_open_channel(cycle);
    }
}
```

#### 1.2.5 fork()系统调用

```c
// src/os/unix/ngx_process.c:87-264
ngx_pid_t
ngx_spawn_process(ngx_cycle_t *cycle, ngx_spawn_proc_pt proc, void *data,
    char *name, ngx_int_t respawn)
{
    // ... 创建进程间通信channel ...
    
    // ← **关键：fork()系统调用**
    pid = fork();
    
    switch (pid) {
    case -1:
        // fork失败
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "fork() failed while spawning \"%s\"", name);
        ngx_close_channel(ngx_processes[s].channel, cycle->log);
        return NGX_INVALID_PID;
    
    case 0:
        // 子进程（Worker进程）
        ngx_parent = ngx_pid;
        ngx_pid = ngx_getpid();
        proc(cycle, data);  // ← 执行Worker进程循环
        break;
    
    default:
        // 父进程（Master进程）
        // ... 记录子进程信息 ...
        break;
    }
    
    return pid;
}
```

### 1.3 时间线总结

| 阶段 | 操作 | 代码位置 |
|------|------|----------|
| **启动阶段** | main函数执行初始化 | src/core/nginx.c:235-467 |
| **配置解析** | 解析nginx.conf，确定worker_processes | src/core/nginx.c:354 |
| **进程模式确定** | 根据配置确定单进程或Master-Worker模式 | src/core/nginx.c:409-412 |
| **Master进程启动** | 调用ngx_master_process_cycle | src/core/nginx.c:464 |
| **Worker进程创建** | Master进程调用ngx_start_worker_processes | src/os/unix/ngx_process_cycle.c:130 |
| **fork()系统调用** | 通过fork()创建子进程 | src/os/unix/ngx_process.c:184 |
| **Worker进程初始化** | Worker进程执行ngx_worker_process_init | src/os/unix/ngx_process_cycle.c:753 |
| **Worker进程循环** | Worker进程进入事件循环 | src/os/unix/ngx_process_cycle.c:663 |

---

## 2. CPU绑核（CPU Affinity）操作

### 2.1 CPU绑核概述

NGINX支持将Worker进程绑定到特定的CPU核心，这可以：
- **减少CPU缓存失效**：进程始终在同一个CPU核心上运行
- **提高性能**：避免进程在不同CPU核心间迁移
- **更好的NUMA优化**：在多CPU系统中，绑定到本地NUMA节点

### 2.2 CPU绑核配置

#### 2.2.1 配置文件设置

```nginx
# nginx.conf
worker_processes 4;
worker_cpu_affinity 0001 0010 0100 1000;  # 绑定到CPU 0, 1, 2, 3

# 或者使用auto模式
worker_processes auto;
worker_cpu_affinity auto;
```

#### 2.2.2 配置解析

```c
// src/core/nginx.c:1523-1618
static char *
ngx_set_cpu_affinity(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_core_conf_t  *ccf = conf;
    ngx_cpuset_t     *mask;
    
    // 分配CPU亲和性掩码数组
    mask = ngx_palloc(cf->pool, (cf->args->nelts - 1) * sizeof(ngx_cpuset_t));
    ccf->cpu_affinity = mask;
    ccf->cpu_affinity_n = cf->args->nelts - 1;
    
    // 解析CPU亲和性配置（二进制字符串，如 "0001" 表示绑定到CPU 0）
    // ...
    
    return NGX_CONF_OK;
}
```

### 2.3 CPU绑核执行时机

CPU绑核操作在**Worker进程初始化时**执行，具体在`ngx_worker_process_init`函数中：

```c
// src/os/unix/ngx_process_cycle.c:753-859
static void
ngx_worker_process_init(ngx_cycle_t *cycle, ngx_int_t worker)
{
    ngx_cpuset_t     *cpu_affinity;
    ngx_core_conf_t  *ccf;
    
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
    
    // 1. 设置进程优先级
    if (worker >= 0 && ccf->priority != 0) {
        setpriority(PRIO_PROCESS, 0, ccf->priority);
    }
    
    // 2. 设置资源限制
    // ...
    
    // 3. 切换用户（降低权限）
    // ...
    
    // 4. ← **关键：设置CPU亲和性**
    if (worker >= 0) {
        cpu_affinity = ngx_get_cpu_affinity(worker);
        if (cpu_affinity) {
            ngx_setaffinity(cpu_affinity, cycle->log);
        }
    }
    
    // 5. 设置工作目录
    // ...
    
    // 6. 初始化模块
    // ...
}
```

### 2.4 CPU绑核实现

#### 2.4.1 获取CPU亲和性掩码

```c
// src/core/nginx.c:1621-1672
ngx_cpuset_t *
ngx_get_cpu_affinity(ngx_uint_t n)
{
    ngx_core_conf_t  *ccf;
    ngx_cpuset_t     *mask;
    
    ccf = (ngx_core_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx,
                                           ngx_core_module);
    
    if (ccf->cpu_affinity == NULL) {
        return NULL;  // 未配置CPU亲和性
    }
    
    // auto模式：自动分配CPU
    if (ccf->cpu_affinity_auto) {
        // 根据worker编号自动选择CPU
        // ...
    }
    
    // 手动模式：使用配置的CPU掩码
    if (ccf->cpu_affinity_n > n) {
        return &ccf->cpu_affinity[n];  // 返回第n个worker的CPU掩码
    }
    
    // 如果配置的掩码数量少于worker数量，使用最后一个掩码
    return &ccf->cpu_affinity[ccf->cpu_affinity_n - 1];
}
```

#### 2.4.2 设置CPU亲和性（Linux）

```c
// src/os/unix/ngx_setaffinity.c:35-51
void
ngx_setaffinity(ngx_cpuset_t *cpu_affinity, ngx_log_t *log)
{
    ngx_uint_t  i;
    
    // 记录要使用的CPU核心
    for (i = 0; i < CPU_SETSIZE; i++) {
        if (CPU_ISSET(i, cpu_affinity)) {
            ngx_log_error(NGX_LOG_NOTICE, log, 0,
                          "sched_setaffinity(): using cpu #%ui", i);
        }
    }
    
    // ← **关键：调用系统调用绑定CPU**
    if (sched_setaffinity(0, sizeof(cpu_set_t), cpu_affinity) == -1) {
        ngx_log_error(NGX_LOG_ALERT, log, ngx_errno,
                      "sched_setaffinity() failed");
    }
}
```

#### 2.4.3 系统调用

`ngx_setaffinity`最终调用Linux的`sched_setaffinity()`系统调用：

```c
// Linux系统调用
int sched_setaffinity(pid_t pid, size_t cpusetsize, const cpu_set_t *mask);
```

**参数说明：**
- `pid`: 0表示当前进程
- `cpusetsize`: CPU掩码大小
- `mask`: CPU掩码，指定允许运行的CPU核心

### 2.5 CPU绑核示例

#### 2.5.1 配置示例

```nginx
# 4个Worker进程，分别绑定到CPU 0, 1, 2, 3
worker_processes 4;
worker_cpu_affinity 0001 0010 0100 1000;

# 8个Worker进程，每2个绑定到同一个CPU
worker_processes 8;
worker_cpu_affinity 0001 0001 0010 0010 0100 0100 1000 1000;

# 自动模式：根据CPU核心数自动分配
worker_processes auto;
worker_cpu_affinity auto;
```

#### 2.5.2 执行流程

```
Master进程启动
│
├─ 解析配置：worker_cpu_affinity 0001 0010 0100 1000
├─ 创建Worker进程 0
│   └─ fork() → Worker进程 0
│       └─ ngx_worker_process_init(worker=0)
│           └─ ngx_get_cpu_affinity(0) → 返回CPU掩码 0001
│               └─ ngx_setaffinity(0001) → sched_setaffinity(0, ..., 0001)
│                   └─ Worker进程 0绑定到CPU 0
├─ 创建Worker进程 1
│   └─ fork() → Worker进程 1
│       └─ ngx_worker_process_init(worker=1)
│           └─ ngx_get_cpu_affinity(1) → 返回CPU掩码 0010
│               └─ ngx_setaffinity(0010) → sched_setaffinity(0, ..., 0010)
│                   └─ Worker进程 1绑定到CPU 1
└─ ... 继续创建其他Worker进程
```

### 2.6 平台支持

NGINX在不同平台上使用不同的CPU亲和性API：

| 平台 | API | 代码位置 |
|------|-----|----------|
| **Linux** | `sched_setaffinity()` | src/os/unix/ngx_setaffinity.c:35-51 |
| **FreeBSD** | `cpuset_setaffinity()` | src/os/unix/ngx_setaffinity.c:14-31 |
| **Windows** | `SetProcessAffinityMask()` | src/os/win32/ngx_process.c |
| **其他平台** | 不支持 | - |

### 2.7 CPU绑核的优缺点

#### 优点：
1. **减少CPU缓存失效**：进程始终在同一个CPU核心上运行
2. **提高性能**：避免进程在不同CPU核心间迁移的开销
3. **更好的NUMA优化**：在多CPU系统中，绑定到本地NUMA节点
4. **可预测性**：进程运行在固定的CPU核心上，便于监控和调试

#### 缺点：
1. **灵活性降低**：进程不能充分利用所有CPU核心
2. **负载不均衡**：如果绑定的CPU核心负载高，无法迁移到其他核心
3. **配置复杂**：需要手动配置CPU亲和性掩码

### 2.8 最佳实践

1. **生产环境**：建议使用CPU绑核，特别是多CPU系统
2. **开发环境**：可以不使用CPU绑核，简化配置
3. **自动模式**：使用`worker_cpu_affinity auto`让NGINX自动分配
4. **监控**：使用`top`、`htop`等工具监控CPU使用情况
5. **测试**：通过压力测试验证CPU绑核的效果

---

## 3. 总结

### 3.1 Master进程和Worker进程分裂

1. **分裂时机**：在main函数的最后阶段，根据进程模式启动相应的工作循环
2. **分裂方式**：Master进程通过`fork()`系统调用创建Worker进程
3. **分裂位置**：
   - 主流程：`main()` → `ngx_master_process_cycle()` → `ngx_start_worker_processes()` → `ngx_spawn_process()` → `fork()`
   - 关键代码：`src/core/nginx.c:464`、`src/os/unix/ngx_process_cycle.c:130`、`src/os/unix/ngx_process.c:184`

### 3.2 CPU绑核操作

1. **配置时机**：在配置解析阶段（`ngx_set_cpu_affinity`）
2. **执行时机**：在Worker进程初始化时（`ngx_worker_process_init`）
3. **实现方式**：使用`sched_setaffinity()`系统调用（Linux）或`cpuset_setaffinity()`（FreeBSD）
4. **关键代码**：`src/core/nginx.c:1523-1672`、`src/os/unix/ngx_process_cycle.c:853-859`、`src/os/unix/ngx_setaffinity.c`

### 3.3 完整流程

```
main()函数启动
│
├─ 1-6. 初始化和配置解析
│
└─ 7. 启动工作循环
    │
    ├─ 单进程模式：ngx_single_process_cycle()
    │
    └─ Master-Worker模式：ngx_master_process_cycle()
        │
        ├─ Master进程：进入事件循环，管理Worker进程
        │
        └─ Worker进程创建：ngx_start_worker_processes()
            │
            └─ 循环创建n个Worker进程
                │
                ├─ fork()创建子进程
                │
                └─ Worker进程初始化：ngx_worker_process_init()
                    │
                    ├─ 设置进程优先级
                    ├─ 设置资源限制
                    ├─ 切换用户
                    ├─ ← **CPU绑核：ngx_setaffinity()**
                    ├─ 设置工作目录
                    └─ 初始化模块
                        │
                        └─ 进入事件循环：ngx_worker_process_cycle()
```

---

## 4. 参考资料

- NGINX官方文档：http://nginx.org/en/docs/
- Linux sched_setaffinity手册：`man sched_setaffinity`
- CPU亲和性配置：`worker_cpu_affinity`指令
