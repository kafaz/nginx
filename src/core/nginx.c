
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <nginx.h>


/* 显示版本信息的静态函数 */
static void ngx_show_version_info(void);

/* 添加继承的套接字到cycle中，用于热升级场景 */
static ngx_int_t ngx_add_inherited_sockets(ngx_cycle_t *cycle);

/* 清理环境变量的回调函数 */
static void ngx_cleanup_environment(void *data);

/* 清理单个环境变量的回调函数 */
static void ngx_cleanup_environment_variable(void *data);

/* 解析命令行参数的函数 */
static ngx_int_t ngx_get_options(int argc, char *const *argv);

/* 处理命令行选项并设置cycle的相关配置 */
static ngx_int_t ngx_process_options(ngx_cycle_t *cycle);

/* 保存命令行参数到全局变量中 */
static ngx_int_t ngx_save_argv(ngx_cycle_t *cycle, int argc, char *const *argv);

/* 创建核心模块配置结构体 */
static void *ngx_core_module_create_conf(ngx_cycle_t *cycle);

/* 初始化核心模块配置，设置默认值 */
static char *ngx_core_module_init_conf(ngx_cycle_t *cycle, void *conf);

/* 设置用户和用户组的配置指令处理函数 */
static char *ngx_set_user(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

/* 设置环境变量的配置指令处理函数 */
static char *ngx_set_env(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

/* 设置worker进程优先级的配置指令处理函数 */
static char *ngx_set_priority(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

/* 设置worker进程CPU亲和性的配置指令处理函数 */
static char *ngx_set_cpu_affinity(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

/* 设置worker进程数量的配置指令处理函数 */
static char *ngx_set_worker_processes(ngx_conf_t *cf, ngx_command_t *cmd,
    void *conf);

/* 动态加载模块的配置指令处理函数 */
static char *ngx_load_module(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);

#if (NGX_HAVE_DLOPEN)
/* 卸载动态加载的模块的回调函数 */
static void ngx_unload_module(void *data);
#endif


static ngx_conf_enum_t  ngx_debug_points[] = {
    { ngx_string("stop"), NGX_DEBUG_POINTS_STOP },
    { ngx_string("abort"), NGX_DEBUG_POINTS_ABORT },
    { ngx_null_string, 0 }
};


static ngx_command_t  ngx_core_commands[] = {

    { ngx_string("daemon"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      0,
      offsetof(ngx_core_conf_t, daemon),
      NULL },

    { ngx_string("master_process"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      0,
      offsetof(ngx_core_conf_t, master),
      NULL },

    { ngx_string("timer_resolution"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      0,
      offsetof(ngx_core_conf_t, timer_resolution),
      NULL },

    { ngx_string("pid"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0,
      offsetof(ngx_core_conf_t, pid),
      NULL },

    { ngx_string("lock_file"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0,
      offsetof(ngx_core_conf_t, lock_file),
      NULL },

    { ngx_string("worker_processes"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_set_worker_processes,
      0,
      0,
      NULL },

    { ngx_string("debug_points"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_enum_slot,
      0,
      offsetof(ngx_core_conf_t, debug_points),
      &ngx_debug_points },

    { ngx_string("user"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE12,
      ngx_set_user,
      0,
      0,
      NULL },

    { ngx_string("worker_priority"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_set_priority,
      0,
      0,
      NULL },

    { ngx_string("worker_cpu_affinity"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_1MORE,
      ngx_set_cpu_affinity,
      0,
      0,
      NULL },

    { ngx_string("worker_rlimit_nofile"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_num_slot,
      0,
      offsetof(ngx_core_conf_t, rlimit_nofile),
      NULL },

    { ngx_string("worker_rlimit_core"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_off_slot,
      0,
      offsetof(ngx_core_conf_t, rlimit_core),
      NULL },

    { ngx_string("worker_shutdown_timeout"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_msec_slot,
      0,
      offsetof(ngx_core_conf_t, shutdown_timeout),
      NULL },

    { ngx_string("working_directory"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      0,
      offsetof(ngx_core_conf_t, working_directory),
      NULL },

    { ngx_string("env"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_set_env,
      0,
      0,
      NULL },

    { ngx_string("load_module"),
      NGX_MAIN_CONF|NGX_DIRECT_CONF|NGX_CONF_TAKE1,
      ngx_load_module,
      0,
      0,
      NULL },

      ngx_null_command
};


static ngx_core_module_t  ngx_core_module_ctx = {
    ngx_string("core"),
    ngx_core_module_create_conf,
    ngx_core_module_init_conf
};


ngx_module_t  ngx_core_module = {
    NGX_MODULE_V1,
    &ngx_core_module_ctx,                  /* module context */
    ngx_core_commands,                     /* module directives */
    NGX_CORE_MODULE,                       /* module type */
    NULL,                                  /* init master */
    NULL,                                  /* init module */
    NULL,                                  /* init process */
    NULL,                                  /* init thread */
    NULL,                                  /* exit thread */
    NULL,                                  /* exit process */
    NULL,                                  /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_uint_t   ngx_show_help;
static ngx_uint_t   ngx_show_version;
static ngx_uint_t   ngx_show_configure;
static u_char      *ngx_prefix;
static u_char      *ngx_error_log;
static u_char      *ngx_conf_file;
static u_char      *ngx_conf_params;
static char        *ngx_signal;


static char **ngx_os_environ;


/*
 * nginx主函数 - 程序入口点
 * 负责初始化、配置解析、进程管理和启动工作循环
 * 
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @return 0表示成功，1表示失败
 */
int ngx_cdecl
main(int argc, char *const *argv)
{
    ngx_buf_t        *b;
    ngx_log_t        *log;
    ngx_uint_t        i;
    ngx_cycle_t      *cycle, init_cycle;
    ngx_conf_dump_t  *cd;
    ngx_core_conf_t  *ccf;

    /* 初始化调试相关功能 */
    ngx_debug_init();

    /* 初始化错误消息系统，用于后续的错误处理 */
    if (ngx_strerror_init() != NGX_OK) {
        return 1;
    }

    /* 解析命令行参数，设置全局标志位 */
    if (ngx_get_options(argc, argv) != NGX_OK) {
        return 1;
    }

    /* 如果需要显示版本信息 */
    if (ngx_show_version) {
        ngx_show_version_info();

        /* 如果不是测试配置模式，显示信息后退出 */
        if (!ngx_test_config) {
            return 0;
        }
    }

    /* TODO: 初始化最大套接字数，暂时设置为-1表示无限制 */
    ngx_max_sockets = -1;

    /* 初始化时间相关功能，包括缓存时间、时区等 */
    ngx_time_init();

#if (NGX_PCRE)
    /* 如果支持PCRE正则表达式，初始化正则表达式库 */
    ngx_regex_init();
#endif

    /* 获取当前进程ID和父进程ID */
    ngx_pid = ngx_getpid();
    ngx_parent = ngx_getppid();

    /* 初始化日志系统，创建日志对象 */
    log = ngx_log_init(ngx_prefix, ngx_error_log);
    if (log == NULL) {
        return 1;
    }

    /* STUB: OpenSSL初始化桩代码 */
#if (NGX_OPENSSL)
    ngx_ssl_init(log);
#endif

    /*
     * 初始化临时cycle结构体
     * init_cycle->log需要在信号处理函数和ngx_process_options()中使用
     */

    /* 将init_cycle结构体清零 */
    ngx_memzero(&init_cycle, sizeof(ngx_cycle_t));
    init_cycle.log = log;  /* 设置日志对象 */
    ngx_cycle = &init_cycle;  /* 设置全局cycle指针 */

    /* 创建内存池，用于管理init_cycle的内存分配 */
    init_cycle.pool = ngx_create_pool(1024, log);
    if (init_cycle.pool == NULL) {
        return 1;
    }

    /* 保存命令行参数到全局变量，用于后续使用 */
    if (ngx_save_argv(&init_cycle, argc, argv) != NGX_OK) {
        return 1;
    }

    /* 处理命令行选项，设置配置文件路径、前缀等 */
    if (ngx_process_options(&init_cycle) != NGX_OK) {
        return 1;
    }

    /* 初始化操作系统相关功能，包括内存页大小、CPU信息等 */
    if (ngx_os_init(log) != NGX_OK) {
        return 1;
    }

    /*
     * 初始化CRC32表
     * ngx_crc32_table_init()需要在ngx_os_init()之后调用，
     * 因为它需要ngx_cacheline_size变量
     */

    if (ngx_crc32_table_init() != NGX_OK) {
        return 1;
    }

    /*
     * 初始化slab内存分配器的大小
     * ngx_slab_sizes_init()需要在ngx_os_init()之后调用，
     * 因为它需要ngx_pagesize变量
     */

    ngx_slab_sizes_init();

    /* 添加从环境变量继承的套接字，用于热升级场景 */
    if (ngx_add_inherited_sockets(&init_cycle) != NGX_OK) {
        return 1;
    }

    /* 预初始化模块，设置模块索引和顺序 */
    if (ngx_preinit_modules() != NGX_OK) {
        return 1;
    }

    /* 初始化主cycle，解析配置文件，创建共享内存等 */
    cycle = ngx_init_cycle(&init_cycle);
    if (cycle == NULL) {
        /* 如果配置测试失败，输出错误信息 */
        if (ngx_test_config) {
            ngx_log_stderr(0, "configuration file %s test failed",
                           init_cycle.conf_file.data);
        }

        return 1;
    }

    /* 如果是配置测试模式 */
    if (ngx_test_config) {
        /* 如果不是安静模式，输出测试成功信息 */
        if (!ngx_quiet_mode) {
            ngx_log_stderr(0, "configuration file %s test is successful",
                           cycle->conf_file.data);
        }

        /* 如果需要转储配置 */
        if (ngx_dump_config) {
            cd = cycle->config_dump.elts;

            /* 遍历所有配置文件并输出其内容 */
            for (i = 0; i < cycle->config_dump.nelts; i++) {

                ngx_write_stdout("# configuration file ");
                (void) ngx_write_fd(ngx_stdout, cd[i].name.data,
                                    cd[i].name.len);
                ngx_write_stdout(":" NGX_LINEFEED);

                b = cd[i].buffer;

                (void) ngx_write_fd(ngx_stdout, b->pos, b->last - b->pos);
                ngx_write_stdout(NGX_LINEFEED);
            }
        }

        return 0;
    }

    /* 信号发送模式：如果有信号需要发送给master进程，则发送信号后退出
     * 
     * 使用场景：
     *   当使用命令行参数 -s signal 时，nginx不会启动新进程，而是向已经运行的
     *   master进程发送信号，然后立即退出。这是一种管理已运行nginx进程的方式。
     * 
     * 支持的信号：
     *   - stop: 快速停止（SIGTERM），立即终止所有进程
     *   - quit: 优雅关闭（SIGQUIT），等待请求处理完成后停止
     *   - reopen: 重新打开日志文件（SIGUSR1），用于日志轮转
     *   - reload: 重新加载配置（SIGHUP），重新读取配置文件并应用
     * 
     * 工作流程：
     *   1. 解析命令行参数 -s signal，设置ngx_signal变量（ngx_get_options函数）
     *   2. 解析配置文件，获取PID文件路径（ngx_init_cycle函数）
     *   3. 读取PID文件，获取master进程的PID
     *   4. 将信号名称转换为信号编号（如"stop" → SIGTERM）
     *   5. 使用kill()系统调用向master进程发送信号
     *   6. 函数返回，nginx进程退出
     * 
     * 示例用法：
     *   nginx -s stop      # 快速停止nginx
     *   nginx -s quit      # 优雅关闭nginx
     *   nginx -s reload    # 重新加载配置
     *   nginx -s reopen    # 重新打开日志文件
     * 
     * 注意：
     *   - 这个功能只在nginx未运行时使用，用于管理已经运行的nginx进程
     *   - 如果nginx未运行，读取PID文件会失败，函数返回错误
     *   - 发送信号后，当前nginx进程（信号发送进程）会立即退出
     *   - Master进程收到信号后，会根据信号类型执行相应操作
     */
    if (ngx_signal) {
        return ngx_signal_process(cycle, ngx_signal);
    }

    /* 输出操作系统状态信息 */
    ngx_os_status(cycle->log);

    /* 设置全局cycle指针为初始化后的cycle */
    ngx_cycle = cycle;

    /* 获取核心模块配置 */
    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    /* 进程模式确定：如果配置要求使用master进程且当前是单进程模式，则切换到master模式
     * 
     * 进程模式说明：
     *   - NGX_PROCESS_SINGLE: 单进程模式，当前进程直接处理所有请求（用于开发调试）
     *   - NGX_PROCESS_MASTER: Master-Worker模式，Master进程管理多个Worker进程（生产环境）
     * 
     * 分裂点说明：
     *   此时进程仍然是单个进程，真正的分裂发生在ngx_master_process_cycle()中
     *   通过fork()系统调用创建Worker进程
     */
    if (ccf->master && ngx_process == NGX_PROCESS_SINGLE) {
        ngx_process = NGX_PROCESS_MASTER;
    }

#if !(NGX_WIN32)

    /* 初始化信号处理 */
    if (ngx_init_signals(cycle->log) != NGX_OK) {
        return 1;
    }

    /* 如果不是继承模式且配置要求守护进程化 */
    if (!ngx_inherited && ccf->daemon) {
        if (ngx_daemon(cycle->log) != NGX_OK) {
            return 1;
        }

        ngx_daemonized = 1;  /* 标记已守护进程化 */
    }

    /* 如果是继承模式，也标记为守护进程化 */
    if (ngx_inherited) {
        ngx_daemonized = 1;
    }

#endif

    /* 创建PID文件：将Master进程的进程ID写入PID文件
     * 
     * PID文件的作用：
     *   1. 进程标识：存储Master进程的进程ID（PID），用于标识NGINX进程
     *   2. 进程管理：通过读取PID文件获取Master进程PID，用于发送信号管理进程
     *      - nginx -s stop/reload/quit/reopen 命令需要读取PID文件
     *      - 系统监控工具可以通过PID文件检查NGINX是否运行
     *   3. 进程检测：通过检查PID文件是否存在来判断NGINX是否运行
     *   4. 热升级支持：热升级时将PID文件重命名为.oldbin，新进程创建新的PID文件
     * 
     * PID文件内容：
     *   - 文件内容：Master进程的PID（整数）
     *   - 文件路径：默认 /var/run/nginx.pid（可通过pid指令配置）
     *   - 文件格式：纯文本，只包含PID数字和换行符
     * 
     * PID文件生命周期：
     *   1. 创建：在Master进程启动后创建（ngx_create_pidfile）
     *   2. 使用：信号发送时读取（ngx_signal_process）
     *   3. 删除：Master进程退出时删除（ngx_delete_pidfile）
     *   4. 热升级：热升级时重命名为.oldbin（ngx_rename_file）
     * 
     * 创建时机：
     *   - 在守护进程化（daemon）之后创建
     *   - 确保写入的是守护进程的PID，而不是父进程的PID
     *   - 只在Master进程或单进程模式下创建，Worker进程不创建
     * 
     * 使用示例：
     *   # 读取PID文件，获取Master进程PID
     *   cat /var/run/nginx.pid
     *   # 输出：12345
     * 
     *   # 使用PID文件发送信号
     *   kill -HUP $(cat /var/run/nginx.pid)  # 等价于 nginx -s reload
     * 
     * 错误处理：
     *   - 如果创建失败，函数返回错误，nginx启动失败
     *   - 如果PID文件已存在，会被覆盖（使用NGX_FILE_TRUNCATE模式）
     */
    if (ngx_create_pidfile(&ccf->pid, cycle->log) != NGX_OK) {
        return 1;
    }

    /* 重定向标准错误到日志文件 */
    if (ngx_log_redirect_stderr(cycle) != NGX_OK) {
        return 1;
    }

    /* 关闭内置日志文件描述符（如果它不是标准错误） */
    if (log->file->fd != ngx_stderr) {
        if (ngx_close_file(log->file->fd) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          ngx_close_file_n " built-in log failed");
        }
    }

    ngx_use_stderr = 0;  /* 标记不再使用标准错误 */

    /* 工作循环启动：根据进程模式启动相应的工作循环
     * 
     * 这是Master进程和Worker进程的分裂点：
     * 
     * 1. 单进程模式（NGX_PROCESS_SINGLE）：
     *    - 当前进程直接处理所有请求
     *    - 主要用于开发调试，便于调试和测试
     *    - 调用：ngx_single_process_cycle(cycle)
     * 
     * 2. Master-Worker模式（NGX_PROCESS_MASTER）：
     *    - Master进程：管理Worker进程，处理信号，监控Worker进程状态
     *    - Worker进程：处理客户端请求，执行事件循环
     *    - 调用：ngx_master_process_cycle(cycle)
     *       └─ ngx_start_worker_processes()  ← 创建Worker进程
     *          └─ ngx_spawn_process()  ← fork()系统调用
     *             └─ ngx_worker_process_cycle()  ← Worker进程执行
     * 
     * 分裂过程：
     *   1. Master进程调用ngx_master_process_cycle()进入Master进程循环
     *   2. Master进程调用ngx_start_worker_processes()创建Worker进程
     *   3. 通过fork()系统调用创建子进程（Worker进程）
     *   4. 子进程执行ngx_worker_process_cycle()进入Worker进程循环
     *   5. 父进程（Master进程）继续执行Master进程循环，管理Worker进程
     */
    if (ngx_process == NGX_PROCESS_SINGLE) {
        /* 单进程模式，直接处理请求 */
        ngx_single_process_cycle(cycle);

    } else {
        /* master进程模式，管理worker进程
         * 
         * 注意：这是Master进程和Worker进程的分裂点
         * - 当前进程成为Master进程
         * - Worker进程通过fork()在ngx_master_process_cycle()中创建
         * 
         * cycle参数说明：
         *   cycle是ngx_cycle_t类型的指针，是NGINX最核心的数据结构
         *   它包含了NGINX运行所需的所有信息，包括：
         * 
         *   1. 配置信息（conf_ctx）：
         *      - 存储所有模块的配置结构体
         *      - 通过ngx_get_conf()函数访问模块配置
         *      - 例如：ccf = ngx_get_conf(cycle->conf_ctx, ngx_core_module)
         * 
         *   2. 内存池（pool）：
         *      - 管理cycle生命周期内的内存分配
         *      - 默认大小为16KB（NGX_CYCLE_POOL_SIZE）
         *      - cycle销毁时，pool自动释放所有内存
         * 
         *   3. 日志对象（log）：
         *      - 全局日志对象，所有模块使用它记录日志
         *      - 支持多级日志（error、warn、info、debug）
         *      - 支持日志文件轮转
         * 
         *   4. 模块数组（modules）：
         *      - 存储所有已加载的模块
         *      - 包括核心模块、事件模块、HTTP模块等
         *      - 模块按类型和索引组织
         * 
         *   5. 连接和事件（connections、read_events、write_events）：
         *      - 连接池：预分配的连接对象数组
         *      - 读事件：每个连接的读事件对象
         *      - 写事件：每个连接的写事件对象
         *      - 用于高效处理大量并发连接
         * 
         *   6. 监听socket（listening）：
         *      - 存储所有监听的socket（HTTP、HTTPS、Stream等）
         *      - 每个listen指令对应一个ngx_listening_t对象
         *      - Worker进程从这些socket接受连接
         * 
         *   7. 共享内存（shared_memory）：
         *      - Worker进程间共享的内存区域
         *      - 用于存储限速计数器、会话信息、缓存元数据等
         *      - 通过ngx_shared_memory_add()添加共享内存区域
         * 
         *   8. 打开的文件（open_files）：
         *      - 跟踪所有打开的文件描述符
         *      - 用于日志文件轮转和文件描述符管理
         *      - Worker进程继承这些文件描述符
         * 
         *   9. 路径信息（paths）：
         *      - 存储所有路径配置（日志路径、临时文件路径等）
         *      - 用于文件管理和路径解析
         * 
         *   10. 旧cycle引用（old_cycle）：
         *       - 指向上一个cycle（配置重载时）
         *       - 用于平滑过渡和资源清理
         *       - 旧cycle会在新cycle稳定后销毁
         * 
         * cycle的生命周期：
         *   1. 创建：在ngx_init_cycle()中创建新的cycle
         *   2. 初始化：解析配置文件，创建监听socket，初始化共享内存
         *   3. 使用：Master和Worker进程都使用同一个cycle
         *   4. 重载：配置重载时创建新cycle，旧cycle保留一段时间
         *   5. 销毁：新cycle稳定后，旧cycle被销毁
         * 
         * cycle的共享方式：
         *   - Master进程和Worker进程通过fork()共享cycle
         *   - 共享内存区域通过mmap()在进程间共享
         *   - 配置信息在每个进程中都有独立的副本
         *   - 连接和事件对象在每个Worker进程中独立
         * 
         * cycle的作用：
         *   - 代表NGINX的一个完整的运行时周期（configuration cycle）
         *   - 包含NGINX运行所需的所有配置和资源
         *   - 是NGINX核心数据结构的容器
         *   - 支持配置热重载和平滑升级
         */
        ngx_master_process_cycle(cycle);
    }

    return 0;
}


static void
ngx_show_version_info(void)
{
    ngx_write_stderr("nginx version: " NGINX_VER_BUILD NGX_LINEFEED);

    if (ngx_show_help) {
        ngx_write_stderr(
            "Usage: nginx [-?hvVtTq] [-s signal] [-p prefix]" NGX_LINEFEED
            "             [-e filename] [-c filename] [-g directives]"
                          NGX_LINEFEED NGX_LINEFEED
            "Options:" NGX_LINEFEED
            "  -?,-h         : this help" NGX_LINEFEED
            "  -v            : show version and exit" NGX_LINEFEED
            "  -V            : show version and configure options then exit"
                               NGX_LINEFEED
            "  -t            : test configuration and exit" NGX_LINEFEED
            "  -T            : test configuration, dump it and exit"
                               NGX_LINEFEED
            "  -q            : suppress non-error messages "
                               "during configuration testing" NGX_LINEFEED
            "  -s signal     : send signal to a master process: "
                               "stop, quit, reopen, reload" NGX_LINEFEED
#ifdef NGX_PREFIX
            "  -p prefix     : set prefix path (default: " NGX_PREFIX ")"
                               NGX_LINEFEED
#else
            "  -p prefix     : set prefix path (default: NONE)" NGX_LINEFEED
#endif
            "  -e filename   : set error log file (default: "
#ifdef NGX_ERROR_LOG_STDERR
                               "stderr)" NGX_LINEFEED
#else
                               NGX_ERROR_LOG_PATH ")" NGX_LINEFEED
#endif
            "  -c filename   : set configuration file (default: " NGX_CONF_PATH
                               ")" NGX_LINEFEED
            "  -g directives : set global directives out of configuration "
                               "file" NGX_LINEFEED NGX_LINEFEED
        );
    }

    if (ngx_show_configure) {

#ifdef NGX_COMPILER
        ngx_write_stderr("built by " NGX_COMPILER NGX_LINEFEED);
#endif

#if (NGX_SSL)
        if (ngx_strcmp(ngx_ssl_version(), OPENSSL_VERSION_TEXT) == 0) {
            ngx_write_stderr("built with " OPENSSL_VERSION_TEXT NGX_LINEFEED);
        } else {
            ngx_write_stderr("built with " OPENSSL_VERSION_TEXT
                             " (running with ");
            ngx_write_stderr((char *) (uintptr_t) ngx_ssl_version());
            ngx_write_stderr(")" NGX_LINEFEED);
        }
#ifdef SSL_CTRL_SET_TLSEXT_HOSTNAME
        ngx_write_stderr("TLS SNI support enabled" NGX_LINEFEED);
#else
        ngx_write_stderr("TLS SNI support disabled" NGX_LINEFEED);
#endif
#endif

        ngx_write_stderr("configure arguments:" NGX_CONFIGURE NGX_LINEFEED);
    }
}


static ngx_int_t
ngx_add_inherited_sockets(ngx_cycle_t *cycle)
{
    u_char           *p, *v, *inherited;
    ngx_int_t         s;
    ngx_listening_t  *ls;

    inherited = (u_char *) getenv(NGINX_VAR);

    if (inherited == NULL) {
        return NGX_OK;
    }

    ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                  "using inherited sockets from \"%s\"", inherited);

    if (ngx_array_init(&cycle->listening, cycle->pool, 10,
                       sizeof(ngx_listening_t))
        != NGX_OK)
    {
        return NGX_ERROR;
    }

    for (p = inherited, v = p; *p; p++) {
        if (*p == ':' || *p == ';') {
            s = ngx_atoi(v, p - v);
            if (s == NGX_ERROR) {
                ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                              "invalid socket number \"%s\" in " NGINX_VAR
                              " environment variable, ignoring the rest"
                              " of the variable", v);
                break;
            }

            v = p + 1;

            ls = ngx_array_push(&cycle->listening);
            if (ls == NULL) {
                return NGX_ERROR;
            }

            ngx_memzero(ls, sizeof(ngx_listening_t));

            ls->fd = (ngx_socket_t) s;
            ls->inherited = 1;
        }
    }

    if (v != p) {
        ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                      "invalid socket number \"%s\" in " NGINX_VAR
                      " environment variable, ignoring", v);
    }

    ngx_inherited = 1;

    return ngx_set_inherited_sockets(cycle);
}


char **
ngx_set_environment(ngx_cycle_t *cycle, ngx_uint_t *last)
{
    char                **p, **env, *str;
    size_t                len;
    ngx_str_t            *var;
    ngx_uint_t            i, n;
    ngx_core_conf_t      *ccf;
    ngx_pool_cleanup_t   *cln;

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    if (last == NULL && ccf->environment) {
        return ccf->environment;
    }

    var = ccf->env.elts;

    for (i = 0; i < ccf->env.nelts; i++) {
        if (ngx_strcmp(var[i].data, "TZ") == 0
            || ngx_strncmp(var[i].data, "TZ=", 3) == 0)
        {
            goto tz_found;
        }
    }

    var = ngx_array_push(&ccf->env);
    if (var == NULL) {
        return NULL;
    }

    var->len = 2;
    var->data = (u_char *) "TZ";

    var = ccf->env.elts;

tz_found:

    n = 0;

    for (i = 0; i < ccf->env.nelts; i++) {

        if (var[i].data[var[i].len] == '=') {
            n++;
            continue;
        }

        for (p = ngx_os_environ; *p; p++) {

            if (ngx_strncmp(*p, var[i].data, var[i].len) == 0
                && (*p)[var[i].len] == '=')
            {
                n++;
                break;
            }
        }
    }

    if (last) {
        env = ngx_alloc((*last + n + 1) * sizeof(char *), cycle->log);
        if (env == NULL) {
            return NULL;
        }

        *last = n;

    } else {
        cln = ngx_pool_cleanup_add(cycle->pool, 0);
        if (cln == NULL) {
            return NULL;
        }

        env = ngx_alloc((n + 1) * sizeof(char *), cycle->log);
        if (env == NULL) {
            return NULL;
        }

        cln->handler = ngx_cleanup_environment;
        cln->data = env;
    }

    n = 0;

    for (i = 0; i < ccf->env.nelts; i++) {

        if (var[i].data[var[i].len] == '=') {

            if (last) {
                env[n++] = (char *) var[i].data;
                continue;
            }

            cln = ngx_pool_cleanup_add(cycle->pool, 0);
            if (cln == NULL) {
                return NULL;
            }

            len = ngx_strlen(var[i].data) + 1;

            str = ngx_alloc(len, cycle->log);
            if (str == NULL) {
                return NULL;
            }

            ngx_memcpy(str, var[i].data, len);

            cln->handler = ngx_cleanup_environment_variable;
            cln->data = str;

            env[n++] = str;

            continue;
        }

        for (p = ngx_os_environ; *p; p++) {

            if (ngx_strncmp(*p, var[i].data, var[i].len) == 0
                && (*p)[var[i].len] == '=')
            {
                env[n++] = *p;
                break;
            }
        }
    }

    env[n] = NULL;

    if (last == NULL) {
        ccf->environment = env;
        environ = env;
    }

    return env;
}


/*
 * 清理环境变量的回调函数
 * 在进程退出时释放环境变量数组内存
 *
 * @param data 环境变量数组指针
 */
static void
ngx_cleanup_environment(void *data)
{
    char  **env = data;

    /* 如果环境变量仍在被使用（如进程退出时），只能内存泄漏 */
    if (environ == env) {

        /*
         * if the environment is still used, as it happens on exit,
         * the only option is to leak it
         */

        return;
    }

    /* 释放环境变量数组内存 */
    ngx_free(env);
}


/*
 * 清理单个环境变量的回调函数
 * 在进程退出时释放单个环境变量字符串内存
 *
 * @param data 环境变量字符串指针
 */
static void
ngx_cleanup_environment_variable(void *data)
{
    char  *var = data;

    char  **p;

    /* 检查环境变量是否仍在被使用 */
    for (p = environ; *p; p++) {

        /*
         * if an environment variable is still used, as it happens on exit,
         * the only option is to leak it
         */

        /* 如果找到该变量仍在使用，直接返回避免释放 */
        if (*p == var) {
            return;
        }
    }

    /* 释放环境变量字符串内存 */
    ngx_free(var);
}


/*
 * 执行新的二进制文件（热升级）
 * 用于nginx热升级，启动新进程并传递监听套接字
 *
 * @param cycle 当前cycle对象
 * @param argv 新进程的命令行参数
 * @return 新进程的PID，NGX_INVALID_PID表示失败
 */
ngx_pid_t
ngx_exec_new_binary(ngx_cycle_t *cycle, char *const *argv)
{
    char             **env, *var;
    u_char            *p;
    ngx_uint_t         i, n;
    ngx_pid_t          pid;
    ngx_exec_ctx_t     ctx;
    ngx_core_conf_t   *ccf;
    ngx_listening_t   *ls;

    /* 清零执行上下文结构体 */
    ngx_memzero(&ctx, sizeof(ngx_exec_ctx_t));

    ctx.path = argv[0];
    ctx.name = "new binary process";
    ctx.argv = argv;

    n = 2;  /* 预留2个位置给NGINX_VAR和SPARE */
    /* 创建新进程的环境变量 */
    env = ngx_set_environment(cycle, &n);
    if (env == NULL) {
        return NGX_INVALID_PID;
    }

    /* 分配内存用于构建NGINX_VAR环境变量 */
    var = ngx_alloc(sizeof(NGINX_VAR)
                    + cycle->listening.nelts * (NGX_INT32_LEN + 1) + 2,
                    cycle->log);
    if (var == NULL) {
        ngx_free(env);
        return NGX_INVALID_PID;
    }

    /* 复制NGINX_VAR=到缓冲区 */
    p = ngx_cpymem(var, NGINX_VAR "=", sizeof(NGINX_VAR));

    /* 将所有监听套接字描述符添加到环境变量 */
    ls = cycle->listening.elts;
    for (i = 0; i < cycle->listening.nelts; i++) {
        if (ls[i].ignore) {
            continue;
        }
        p = ngx_sprintf(p, "%ud;", ls[i].fd);
    }

    *p = '\0';  /* 字符串结束符 */

    env[n++] = var;  /* 将NGINX_VAR添加到环境变量数组 */

#if (NGX_SETPROCTITLE_USES_ENV)

    /* 为新二进制进程标题分配300字节的备用空间 */
    env[n++] = "SPARE=XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX";

#endif

    env[n] = NULL;  /* 环境变量数组以NULL结尾 */

#if (NGX_DEBUG)
    {
    char  **e;
    /* 调试模式下输出所有环境变量 */
    for (e = env; *e; e++) {
        ngx_log_debug1(NGX_LOG_DEBUG_CORE, cycle->log, 0, "env: %s", *e);
    }
    }
#endif

    ctx.envp = (char *const *) env;

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    /* 重命名PID文件，为旧进程保留 */
    if (ngx_rename_file(ccf->pid.data, ccf->oldpid.data) == NGX_FILE_ERROR) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                      ngx_rename_file_n " %s to %s failed "
                      "before executing new binary process \"%s\"",
                      ccf->pid.data, ccf->oldpid.data, argv[0]);

        ngx_free(env);
        ngx_free(var);

        return NGX_INVALID_PID;
    }

    /* 执行新进程 */
    pid = ngx_execute(cycle, &ctx);

    /* 如果执行失败，恢复PID文件名 */
    if (pid == NGX_INVALID_PID) {
        if (ngx_rename_file(ccf->oldpid.data, ccf->pid.data)
            == NGX_FILE_ERROR)
        {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_errno,
                          ngx_rename_file_n " %s back to %s failed after "
                          "an attempt to execute new binary process \"%s\"",
                          ccf->oldpid.data, ccf->pid.data, argv[0]);
        }
    }

    ngx_free(env);
    ngx_free(var);

    return pid;
}


static ngx_int_t
ngx_get_options(int argc, char *const *argv)
{
    u_char     *p;
    ngx_int_t   i;

    for (i = 1; i < argc; i++) {

        p = (u_char *) argv[i];

        if (*p++ != '-') {
            ngx_log_stderr(0, "invalid option: \"%s\"", argv[i]);
            return NGX_ERROR;
        }

        while (*p) {

            switch (*p++) {

            case '?':
            case 'h':
                ngx_show_version = 1;
                ngx_show_help = 1;
                break;

            case 'v':
                ngx_show_version = 1;
                break;

            case 'V':
                ngx_show_version = 1;
                ngx_show_configure = 1;
                break;

            case 't':
                ngx_test_config = 1;
                break;

            case 'T':
                ngx_test_config = 1;
                ngx_dump_config = 1;
                break;

            case 'q':
                ngx_quiet_mode = 1;
                break;

            case 'p':
                if (*p) {
                    ngx_prefix = p;
                    goto next;
                }

                if (argv[++i]) {
                    ngx_prefix = (u_char *) argv[i];
                    goto next;
                }

                ngx_log_stderr(0, "option \"-p\" requires directory name");
                return NGX_ERROR;

            case 'e':
                if (*p) {
                    ngx_error_log = p;

                } else if (argv[++i]) {
                    ngx_error_log = (u_char *) argv[i];

                } else {
                    ngx_log_stderr(0, "option \"-e\" requires file name");
                    return NGX_ERROR;
                }

                if (ngx_strcmp(ngx_error_log, "stderr") == 0) {
                    ngx_error_log = (u_char *) "";
                }

                goto next;

            case 'c':
                if (*p) {
                    ngx_conf_file = p;
                    goto next;
                }

                if (argv[++i]) {
                    ngx_conf_file = (u_char *) argv[i];
                    goto next;
                }

                ngx_log_stderr(0, "option \"-c\" requires file name");
                return NGX_ERROR;

            case 'g':
                if (*p) {
                    ngx_conf_params = p;
                    goto next;
                }

                if (argv[++i]) {
                    ngx_conf_params = (u_char *) argv[i];
                    goto next;
                }

                ngx_log_stderr(0, "option \"-g\" requires parameter");
                return NGX_ERROR;

            case 's':
                if (*p) {
                    ngx_signal = (char *) p;

                } else if (argv[++i]) {
                    ngx_signal = argv[i];

                } else {
                    ngx_log_stderr(0, "option \"-s\" requires parameter");
                    return NGX_ERROR;
                }

                if (ngx_strcmp(ngx_signal, "stop") == 0
                    || ngx_strcmp(ngx_signal, "quit") == 0
                    || ngx_strcmp(ngx_signal, "reopen") == 0
                    || ngx_strcmp(ngx_signal, "reload") == 0)
                {
                    ngx_process = NGX_PROCESS_SIGNALLER;
                    goto next;
                }

                ngx_log_stderr(0, "invalid option: \"-s %s\"", ngx_signal);
                return NGX_ERROR;

            default:
                ngx_log_stderr(0, "invalid option: \"%c\"", *(p - 1));
                return NGX_ERROR;
            }
        }

    next:

        continue;
    }

    return NGX_OK;
}


static ngx_int_t
ngx_save_argv(ngx_cycle_t *cycle, int argc, char *const *argv)
{
#if (NGX_FREEBSD)

    ngx_os_argv = (char **) argv;
    ngx_argc = argc;
    ngx_argv = (char **) argv;

#else
    size_t     len;
    ngx_int_t  i;

    ngx_os_argv = (char **) argv;
    ngx_argc = argc;

    ngx_argv = ngx_alloc((argc + 1) * sizeof(char *), cycle->log);
    if (ngx_argv == NULL) {
        return NGX_ERROR;
    }

    for (i = 0; i < argc; i++) {
        len = ngx_strlen(argv[i]) + 1;

        ngx_argv[i] = ngx_alloc(len, cycle->log);
        if (ngx_argv[i] == NULL) {
            return NGX_ERROR;
        }

        (void) ngx_cpystrn((u_char *) ngx_argv[i], (u_char *) argv[i], len);
    }

    ngx_argv[i] = NULL;

#endif

    ngx_os_environ = environ;

    return NGX_OK;
}


static ngx_int_t
ngx_process_options(ngx_cycle_t *cycle)
{
    u_char  *p;
    size_t   len;

    if (ngx_prefix) {
        len = ngx_strlen(ngx_prefix);
        p = ngx_prefix;

        if (len && !ngx_path_separator(p[len - 1])) {
            p = ngx_pnalloc(cycle->pool, len + 1);
            if (p == NULL) {
                return NGX_ERROR;
            }

            ngx_memcpy(p, ngx_prefix, len);
            p[len++] = '/';
        }

        cycle->conf_prefix.len = len;
        cycle->conf_prefix.data = p;
        cycle->prefix.len = len;
        cycle->prefix.data = p;

    } else {

#ifndef NGX_PREFIX

        p = ngx_pnalloc(cycle->pool, NGX_MAX_PATH);
        if (p == NULL) {
            return NGX_ERROR;
        }

        if (ngx_getcwd(p, NGX_MAX_PATH) == 0) {
            ngx_log_stderr(ngx_errno, "[emerg]: " ngx_getcwd_n " failed");
            return NGX_ERROR;
        }

        len = ngx_strlen(p);

        p[len++] = '/';

        cycle->conf_prefix.len = len;
        cycle->conf_prefix.data = p;
        cycle->prefix.len = len;
        cycle->prefix.data = p;

#else

#ifdef NGX_CONF_PREFIX
        ngx_str_set(&cycle->conf_prefix, NGX_CONF_PREFIX);
#else
        ngx_str_set(&cycle->conf_prefix, NGX_PREFIX);
#endif
        ngx_str_set(&cycle->prefix, NGX_PREFIX);

#endif
    }

    if (ngx_conf_file) {
        cycle->conf_file.len = ngx_strlen(ngx_conf_file);
        cycle->conf_file.data = ngx_conf_file;

    } else {
        ngx_str_set(&cycle->conf_file, NGX_CONF_PATH);
    }

    if (ngx_conf_full_name(cycle, &cycle->conf_file, 0) != NGX_OK) {
        return NGX_ERROR;
    }

    for (p = cycle->conf_file.data + cycle->conf_file.len - 1;
         p > cycle->conf_file.data;
         p--)
    {
        if (ngx_path_separator(*p)) {
            cycle->conf_prefix.len = p - cycle->conf_file.data + 1;
            cycle->conf_prefix.data = cycle->conf_file.data;
            break;
        }
    }

    if (ngx_error_log) {
        cycle->error_log.len = ngx_strlen(ngx_error_log);
        cycle->error_log.data = ngx_error_log;

    } else {
        ngx_str_set(&cycle->error_log, NGX_ERROR_LOG_PATH);
    }

    if (ngx_conf_params) {
        cycle->conf_param.len = ngx_strlen(ngx_conf_params);
        cycle->conf_param.data = ngx_conf_params;
    }

    if (ngx_test_config) {
        cycle->log->log_level = NGX_LOG_INFO;
    }

    return NGX_OK;
}


/*
 * 创建核心模块配置结构体
 * 分配并初始化ngx_core_conf_t结构体，设置默认值
 *
 * @param cycle 当前cycle对象
 * @return 配置结构体指针，NULL表示失败
 */
static void *
ngx_core_module_create_conf(ngx_cycle_t *cycle)
{
    ngx_core_conf_t  *ccf;

    /* 分配核心配置结构体内存 */
    ccf = ngx_pcalloc(cycle->pool, sizeof(ngx_core_conf_t));
    if (ccf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc()
     *
     *     ccf->pid = NULL;
     *     ccf->oldpid = NULL;
     *     ccf->priority = 0;
     *     ccf->cpu_affinity_auto = 0;
     *     ccf->cpu_affinity_n = 0;
     *     ccf->cpu_affinity = NULL;
     */

    /* 设置配置项为未设置状态，后续会初始化 */
    ccf->daemon = NGX_CONF_UNSET;
    ccf->master = NGX_CONF_UNSET;
    ccf->timer_resolution = NGX_CONF_UNSET_MSEC;
    ccf->shutdown_timeout = NGX_CONF_UNSET_MSEC;

    ccf->worker_processes = NGX_CONF_UNSET;
    ccf->debug_points = NGX_CONF_UNSET;

    ccf->rlimit_nofile = NGX_CONF_UNSET;
    ccf->rlimit_core = NGX_CONF_UNSET;

    ccf->user = (ngx_uid_t) NGX_CONF_UNSET_UINT;
    ccf->group = (ngx_gid_t) NGX_CONF_UNSET_UINT;

    /* 初始化环境变量数组 */
    if (ngx_array_init(&ccf->env, cycle->pool, 1, sizeof(ngx_str_t))
        != NGX_OK)
    {
        return NULL;
    }

    return ccf;
}


static char *
ngx_core_module_init_conf(ngx_cycle_t *cycle, void *conf)
{
    ngx_core_conf_t  *ccf = conf;

    ngx_conf_init_value(ccf->daemon, 1);
    ngx_conf_init_value(ccf->master, 1);
    ngx_conf_init_msec_value(ccf->timer_resolution, 0);
    ngx_conf_init_msec_value(ccf->shutdown_timeout, 0);

    ngx_conf_init_value(ccf->worker_processes, 1);
    ngx_conf_init_value(ccf->debug_points, 0);

#if (NGX_HAVE_CPU_AFFINITY)

    if (!ccf->cpu_affinity_auto
        && ccf->cpu_affinity_n
        && ccf->cpu_affinity_n != 1
        && ccf->cpu_affinity_n != (ngx_uint_t) ccf->worker_processes)
    {
        ngx_log_error(NGX_LOG_WARN, cycle->log, 0,
                      "the number of \"worker_processes\" is not equal to "
                      "the number of \"worker_cpu_affinity\" masks, "
                      "using last mask for remaining worker processes");
    }

#endif


    if (ccf->pid.len == 0) {
        ngx_str_set(&ccf->pid, NGX_PID_PATH);
    }

    if (ngx_conf_full_name(cycle, &ccf->pid, 0) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    ccf->oldpid.len = ccf->pid.len + sizeof(NGX_OLDPID_EXT);

    ccf->oldpid.data = ngx_pnalloc(cycle->pool, ccf->oldpid.len);
    if (ccf->oldpid.data == NULL) {
        return NGX_CONF_ERROR;
    }

    ngx_memcpy(ngx_cpymem(ccf->oldpid.data, ccf->pid.data, ccf->pid.len),
               NGX_OLDPID_EXT, sizeof(NGX_OLDPID_EXT));


#if !(NGX_WIN32)

    if (ccf->user == (uid_t) NGX_CONF_UNSET_UINT && geteuid() == 0) {
        struct group   *grp;
        struct passwd  *pwd;

        ngx_set_errno(0);
        pwd = getpwnam(NGX_USER);
        if (pwd == NULL) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "getpwnam(\"" NGX_USER "\") failed");
            return NGX_CONF_ERROR;
        }

        ccf->username = NGX_USER;
        ccf->user = pwd->pw_uid;

        ngx_set_errno(0);
        grp = getgrnam(NGX_GROUP);
        if (grp == NULL) {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, ngx_errno,
                          "getgrnam(\"" NGX_GROUP "\") failed");
            return NGX_CONF_ERROR;
        }

        ccf->group = grp->gr_gid;
    }


    if (ccf->lock_file.len == 0) {
        ngx_str_set(&ccf->lock_file, NGX_LOCK_PATH);
    }

    if (ngx_conf_full_name(cycle, &ccf->lock_file, 0) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    {
    ngx_str_t  lock_file;

    lock_file = cycle->old_cycle->lock_file;

    if (lock_file.len) {
        lock_file.len--;

        if (ccf->lock_file.len != lock_file.len
            || ngx_strncmp(ccf->lock_file.data, lock_file.data, lock_file.len)
               != 0)
        {
            ngx_log_error(NGX_LOG_EMERG, cycle->log, 0,
                          "\"lock_file\" could not be changed, ignored");
        }

        cycle->lock_file.len = lock_file.len + 1;
        lock_file.len += sizeof(".accept");

        cycle->lock_file.data = ngx_pstrdup(cycle->pool, &lock_file);
        if (cycle->lock_file.data == NULL) {
            return NGX_CONF_ERROR;
        }

    } else {
        cycle->lock_file.len = ccf->lock_file.len + 1;
        cycle->lock_file.data = ngx_pnalloc(cycle->pool,
                                      ccf->lock_file.len + sizeof(".accept"));
        if (cycle->lock_file.data == NULL) {
            return NGX_CONF_ERROR;
        }

        ngx_memcpy(ngx_cpymem(cycle->lock_file.data, ccf->lock_file.data,
                              ccf->lock_file.len),
                   ".accept", sizeof(".accept"));
    }
    }

#endif

    return NGX_CONF_OK;
}


static char *
ngx_set_user(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
#if (NGX_WIN32)

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "\"user\" is not supported, ignored");

    return NGX_CONF_OK;

#else

    ngx_core_conf_t  *ccf = conf;

    char             *group;
    struct passwd    *pwd;
    struct group     *grp;
    ngx_str_t        *value;

    if (ccf->user != (uid_t) NGX_CONF_UNSET_UINT) {
        return "is duplicate";
    }

    if (geteuid() != 0) {
        ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                           "the \"user\" directive makes sense only "
                           "if the master process runs "
                           "with super-user privileges, ignored");
        return NGX_CONF_OK;
    }

    value = cf->args->elts;

    ccf->username = (char *) value[1].data;

    ngx_set_errno(0);
    pwd = getpwnam((const char *) value[1].data);
    if (pwd == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno,
                           "getpwnam(\"%s\") failed", value[1].data);
        return NGX_CONF_ERROR;
    }

    ccf->user = pwd->pw_uid;

    group = (char *) ((cf->args->nelts == 2) ? value[1].data : value[2].data);

    ngx_set_errno(0);
    grp = getgrnam(group);
    if (grp == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, ngx_errno,
                           "getgrnam(\"%s\") failed", group);
        return NGX_CONF_ERROR;
    }

    ccf->group = grp->gr_gid;

    return NGX_CONF_OK;

#endif
}


static char *
ngx_set_env(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_core_conf_t  *ccf = conf;

    ngx_str_t   *value, *var;
    ngx_uint_t   i;

    var = ngx_array_push(&ccf->env);
    if (var == NULL) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;
    *var = value[1];

    for (i = 0; i < value[1].len; i++) {

        if (value[1].data[i] == '=') {

            var->len = i;

            return NGX_CONF_OK;
        }
    }

    return NGX_CONF_OK;
}


static char *
ngx_set_priority(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_core_conf_t  *ccf = conf;

    ngx_str_t        *value;
    ngx_uint_t        n, minus;

    if (ccf->priority != 0) {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (value[1].data[0] == '-') {
        n = 1;
        minus = 1;

    } else if (value[1].data[0] == '+') {
        n = 1;
        minus = 0;

    } else {
        n = 0;
        minus = 0;
    }

    ccf->priority = ngx_atoi(&value[1].data[n], value[1].len - n);
    if (ccf->priority == NGX_ERROR) {
        return "invalid number";
    }

    if (minus) {
        ccf->priority = -ccf->priority;
    }

    return NGX_CONF_OK;
}


/**
 * 函数功能：设置Worker进程的CPU亲和性（CPU绑核）
 * 
 * 参数说明：
 *   @param cf: 配置上下文
 *   @param cmd: 配置指令
 *   @param conf: 核心模块配置结构体
 * 
 * 返回值：
 *   @return NGX_CONF_OK: 配置成功
 *   @return NGX_CONF_ERROR: 配置失败
 *   @return "is duplicate": 重复配置
 * 
 * CPU绑核说明：
 *   - CPU绑核（CPU Affinity）将Worker进程绑定到特定的CPU核心
 *   - 可以减少CPU缓存失效，提高性能
 *   - 配置格式：worker_cpu_affinity 0001 0010 0100 1000;
 *     - 每个二进制字符串表示一个Worker进程的CPU掩码
 *     - "0001"表示绑定到CPU 0，"0010"表示绑定到CPU 1，以此类推
 *   - auto模式：worker_cpu_affinity auto; 自动分配CPU核心
 * 
 * 执行时机：
 *   - 配置解析阶段：解析worker_cpu_affinity指令
 *   - Worker进程初始化：在ngx_worker_process_init()中执行CPU绑核
 *     - 调用ngx_get_cpu_affinity()获取CPU掩码
 *     - 调用ngx_setaffinity()设置CPU亲和性
 *     - 使用sched_setaffinity()系统调用（Linux）或cpuset_setaffinity()（FreeBSD）
 */
static char *
ngx_set_cpu_affinity(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
#if (NGX_HAVE_CPU_AFFINITY)
    ngx_core_conf_t  *ccf = conf;

    u_char            ch, *p;
    ngx_str_t        *value;
    ngx_uint_t        i, n;
    ngx_cpuset_t     *mask;

    if (ccf->cpu_affinity) {
        return "is duplicate";
    }

    /* 分配CPU亲和性掩码数组
     * 每个Worker进程对应一个CPU掩码
     */
    mask = ngx_palloc(cf->pool, (cf->args->nelts - 1) * sizeof(ngx_cpuset_t));
    if (mask == NULL) {
        return NGX_CONF_ERROR;
    }

    ccf->cpu_affinity_n = cf->args->nelts - 1;
    ccf->cpu_affinity = mask;

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "auto") == 0) {

        if (cf->args->nelts > 3) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                               "invalid number of arguments in "
                               "\"worker_cpu_affinity\" directive");
            return NGX_CONF_ERROR;
        }

        ccf->cpu_affinity_auto = 1;

        CPU_ZERO(&mask[0]);
        for (i = 0; i < (ngx_uint_t) ngx_min(ngx_ncpu, CPU_SETSIZE); i++) {
            CPU_SET(i, &mask[0]);
        }

        n = 2;

    } else {
        n = 1;
    }

    for ( /* void */ ; n < cf->args->nelts; n++) {

        if (value[n].len > CPU_SETSIZE) {
            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                         "\"worker_cpu_affinity\" supports up to %d CPUs only",
                         CPU_SETSIZE);
            return NGX_CONF_ERROR;
        }

        i = 0;
        CPU_ZERO(&mask[n - 1]);

        for (p = value[n].data + value[n].len - 1;
             p >= value[n].data;
             p--)
        {
            ch = *p;

            if (ch == ' ') {
                continue;
            }

            i++;

            if (ch == '0') {
                continue;
            }

            if (ch == '1') {
                CPU_SET(i - 1, &mask[n - 1]);
                continue;
            }

            ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                          "invalid character \"%c\" in \"worker_cpu_affinity\"",
                          ch);
            return NGX_CONF_ERROR;
        }
    }

#else

    ngx_conf_log_error(NGX_LOG_WARN, cf, 0,
                       "\"worker_cpu_affinity\" is not supported "
                       "on this platform, ignored");
#endif

    return NGX_CONF_OK;
}


/**
 * 函数功能：获取指定Worker进程的CPU亲和性掩码
 * 
 * 参数说明：
 *   @param n: Worker进程编号（从0开始）
 * 
 * 返回值：
 *   @return CPU亲和性掩码指针：成功
 *   @return NULL: 未配置CPU亲和性或配置错误
 * 
 * 使用场景：
 *   - Worker进程初始化时调用，获取该Worker进程应该绑定到的CPU核心
 *   - 调用位置：ngx_worker_process_init() → ngx_get_cpu_affinity(worker)
 * 
 * 工作原理：
 *   1. auto模式：根据Worker进程编号自动选择CPU核心
 *   2. 手动模式：使用配置的CPU掩码数组，每个Worker进程对应一个掩码
 *   3. 如果配置的掩码数量少于Worker进程数量，使用最后一个掩码
 */
ngx_cpuset_t *
ngx_get_cpu_affinity(ngx_uint_t n)
{
#if (NGX_HAVE_CPU_AFFINITY)
    ngx_uint_t        i, j;
    ngx_cpuset_t     *mask;
    ngx_core_conf_t  *ccf;

    static ngx_cpuset_t  result;

    ccf = (ngx_core_conf_t *) ngx_get_conf(ngx_cycle->conf_ctx,
                                           ngx_core_module);

    /* 如果未配置CPU亲和性，返回NULL */
    if (ccf->cpu_affinity == NULL) {
        return NULL;
    }

    if (ccf->cpu_affinity_auto) {
        mask = &ccf->cpu_affinity[ccf->cpu_affinity_n - 1];

        for (i = 0, j = n; /* void */ ; i++) {

            if (CPU_ISSET(i % CPU_SETSIZE, mask) && j-- == 0) {
                break;
            }

            if (i == CPU_SETSIZE && j == n) {
                /* empty mask */
                return NULL;
            }

            /* void */
        }

        CPU_ZERO(&result);
        CPU_SET(i % CPU_SETSIZE, &result);

        return &result;
    }

    if (ccf->cpu_affinity_n > n) {
        return &ccf->cpu_affinity[n];
    }

    return &ccf->cpu_affinity[ccf->cpu_affinity_n - 1];

#else

    return NULL;

#endif
}


static char *
ngx_set_worker_processes(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_str_t        *value;
    ngx_core_conf_t  *ccf;

    ccf = (ngx_core_conf_t *) conf;

    if (ccf->worker_processes != NGX_CONF_UNSET) {
        return "is duplicate";
    }

    value = cf->args->elts;

    if (ngx_strcmp(value[1].data, "auto") == 0) {
        ccf->worker_processes = ngx_ncpu;
        return NGX_CONF_OK;
    }

    ccf->worker_processes = ngx_atoi(value[1].data, value[1].len);

    if (ccf->worker_processes == NGX_ERROR) {
        return "invalid value";
    }

    return NGX_CONF_OK;
}


static char *
ngx_load_module(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
#if (NGX_HAVE_DLOPEN)
    void                *handle;
    char               **names, **order;
    ngx_str_t           *value, file;
    ngx_uint_t           i;
    ngx_module_t        *module, **modules;
    ngx_pool_cleanup_t  *cln;

    if (cf->cycle->modules_used) {
        return "is specified too late";
    }

    value = cf->args->elts;

    file = value[1];

    if (ngx_conf_full_name(cf->cycle, &file, 0) != NGX_OK) {
        return NGX_CONF_ERROR;
    }

    cln = ngx_pool_cleanup_add(cf->cycle->pool, 0);
    if (cln == NULL) {
        return NGX_CONF_ERROR;
    }

    handle = ngx_dlopen(file.data);
    if (handle == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           ngx_dlopen_n " \"%s\" failed (%s)",
                           file.data, ngx_dlerror());
        return NGX_CONF_ERROR;
    }

    cln->handler = ngx_unload_module;
    cln->data = handle;

    modules = ngx_dlsym(handle, "ngx_modules");
    if (modules == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           ngx_dlsym_n " \"%V\", \"%s\" failed (%s)",
                           &value[1], "ngx_modules", ngx_dlerror());
        return NGX_CONF_ERROR;
    }

    names = ngx_dlsym(handle, "ngx_module_names");
    if (names == NULL) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           ngx_dlsym_n " \"%V\", \"%s\" failed (%s)",
                           &value[1], "ngx_module_names", ngx_dlerror());
        return NGX_CONF_ERROR;
    }

    order = ngx_dlsym(handle, "ngx_module_order");

    for (i = 0; modules[i]; i++) {
        module = modules[i];
        module->name = names[i];

        if (ngx_add_module(cf, &file, module, order) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        ngx_log_debug2(NGX_LOG_DEBUG_CORE, cf->log, 0, "module: %s i:%ui",
                       module->name, module->index);
    }

    return NGX_CONF_OK;

#else

    ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                       "\"load_module\" is not supported "
                       "on this platform");
    return NGX_CONF_ERROR;

#endif
}


#if (NGX_HAVE_DLOPEN)

static void
ngx_unload_module(void *data)
{
    void  *handle = data;

    if (ngx_dlclose(handle) != 0) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0,
                      ngx_dlclose_n " failed (%s)", ngx_dlerror());
    }
}

#endif
