
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_event.h>
#include <ngx_channel.h>


static void ngx_start_worker_processes(ngx_cycle_t *cycle, ngx_int_t n,
    ngx_int_t type);
static void ngx_start_cache_manager_processes(ngx_cycle_t *cycle,
    ngx_uint_t respawn);
static void ngx_pass_open_channel(ngx_cycle_t *cycle);
static void ngx_signal_worker_processes(ngx_cycle_t *cycle, int signo);
static ngx_uint_t ngx_reap_children(ngx_cycle_t *cycle);
static void ngx_master_process_exit(ngx_cycle_t *cycle);
static void ngx_worker_process_cycle(ngx_cycle_t *cycle, void *data);
static void ngx_worker_process_init(ngx_cycle_t *cycle, ngx_int_t worker);
static void ngx_worker_process_exit(ngx_cycle_t *cycle);
static void ngx_channel_handler(ngx_event_t *ev);
static void ngx_cache_manager_process_cycle(ngx_cycle_t *cycle, void *data);
static void ngx_cache_manager_process_handler(ngx_event_t *ev);
static void ngx_cache_loader_process_handler(ngx_event_t *ev);


ngx_uint_t    ngx_process;
ngx_uint_t    ngx_worker;
ngx_pid_t     ngx_pid;
ngx_pid_t     ngx_parent;

sig_atomic_t  ngx_reap;
sig_atomic_t  ngx_sigio;
sig_atomic_t  ngx_sigalrm;
sig_atomic_t  ngx_terminate;
sig_atomic_t  ngx_quit;
sig_atomic_t  ngx_debug_quit;
ngx_uint_t    ngx_exiting;
sig_atomic_t  ngx_reconfigure;
sig_atomic_t  ngx_reopen;

sig_atomic_t  ngx_change_binary;
ngx_pid_t     ngx_new_binary;
ngx_uint_t    ngx_inherited;
ngx_uint_t    ngx_daemonized;

sig_atomic_t  ngx_noaccept;
ngx_uint_t    ngx_noaccepting;
ngx_uint_t    ngx_restart;


static u_char  master_process[] = "master process";


static ngx_cache_manager_ctx_t  ngx_cache_manager_ctx = {
    ngx_cache_manager_process_handler, "cache manager process", 0
};

static ngx_cache_manager_ctx_t  ngx_cache_loader_ctx = {
    ngx_cache_loader_process_handler, "cache loader process", 60000
};


static ngx_cycle_t      ngx_exit_cycle;
static ngx_log_t        ngx_exit_log;
static ngx_open_file_t  ngx_exit_log_file;


/**
 * 函数功能：Master进程主循环，管理Worker进程和处理信号
 * 
 * 参数说明：
 *   @param cycle: 当前cycle对象，包含所有配置和运行时信息
 * 
 * 函数作用：
 *   这是NGINX Master进程的核心函数，负责：
 *   1. 设置信号处理：阻塞所有需要处理的信号，等待信号到来
 *   2. 设置进程标题：设置进程名称，便于系统监控和调试
 *   3. 启动Worker进程：根据配置启动指定数量的Worker进程
 *   4. 启动Cache Manager进程：启动缓存管理进程（如果配置了缓存）
 *   5. 进入主循环：处理各种信号和事件
 *      - SIGCHLD: Worker进程退出信号，回收子进程
 *      - SIGHUP: 配置重载信号，重新加载配置文件
 *      - SIGQUIT: 优雅关闭信号，等待请求处理完成后退出
 *      - SIGTERM: 快速终止信号，立即终止所有进程
 *      - SIGUSR1: 重新打开日志文件信号
 *      - SIGUSR2: 热升级信号，执行热升级
 *   6. 管理Worker进程生命周期：
 *      - 监控Worker进程状态
 *      - 自动重启崩溃的Worker进程
 *      - 处理Worker进程退出
 *   7. 处理配置重载：重新解析配置文件，启动新Worker进程，优雅关闭旧进程
 *   8. 处理进程终止：优雅关闭或强制终止所有Worker进程
 * 
 * 工作流程：
 *   1. 初始化阶段：
 *      - 设置信号掩码，阻塞所有需要处理的信号
 *      - 设置进程标题
 *      - 启动Worker进程和Cache Manager进程
 *   2. 主循环阶段：
 *      - 使用sigsuspend()等待信号到来
 *      - 处理SIGCHLD信号，回收子进程
 *      - 处理各种管理信号（重载、关闭、重开日志等）
 *      - 监控Worker进程状态，自动重启崩溃的进程
 *   3. 退出阶段：
 *      - 发送信号给所有Worker进程
 *      - 等待所有Worker进程退出
 *      - 清理资源，删除PID文件
 *      - 退出进程
 * 
 * 信号处理说明：
 *   - SIGCHLD: Worker进程退出时发送，Master进程回收子进程并可能重启
 *   - SIGHUP: 配置重载信号，重新加载配置文件（nginx -s reload）
 *   - SIGQUIT: 优雅关闭信号，等待请求处理完成后退出（nginx -s quit）
 *   - SIGTERM: 快速终止信号，立即终止所有进程（nginx -s stop）
 *   - SIGUSR1: 重新打开日志文件信号（nginx -s reopen）
 *   - SIGUSR2: 热升级信号，执行热升级（需要特殊处理）
 * 
 * 进程管理说明：
 *   - Worker进程：处理客户端请求，执行事件循环
 *   - Cache Manager进程：管理缓存，清理过期缓存
 *   - Cache Loader进程：加载缓存到内存
 *   - Master进程：管理所有子进程，处理信号，监控进程状态
 * 
 * 自动重启机制：
 *   - 如果Worker进程异常退出，Master进程会自动重启它
 *   - 重启延迟：首次重启延迟50ms，每次重启延迟翻倍，最大1000ms
 *   - 如果Worker进程在1秒内无法正常退出，Master进程会发送SIGKILL强制终止
 * 
 * 配置重载机制：
 *   1. 收到SIGHUP信号，设置ngx_reconfigure = 1
 *   2. 调用ngx_init_cycle()重新解析配置文件
 *   3. 启动新的Worker进程（使用新配置）
 *   4. 等待新Worker进程启动（100ms）
 *   5. 向旧Worker进程发送SIGQUIT信号，优雅关闭
 *   6. 旧Worker进程处理完现有请求后退出
 *   7. 新Worker进程继续处理新请求
 *   8. 实现零停机时间配置重载
 */
void
ngx_master_process_cycle(ngx_cycle_t *cycle)
{
    /* 局部变量说明：
     * 
     * title (char *):
     *   作用：存储进程标题字符串
     *   用途：设置进程名称，便于系统监控和调试
     *   格式：master process /usr/sbin/nginx -c /etc/nginx/nginx.conf
     *   使用：通过ngx_setproctitle()设置进程标题
     *   生命周期：在函数开始时分配，设置后不再使用
     * 
     * p (u_char *):
     *   作用：指针，用于构建进程标题字符串
     *   用途：在构建进程标题时，指向当前写入位置
     *   使用：通过ngx_cpymem()和ngx_cpystrn()复制字符串
     *   生命周期：在构建进程标题时使用，设置后不再使用
     * 
     * size (size_t):
     *   作用：进程标题字符串的大小（字节数）
     *   用途：计算进程标题所需的内存大小
     *   计算：master_process长度 + 所有命令行参数长度 + 空格
     *   使用：通过ngx_pnalloc()分配内存
     *   生命周期：在构建进程标题时使用，分配内存后不再使用
     * 
     * i (ngx_int_t):
     *   作用：循环计数器
     *   用途：遍历命令行参数数组（ngx_argv）
     *   使用：在for循环中遍历所有命令行参数
     *   生命周期：在构建进程标题时使用，设置后不再使用
     * 
     * sigio (ngx_uint_t):
     *   作用：信号I/O计数器，用于进程终止超时控制
     *   用途：控制进程终止的超时机制
     *   初始值：0
     *   使用场景：
     *     - 进程终止时，设置为Worker进程数 + Cache进程数（2个）
     *     - 每次循环递减，直到归零
     *     - 归零后，发送终止信号给Worker进程
     *   工作原理：
     *     1. 收到SIGTERM信号，设置sigio = worker_processes + 2
     *     2. 每次循环检查sigio，如果非零则递减
     *     3. 当sigio归零时，发送SIGTERM信号给Worker进程
     *     4. 如果进程在延迟时间内未退出，延迟翻倍，重新设置sigio
     *   生命周期：在整个主循环中使用，用于控制进程终止流程
     * 
     * set (sigset_t):
     *   作用：信号集合，用于信号处理和等待
     *   用途：
     *     1. 初始化阶段：阻塞所有需要处理的信号
     *     2. 主循环阶段：用于sigsuspend()等待信号
     *   信号类型：
     *     - SIGCHLD: Worker进程退出信号
     *     - SIGALRM: 定时器信号（进程终止超时）
     *     - SIGIO: I/O就绪信号（通常不使用）
     *     - SIGINT: 中断信号（Ctrl+C）
     *     - SIGHUP: 配置重载信号
     *     - SIGUSR1: 日志重开信号
     *     - SIGUSR2: 热升级信号
     *     - SIGTERM: 快速终止信号
     *     - SIGQUIT: 优雅关闭信号
     *   使用流程：
     *     1. sigemptyset()清空信号集合
     *     2. sigaddset()添加需要处理的信号
     *     3. sigprocmask()阻塞信号
     *     4. sigemptyset()清空信号集合（用于sigsuspend）
     *     5. sigsuspend()等待信号到来
     *   生命周期：在整个函数生命周期中使用
     * 
     * itv (struct itimerval):
     *   作用：定时器结构，用于设置进程终止延迟定时器
     *   用途：控制进程终止的延迟时间
     *   结构说明：
     *     - it_interval: 定时器间隔（秒和微秒）
     *     - it_value: 定时器初始值（秒和微秒）
     *   使用场景：
     *     - 进程终止时，设置延迟定时器
     *     - 延迟时间从50ms开始，每次翻倍（50ms → 100ms → 200ms → ...）
     *     - 如果延迟超过1000ms，发送SIGKILL强制终止
     *   工作原理：
     *     1. 计算延迟时间（delay毫秒）
     *     2. 设置itv.it_value为延迟时间
     *     3. 调用setitimer()设置定时器
     *     4. 定时器到期后，发送SIGALRM信号
     *     5. 收到SIGALRM信号，延迟翻倍，重新设置定时器
     *   生命周期：在进程终止流程中使用
     * 
     * live (ngx_uint_t):
     *   作用：标志位，表示是否还有存活的Worker进程
     *   用途：判断Master进程是否可以退出
     *   初始值：1（表示有存活的进程）
     *   更新时机：
     *     - 初始化为1（启动Worker进程后）
     *     - 调用ngx_reap_children()后更新（回收子进程时）
     *     - 配置重载时设置为1（启动新Worker进程后）
     *     - 进程重启时设置为1（启动新Worker进程后）
     *   退出条件：
     *     - live = 0 且收到终止信号（ngx_terminate或ngx_quit）
     *     - 调用ngx_master_process_exit()退出
     *   生命周期：在整个主循环中使用，用于判断退出条件
     * 
     * delay (ngx_msec_t):
     *   作用：延迟时间（毫秒），用于进程终止延迟
     *   用途：控制进程终止的延迟时间
     *   初始值：0（表示未设置延迟）
     *   使用场景：
     *     - 进程终止时，设置延迟时间
     *     - 延迟时间从50ms开始，每次翻倍
     *     - 如果延迟超过1000ms，发送SIGKILL强制终止
     *   延迟机制：
     *     1. 首次延迟：50ms
     *     2. 延迟递增：每次延迟翻倍（50ms → 100ms → 200ms → 400ms → 800ms → 1600ms）
     *     3. 最大延迟：1000ms（超过后发送SIGKILL）
     *   工作原理：
     *     1. 收到SIGTERM信号，设置delay = 50
     *     2. 设置定时器，等待delay毫秒
     *     3. 定时器到期，收到SIGALRM信号
     *     4. 延迟翻倍，重新设置定时器
     *     5. 如果延迟超过1000ms，发送SIGKILL强制终止
     *   生命周期：在进程终止流程中使用
     * 
     * ccf (ngx_core_conf_t *):
     *   作用：核心模块配置指针，用于访问配置信息
     *   用途：获取核心模块的配置信息
     *   配置信息：
     *     - worker_processes: Worker进程数量
     *     - user: 运行用户
     *     - group: 运行用户组
     *     - pid: PID文件路径
     *     - 其他核心配置项
     *   使用场景：
     *     - 启动Worker进程：ccf->worker_processes
     *     - 日志重开：ccf->user
     *     - 配置重载：重新获取配置信息
     *   获取方式：
     *     - ngx_get_conf(cycle->conf_ctx, ngx_core_module)
     *   生命周期：在整个函数生命周期中使用
     */
    char              *title;
    u_char            *p;
    size_t             size;
    ngx_int_t          i;
    ngx_uint_t         sigio;
    sigset_t           set;
    struct itimerval   itv;
    ngx_uint_t         live;
    ngx_msec_t         delay;
    ngx_core_conf_t   *ccf;

    /* 1. 初始化信号集合：准备阻塞所有需要处理的信号
     * 
     * 信号说明：
     *   - SIGCHLD: Worker进程退出信号，Master进程需要回收子进程
     *   - SIGALRM: 定时器信号，用于进程终止超时
     *   - SIGIO: I/O就绪信号（通常不使用）
     *   - SIGINT: 中断信号（Ctrl+C），通常被SIGTERM替代
     *   - SIGHUP: 配置重载信号（nginx -s reload）
     *   - SIGUSR1: 重新打开日志文件信号（nginx -s reopen）
     *   - SIGUSR2: 热升级信号（需要特殊处理）
     *   - SIGTERM: 快速终止信号（nginx -s stop）
     *   - SIGQUIT: 优雅关闭信号（nginx -s quit）
     */
    sigemptyset(&set);
    sigaddset(&set, SIGCHLD);
    sigaddset(&set, SIGALRM);
    sigaddset(&set, SIGIO);
    sigaddset(&set, SIGINT);
    sigaddset(&set, ngx_signal_value(NGX_RECONFIGURE_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_REOPEN_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_NOACCEPT_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_TERMINATE_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
    sigaddset(&set, ngx_signal_value(NGX_CHANGEBIN_SIGNAL));

    /* 2. 阻塞信号：防止信号处理函数在关键代码段执行时被调用
     * 
     * 为什么需要阻塞信号：
     *   - 防止信号处理函数在关键代码段执行时被调用
     *   - 确保信号处理的原子性
     *   - 避免竞争条件
     * 
     * 信号处理流程：
     *   1. 信号到来时，信号处理函数设置全局标志位（如ngx_reap = 1）
     *   2. Master进程在主循环中检查标志位
     *   3. 根据标志位执行相应的操作
     */
    if (sigprocmask(SIG_BLOCK, &set, NULL) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "sigprocmask() failed");
    }

    /* 3. 清空信号集合：准备用于sigsuspend()等待信号
     * 
     * sigsuspend()说明：
     *   - 临时替换信号掩码，然后挂起进程等待信号
     *   - 收到信号后，恢复原来的信号掩码，然后返回
     *   - 这是一个原子操作，避免竞争条件
     */
    sigemptyset(&set);


    /* 4. 设置进程标题：设置进程名称，便于系统监控和调试
     * 
     * 进程标题格式：master process /usr/sbin/nginx -c /etc/nginx/nginx.conf
     * 
     * 作用：
     *   - 便于系统监控工具识别进程
     *   - 便于调试和故障排查
     *   - 使用ps命令可以看到进程的完整命令
     */
    size = sizeof(master_process);

    for (i = 0; i < ngx_argc; i++) {
        size += ngx_strlen(ngx_argv[i]) + 1;
    }

    title = ngx_pnalloc(cycle->pool, size);
    if (title == NULL) {
        /* fatal */
        exit(2);
    }

    p = ngx_cpymem(title, master_process, sizeof(master_process) - 1);
    for (i = 0; i < ngx_argc; i++) {
        *p++ = ' ';
        p = ngx_cpystrn(p, (u_char *) ngx_argv[i], size);
    }

    ngx_setproctitle(title);

    /* 5. 获取核心模块配置：获取Worker进程数量等配置信息 */
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    /* 6. 启动Worker进程：根据配置启动指定数量的Worker进程
     * 
     * Worker进程作用：
     *   - 处理客户端请求
     *   - 执行事件循环
     *   - 处理HTTP请求、Stream连接等
     * 
     * 进程类型说明：
     *   - NGX_PROCESS_RESPAWN: 如果进程退出，自动重启
     *   - NGX_PROCESS_JUST_RESPAWN: 刚刚启动的进程（配置重载时使用）
     *   - NGX_PROCESS_DETACHED:  detached进程（不自动重启）
     */
    ngx_start_worker_processes(cycle, ccf->worker_processes,
                               NGX_PROCESS_RESPAWN);
    
    /* 7. 启动Cache Manager进程：启动缓存管理进程（如果配置了缓存）
     * 
     * Cache Manager进程作用：
     *   - 管理缓存，清理过期缓存
     *   - 定期检查缓存目录，删除过期文件
     *   - 只在配置了缓存路径时启动
     */
    ngx_start_cache_manager_processes(cycle, 0);

    /* 8. 初始化状态变量：
     *   - ngx_new_binary: 热升级时的新进程PID
     *   - delay: 进程终止延迟（毫秒）
     *   - sigio: 信号I/O计数器（用于进程终止超时）
     *   - live: 是否还有存活的Worker进程
     */
    ngx_new_binary = 0;
    delay = 0;
    sigio = 0;
    live = 1;

    /* 9. 主循环：处理各种信号和事件
     * 
     * 主循环流程：
     *   1. 如果设置了延迟，设置定时器等待进程退出
     *   2. 使用sigsuspend()挂起进程，等待信号到来
     *   3. 信号到来后，更新时间，检查各种标志位
     *   4. 根据标志位执行相应的操作
     *   5. 继续循环，等待下一个信号
     */
    for ( ;; ) {
        /* 9.1 进程终止延迟处理：如果设置了延迟，使用定时器等待进程退出
         * 
         * 延迟机制说明：
         *   - 首次延迟：50ms
         *   - 延迟递增：每次延迟翻倍（50ms → 100ms → 200ms → ...）
         *   - 最大延迟：1000ms
         *   - 如果超过1000ms，发送SIGKILL强制终止
         * 
         * 延迟目的：
         *   - 给Worker进程时间优雅退出
         *   - 避免立即强制终止，导致请求中断
         *   - 如果Worker进程无法正常退出，逐步增加延迟，最终强制终止
         */
        if (delay) {
            if (ngx_sigalrm) {
                sigio = 0;
                delay *= 2;  /* 延迟翻倍 */
                ngx_sigalrm = 0;
            }

            ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                           "termination cycle: %M", delay);

            /* 设置定时器：在delay毫秒后发送SIGALRM信号 */
            itv.it_interval.tv_sec = 0;
            itv.it_interval.tv_usec = 0;
            itv.it_value.tv_sec = delay / 1000;
            itv.it_value.tv_usec = (delay % 1000 ) * 1000;

            if (setitimer(ITIMER_REAL, &itv, NULL) == -1) {
                ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                              "setitimer() failed");
            }
        }

        /* 9.2 等待信号：使用sigsuspend()挂起进程，等待信号到来
         * 
         * sigsuspend()说明：
         *   - 临时替换信号掩码为空的集合（不阻塞任何信号）
         *   - 挂起进程，等待信号到来
         *   - 收到信号后，恢复原来的信号掩码，然后返回
         *   - 这是一个原子操作，避免竞争条件
         * 
         * 为什么使用sigsuspend()：
         *   - 避免忙等待（busy waiting）
         *   - 原子操作，避免竞争条件
         *   - 高效等待信号
         */
        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "sigsuspend");

        sigsuspend(&set);

        /* 9.3 更新时间：信号到来后，更新系统时间 */
        ngx_time_update();

        ngx_log_debug1(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "wake up, sigio %i", sigio);

        /* 9.4 处理SIGCHLD信号：回收退出的子进程
         * 
         * SIGCHLD信号处理：
         *   - Worker进程退出时，发送SIGCHLD信号给Master进程
         *   - 信号处理函数设置ngx_reap = 1
         *   - Master进程检查ngx_reap标志，调用ngx_reap_children()回收子进程
         *   - 如果Worker进程配置了自动重启，Master进程会重新启动它
         * 
         * ngx_reap_children()功能：
         *   - 回收退出的子进程（调用waitpid()）
         *   - 检查进程退出状态
         *   - 如果进程配置了自动重启，重新启动进程
         *   - 返回是否还有存活的Worker进程
         */
        if (ngx_reap) {
            ngx_reap = 0;
            ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "reap children");

            live = ngx_reap_children(cycle);
        }

        /* 9.5 检查退出条件：如果没有存活的Worker进程且收到终止信号，退出
         * 
         * 退出条件：
         *   - 没有存活的Worker进程（live = 0）
         *   - 收到终止信号（ngx_terminate或ngx_quit）
         * 
         * 退出流程：
         *   - 删除PID文件
         *   - 调用模块的exit_master钩子
         *   - 关闭监听socket
         *   - 销毁内存池
         *   - 退出进程
         */
        if (!live && (ngx_terminate || ngx_quit)) {
            ngx_master_process_exit(cycle);
        }

        /* 9.6 处理快速终止信号（SIGTERM）：立即终止所有Worker进程
         * 
         * 快速终止流程：
         *   1. 设置延迟（首次50ms）
         *   2. 等待sigio计数器归零（给进程时间退出）
         *   3. 如果延迟超过1000ms，发送SIGKILL强制终止
         *   4. 否则发送SIGTERM信号
         *   5. 延迟翻倍，继续等待
         * 
         * 使用场景：
         *   - nginx -s stop命令
         *   - systemctl stop nginx命令
         *   - kill -TERM <pid>命令
         */
        if (ngx_terminate) {
            if (delay == 0) {
                delay = 50;  /* 首次延迟50ms */
            }

            /* 等待sigio计数器归零：给进程时间退出 */
            if (sigio) {
                sigio--;
                continue;
            }

            /* 设置sigio计数器：Worker进程数 + Cache进程数（2个） */
            sigio = ccf->worker_processes + 2 /* cache processes */;

            /* 如果延迟超过1000ms，发送SIGKILL强制终止 */
            if (delay > 1000) {
                ngx_signal_worker_processes(cycle, SIGKILL);
            } else {
                /* 发送SIGTERM信号：请求进程终止 */
                ngx_signal_worker_processes(cycle,
                                       ngx_signal_value(NGX_TERMINATE_SIGNAL));
            }

            continue;
        }

        /* 9.7 处理优雅关闭信号（SIGQUIT）：等待请求处理完成后退出
         * 
         * 优雅关闭流程：
         *   1. 向所有Worker进程发送SIGQUIT信号
         *   2. 关闭监听socket（不再接受新连接）
         *   3. Worker进程停止接受新连接，等待现有请求处理完成
         *   4. Worker进程处理完所有请求后退出
         *   5. Master进程等待所有Worker进程退出后退出
         * 
         * 使用场景：
         *   - nginx -s quit命令
         *   - kill -QUIT <pid>命令
         * 
         * 与快速终止的区别：
         *   - 优雅关闭：等待请求处理完成，零中断
         *   - 快速终止：立即终止，可能中断请求
         */
        if (ngx_quit) {
            ngx_signal_worker_processes(cycle,
                                        ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
            ngx_close_listening_sockets(cycle);

            continue;
        }

        /* 9.8 处理配置重载信号（SIGHUP）：重新加载配置文件
         * 
         * 配置重载流程（零停机时间）：
         *   1. 收到SIGHUP信号，设置ngx_reconfigure = 1
         *   2. 调用ngx_init_cycle()重新解析配置文件
         *   3. 如果配置有效，启动新的Worker进程（使用新配置）
         *   4. 等待新Worker进程启动（100ms）
         *   5. 向旧Worker进程发送SIGQUIT信号，优雅关闭
         *   6. 旧Worker进程停止接受新连接，等待现有请求处理完成
         *   7. 旧Worker进程处理完所有请求后退出
         *   8. 新Worker进程继续处理新请求
         *   9. 实现零停机时间配置重载
         * 
         * 使用场景：
         *   - nginx -s reload命令
         *   - kill -HUP <pid>命令
         *   - systemctl reload nginx命令
         * 
         * 热升级处理：
         *   - 如果ngx_new_binary非零，说明正在进行热升级
         *   - 启动新的Worker进程，使用新二进制文件
         *   - 旧Worker进程继续运行，直到新进程稳定
         */
        if (ngx_reconfigure) {
            ngx_reconfigure = 0;

            /* 热升级场景：启动新进程，使用新二进制文件 */
            if (ngx_new_binary) {
                ngx_start_worker_processes(cycle, ccf->worker_processes,
                                           NGX_PROCESS_RESPAWN);
                ngx_start_cache_manager_processes(cycle, 0);
                ngx_noaccepting = 0;

                continue;
            }

            /* 配置重载：重新解析配置文件 */
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reconfiguring");

            cycle = ngx_init_cycle(cycle);
            if (cycle == NULL) {
                /* 配置无效，使用旧cycle */
                cycle = (ngx_cycle_t *) ngx_cycle;
                continue;
            }

            /* 更新全局cycle指针 */
            ngx_cycle = cycle;
            ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx,
                                                   ngx_core_module);
            
            /* 启动新的Worker进程（使用新配置） */
            ngx_start_worker_processes(cycle, ccf->worker_processes,
                                       NGX_PROCESS_JUST_RESPAWN);
            ngx_start_cache_manager_processes(cycle, 1);

            /* 等待新进程启动：给新进程时间初始化 */
            ngx_msleep(100);

            /* 标记有存活的进程 */
            live = 1;
            
            /* 向旧Worker进程发送SIGQUIT信号，优雅关闭 */
            ngx_signal_worker_processes(cycle,
                                        ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
        }

        /* 9.9 处理进程重启信号：重启所有Worker进程
         * 
         * 进程重启流程：
         *   1. 停止所有Worker进程
         *   2. 启动新的Worker进程
         *   3. 标记有存活的进程
         * 
         * 使用场景：
         *   - 内部使用，通常不对外暴露
         *   - 用于处理某些异常情况
         */
        if (ngx_restart) {
            ngx_restart = 0;
            ngx_start_worker_processes(cycle, ccf->worker_processes,
                                       NGX_PROCESS_RESPAWN);
            ngx_start_cache_manager_processes(cycle, 0);
            live = 1;
        }

        /* 9.10 处理日志重开信号（SIGUSR1）：重新打开日志文件
         * 
         * 日志重开流程：
         *   1. Master进程重新打开日志文件
         *   2. 向所有Worker进程发送SIGUSR1信号
         *   3. Worker进程重新打开日志文件
         *   4. 实现日志文件轮转（log rotation）
         * 
         * 使用场景：
         *   - nginx -s reopen命令
         *   - kill -USR1 <pid>命令
         *   - 日志轮转脚本（logrotate）
         * 
         * 日志轮转示例：
         *   1. 重命名日志文件：mv access.log access.log.1
         *   2. 发送SIGUSR1信号：kill -USR1 <pid>
         *   3. NGINX重新打开日志文件，创建新的access.log
         *   4. 旧的日志文件可以压缩或删除
         */
        if (ngx_reopen) {
            ngx_reopen = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reopening logs");
            ngx_reopen_files(cycle, ccf->user);
            ngx_signal_worker_processes(cycle,
                                        ngx_signal_value(NGX_REOPEN_SIGNAL));
        }

        /* 9.11 处理热升级信号（SIGUSR2）：执行热升级
         * 
         * 热升级流程：
         *   1. 收到SIGUSR2信号，设置ngx_change_binary = 1
         *   2. 调用ngx_exec_new_binary()执行新二进制文件
         *   3. 新进程继承旧进程的监听socket
         *   4. 新进程启动新的Worker进程
         *   5. 旧进程的Worker进程优雅退出
         *   6. 新进程的Worker进程继续处理请求
         *   7. 实现零停机时间热升级
         * 
         * 使用场景：
         *   - 升级NGINX版本
         *   - 更新NGINX二进制文件
         *   - 需要零停机时间升级
         * 
         * 热升级命令：
         *   - kill -USR2 <old_pid>
         *   - 新进程启动后，旧进程的PID文件重命名为.oldbin
         *   - 新进程创建新的PID文件
         */
        if (ngx_change_binary) {
            ngx_change_binary = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "changing binary");
            ngx_new_binary = ngx_exec_new_binary(cycle, ngx_argv);
        }

        /* 9.12 处理停止接受连接信号：停止接受新连接
         * 
         * 停止接受连接流程：
         *   1. 设置ngx_noaccepting = 1
         *   2. 向所有Worker进程发送SIGQUIT信号
         *   3. Worker进程停止接受新连接
         *   4. Worker进程处理完现有连接后退出
         * 
         * 使用场景：
         *   - 内部使用，通常不对外暴露
         *   - 用于处理某些异常情况
         *   - 配置重载时的中间状态
         */
        if (ngx_noaccept) {
            ngx_noaccept = 0;
            ngx_noaccepting = 1;
            ngx_signal_worker_processes(cycle,
                                        ngx_signal_value(NGX_SHUTDOWN_SIGNAL));
        }
    }
}


void
ngx_single_process_cycle(ngx_cycle_t *cycle)
{
    ngx_uint_t  i;

    if (ngx_set_environment(cycle, NULL) == NULL) {
        /* fatal */
        exit(2);
    }

    for (i = 0; cycle->modules[i]; i++) {
        if (cycle->modules[i]->init_process) {
            if (cycle->modules[i]->init_process(cycle) == NGX_ERROR) {
                /* fatal */
                exit(2);
            }
        }
    }

    for ( ;; ) {
        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "worker cycle");

        ngx_process_events_and_timers(cycle);

        if (ngx_terminate || ngx_quit) {

            for (i = 0; cycle->modules[i]; i++) {
                if (cycle->modules[i]->exit_process) {
                    cycle->modules[i]->exit_process(cycle);
                }
            }

            ngx_master_process_exit(cycle);
        }

        if (ngx_reconfigure) {
            ngx_reconfigure = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reconfiguring");

            cycle = ngx_init_cycle(cycle);
            if (cycle == NULL) {
                cycle = (ngx_cycle_t *) ngx_cycle;
                continue;
            }

            ngx_cycle = cycle;
        }

        if (ngx_reopen) {
            ngx_reopen = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reopening logs");
            ngx_reopen_files(cycle, (ngx_uid_t) -1);
        }
    }
}


static void
ngx_start_worker_processes(ngx_cycle_t *cycle, ngx_int_t n, ngx_int_t type)
{
    ngx_int_t  i;

    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "start worker processes");

    for (i = 0; i < n; i++) {

        ngx_spawn_process(cycle, ngx_worker_process_cycle,
                          (void *) (intptr_t) i, "worker process", type);

        ngx_pass_open_channel(cycle);
    }
}


static void
ngx_start_cache_manager_processes(ngx_cycle_t *cycle, ngx_uint_t respawn)
{
    ngx_uint_t    i, manager, loader;
    ngx_path_t  **path;

    manager = 0;
    loader = 0;

    path = ngx_cycle->paths.elts;
    for (i = 0; i < ngx_cycle->paths.nelts; i++) {

        if (path[i]->manager) {
            manager = 1;
        }

        if (path[i]->loader) {
            loader = 1;
        }
    }

    if (manager == 0) {
        return;
    }

    ngx_spawn_process(cycle, ngx_cache_manager_process_cycle,
                      &ngx_cache_manager_ctx, "cache manager process",
                      respawn ? NGX_PROCESS_JUST_RESPAWN : NGX_PROCESS_RESPAWN);

    ngx_pass_open_channel(cycle);

    if (loader == 0) {
        return;
    }

    ngx_spawn_process(cycle, ngx_cache_manager_process_cycle,
                      &ngx_cache_loader_ctx, "cache loader process",
                      respawn ? NGX_PROCESS_JUST_SPAWN : NGX_PROCESS_NORESPAWN);

    ngx_pass_open_channel(cycle);
}


static void
ngx_pass_open_channel(ngx_cycle_t *cycle)
{
    ngx_int_t      i;
    ngx_channel_t  ch;

    ngx_memzero(&ch, sizeof(ngx_channel_t));

    ch.command = NGX_CMD_OPEN_CHANNEL;
    ch.pid = ngx_processes[ngx_process_slot].pid;
    ch.slot = ngx_process_slot;
    ch.fd = ngx_processes[ngx_process_slot].channel[0];

    for (i = 0; i < ngx_last_process; i++) {

        if (i == ngx_process_slot
            || ngx_processes[i].pid == -1
            || ngx_processes[i].channel[0] == -1)
        {
            continue;
        }

        ngx_log_debug6(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                      "pass channel s:%i pid:%P fd:%d to s:%i pid:%P fd:%d",
                      ch.slot, ch.pid, ch.fd,
                      i, ngx_processes[i].pid,
                      ngx_processes[i].channel[0]);

        /* TODO: NGX_AGAIN */

        ngx_write_channel(ngx_processes[i].channel[0],
                          &ch, sizeof(ngx_channel_t), cycle->log);
    }
}


static void
ngx_signal_worker_processes(ngx_cycle_t *cycle, int signo)
{
    ngx_int_t      i;
    ngx_err_t      err;
    ngx_channel_t  ch;

    ngx_memzero(&ch, sizeof(ngx_channel_t));

#if (NGX_BROKEN_SCM_RIGHTS)

    ch.command = 0;

#else

    switch (signo) {

    case ngx_signal_value(NGX_SHUTDOWN_SIGNAL):
        ch.command = NGX_CMD_QUIT;
        break;

    case ngx_signal_value(NGX_TERMINATE_SIGNAL):
        ch.command = NGX_CMD_TERMINATE;
        break;

    case ngx_signal_value(NGX_REOPEN_SIGNAL):
        ch.command = NGX_CMD_REOPEN;
        break;

    default:
        ch.command = 0;
    }

#endif

    ch.fd = -1;


    for (i = 0; i < ngx_last_process; i++) {

        ngx_log_debug7(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "child: %i %P e:%d t:%d d:%d r:%d j:%d",
                       i,
                       ngx_processes[i].pid,
                       ngx_processes[i].exiting,
                       ngx_processes[i].exited,
                       ngx_processes[i].detached,
                       ngx_processes[i].respawn,
                       ngx_processes[i].just_spawn);

        if (ngx_processes[i].detached || ngx_processes[i].pid == -1) {
            continue;
        }

        if (ngx_processes[i].just_spawn) {
            ngx_processes[i].just_spawn = 0;
            continue;
        }

        if (ngx_processes[i].exiting
            && signo == ngx_signal_value(NGX_SHUTDOWN_SIGNAL))
        {
            continue;
        }

        if (ch.command) {
            if (ngx_write_channel(ngx_processes[i].channel[0],
                                  &ch, sizeof(ngx_channel_t), cycle->log)
                == NGX_OK)
            {
                if (signo != ngx_signal_value(NGX_REOPEN_SIGNAL)) {
                    ngx_processes[i].exiting = 1;
                }

                continue;
            }
        }

        ngx_log_debug2(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                       "kill (%P, %d)", ngx_processes[i].pid, signo);

        if (kill(ngx_processes[i].pid, signo) == -1) {
            err = ngx_errno;
            ngx_log_error(NGX_LOG_ALERT, cycle->log, err,
                          "kill(%P, %d) failed", ngx_processes[i].pid, signo);

            if (err == NGX_ESRCH) {
                ngx_processes[i].exited = 1;
                ngx_processes[i].exiting = 0;
                ngx_reap = 1;
            }

            continue;
        }

        if (signo != ngx_signal_value(NGX_REOPEN_SIGNAL)) {
            ngx_processes[i].exiting = 1;
        }
    }
}


static ngx_uint_t
ngx_reap_children(ngx_cycle_t *cycle)
{
    ngx_int_t         i, n;
    ngx_uint_t        live;
    ngx_channel_t     ch;
    ngx_core_conf_t  *ccf;

    ngx_memzero(&ch, sizeof(ngx_channel_t));

    ch.command = NGX_CMD_CLOSE_CHANNEL;
    ch.fd = -1;

    live = 0;
    for (i = 0; i < ngx_last_process; i++) {

        ngx_log_debug7(NGX_LOG_DEBUG_EVENT, cycle->log, 0,
                       "child: %i %P e:%d t:%d d:%d r:%d j:%d",
                       i,
                       ngx_processes[i].pid,
                       ngx_processes[i].exiting,
                       ngx_processes[i].exited,
                       ngx_processes[i].detached,
                       ngx_processes[i].respawn,
                       ngx_processes[i].just_spawn);

        if (ngx_processes[i].pid == -1) {
            continue;
        }

        if (ngx_processes[i].exited) {

            if (!ngx_processes[i].detached) {
                ngx_close_channel(ngx_processes[i].channel, cycle->log);

                ngx_processes[i].channel[0] = -1;
                ngx_processes[i].channel[1] = -1;

                ch.pid = ngx_processes[i].pid;
                ch.slot = i;

                for (n = 0; n < ngx_last_process; n++) {
                    if (ngx_processes[n].exited
                        || ngx_processes[n].pid == -1
                        || ngx_processes[n].channel[0] == -1)
                    {
                        continue;
                    }

                    ngx_log_debug3(NGX_LOG_DEBUG_CORE, cycle->log, 0,
                                   "pass close channel s:%i pid:%P to:%P",
                                   ch.slot, ch.pid, ngx_processes[n].pid);

                    /* TODO: NGX_AGAIN */

                    ngx_write_channel(ngx_processes[n].channel[0],
                                      &ch, sizeof(ngx_channel_t), cycle->log);
                }
            }

            if (ngx_processes[i].respawn
                && !ngx_processes[i].exiting
                && !ngx_terminate
                && !ngx_quit)
            {
                if (ngx_spawn_process(cycle, ngx_processes[i].proc,
                                      ngx_processes[i].data,
                                      ngx_processes[i].name, i)
                    == NGX_INVALID_PID)
                {
                    ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                                  "could not respawn %s",
                                  ngx_processes[i].name);
                    continue;
                }


                ngx_pass_open_channel(cycle);

                live = 1;

                continue;
            }

            if (ngx_processes[i].pid == ngx_new_binary) {

                ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx,
                                                       ngx_core_module);

                if (ngx_rename_file((char *) ccf->oldpid.data,
                                    (char *) ccf->pid.data)
                    == NGX_FILE_ERROR)
                {
                    ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                                  ngx_rename_file_n " %s back to %s failed "
                                  "after the new binary process \"%s\" exited",
                                  ccf->oldpid.data, ccf->pid.data, ngx_argv[0]);
                }

                ngx_new_binary = 0;
                if (ngx_noaccepting) {
                    ngx_restart = 1;
                    ngx_noaccepting = 0;
                }
            }

            if (i == ngx_last_process - 1) {
                ngx_last_process--;

            } else {
                ngx_processes[i].pid = -1;
            }

        } else if (ngx_processes[i].exiting || !ngx_processes[i].detached) {
            live = 1;
        }
    }

    return live;
}


static void
ngx_master_process_exit(ngx_cycle_t *cycle)
{
    ngx_uint_t  i;

    ngx_delete_pidfile(cycle);

    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "exit");

    for (i = 0; cycle->modules[i]; i++) {
        if (cycle->modules[i]->exit_master) {
            cycle->modules[i]->exit_master(cycle);
        }
    }

    ngx_close_listening_sockets(cycle);

    /*
     * Copy ngx_cycle->log related data to the special static exit cycle,
     * log, and log file structures enough to allow a signal handler to log.
     * The handler may be called when standard ngx_cycle->log allocated from
     * ngx_cycle->pool is already destroyed.
     */


    ngx_exit_log = *ngx_log_get_file_log(ngx_cycle->log);

    ngx_exit_log_file.fd = ngx_exit_log.file->fd;
    ngx_exit_log.file = &ngx_exit_log_file;
    ngx_exit_log.next = NULL;
    ngx_exit_log.writer = NULL;

    ngx_exit_cycle.log = &ngx_exit_log;
    ngx_exit_cycle.files = ngx_cycle->files;
    ngx_exit_cycle.files_n = ngx_cycle->files_n;
    ngx_cycle = &ngx_exit_cycle;

    ngx_destroy_pool(cycle->pool);

    exit(0);
}


static void
ngx_worker_process_cycle(ngx_cycle_t *cycle, void *data)
{
    ngx_int_t worker = (intptr_t) data;

    ngx_process = NGX_PROCESS_WORKER;
    ngx_worker = worker;

    ngx_worker_process_init(cycle, worker);

    ngx_setproctitle("worker process");

    for ( ;; ) {

        if (ngx_exiting) {
            if (ngx_event_no_timers_left() == NGX_OK) {
                ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "exiting");
                ngx_worker_process_exit(cycle);
            }
        }

        ngx_log_debug0(NGX_LOG_DEBUG_EVENT, cycle->log, 0, "worker cycle");

        ngx_process_events_and_timers(cycle);

        if (ngx_terminate) {
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "exiting");
            ngx_worker_process_exit(cycle);
        }

        if (ngx_quit) {
            ngx_quit = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                          "gracefully shutting down");
            ngx_setproctitle("worker process is shutting down");

            if (!ngx_exiting) {
                ngx_exiting = 1;
                ngx_set_shutdown_timer(cycle);
                ngx_close_listening_sockets(cycle);
                ngx_close_idle_connections(cycle);
                ngx_event_process_posted(cycle, &ngx_posted_events);
            }
        }

        if (ngx_reopen) {
            ngx_reopen = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reopening logs");
            ngx_reopen_files(cycle, -1);
        }
    }
}


static void
ngx_worker_process_init(ngx_cycle_t *cycle, ngx_int_t worker)
{
    sigset_t          set;
    ngx_int_t         n;
    ngx_time_t       *tp;
    ngx_uint_t        i;
    ngx_cpuset_t     *cpu_affinity;
    struct rlimit     rlmt;
    ngx_core_conf_t  *ccf;

    if (ngx_set_environment(cycle, NULL) == NULL) {
        /* fatal */
        exit(2);
    }

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    if (worker >= 0 && ccf->priority != 0) {
        if (setpriority(PRIO_PROCESS, 0, ccf->priority) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setpriority(%d) failed", ccf->priority);
        }
    }

    if (ccf->rlimit_nofile != NGX_CONF_UNSET) {
        rlmt.rlim_cur = (rlim_t) ccf->rlimit_nofile;
        rlmt.rlim_max = (rlim_t) ccf->rlimit_nofile;

        if (setrlimit(RLIMIT_NOFILE, &rlmt) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setrlimit(RLIMIT_NOFILE, %i) failed",
                          ccf->rlimit_nofile);
        }
    }

    if (ccf->rlimit_core != NGX_CONF_UNSET) {
        rlmt.rlim_cur = (rlim_t) ccf->rlimit_core;
        rlmt.rlim_max = (rlim_t) ccf->rlimit_core;

        if (setrlimit(RLIMIT_CORE, &rlmt) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "setrlimit(RLIMIT_CORE, %O) failed",
                          ccf->rlimit_core);
        }
    }

    if (geteuid() == 0) {
        if (setgid(ccf->group) == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "setgid(%d) failed", ccf->group);
            /* fatal */
            exit(2);
        }

        if (initgroups(ccf->username, ccf->group) == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "initgroups(%s, %d) failed",
                          ccf->username, ccf->group);
        }

#if (NGX_HAVE_PR_SET_KEEPCAPS && NGX_HAVE_CAPABILITIES)
        if (ccf->transparent && ccf->user) {
            if (prctl(PR_SET_KEEPCAPS, 1, 0, 0, 0) == -1) {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                              "prctl(PR_SET_KEEPCAPS, 1) failed");
                /* fatal */
                exit(2);
            }
        }
#endif

        if (setuid(ccf->user) == -1) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "setuid(%d) failed", ccf->user);
            /* fatal */
            exit(2);
        }

#if (NGX_HAVE_CAPABILITIES)
        if (ccf->transparent && ccf->user) {
            struct __user_cap_data_struct    data;
            struct __user_cap_header_struct  header;

            ngx_memzero(&header, sizeof(struct __user_cap_header_struct));
            ngx_memzero(&data, sizeof(struct __user_cap_data_struct));

            header.version = _LINUX_CAPABILITY_VERSION_1;
            data.effective = CAP_TO_MASK(CAP_NET_RAW);
            data.permitted = data.effective;

            if (syscall(SYS_capset, &header, &data) == -1) {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                              "capset() failed");
                /* fatal */
                exit(2);
            }
        }
#endif
    }

    if (worker >= 0) {
        cpu_affinity = ngx_get_cpu_affinity(worker);

        if (cpu_affinity) {
            ngx_setaffinity(cpu_affinity, cycle->log);
        }
    }

#if (NGX_HAVE_PR_SET_DUMPABLE)

    /* allow coredump after setuid() in Linux 2.4.x */

    if (prctl(PR_SET_DUMPABLE, 1, 0, 0, 0) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "prctl(PR_SET_DUMPABLE) failed");
    }

#endif

    if (ccf->working_directory.len) {
        if (chdir((char *) ccf->working_directory.data) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "chdir(\"%s\") failed", ccf->working_directory.data);
            /* fatal */
            exit(2);
        }
    }

    sigemptyset(&set);

    if (sigprocmask(SIG_SETMASK, &set, NULL) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "sigprocmask() failed");
    }

    tp = ngx_timeofday();
    srandom(((unsigned) ngx_pid << 16) ^ tp->sec ^ tp->msec);

    for (i = 0; cycle->modules[i]; i++) {
        if (cycle->modules[i]->init_process) {
            if (cycle->modules[i]->init_process(cycle) == NGX_ERROR) {
                /* fatal */
                exit(2);
            }
        }
    }

    for (n = 0; n < ngx_last_process; n++) {

        if (ngx_processes[n].pid == -1) {
            continue;
        }

        if (n == ngx_process_slot) {
            continue;
        }

        if (ngx_processes[n].channel[1] == -1) {
            continue;
        }

        if (close(ngx_processes[n].channel[1]) == -1) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          "close() channel failed");
        }
    }

    if (close(ngx_processes[ngx_process_slot].channel[0]) == -1) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      "close() channel failed");
    }

#if 0
    ngx_last_process = 0;
#endif

    if (ngx_add_channel_event(cycle, ngx_channel, NGX_READ_EVENT,
                              ngx_channel_handler)
        == NGX_ERROR)
    {
        /* fatal */
        exit(2);
    }
}


static void
ngx_worker_process_exit(ngx_cycle_t *cycle)
{
    ngx_uint_t         i;
    ngx_connection_t  *c;

    for (i = 0; cycle->modules[i]; i++) {
        if (cycle->modules[i]->exit_process) {
            cycle->modules[i]->exit_process(cycle);
        }
    }

    if (ngx_exiting && !ngx_terminate) {
        c = cycle->connections;
        for (i = 0; i < cycle->connection_n; i++) {
            if (c[i].fd != -1
                && c[i].read
                && !c[i].read->accept
                && !c[i].read->channel
                && !c[i].read->resolver)
            {
                ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                              "*%uA open socket #%d left in connection %ui",
                              c[i].number, c[i].fd, i);
                ngx_debug_quit = 1;
            }
        }
    }

    if (ngx_debug_quit) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, 0, "aborting");
        ngx_debug_point();
    }

    /*
     * Copy ngx_cycle->log related data to the special static exit cycle,
     * log, and log file structures enough to allow a signal handler to log.
     * The handler may be called when standard ngx_cycle->log allocated from
     * ngx_cycle->pool is already destroyed.
     */

    ngx_exit_log = *ngx_log_get_file_log(ngx_cycle->log);

    ngx_exit_log_file.fd = ngx_exit_log.file->fd;
    ngx_exit_log.file = &ngx_exit_log_file;
    ngx_exit_log.next = NULL;
    ngx_exit_log.writer = NULL;

    ngx_exit_cycle.log = &ngx_exit_log;
    ngx_exit_cycle.files = ngx_cycle->files;
    ngx_exit_cycle.files_n = ngx_cycle->files_n;
    ngx_cycle = &ngx_exit_cycle;

    ngx_destroy_pool(cycle->pool);

    ngx_log_error(NGX_LOG_NOTICE, ngx_cycle->log, 0, "exit");

    exit(0);
}


static void
ngx_channel_handler(ngx_event_t *ev)
{
    ngx_int_t          n;
    ngx_channel_t      ch;
    ngx_connection_t  *c;

    if (ev->timedout) {
        ev->timedout = 0;
        return;
    }

    c = ev->data;

    ngx_log_debug0(NGX_LOG_DEBUG_CORE, ev->log, 0, "channel handler");

    for ( ;; ) {

        n = ngx_read_channel(c->fd, &ch, sizeof(ngx_channel_t), ev->log);

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, ev->log, 0, "channel: %i", n);

        if (n == NGX_ERROR) {

            if (ngx_event_flags & NGX_USE_EPOLL_EVENT) {
                ngx_del_conn(c, 0);
            }

            ngx_close_connection(c);
            return;
        }

        if (ngx_event_flags & NGX_USE_EVENTPORT_EVENT) {
            if (ngx_add_event(ev, NGX_READ_EVENT, 0) == NGX_ERROR) {
                return;
            }
        }

        if (n == NGX_AGAIN) {
            return;
        }

        ngx_log_debug1(NGX_LOG_DEBUG_CORE, ev->log, 0,
                       "channel command: %ui", ch.command);

        switch (ch.command) {

        case NGX_CMD_QUIT:
            ngx_quit = 1;
            break;

        case NGX_CMD_TERMINATE:
            ngx_terminate = 1;
            break;

        case NGX_CMD_REOPEN:
            ngx_reopen = 1;
            break;

        case NGX_CMD_OPEN_CHANNEL:

            ngx_log_debug3(NGX_LOG_DEBUG_CORE, ev->log, 0,
                           "get channel s:%i pid:%P fd:%d",
                           ch.slot, ch.pid, ch.fd);

            ngx_processes[ch.slot].pid = ch.pid;
            ngx_processes[ch.slot].channel[0] = ch.fd;
            break;

        case NGX_CMD_CLOSE_CHANNEL:

            ngx_log_debug4(NGX_LOG_DEBUG_CORE, ev->log, 0,
                           "close channel s:%i pid:%P our:%P fd:%d",
                           ch.slot, ch.pid, ngx_processes[ch.slot].pid,
                           ngx_processes[ch.slot].channel[0]);

            if (close(ngx_processes[ch.slot].channel[0]) == -1) {
                ngx_log_error(NGX_LOG_ALERT, ev->log, ngx_errno,
                              "close() channel failed");
            }

            ngx_processes[ch.slot].channel[0] = -1;
            break;
        }
    }
}


static void
ngx_cache_manager_process_cycle(ngx_cycle_t *cycle, void *data)
{
    ngx_cache_manager_ctx_t *ctx = data;

    void         *ident[4];
    ngx_event_t   ev;

    /*
     * Set correct process type since closing listening Unix domain socket
     * in a master process also removes the Unix domain socket file.
     */
    ngx_process = NGX_PROCESS_HELPER;

    ngx_close_listening_sockets(cycle);

    /* Set a moderate number of connections for a helper process. */
    cycle->connection_n = 512;

    ngx_worker_process_init(cycle, -1);

    ngx_memzero(&ev, sizeof(ngx_event_t));
    ev.handler = ctx->handler;
    ev.data = ident;
    ev.log = cycle->log;
    ident[3] = (void *) -1;

    ngx_use_accept_mutex = 0;

    ngx_setproctitle(ctx->name);

    ngx_add_timer(&ev, ctx->delay);

    for ( ;; ) {

        if (ngx_terminate || ngx_quit) {
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "exiting");
            exit(0);
        }

        if (ngx_reopen) {
            ngx_reopen = 0;
            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0, "reopening logs");
            ngx_reopen_files(cycle, -1);
        }

        ngx_process_events_and_timers(cycle);
    }
}


static void
ngx_cache_manager_process_handler(ngx_event_t *ev)
{
    ngx_uint_t    i;
    ngx_msec_t    next, n;
    ngx_path_t  **path;

    next = 60 * 60 * 1000;

    path = ngx_cycle->paths.elts;
    for (i = 0; i < ngx_cycle->paths.nelts; i++) {

        if (path[i]->manager) {
            n = path[i]->manager(path[i]->data);

            next = (n <= next) ? n : next;

            ngx_time_update();
        }
    }

    if (next == 0) {
        next = 1;
    }

    ngx_add_timer(ev, next);
}


static void
ngx_cache_loader_process_handler(ngx_event_t *ev)
{
    ngx_uint_t     i;
    ngx_path_t   **path;
    ngx_cycle_t   *cycle;

    cycle = (ngx_cycle_t *) ngx_cycle;

    path = cycle->paths.elts;
    for (i = 0; i < cycle->paths.nelts; i++) {

        if (ngx_terminate || ngx_quit) {
            break;
        }

        if (path[i]->loader) {
            path[i]->loader(path[i]->data);
            ngx_time_update();
        }
    }

    exit(0);
}
