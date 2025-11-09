
/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#include <ngx_config.h>
#include <ngx_core.h>
#include <nginx.h>


ngx_int_t   ngx_ncpu;
ngx_int_t   ngx_max_sockets;
ngx_uint_t  ngx_inherited_nonblocking;
ngx_uint_t  ngx_tcp_nodelay_and_tcp_nopush;


struct rlimit  rlmt;


ngx_os_io_t ngx_os_io = {
    ngx_unix_recv,
    ngx_readv_chain,
    ngx_udp_unix_recv,
    ngx_unix_send,
    ngx_udp_unix_send,
    ngx_udp_unix_sendmsg_chain,
    ngx_writev_chain,
    0
};


/**
 * 函数功能：操作系统初始化函数，获取系统资源信息并初始化相关全局变量
 * 
 * 参数说明：
 *   @param log: 日志对象，用于记录初始化过程中的错误信息
 * 
 * 返回值：
 *   @return NGX_OK: 初始化成功
 *   @return NGX_ERROR: 初始化失败，函数会记录错误日志
 * 
 * 初始化操作类型：
 *   1. 平台特定初始化：获取系统信息（内核类型、版本）并设置I/O函数指针
 *   2. 进程标题初始化：设置进程标题，用于进程管理和监控
 *   3. 内存页大小获取：获取系统内存页大小（通常为4KB）并计算页大小位移
 *   4. CPU缓存行大小设置：设置默认缓存行大小，优先使用系统提供的L1缓存行大小
 *   5. CPU核心数获取：获取在线CPU核心数，用于Worker进程数量配置
 *   6. CPU信息检测：通过CPUID指令检测CPU厂商和型号，优化缓存行大小设置
 *   7. 文件描述符限制获取：获取系统RLIMIT_NOFILE限制，确定最大并发连接数
 *   8. 非阻塞标志设置：根据平台特性设置非阻塞I/O标志
 *   9. 随机数生成器初始化：使用进程ID和时间戳初始化随机数种子
 * 
 * 交互模块：
 *   - ngx_os_specific_init: 平台特定初始化（Linux/FreeBSD/Solaris/Darwin）
 *   - ngx_init_setproctitle: 进程标题初始化
 *   - ngx_cpuinfo: CPU信息检测
 *   - ngx_timeofday: 获取当前时间
 * 
 * 设计优势：
 *   - 性能优化：获取缓存行大小用于数据结构对齐，避免false sharing
 *   - 资源管理：获取CPU核心数和文件描述符限制，为进程配置提供依据
 *   - 平台适配：通过平台特定初始化实现不同操作系统的适配
 *   - 可维护性：集中管理操作系统相关的初始化逻辑
 */
ngx_int_t
ngx_os_init(ngx_log_t *log)
{
    ngx_time_t  *tp;
    ngx_uint_t   n;
#if (NGX_HAVE_LEVEL1_DCACHE_LINESIZE)
    long         size;
#endif

    /* 1. 平台特定初始化：获取系统信息（内核类型、版本）并设置I/O函数指针
     *    - Linux: 获取内核类型和版本，设置sendfile支持
     *    - FreeBSD: 获取系统版本信息
     *    - Solaris: 获取系统版本信息
     *    - Darwin: 获取系统版本信息
     */
#if (NGX_HAVE_OS_SPECIFIC_INIT)
    if (ngx_os_specific_init(log) != NGX_OK) {
        return NGX_ERROR;
    }
#endif

    /* 2. 进程标题初始化：设置进程标题，用于进程管理和监控
     *    - 修改进程标题，使其显示nginx的工作状态
     *    - 便于通过ps命令查看nginx进程的工作状态
     */
    if (ngx_init_setproctitle(log) != NGX_OK) {
        return NGX_ERROR;
    }

    /* 3. 内存页大小获取：获取系统内存页大小（通常为4KB）
     *    - 操作系统内存管理的基本单位，通常是4096字节（4KB）
     *    - 用于内存分配对齐和slab分配器
     *    
     * 主要用途：
     *    1. Slab分配器：共享内存管理，按页分配内存
     *       - ngx_slab_max_size = ngx_pagesize / 2
     *       - 大对象按页对齐分配：size >> ngx_pagesize_shift
     *    2. 内存对齐：共享内存必须按页边界对齐
     *       - ngx_align_ptr(ptr, ngx_pagesize)
     *    3. 文件I/O优化：文件读取位置对齐到页边界
     *    4. 页大小位移：通过位移快速计算页对齐（避免除法）
     *    
     * 注意：这里获取的是普通页大小（4KB），不是大页内存（2MB/1GB）
     *    - 大页内存需要系统预先配置，NGINX当前使用普通页
     *    - 大页内存可以减少TLB缺失，但需要系统支持
     */
    ngx_pagesize = getpagesize();

    /* 4. CPU缓存行大小设置：设置默认缓存行大小（通常为64字节）
     *    - 如果未设置，使用默认值NGX_CPU_CACHE_LINE
     *    - 用于数据结构对齐，避免false sharing（伪共享）
     *    
     * False Sharing问题说明：
     *    - CPU缓存以缓存行为单位（通常64字节），不是按字节
     *    - 当两个不同CPU核心频繁访问同一缓存行中的不同变量时
     *    - 即使变量不相关，也会导致缓存行在CPU核心间频繁同步
     *    - 造成性能严重下降（可能下降10-100倍）
     *    
     * 解决方案：
     *    - 通过缓存行对齐，确保每个变量独占一个缓存行
     *    - 例如：哈希表bucket、CRC32查找表、共享内存中的计数器等
     *    - 对齐方式：ngx_align_ptr(ptr, ngx_cacheline_size)
     */
    if (ngx_cacheline_size == 0) {
        ngx_cacheline_size = NGX_CPU_CACHE_LINE;
    }

    /* 5. 计算页大小位移：通过循环右移计算页大小的对数（log2）
     *    - 用于快速计算页对齐的内存大小
     *    - 例如：4096字节的页大小，位移为12（2^12 = 4096）
     *    
     * 为什么使用循环而不是库函数：
     *    1. 可移植性：C标准库没有专门计算log2的整数函数
     *       - ffs()：POSIX函数，但某些系统可能没有
     *       - log2()：C99标准，但涉及浮点数运算，性能较差
     *       - __builtin_clz()：GCC内置函数，但不可移植
     *    2. 性能：页大小位移计算只在启动时执行一次，性能差异可忽略
     *    3. 简洁性：循环方式代码简洁，易于理解和维护
     *    4. 无需依赖：不需要额外的头文件或库
     *    
     * 工作原理：
     *    - 通过循环右移，计算需要右移多少次才能得到1
     *    - 例如：4096需要右移12次才能得到1，所以位移为12
     *    - 循环次数 = log2(pagesize)，最多循环32次（对于32位整数）
     */
    for (n = ngx_pagesize; n >>= 1; ngx_pagesize_shift++) { /* void */ }

    /* 6. CPU核心数获取：获取在线CPU核心数
     *    - 用于Worker进程数量配置（worker_processes auto）
     *    - 影响Nginx的并发处理能力
     *    - 如果获取失败或为0，设置为1（单核）
     */
#if (NGX_HAVE_SC_NPROCESSORS_ONLN)
    if (ngx_ncpu == 0) {
        ngx_ncpu = sysconf(_SC_NPROCESSORS_ONLN);
    }
#endif

    if (ngx_ncpu < 1) {
        ngx_ncpu = 1;
    }

    /* 7. L1数据缓存行大小获取：如果系统支持，获取L1数据缓存行大小
     *    - 优先使用系统提供的缓存行大小
     *    - 比默认值更准确，能够优化数据结构对齐
     *    - 如果获取成功，覆盖默认的缓存行大小设置
     */
#if (NGX_HAVE_LEVEL1_DCACHE_LINESIZE)
    size = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    if (size > 0) {
        ngx_cacheline_size = size;
    }
#endif

    /* 8. CPU信息检测：通过CPUID指令检测CPU厂商和型号
     *    - 检测Intel CPU：根据型号设置缓存行大小（32/64/128字节）
     *    - 检测AMD CPU：设置缓存行大小为64字节
     *    - 优化缓存行大小设置，提升多核环境下的性能
     */
    ngx_cpuinfo();

    /* 9. 文件描述符限制获取：获取系统RLIMIT_NOFILE限制
     *    - 确定最大并发连接数（每个连接占用一个文件描述符）
     *    - 影响Nginx的并发处理能力
     *    - 如果获取失败，记录错误并返回失败
     */
    if (getrlimit(RLIMIT_NOFILE, &rlmt) == -1) {
        ngx_log_error(NGX_LOG_ALERT, log, errno,
                      "getrlimit(RLIMIT_NOFILE) failed");
        return NGX_ERROR;
    }

    /* 10. 设置最大socket数：使用当前文件描述符限制作为最大socket数
     *     - 用于限制并发连接数
     *     - 影响Nginx的并发处理能力
     */
    ngx_max_sockets = (ngx_int_t) rlmt.rlim_cur;

    /* 11. 非阻塞标志设置：根据平台特性设置非阻塞I/O标志
     *     - 如果平台支持继承非阻塞标志（accept4）或自动非阻塞，设置为1
     *     - 否则设置为0，需要在accept后手动设置非阻塞标志
     *     - 影响socket创建的性能
     */
#if (NGX_HAVE_INHERITED_NONBLOCK || NGX_HAVE_ACCEPT4)
    ngx_inherited_nonblocking = 1;
#else
    ngx_inherited_nonblocking = 0;
#endif

    /* 12. 随机数生成器初始化：使用进程ID和时间戳初始化随机数种子
     *     - 种子构成：(进程ID << 16) ^ 秒数 ^ 毫秒数
     *     - 确保不同进程、不同启动时间有不同的随机数序列
     *     
     * 主要应用场景：
     *    1. 负载均衡：随机选择后端服务器（upstream_random模块）
     *       - x = ngx_random() % total_weight
     *       - 在大量请求下，保证请求均匀分布到各后端服务器
     *    2. DNS解析：随机选择DNS服务器，避免单点故障
     *    3. 会话ID生成：生成唯一的会话标识符
     *    4. 随机索引：随机选择文件索引（random_index模块）
     *    5. 变量值生成：生成随机变量值
     *    
     * 负载均衡原理：
     *    - 通过随机数在权重范围内选择后端服务器
     *    - 在大量请求下（大数定律），请求分布接近权重比例
     *    - 例如：权重比3:2:1，长期来看请求分布也接近3:2:1
     *    - 适合短连接、无状态服务场景
     */
    tp = ngx_timeofday();
    srandom(((unsigned) ngx_pid << 16) ^ tp->sec ^ tp->msec);

    return NGX_OK;
}


void
ngx_os_status(ngx_log_t *log)
{
    ngx_log_error(NGX_LOG_NOTICE, log, 0, NGINX_VER_BUILD);

#ifdef NGX_COMPILER
    ngx_log_error(NGX_LOG_NOTICE, log, 0, "built by " NGX_COMPILER);
#endif

#if (NGX_HAVE_OS_SPECIFIC_INIT)
    ngx_os_specific_status(log);
#endif

    ngx_log_error(NGX_LOG_NOTICE, log, 0,
                  "getrlimit(RLIMIT_NOFILE): %r:%r",
                  rlmt.rlim_cur, rlmt.rlim_max);
}


#if 0

ngx_int_t
ngx_posix_post_conf_init(ngx_log_t *log)
{
    ngx_fd_t  pp[2];

    if (pipe(pp) == -1) {
        ngx_log_error(NGX_LOG_EMERG, log, ngx_errno, "pipe() failed");
        return NGX_ERROR;
    }

    if (dup2(pp[1], STDERR_FILENO) == -1) {
        ngx_log_error(NGX_LOG_EMERG, log, errno, "dup2(STDERR) failed");
        return NGX_ERROR;
    }

    if (pp[1] > STDERR_FILENO) {
        if (close(pp[1]) == -1) {
            ngx_log_error(NGX_LOG_EMERG, log, errno, "close() failed");
            return NGX_ERROR;
        }
    }

    return NGX_OK;
}

#endif
