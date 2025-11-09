# NGINX信号发送机制详解

## 1. 代码位置和功能

### 1.1 代码位置

```c
// src/core/nginx.c:395-398
/* 如果有信号需要发送给master进程 */
if (ngx_signal) {
    return ngx_signal_process(cycle, ngx_signal);
}
```

### 1.2 功能说明

这段代码实现了**NGINX信号发送模式**，用于管理已经运行的NGINX进程。当使用命令行参数 `-s signal` 时，NGINX不会启动新的服务器进程，而是向已经运行的master进程发送信号，然后立即退出。

---

## 2. 使用场景

### 2.1 典型场景

**场景1：停止NGINX服务**
```bash
nginx -s stop
```

**场景2：重新加载配置**
```bash
nginx -s reload
```

**场景3：优雅关闭**
```bash
nginx -s quit
```

**场景4：重新打开日志文件**
```bash
nginx -s reopen
```

### 2.2 为什么需要这个功能？

1. **进程管理**：NGINX以守护进程方式运行，不能直接交互，需要通过信号管理
2. **配置热重载**：在不停止服务的情况下重新加载配置文件
3. **日志轮转**：在不停止服务的情况下重新打开日志文件
4. **优雅关闭**：允许正在处理的请求完成后再关闭

---

## 3. 工作流程

### 3.1 完整流程

```
用户执行命令：nginx -s stop
│
├─ 1. 解析命令行参数（ngx_get_options函数）
│   └─ 解析 -s 参数，设置 ngx_signal = "stop"
│   └─ 设置 ngx_process = NGX_PROCESS_SIGNALLER
│
├─ 2. 初始化基础系统
│   ├─ ngx_strerror_init()  # 错误消息系统
│   ├─ ngx_time_init()      # 时间系统
│   └─ ngx_log_init()       # 日志系统
│
├─ 3. 解析配置文件（ngx_init_cycle函数）
│   └─ 获取PID文件路径（ccf->pid）
│
├─ 4. 检查信号发送模式（第395-398行）← **关键检查点**
│   └─ if (ngx_signal) {
│       └─ return ngx_signal_process(cycle, ngx_signal);
│   }
│
├─ 5. 读取PID文件（ngx_signal_process函数）
│   └─ 打开PID文件（通常位于 /var/run/nginx.pid）
│   └─ 读取master进程的PID
│
├─ 6. 发送信号（ngx_os_signal_process函数）
│   └─ 将信号名称转换为信号编号（"stop" → SIGTERM）
│   └─ 使用kill(pid, signo)系统调用发送信号
│
└─ 7. 退出进程
    └─ 函数返回，nginx进程退出
```

### 3.2 关键函数调用链

```
main()
│
├─ ngx_get_options()          # 解析 -s 参数
│   └─ ngx_signal = "stop"    # 设置信号名称
│
├─ ngx_init_cycle()           # 解析配置，获取PID文件路径
│
└─ ngx_signal_process()       # 发送信号
    │
    ├─ 读取PID文件
    │   └─ ngx_open_file()    # 打开PID文件
    │   └─ ngx_read_file()    # 读取PID
    │
    └─ ngx_os_signal_process() # 发送信号
        │
        ├─ 查找信号编号
        │   └─ 遍历signals[]数组，找到对应的信号编号
        │
        └─ kill(pid, signo)   # 系统调用发送信号
```

---

## 4. 支持的信号

### 4.1 信号列表

| 信号名称 | 信号值 | 系统信号 | 功能说明 |
|---------|--------|---------|---------|
| **stop** | NGX_TERMINATE_SIGNAL | SIGTERM | 快速停止，立即终止所有进程 |
| **quit** | NGX_SHUTDOWN_SIGNAL | SIGQUIT | 优雅关闭，等待请求处理完成后停止 |
| **reload** | NGX_RECONFIGURE_SIGNAL | SIGHUP | 重新加载配置，重新读取配置文件 |
| **reopen** | NGX_REOPEN_SIGNAL | SIGUSR1 | 重新打开日志文件，用于日志轮转 |

### 4.2 信号定义

```c
// src/os/unix/ngx_process.c:39-83
ngx_signal_t  signals[] = {
    { ngx_signal_value(NGX_RECONFIGURE_SIGNAL), "SIGHUP",  "reload", ngx_signal_handler },
    { ngx_signal_value(NGX_REOPEN_SIGNAL),      "SIGUSR1", "reopen", ngx_signal_handler },
    { ngx_signal_value(NGX_TERMINATE_SIGNAL),   "SIGTERM", "stop",   ngx_signal_handler },
    { ngx_signal_value(NGX_SHUTDOWN_SIGNAL),    "SIGQUIT", "quit",   ngx_signal_handler },
    // ...
};
```

---

## 5. 实现细节

### 5.1 命令行参数解析

```c
// src/core/nginx.c:1058-1080
case 's':
    if (*p) {
        ngx_signal = (char *) p;
    } else if (argv[++i]) {
        ngx_signal = argv[i];
    } else {
        ngx_log_stderr(0, "option \"-s\" requires parameter");
        return NGX_ERROR;
    }
    
    // 验证信号名称
    if (ngx_strcmp(ngx_signal, "stop") == 0
        || ngx_strcmp(ngx_signal, "quit") == 0
        || ngx_strcmp(ngx_signal, "reopen") == 0
        || ngx_strcmp(ngx_signal, "reload") == 0)
    {
        ngx_process = NGX_PROCESS_SIGNALLER;  // 设置为信号发送模式
        goto next;
    }
    
    ngx_log_stderr(0, "invalid option: \"-s %s\"", ngx_signal);
    return NGX_ERROR;
```

### 5.2 读取PID文件

```c
// src/core/ngx_cycle.c:1089-1139
ngx_int_t
ngx_signal_process(ngx_cycle_t *cycle, char *sig)
{
    ssize_t           n;
    ngx_pid_t         pid;
    ngx_file_t        file;
    ngx_core_conf_t  *ccf;
    u_char            buf[NGX_INT64_LEN + 2];
    
    // 获取核心模块配置
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
    
    // 打开PID文件
    file.name = ccf->pid;
    file.fd = ngx_open_file(file.name.data, NGX_FILE_RDONLY,
                            NGX_FILE_OPEN, NGX_FILE_DEFAULT_ACCESS);
    
    if (file.fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, ngx_errno,
                      ngx_open_file_n " \"%s\" failed", file.name.data);
        return 1;  // PID文件不存在，nginx未运行
    }
    
    // 读取PID
    n = ngx_read_file(&file, buf, NGX_INT64_LEN + 2, 0);
    ngx_close_file(file.fd);
    
    // 解析PID
    pid = ngx_atoi(buf, n);
    if (pid == (ngx_pid_t) NGX_ERROR) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                      "invalid PID number \"%*s\" in \"%s\"",
                      n, buf, file.name.data);
        return 1;
    }
    
    // 发送信号
    return ngx_os_signal_process(cycle, sig, pid);
}
```

### 5.3 发送信号

```c
// src/os/unix/ngx_process.c:632-648
ngx_int_t
ngx_os_signal_process(ngx_cycle_t *cycle, char *name, ngx_pid_t pid)
{
    ngx_signal_t  *sig;
    
    // 查找信号编号
    for (sig = signals; sig->signo != 0; sig++) {
        if (ngx_strcmp(name, sig->name) == 0) {
            // 发送信号
            if (kill(pid, sig->signo) != -1) {
                return 0;  // 成功
            }
            
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "kill(%P, %d) failed", pid, sig->signo);
        }
    }
    
    return 1;  // 失败
}
```

---

## 6. 信号处理

### 6.1 Master进程信号处理

当Master进程收到信号后，会触发信号处理函数：

```c
// src/os/unix/ngx_process.c:318-467
static void
ngx_signal_handler(int signo, siginfo_t *siginfo, void *ucontext)
{
    switch (signo) {
    case ngx_signal_value(NGX_SHUTDOWN_SIGNAL):  // SIGQUIT (quit)
        ngx_quit = 1;  // 设置优雅关闭标志
        break;
    
    case ngx_signal_value(NGX_TERMINATE_SIGNAL):  // SIGTERM (stop)
        ngx_terminate = 1;  // 设置快速停止标志
        break;
    
    case ngx_signal_value(NGX_RECONFIGURE_SIGNAL):  // SIGHUP (reload)
        ngx_reconfigure = 1;  // 设置重载配置标志
        break;
    
    case ngx_signal_value(NGX_REOPEN_SIGNAL):  // SIGUSR1 (reopen)
        ngx_reopen = 1;  // 设置重新打开日志标志
        break;
    }
}
```

### 6.2 Master进程响应信号

Master进程在主循环中检查这些标志，并执行相应操作：

```c
// src/os/unix/ngx_process_cycle.c:139-274
for ( ;; ) {
    // 检查重载配置标志
    if (ngx_reconfigure) {
        ngx_reconfigure = 0;
        // 重新初始化cycle，重新加载配置
        cycle = ngx_init_cycle(cycle);
        // 启动新的Worker进程
        ngx_start_worker_processes(cycle, ccf->worker_processes, NGX_PROCESS_RESPAWN);
    }
    
    // 检查重新打开日志标志
    if (ngx_reopen) {
        ngx_reopen = 0;
        // 重新打开所有日志文件
        ngx_reopen_files(cycle, (ngx_uid_t) -1);
    }
    
    // 检查优雅关闭标志
    if (ngx_quit) {
        // 向Worker进程发送退出信号
        ngx_signal_worker_processes(cycle, ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
        // 等待Worker进程退出
        // ...
    }
    
    // 检查快速停止标志
    if (ngx_terminate) {
        // 向Worker进程发送终止信号
        ngx_signal_worker_processes(cycle, ngx_signal_value(NGX_TERMINATE_SIGNAL));
        // 立即退出
        // ...
    }
}
```

---

## 7. 使用示例

### 7.1 停止NGINX

```bash
# 快速停止（立即终止）
nginx -s stop

# 等价于：
kill -TERM $(cat /var/run/nginx.pid)
```

**执行流程：**
1. 读取PID文件，获取master进程PID
2. 发送SIGTERM信号
3. Master进程收到信号，设置`ngx_terminate = 1`
4. Master进程向所有Worker进程发送SIGTERM
5. 所有进程立即退出

### 7.2 优雅关闭

```bash
# 优雅关闭（等待请求处理完成）
nginx -s quit

# 等价于：
kill -QUIT $(cat /var/run/nginx.pid)
```

**执行流程：**
1. 读取PID文件，获取master进程PID
2. 发送SIGQUIT信号
3. Master进程收到信号，设置`ngx_quit = 1`
4. Master进程向所有Worker进程发送SIGQUIT
5. Worker进程停止接受新连接，等待现有请求处理完成
6. 所有进程优雅退出

### 7.3 重新加载配置

```bash
# 重新加载配置（热重载）
nginx -s reload

# 等价于：
kill -HUP $(cat /var/run/nginx.pid)
```

**执行流程：**
1. 读取PID文件，获取master进程PID
2. 发送SIGHUP信号
3. Master进程收到信号，设置`ngx_reconfigure = 1`
4. Master进程重新解析配置文件
5. 如果配置有效，启动新的Worker进程
6. 旧的Worker进程优雅退出
7. **服务不中断**

### 7.4 重新打开日志文件

```bash
# 重新打开日志文件（用于日志轮转）
nginx -s reopen

# 等价于：
kill -USR1 $(cat /var/run/nginx.pid)
```

**执行流程：**
1. 读取PID文件，获取master进程PID
2. 发送SIGUSR1信号
3. Master进程收到信号，设置`ngx_reopen = 1`
4. Master进程和所有Worker进程重新打开日志文件
5. **服务不中断**

---

## 8. 错误处理

### 8.1 PID文件不存在

如果PID文件不存在（NGINX未运行），函数会返回错误：

```c
if (file.fd == NGX_INVALID_FILE) {
    ngx_log_error(NGX_LOG_ERR, cycle->log, ngx_errno,
                  ngx_open_file_n " \"%s\" failed", file.name.data);
    return 1;  // 返回错误
}
```

**错误信息：**
```
nginx: [error] open() "/var/run/nginx.pid" failed (2: No such file or directory)
```

### 8.2 无效的PID

如果PID文件内容无效，函数会返回错误：

```c
if (pid == (ngx_pid_t) NGX_ERROR) {
    ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                  "invalid PID number \"%*s\" in \"%s\"",
                  n, buf, file.name.data);
    return 1;
}
```

### 8.3 发送信号失败

如果进程不存在或没有权限，`kill()`会失败：

```c
if (kill(pid, sig->signo) != -1) {
    return 0;  // 成功
}

ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
              "kill(%P, %d) failed", pid, sig->signo);
```

**错误信息：**
```
nginx: [alert] kill(12345, 15) failed (3: No such process)
```

---

## 9. 设计优势

### 9.1 进程管理

- **统一接口**：通过命令行参数统一管理NGINX进程
- **安全性**：通过PID文件确保只向正确的进程发送信号
- **可靠性**：错误处理完善，提供清晰的错误信息

### 9.2 热重载

- **零停机**：重新加载配置时，服务不中断
- **配置验证**：重新加载前会验证配置文件的正确性
- **平滑切换**：新Worker进程启动后，旧Worker进程优雅退出

### 9.3 日志管理

- **日志轮转**：支持在不停止服务的情况下重新打开日志文件
- **文件描述符**：重新打开日志文件时，会关闭旧的文件描述符
- **多进程同步**：所有进程同步重新打开日志文件

---

## 10. 总结

### 10.1 关键点

1. **信号发送模式**：使用 `-s signal` 参数时，NGINX不会启动服务器，而是发送信号后退出
2. **PID文件**：通过读取PID文件获取master进程的PID
3. **信号映射**：将信号名称（如"stop"）映射到系统信号（如SIGTERM）
4. **系统调用**：使用`kill()`系统调用发送信号
5. **进程退出**：发送信号后，当前进程立即退出

### 10.2 使用建议

1. **生产环境**：使用 `nginx -s reload` 进行配置热重载
2. **停止服务**：优先使用 `nginx -s quit` 进行优雅关闭
3. **日志轮转**：使用 `nginx -s reopen` 配合日志轮转工具（如logrotate）
4. **紧急情况**：使用 `nginx -s stop` 快速停止服务

### 10.3 注意事项

1. **权限**：需要有权限读取PID文件和发送信号
2. **PID文件**：确保PID文件路径正确
3. **进程状态**：确保NGINX进程正在运行
4. **信号处理**：Master进程必须正在运行并能够接收信号
