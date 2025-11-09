# NGINX PID文件详解

## 1. PID文件概述

### 1.1 什么是PID文件

**PID文件（Process ID File）**是一个文本文件，用于存储NGINX Master进程的进程ID（Process ID）。它是NGINX进程管理和通信的重要机制。

### 1.2 PID文件的基本信息

- **文件路径**：默认 `/var/run/nginx.pid`（可通过`pid`指令配置）
- **文件内容**：Master进程的PID（整数，如 `12345`）
- **文件格式**：纯文本文件，只包含PID数字和换行符
- **文件权限**：通常为644（rw-r--r--）

---

## 2. PID文件的主要作用

### 2.1 进程标识

PID文件存储Master进程的进程ID，用于标识NGINX进程。

**示例：**
```bash
# 查看PID文件内容
cat /var/run/nginx.pid
# 输出：12345

# 验证进程是否存在
ps aux | grep 12345
# 输出：root 12345 ... nginx: master process ...
```

### 2.2 进程管理（信号发送）

PID文件最重要的作用是**支持进程管理**，通过读取PID文件获取Master进程PID，然后发送信号管理进程。

**使用场景：**
- `nginx -s stop`：读取PID文件，发送SIGTERM信号停止NGINX
- `nginx -s reload`：读取PID文件，发送SIGHUP信号重新加载配置
- `nginx -s quit`：读取PID文件，发送SIGQUIT信号优雅关闭
- `nginx -s reopen`：读取PID文件，发送SIGUSR1信号重新打开日志文件

**实现代码：**
```c
// src/core/ngx_cycle.c:1089-1139
ngx_int_t
ngx_signal_process(ngx_cycle_t *cycle, char *sig)
{
    // 1. 读取PID文件
    file.fd = ngx_open_file(file.name.data, NGX_FILE_RDONLY, ...);
    n = ngx_read_file(&file, buf, NGX_INT64_LEN + 2, 0);
    
    // 2. 解析PID
    pid = ngx_atoi(buf, n);
    
    // 3. 发送信号
    return ngx_os_signal_process(cycle, sig, pid);
}
```

### 2.3 进程检测

通过检查PID文件是否存在来判断NGINX是否运行。

**使用场景：**
- 系统监控工具检查NGINX运行状态
- 启动脚本检查NGINX是否已运行
- 系统服务管理（systemd、init.d等）

**示例：**
```bash
# 检查NGINX是否运行
if [ -f /var/run/nginx.pid ]; then
    echo "NGINX is running"
    PID=$(cat /var/run/nginx.pid)
    echo "Master process PID: $PID"
else
    echo "NGINX is not running"
fi
```

### 2.4 热升级支持

在热升级（hot upgrade）场景中，PID文件用于管理新旧进程。

**热升级流程：**
1. 旧进程将PID文件重命名为 `.oldbin`（如 `nginx.pid.oldbin`）
2. 新进程创建新的PID文件（`nginx.pid`）
3. 旧进程退出后，新进程继续使用PID文件
4. 如果新进程启动失败，旧进程恢复PID文件名

**实现代码：**
```c
// src/core/nginx.c:947-958
/* 热升级：重命名PID文件 */
if (ngx_rename_file(ccf->pid.data, ccf->oldpid.data) == NGX_FILE_ERROR) {
    // 错误处理
}

// 执行新进程
pid = ngx_execute(cycle, &ctx);

// 如果失败，恢复PID文件名
if (pid == NGX_INVALID_PID) {
    ngx_rename_file(ccf->oldpid.data, ccf->pid.data);
}
```

---

## 3. PID文件的生命周期

### 3.1 创建（ngx_create_pidfile）

**创建时机：**
- Master进程启动后
- 在守护进程化（daemon）之后
- 确保写入的是守护进程的PID，而不是父进程的PID

**创建位置：**
```c
// src/core/nginx.c:477-480
/* 创建PID文件 */
if (ngx_create_pidfile(&ccf->pid, cycle->log) != NGX_OK) {
    return 1;
}
```

**创建实现：**
```c
// src/core/ngx_cycle.c:1024-1068
ngx_int_t
ngx_create_pidfile(ngx_str_t *name, ngx_log_t *log)
{
    // 1. 只在Master进程或单进程模式下创建
    if (ngx_process > NGX_PROCESS_MASTER) {
        return NGX_OK;  // Worker进程不创建PID文件
    }
    
    // 2. 打开PID文件（如果存在则截断）
    file.fd = ngx_open_file(file.name.data, NGX_FILE_RDWR,
                            NGX_FILE_TRUNCATE, NGX_FILE_DEFAULT_ACCESS);
    
    // 3. 写入PID
    len = ngx_snprintf(pid, NGX_INT64_LEN + 2, "%P%N", ngx_pid) - pid;
    ngx_write_file(&file, pid, len, 0);
    
    // 4. 关闭文件
    ngx_close_file(file.fd);
    
    return NGX_OK;
}
```

### 3.2 使用（读取PID）

**使用场景：**
- 信号发送：`nginx -s signal` 命令
- 进程管理：系统监控工具
- 进程检测：检查NGINX是否运行

**读取实现：**
```c
// src/core/ngx_cycle.c:1089-1139
ngx_int_t
ngx_signal_process(ngx_cycle_t *cycle, char *sig)
{
    // 1. 打开PID文件
    file.fd = ngx_open_file(file.name.data, NGX_FILE_RDONLY, ...);
    
    // 2. 读取PID
    n = ngx_read_file(&file, buf, NGX_INT64_LEN + 2, 0);
    
    // 3. 解析PID
    pid = ngx_atoi(buf, n);
    
    // 4. 发送信号
    return ngx_os_signal_process(cycle, sig, pid);
}
```

### 3.3 删除（ngx_delete_pidfile）

**删除时机：**
- Master进程退出时
- 进程正常关闭时
- 进程异常退出时（如果可能）

**删除位置：**
```c
// src/os/unix/ngx_process_cycle.c:656-695
static void
ngx_master_process_exit(ngx_cycle_t *cycle)
{
    // 删除PID文件
    ngx_delete_pidfile(cycle);
    
    // 清理资源
    // ...
    
    exit(0);
}
```

**删除实现：**
```c
// src/core/ngx_cycle.c:1072-1085
void
ngx_delete_pidfile(ngx_cycle_t *cycle)
{
    ngx_core_conf_t  *ccf;
    
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
    
    // 热升级场景：删除oldpid文件；正常退出：删除pid文件
    name = ngx_new_binary ? ccf->oldpid.data : ccf->pid.data;
    
    // 删除文件
    ngx_delete_file(name);
}
```

### 3.4 热升级（重命名）

**热升级流程：**
1. 旧进程重命名PID文件：`nginx.pid` → `nginx.pid.oldbin`
2. 新进程创建新的PID文件：`nginx.pid`
3. 旧进程退出后，新进程继续运行
4. 如果新进程启动失败，恢复PID文件名

**重命名实现：**
```c
// src/core/nginx.c:947-958
/* 热升级：重命名PID文件 */
if (ngx_rename_file(ccf->pid.data, ccf->oldpid.data) == NGX_FILE_ERROR) {
    // 错误处理
}

// 执行新进程
pid = ngx_execute(cycle, &ctx);

// 如果失败，恢复PID文件名
if (pid == NGX_INVALID_PID) {
    ngx_rename_file(ccf->oldpid.data, ccf->pid.data);
}
```

---

## 4. PID文件的配置

### 4.1 配置文件设置

**nginx.conf配置：**
```nginx
# 设置PID文件路径
pid /var/run/nginx.pid;

# 或者使用相对路径
pid logs/nginx.pid;
```

**默认路径：**
- 如果未配置，默认使用 `NGX_PID_PATH`（通常为 `/var/run/nginx.pid`）

### 4.2 配置解析

```c
// src/core/nginx.c:1358-1364
/* 如果未配置PID文件路径，使用默认路径 */
if (ccf->pid.len == 0) {
    ngx_str_set(&ccf->pid, NGX_PID_PATH);  // 默认路径
}

/* 转换为绝对路径 */
if (ngx_conf_full_name(cycle, &ccf->pid, 0) != NGX_OK) {
    return NGX_CONF_ERROR;
}
```

---

## 5. PID文件的使用示例

### 5.1 命令行使用

**读取PID文件：**
```bash
# 查看Master进程PID
cat /var/run/nginx.pid
# 输出：12345

# 使用PID文件发送信号
kill -HUP $(cat /var/run/nginx.pid)  # 等价于 nginx -s reload
kill -TERM $(cat /var/run/nginx.pid)  # 等价于 nginx -s stop
kill -QUIT $(cat /var/run/nginx.pid)  # 等价于 nginx -s quit
kill -USR1 $(cat /var/run/nginx.pid)  # 等价于 nginx -s reopen
```

**NGINX命令（内部使用PID文件）：**
```bash
# 这些命令内部都会读取PID文件
nginx -s stop      # 快速停止
nginx -s quit      # 优雅关闭
nginx -s reload    # 重新加载配置
nginx -s reopen    # 重新打开日志文件
```

### 5.2 脚本使用

**检查NGINX运行状态：**
```bash
#!/bin/bash

PIDFILE="/var/run/nginx.pid"

if [ -f "$PIDFILE" ]; then
    PID=$(cat "$PIDFILE")
    if ps -p "$PID" > /dev/null 2>&1; then
        echo "NGINX is running (PID: $PID)"
    else
        echo "NGINX PID file exists but process is not running"
        rm -f "$PIDFILE"
    fi
else
    echo "NGINX is not running"
fi
```

**启动NGINX（检查是否已运行）：**
```bash
#!/bin/bash

PIDFILE="/var/run/nginx.pid"

if [ -f "$PIDFILE" ]; then
    PID=$(cat "$PIDFILE")
    if ps -p "$PID" > /dev/null 2>&1; then
        echo "NGINX is already running (PID: $PID)"
        exit 1
    else
        echo "Removing stale PID file"
        rm -f "$PIDFILE"
    fi
fi

# 启动NGINX
nginx
```

### 5.3 系统服务管理

**systemd服务文件（使用PID文件）：**
```ini
[Unit]
Description=The nginx HTTP and reverse proxy server
After=network.target remote-fs.target nss-lookup.target

[Service]
Type=forking
PIDFile=/var/run/nginx.pid
ExecStartPre=/usr/sbin/nginx -t
ExecStart=/usr/sbin/nginx
ExecReload=/bin/kill -s HUP $MAINPID
ExecStop=/bin/kill -s TERM $MAINPID
PrivateTmp=true

[Install]
WantedBy=multi-user.target
```

**init.d脚本（使用PID文件）：**
```bash
#!/bin/bash

PIDFILE="/var/run/nginx.pid"

case "$1" in
    start)
        if [ -f "$PIDFILE" ]; then
            echo "NGINX is already running"
            exit 1
        fi
        nginx
        ;;
    stop)
        if [ ! -f "$PIDFILE" ]; then
            echo "NGINX is not running"
            exit 1
        fi
        kill -TERM $(cat "$PIDFILE")
        ;;
    reload)
        if [ ! -f "$PIDFILE" ]; then
            echo "NGINX is not running"
            exit 1
        fi
        kill -HUP $(cat "$PIDFILE")
        ;;
    status)
        if [ -f "$PIDFILE" ]; then
            PID=$(cat "$PIDFILE")
            if ps -p "$PID" > /dev/null 2>&1; then
                echo "NGINX is running (PID: $PID)"
            else
                echo "NGINX PID file exists but process is not running"
            fi
        else
            echo "NGINX is not running"
        fi
        ;;
esac
```

---

## 6. PID文件的实现细节

### 6.1 创建PID文件

```c
// src/core/ngx_cycle.c:1024-1068
ngx_int_t
ngx_create_pidfile(ngx_str_t *name, ngx_log_t *log)
{
    size_t      len;
    ngx_int_t   rc;
    ngx_uint_t  create;
    ngx_file_t  file;
    u_char      pid[NGX_INT64_LEN + 2];
    
    /* 只在Master进程或单进程模式下创建PID文件
     * Worker进程不创建PID文件
     */
    if (ngx_process > NGX_PROCESS_MASTER) {
        return NGX_OK;
    }
    
    /* 打开PID文件
     * - 配置测试模式：NGX_FILE_CREATE_OR_OPEN（创建或打开）
     * - 正常运行模式：NGX_FILE_TRUNCATE（截断，覆盖已存在的文件）
     */
    create = ngx_test_config ? NGX_FILE_CREATE_OR_OPEN : NGX_FILE_TRUNCATE;
    file.fd = ngx_open_file(file.name.data, NGX_FILE_RDWR,
                            create, NGX_FILE_DEFAULT_ACCESS);
    
    if (file.fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                      ngx_open_file_n " \"%s\" failed", file.name.data);
        return NGX_ERROR;
    }
    
    /* 写入PID（只在非配置测试模式下） */
    if (!ngx_test_config) {
        // 格式化PID：%P表示进程ID，%N表示换行符
        len = ngx_snprintf(pid, NGX_INT64_LEN + 2, "%P%N", ngx_pid) - pid;
        
        // 写入文件
        if (ngx_write_file(&file, pid, len, 0) == NGX_ERROR) {
            rc = NGX_ERROR;
        }
    }
    
    /* 关闭文件 */
    if (ngx_close_file(file.fd) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, log, ngx_errno,
                      ngx_close_file_n " \"%s\" failed", file.name.data);
    }
    
    return rc;
}
```

### 6.2 读取PID文件

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
    
    /* 获取核心模块配置，获取PID文件路径 */
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
    
    /* 打开PID文件（只读模式） */
    file.name = ccf->pid;
    file.fd = ngx_open_file(file.name.data, NGX_FILE_RDONLY,
                            NGX_FILE_OPEN, NGX_FILE_DEFAULT_ACCESS);
    
    if (file.fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, ngx_errno,
                      ngx_open_file_n " \"%s\" failed", file.name.data);
        return 1;  // PID文件不存在，nginx未运行
    }
    
    /* 读取PID文件内容 */
    n = ngx_read_file(&file, buf, NGX_INT64_LEN + 2, 0);
    ngx_close_file(file.fd);
    
    if (n == NGX_ERROR) {
        return 1;
    }
    
    /* 去除换行符 */
    while (n-- && (buf[n] == CR || buf[n] == LF)) { /* void */ }
    
    /* 解析PID */
    pid = ngx_atoi(buf, ++n);
    if (pid == (ngx_pid_t) NGX_ERROR) {
        ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                      "invalid PID number \"%*s\" in \"%s\"",
                      n, buf, file.name.data);
        return 1;
    }
    
    /* 发送信号 */
    return ngx_os_signal_process(cycle, sig, pid);
}
```

### 6.3 删除PID文件

```c
// src/core/ngx_cycle.c:1072-1085
void
ngx_delete_pidfile(ngx_cycle_t *cycle)
{
    u_char           *name;
    ngx_core_conf_t  *ccf;
    
    /* 获取核心模块配置 */
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);
    
    /* 确定要删除的PID文件
     * - 热升级场景：删除oldpid文件
     * - 正常退出：删除pid文件
     */
    name = ngx_new_binary ? ccf->oldpid.data : ccf->pid.data;
    
    /* 删除文件 */
    if (ngx_delete_file(name) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      ngx_delete_file_n " \"%s\" failed", name);
    }
}
```

---

## 7. PID文件的错误处理

### 7.1 创建失败

**原因：**
- 目录不存在
- 权限不足
- 磁盘空间不足

**错误处理：**
```c
if (file.fd == NGX_INVALID_FILE) {
    ngx_log_error(NGX_LOG_EMERG, log, ngx_errno,
                  ngx_open_file_n " \"%s\" failed", file.name.data);
    return NGX_ERROR;  // 返回错误，nginx启动失败
}
```

### 7.2 读取失败

**原因：**
- PID文件不存在（NGINX未运行）
- 权限不足
- 文件损坏

**错误处理：**
```c
if (file.fd == NGX_INVALID_FILE) {
    ngx_log_error(NGX_LOG_ERR, cycle->log, ngx_errno,
                  ngx_open_file_n " \"%s\" failed", file.name.data);
    return 1;  // 返回错误
}
```

### 7.3 无效的PID

**原因：**
- PID文件内容不是数字
- PID文件为空
- PID文件格式错误

**错误处理：**
```c
pid = ngx_atoi(buf, n);
if (pid == (ngx_pid_t) NGX_ERROR) {
    ngx_log_error(NGX_LOG_ERR, cycle->log, 0,
                  "invalid PID number \"%*s\" in \"%s\"",
                  n, buf, file.name.data);
    return 1;
}
```

### 7.4 进程不存在

**原因：**
- PID文件中的进程已退出
- PID被其他进程重用
- 进程被kill -9强制杀死

**错误处理：**
```c
// 在ngx_os_signal_process中
if (kill(pid, sig->signo) != -1) {
    return 0;  // 成功
}

ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
              "kill(%P, %d) failed", pid, sig->signo);
// 返回错误
```

---

## 8. PID文件的安全考虑

### 8.1 文件权限

**推荐权限：**
- 文件所有者：root或nginx用户
- 文件权限：644（rw-r--r--）
- 目录权限：755（rwxr-xr-x）

**安全考虑：**
- 只有所有者可以写入PID文件
- 其他用户只能读取PID文件
- 防止恶意修改PID文件

### 8.2 文件位置

**推荐位置：**
- `/var/run/nginx.pid`：系统运行时目录
- `/run/nginx.pid`：现代Linux系统的运行时目录
- `logs/nginx.pid`：相对路径（不推荐生产环境）

**安全考虑：**
- 使用系统运行时目录，避免权限问题
- 确保目录存在且有写权限
- 避免使用用户目录，防止权限问题

### 8.3 文件锁定

**问题：**
- 多个进程同时写入PID文件可能导致竞争条件
- 热升级时新旧进程可能同时访问PID文件

**解决方案：**
- 使用文件锁（flock）保护PID文件
- 热升级时使用文件重命名避免竞争
- 使用原子操作确保文件一致性

---

## 9. PID文件的最佳实践

### 9.1 配置建议

**生产环境：**
```nginx
# 使用系统运行时目录
pid /var/run/nginx.pid;

# 或者使用/run目录（现代Linux系统）
pid /run/nginx.pid;
```

**开发环境：**
```nginx
# 可以使用相对路径
pid logs/nginx.pid;
```

### 9.2 监控建议

**检查PID文件：**
- 定期检查PID文件是否存在
- 验证PID文件中的进程是否运行
- 清理过期的PID文件

**监控脚本：**
```bash
#!/bin/bash

PIDFILE="/var/run/nginx.pid"

if [ -f "$PIDFILE" ]; then
    PID=$(cat "$PIDFILE")
    if ! ps -p "$PID" > /dev/null 2>&1; then
        echo "Warning: PID file exists but process is not running"
        echo "Removing stale PID file"
        rm -f "$PIDFILE"
    fi
fi
```

### 9.3 故障排查

**PID文件不存在：**
- 检查NGINX是否运行
- 检查PID文件路径配置
- 检查目录权限

**PID文件无效：**
- 检查PID文件内容
- 验证进程是否存在
- 清理并重新启动NGINX

**PID文件权限问题：**
- 检查文件所有者
- 检查文件权限
- 检查目录权限

---

## 10. 总结

### 10.1 PID文件的核心作用

1. **进程标识**：存储Master进程的PID，用于标识NGINX进程
2. **进程管理**：支持通过信号管理NGINX进程（stop/reload/quit/reopen）
3. **进程检测**：通过检查PID文件判断NGINX是否运行
4. **热升级支持**：在热升级场景中管理新旧进程

### 10.2 PID文件的生命周期

1. **创建**：Master进程启动后，在守护进程化之后创建
2. **使用**：信号发送时读取，进程管理时使用
3. **删除**：Master进程退出时删除
4. **热升级**：热升级时重命名为.oldbin，新进程创建新的PID文件

### 10.3 关键代码位置

- **创建**：`src/core/nginx.c:477-480`、`src/core/ngx_cycle.c:1024-1068`
- **读取**：`src/core/ngx_cycle.c:1089-1139` (`ngx_signal_process`)
- **删除**：`src/core/ngx_cycle.c:1072-1085`、`src/os/unix/ngx_process_cycle.c:660`
- **热升级**：`src/core/nginx.c:947-958` (`ngx_exec_new_binary`)

### 10.4 使用建议

1. **生产环境**：使用系统运行时目录（`/var/run/nginx.pid`或`/run/nginx.pid`）
2. **权限设置**：确保文件权限正确（644），目录权限正确（755）
3. **监控检查**：定期检查PID文件，验证进程是否运行
4. **故障处理**：及时清理过期的PID文件，避免启动失败

PID文件是NGINX进程管理的重要机制，它使得NGINX能够被外部工具和脚本管理和监控，是NGINX作为系统服务运行的基础。
