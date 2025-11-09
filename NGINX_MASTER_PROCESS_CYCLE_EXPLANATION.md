# NGINX Master进程循环详解

## 1. 函数概述

### 1.1 函数定义

```c
void ngx_master_process_cycle(ngx_cycle_t *cycle);
```

### 1.2 函数作用

`ngx_master_process_cycle`是NGINX Master进程的核心函数，负责：
1. 管理Worker进程生命周期
2. 处理各种系统信号
3. 监控Worker进程状态
4. 自动重启崩溃的Worker进程
5. 处理配置重载、日志重开、热升级等操作

### 1.3 函数位置

- **Unix版本**：`src/os/unix/ngx_process_cycle.c:73-608`
- **Windows版本**：`src/os/win32/ngx_process_cycle.c:63-274`

---

## 2. 函数工作流程

### 2.1 初始化阶段

```
1. 设置信号处理
   ├─ 初始化信号集合（SIGCHLD、SIGHUP、SIGTERM等）
   ├─ 阻塞信号，防止信号处理函数在关键代码段执行
   └─ 清空信号集合，准备用于sigsuspend()

2. 设置进程标题
   ├─ 构建进程标题字符串
   └─ 设置进程名称（master process /usr/sbin/nginx ...）

3. 启动子进程
   ├─ 启动Worker进程（根据配置数量）
   └─ 启动Cache Manager进程（如果配置了缓存）

4. 初始化状态变量
   ├─ ngx_new_binary: 热升级时的新进程PID
   ├─ delay: 进程终止延迟
   ├─ sigio: 信号I/O计数器
   └─ live: 是否还有存活的Worker进程
```

### 2.2 主循环阶段

```
主循环（for (;;)）：
│
├─ 1. 进程终止延迟处理
│   ├─ 如果设置了延迟，设置定时器等待进程退出
│   ├─ 延迟递增：50ms → 100ms → 200ms → ...
│   └─ 如果超过1000ms，发送SIGKILL强制终止
│
├─ 2. 等待信号
│   ├─ 使用sigsuspend()挂起进程，等待信号到来
│   └─ 收到信号后，恢复信号掩码，然后返回
│
├─ 3. 更新时间
│   └─ 更新系统时间缓存
│
├─ 4. 处理SIGCHLD信号
│   ├─ 回收退出的子进程（调用waitpid()）
│   ├─ 检查进程退出状态
│   ├─ 如果进程配置了自动重启，重新启动进程
│   └─ 返回是否还有存活的Worker进程
│
├─ 5. 检查退出条件
│   ├─ 如果没有存活的Worker进程且收到终止信号
│   └─ 调用ngx_master_process_exit()退出
│
├─ 6. 处理快速终止信号（SIGTERM）
│   ├─ 设置延迟（首次50ms）
│   ├─ 等待sigio计数器归零
│   ├─ 如果延迟超过1000ms，发送SIGKILL
│   └─ 否则发送SIGTERM信号
│
├─ 7. 处理优雅关闭信号（SIGQUIT）
│   ├─ 向所有Worker进程发送SIGQUIT信号
│   ├─ 关闭监听socket（不再接受新连接）
│   └─ Worker进程等待请求处理完成后退出
│
├─ 8. 处理配置重载信号（SIGHUP）
│   ├─ 重新解析配置文件
│   ├─ 启动新的Worker进程（使用新配置）
│   ├─ 等待新Worker进程启动（100ms）
│   └─ 向旧Worker进程发送SIGQUIT信号，优雅关闭
│
├─ 9. 处理进程重启信号
│   ├─ 停止所有Worker进程
│   └─ 启动新的Worker进程
│
├─ 10. 处理日志重开信号（SIGUSR1）
│    ├─ Master进程重新打开日志文件
│    └─ 向所有Worker进程发送SIGUSR1信号
│
├─ 11. 处理热升级信号（SIGUSR2）
│    ├─ 调用ngx_exec_new_binary()执行新二进制文件
│    ├─ 新进程继承旧进程的监听socket
│    └─ 新进程启动新的Worker进程
│
└─ 12. 处理停止接受连接信号
     ├─ 设置ngx_noaccepting = 1
     └─ 向所有Worker进程发送SIGQUIT信号
```

### 2.3 退出阶段

```
1. 发送信号给所有Worker进程
   └─ 根据信号类型，发送SIGTERM或SIGQUIT

2. 等待所有Worker进程退出
   └─ 通过SIGCHLD信号回收子进程

3. 清理资源
   ├─ 删除PID文件
   ├─ 调用模块的exit_master钩子
   ├─ 关闭监听socket
   └─ 销毁内存池

4. 退出进程
   └─ 调用exit(0)退出
```

---

## 3. 信号处理详解

### 3.1 SIGCHLD信号

**信号作用：**Worker进程退出时发送，Master进程回收子进程。

**处理流程：**
1. Worker进程退出时，发送SIGCHLD信号给Master进程
2. 信号处理函数设置`ngx_reap = 1`
3. Master进程检查`ngx_reap`标志，调用`ngx_reap_children()`回收子进程
4. 如果Worker进程配置了自动重启，Master进程会重新启动它

**代码位置：**
```c
if (ngx_reap) {
    ngx_reap = 0;
    live = ngx_reap_children(cycle);
}
```

### 3.2 SIGHUP信号（配置重载）

**信号作用：**重新加载配置文件，实现零停机时间配置重载。

**处理流程：**
1. 收到SIGHUP信号，设置`ngx_reconfigure = 1`
2. 调用`ngx_init_cycle()`重新解析配置文件
3. 如果配置有效，启动新的Worker进程（使用新配置）
4. 等待新Worker进程启动（100ms）
5. 向旧Worker进程发送SIGQUIT信号，优雅关闭
6. 旧Worker进程处理完现有请求后退出
7. 新Worker进程继续处理新请求

**使用场景：**
- `nginx -s reload`命令
- `kill -HUP <pid>`命令
- `systemctl reload nginx`命令

**代码位置：**
```c
if (ngx_reconfigure) {
    ngx_reconfigure = 0;
    cycle = ngx_init_cycle(cycle);
    ngx_start_worker_processes(cycle, ccf->worker_processes, NGX_PROCESS_JUST_RESPAWN);
    ngx_msleep(100);
    ngx_signal_worker_processes(cycle, ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
}
```

### 3.3 SIGQUIT信号（优雅关闭）

**信号作用：**等待请求处理完成后退出，实现零中断关闭。

**处理流程：**
1. 向所有Worker进程发送SIGQUIT信号
2. 关闭监听socket（不再接受新连接）
3. Worker进程停止接受新连接，等待现有请求处理完成
4. Worker进程处理完所有请求后退出
5. Master进程等待所有Worker进程退出后退出

**使用场景：**
- `nginx -s quit`命令
- `kill -QUIT <pid>`命令

**代码位置：**
```c
if (ngx_quit) {
    ngx_signal_worker_processes(cycle, ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
    ngx_close_listening_sockets(cycle);
}
```

### 3.4 SIGTERM信号（快速终止）

**信号作用：**立即终止所有进程，可能中断请求。

**处理流程：**
1. 设置延迟（首次50ms）
2. 等待sigio计数器归零（给进程时间退出）
3. 如果延迟超过1000ms，发送SIGKILL强制终止
4. 否则发送SIGTERM信号
5. 延迟翻倍，继续等待

**使用场景：**
- `nginx -s stop`命令
- `systemctl stop nginx`命令
- `kill -TERM <pid>`命令

**代码位置：**
```c
if (ngx_terminate) {
    if (delay == 0) {
        delay = 50;
    }
    if (delay > 1000) {
        ngx_signal_worker_processes(cycle, SIGKILL);
    } else {
        ngx_signal_worker_processes(cycle, ngx_signal_value(NGX_TERMINATE_SIGNAL));
    }
}
```

### 3.5 SIGUSR1信号（日志重开）

**信号作用：**重新打开日志文件，实现日志文件轮转。

**处理流程：**
1. Master进程重新打开日志文件
2. 向所有Worker进程发送SIGUSR1信号
3. Worker进程重新打开日志文件
4. 实现日志文件轮转（log rotation）

**使用场景：**
- `nginx -s reopen`命令
- `kill -USR1 <pid>`命令
- 日志轮转脚本（logrotate）

**代码位置：**
```c
if (ngx_reopen) {
    ngx_reopen = 0;
    ngx_reopen_files(cycle, ccf->user);
    ngx_signal_worker_processes(cycle, ngx_signal_value(NGX_REOPEN_SIGNAL));
}
```

### 3.6 SIGUSR2信号（热升级）

**信号作用：**执行热升级，实现零停机时间升级。

**处理流程：**
1. 收到SIGUSR2信号，设置`ngx_change_binary = 1`
2. 调用`ngx_exec_new_binary()`执行新二进制文件
3. 新进程继承旧进程的监听socket
4. 新进程启动新的Worker进程
5. 旧进程的Worker进程优雅退出
6. 新进程的Worker进程继续处理请求

**使用场景：**
- 升级NGINX版本
- 更新NGINX二进制文件
- 需要零停机时间升级

**代码位置：**
```c
if (ngx_change_binary) {
    ngx_change_binary = 0;
    ngx_new_binary = ngx_exec_new_binary(cycle, ngx_argv);
}
```

---

## 4. 进程管理详解

### 4.1 Worker进程管理

**启动Worker进程：**
```c
ngx_start_worker_processes(cycle, ccf->worker_processes, NGX_PROCESS_RESPAWN);
```

**进程类型说明：**
- `NGX_PROCESS_RESPAWN`: 如果进程退出，自动重启
- `NGX_PROCESS_JUST_RESPAWN`: 刚刚启动的进程（配置重载时使用）
- `NGX_PROCESS_DETACHED`: detached进程（不自动重启）

**自动重启机制：**
- 如果Worker进程异常退出，Master进程会自动重启它
- 重启延迟：首次重启延迟50ms，每次重启延迟翻倍，最大1000ms
- 如果Worker进程在1秒内无法正常退出，Master进程会发送SIGKILL强制终止

### 4.2 Cache Manager进程管理

**启动Cache Manager进程：**
```c
ngx_start_cache_manager_processes(cycle, 0);
```

**Cache Manager进程作用：**
- 管理缓存，清理过期缓存
- 定期检查缓存目录，删除过期文件
- 只在配置了缓存路径时启动

### 4.3 进程监控

**进程状态监控：**
- Master进程通过SIGCHLD信号监控Worker进程状态
- 如果Worker进程退出，Master进程会回收子进程并可能重启
- Master进程维护所有子进程的状态信息

---

## 5. 配置重载机制

### 5.1 零停机时间配置重载

**配置重载流程：**
1. 收到SIGHUP信号，设置`ngx_reconfigure = 1`
2. 调用`ngx_init_cycle()`重新解析配置文件
3. 如果配置有效，启动新的Worker进程（使用新配置）
4. 等待新Worker进程启动（100ms）
5. 向旧Worker进程发送SIGQUIT信号，优雅关闭
6. 旧Worker进程停止接受新连接，等待现有请求处理完成
7. 旧Worker进程处理完所有请求后退出
8. 新Worker进程继续处理新请求
9. 实现零停机时间配置重载

### 5.2 配置重载的优势

**零停机时间：**
- 新Worker进程启动后，立即开始处理新请求
- 旧Worker进程继续处理现有请求，直到请求完成
- 没有请求中断，实现零停机时间配置重载

**配置验证：**
- 如果新配置无效，Master进程会继续使用旧配置
- 不会影响现有服务，保证服务稳定性

---

## 6. 热升级机制

### 6.1 零停机时间热升级

**热升级流程：**
1. 收到SIGUSR2信号，设置`ngx_change_binary = 1`
2. 调用`ngx_exec_new_binary()`执行新二进制文件
3. 新进程继承旧进程的监听socket
4. 新进程启动新的Worker进程
5. 旧进程的Worker进程优雅退出
6. 新进程的Worker进程继续处理请求
7. 实现零停机时间热升级

### 6.2 热升级的优势

**零停机时间：**
- 新进程启动后，立即开始处理新请求
- 旧进程继续处理现有请求，直到请求完成
- 没有请求中断，实现零停机时间热升级

**平滑过渡：**
- 新进程继承旧进程的监听socket
- 新进程和旧进程可以同时运行
- 旧进程处理完所有请求后退出

---

## 7. 进程终止机制

### 7.1 优雅关闭（SIGQUIT）

**优雅关闭流程：**
1. 向所有Worker进程发送SIGQUIT信号
2. 关闭监听socket（不再接受新连接）
3. Worker进程停止接受新连接，等待现有请求处理完成
4. Worker进程处理完所有请求后退出
5. Master进程等待所有Worker进程退出后退出

**优势：**
- 等待请求处理完成，零中断
- 不会中断正在处理的请求
- 保证数据完整性

### 7.2 快速终止（SIGTERM）

**快速终止流程：**
1. 设置延迟（首次50ms）
2. 等待sigio计数器归零（给进程时间退出）
3. 如果延迟超过1000ms，发送SIGKILL强制终止
4. 否则发送SIGTERM信号
5. 延迟翻倍，继续等待

**优势：**
- 立即终止，快速响应
- 适用于紧急情况
- 可能中断正在处理的请求

---

## 8. 日志重开机制

### 8.1 日志文件轮转

**日志重开流程：**
1. Master进程重新打开日志文件
2. 向所有Worker进程发送SIGUSR1信号
3. Worker进程重新打开日志文件
4. 实现日志文件轮转（log rotation）

### 8.2 日志轮转示例

**日志轮转步骤：**
1. 重命名日志文件：`mv access.log access.log.1`
2. 发送SIGUSR1信号：`kill -USR1 <pid>`
3. NGINX重新打开日志文件，创建新的access.log
4. 旧的日志文件可以压缩或删除

---

## 9. 关键函数说明

### 9.1 ngx_start_worker_processes()

**函数作用：**启动Worker进程。

**函数签名：**
```c
static void ngx_start_worker_processes(ngx_cycle_t *cycle, ngx_int_t n, ngx_int_t type);
```

**参数说明：**
- `cycle`: 当前cycle对象
- `n`: Worker进程数量
- `type`: 进程类型（NGX_PROCESS_RESPAWN等）

### 9.2 ngx_reap_children()

**函数作用：**回收退出的子进程。

**函数签名：**
```c
static ngx_uint_t ngx_reap_children(ngx_cycle_t *cycle);
```

**返回值：**
- 返回是否还有存活的Worker进程

### 9.3 ngx_signal_worker_processes()

**函数作用：**向所有Worker进程发送信号。

**函数签名：**
```c
static void ngx_signal_worker_processes(ngx_cycle_t *cycle, int signo);
```

**参数说明：**
- `cycle`: 当前cycle对象
- `signo`: 信号编号

### 9.4 ngx_master_process_exit()

**函数作用：**Master进程退出，清理资源。

**函数签名：**
```c
static void ngx_master_process_exit(ngx_cycle_t *cycle);
```

**退出流程：**
1. 删除PID文件
2. 调用模块的exit_master钩子
3. 关闭监听socket
4. 销毁内存池
5. 退出进程

---

## 10. 总结

`ngx_master_process_cycle`是NGINX Master进程的核心函数，负责管理Worker进程和处理各种系统信号。它实现了以下关键功能：

1. **进程管理**：启动、监控、重启Worker进程
2. **信号处理**：处理配置重载、日志重开、热升级等信号
3. **零停机时间**：实现配置重载和热升级的零停机时间
4. **优雅关闭**：实现进程的优雅关闭，保证数据完整性
5. **自动重启**：自动重启崩溃的Worker进程，保证服务可用性

理解`ngx_master_process_cycle`函数对于理解NGINX的进程管理和信号处理机制非常重要。
