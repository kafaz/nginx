# Nginx源码深度学习规划（两周）
## 学习目标
1. **架构设计**：深入理解Nginx的Master-Worker模型、事件驱动架构、模块化设计
2. **代码规范**：掌握Nginx的编码风格、命名约定、代码组织方式
3. **核心机制**：理解内存管理、请求处理流程、性能优化技术
4. **实践能力**：能够阅读和修改Nginx源码，开发简单模块
## 总体时间安排
- **第一周**：基础架构与核心机制（Day 1-5）
- **第二周**：HTTP处理与模块开发（Day 6-10）
- **最后4天**：实践项目与总结（Day 11-14）
---
## 第一周：基础架构与核心机制
### Day 1：项目结构与代码规范（6小时）
#### 上午（3小时）：代码规范学习
**目标**：熟悉Nginx代码风格，培养阅读习惯
**学习内容**：
1. **代码格式规范**
   - 阅读 `docs/development_guide.html` 或 `nginx.md` 中的代码规范章节
   - 缩进：4空格，禁用TAB
   - 行宽：80字符限制
   - 花括号：K&R风格，左花括号与关键字同行
2. **命名约定**
   - 全局符号：`ngx_` 前缀
   - 模块符号：`ngx_http_`、`ngx_event_` 等
   - 类型定义：`_t` 后缀（如 `ngx_pool_t`）
   - 结构体：`_s` 后缀（如 `ngx_pool_s`）
3. **注释风格**
   - C风格注释 `/* */`，禁用 `//`
   - 函数头注释规范
**实践任务**：
- [ ] 阅读 `src/core/nginx.c` 的 `main` 函数，分析代码格式
- [ ] 阅读 `src/core/ngx_palloc.h`，观察命名约定
- [ ] 找出5个函数，分析其注释风格
**重点文件**：
- `src/core/nginx.c` (行1-500)
- `src/core/ngx_palloc.h`
- `src/core/ngx_string.h`
#### 下午（3小时）：项目结构探索
**目标**：理解Nginx源码目录结构
**学习内容**：
1. **目录结构分析**
   ```
   src/
   ├── core/        # 核心功能：内存池、字符串、配置解析
   ├── event/       # 事件处理：epoll、kqueue、事件循环
   ├── http/        # HTTP模块：请求处理、响应生成
   ├── mail/        # 邮件代理模块
   ├── stream/      # 流代理模块
   ├── os/          # 操作系统抽象层
   └── misc/        # 工具函数
   ```
2. **核心头文件**
   - `ngx_config.h`：平台相关配置
   - `ngx_core.h`：核心数据结构
   - `ngx_string.h`：字符串处理
**实践任务**：
- [ ] 绘制Nginx源码目录结构图
- [ ] 列出每个目录的主要功能
- [ ] 找出5个核心头文件，理解其作用
**重点文件**：
- `src/core/ngx_config.h`
- `src/core/ngx_core.h`
- `src/core/ngx_string.h`
- `src/core/ngx_palloc.h`
---
### Day 2：启动流程与进程模型（6小时）
#### 上午（3小时）：main函数分析
**目标**：理解Nginx启动的完整流程
**学习内容**：
1. **main函数流程**
   - 基础系统初始化（错误处理、时间、日志）
   - 临时cycle创建
   - 配置文件解析
   - 主cycle初始化
   - 进程模式选择（Master/Worker/单进程）
2. **关键函数调用链**
   ```
   main()
   ├── ngx_strerror_init()      # 错误消息初始化
   ├── ngx_time_init()          # 时间系统初始化
   ├── ngx_log_init()           # 日志系统初始化
   ├── ngx_os_init()            # 操作系统初始化
   ├── ngx_init_cycle()         # 主cycle初始化
   └── ngx_master_process_cycle()  # Master进程循环
   ```
**实践任务**：
- [ ] 阅读 `src/core/nginx.c:235-468`，画出main函数流程图
- [ ] 理解每个初始化函数的作用
- [ ] 找出配置文件解析的入口
**重点文件**：
- `src/core/nginx.c` (行235-468)
- `src/core/ngx_cycle.c` (ngx_init_cycle函数)
- `src/os/unix/ngx_process_cycle.c`
#### 下午（3小时）：Master-Worker进程模型
**目标**：理解多进程架构设计
**学习内容**：
1. **Master进程职责**
   - 管理Worker进程生命周期
   - 处理信号（SIGTERM、SIGHUP、SIGUSR1、SIGUSR2）
   - 监听Worker进程状态
   - 平滑重启和热升级
2. **Worker进程初始化**
   - 权限降低（setuid/setgid）
   - CPU亲和性设置
   - 资源限制（setrlimit）
   - 事件模块初始化
3. **进程间通信**
   - 信号通信（Master→Worker）
   - 共享内存（Worker之间）
   - 原子操作和互斥锁
**实践任务**：
- [ ] 阅读 `src/os/unix/ngx_process_cycle.c`
   - `ngx_master_process_cycle()` 函数
   - `ngx_worker_process_cycle()` 函数
   - `ngx_worker_process_init()` 函数
- [ ] 理解信号处理机制
- [ ] 分析平滑重启的实现
**重点文件**：
- `src/os/unix/ngx_process_cycle.c`
- `src/core/ngx_signal.c`
- `src/core/ngx_shmem.h`
**代码规范观察点**：
- 函数命名：`ngx_*_process_*`
- 变量对齐：观察变量声明的对齐方式
- 错误处理：观察错误处理的统一模式
---
### Day 3：内存管理机制（6小时）
#### 上午（3小时）：内存池设计
**目标**：深入理解Nginx的内存池机制
**学习内容**：
1. **内存池数据结构**
   ```c
   struct ngx_pool_s {
       ngx_pool_data_t    d;          // 数据块
       size_t             max;        // 小块内存最大值
       ngx_pool_t        *current;    // 当前内存池
       ngx_pool_large_t  *large;      // 大块内存链表
       ngx_pool_cleanup_t *cleanup;   // 清理函数链表
   };
   ```
2. **内存分配策略**
   - 小内存：从内存池顺序分配
   - 大内存：单独malloc，挂到大块链表
   - 对齐分配：提升CPU缓存效率
3. **内存池层级**
   - cycle内存池（16KB）
   - 连接内存池（4KB）
   - 请求内存池（动态）
**实践任务**：
- [ ] 阅读 `src/core/ngx_palloc.c` 全部代码
   - `ngx_create_pool()`：创建内存池
   - `ngx_palloc()`：分配内存
   - `ngx_pfree()`：释放大块内存
   - `ngx_destroy_pool()`：销毁内存池
- [ ] 理解内存对齐机制（`ngx_align_ptr`）
- [ ] 分析清理函数链表的实现
**重点文件**：
- `src/core/ngx_palloc.h`
- `src/core/ngx_palloc.c`
- `src/core/ngx_core.h` (内存对齐宏)
#### 下午（3小时）：内存管理实践
**目标**：理解内存池在实际场景中的应用
**学习内容**：
1. **内存池使用场景**
   - cycle创建时的内存池
   - 连接建立时的内存池
   - 请求处理时的内存池
2. **内存泄漏预防**
   - 自动清理机制（cleanup链表）
   - 批量释放策略
   - 内存对齐优化
3. **性能优化技巧**
   - 预分配策略
   - 内存对齐
   - 减少系统调用
**实践任务**：
- [ ] 在 `src/core/nginx.c` 中找出所有内存池创建点
- [ ] 分析连接内存池的使用（`ngx_event_accept`）
- [ ] 理解清理函数的注册和使用
- [ ] 编写一个小程序，模拟内存池的分配和释放
**重点文件**：
- `src/event/ngx_event_accept.c` (连接内存池使用)
- `src/http/ngx_http_request.c` (请求内存池使用)
- `src/core/ngx_pool.c` (清理函数实现)
**代码规范观察点**：
- 内存分配函数的返回值检查
- 错误处理的一致性
- 内存对齐的实现方式
---
### Day 4：事件驱动模型（6小时）
#### 上午（3小时）：事件模块抽象
**目标**：理解Nginx的事件驱动架构
**学习内容**：
1. **事件抽象层**
   ```c
   typedef struct {
       ngx_int_t  (*add)(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags);
       ngx_int_t  (*del)(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags);
       ngx_int_t  (*enable)(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags);
       ngx_int_t  (*disable)(ngx_event_t *ev, ngx_int_t event, ngx_uint_t flags);
       ngx_int_t  (*process_events)(ngx_cycle_t *cycle, ngx_msec_t timer,
                                    ngx_uint_t flags);
   } ngx_event_actions_t;
   ```
2. **事件数据结构**
   - `ngx_event_t`：事件对象
   - `ngx_connection_t`：连接对象
   - 事件与连接的关联
3. **平台抽象**
   - epoll（Linux）
   - kqueue（BSD）
   - select（通用）
   - IOCP（Windows）
**实践任务**：
- [ ] 阅读 `src/event/ngx_event.h`，理解事件数据结构
- [ ] 阅读 `src/event/modules/ngx_epoll_module.c`
   - `ngx_epoll_init()`：初始化epoll
   - `ngx_epoll_add_event()`：添加事件
   - `ngx_epoll_process_events()`：处理事件
- [ ] 对比不同事件模块的接口实现
**重点文件**：
- `src/event/ngx_event.h`
- `src/event/ngx_event.c`
- `src/event/modules/ngx_epoll_module.c`
- `src/event/modules/ngx_kqueue_module.c`
#### 下午（3小时）：事件循环与连接处理
**目标**：理解事件循环的工作机制
**学习内容**：
1. **事件循环流程**
   ```
   ngx_worker_process_cycle()
   └── ngx_process_events_and_timers()
       ├── ngx_trylock_accept_mutex()  # 获取accept锁
       ├── ngx_process_events()        # 处理I/O事件
       ├── ngx_event_process_posted()  # 处理延迟事件
       └── ngx_event_expire_timers()   # 处理定时器
   ```
2. **连接处理流程**
   - accept新连接
   - 注册读事件
   - 读取请求数据
   - 处理请求
   - 发送响应
3. **定时器机制**
   - 红黑树管理定时器
   - 超时处理
   - 定时器精度控制
**实践任务**：
- [ ] 阅读 `src/event/ngx_event.c`
   - `ngx_process_events_and_timers()`：事件处理主循环
   - `ngx_event_find_timer()`：查找最近的定时器
   - `ngx_event_expire_timers()`：处理超时定时器
- [ ] 阅读 `src/event/ngx_event_accept.c`
   - `ngx_event_accept()`：接受新连接
   - 理解连接池的使用
- [ ] 分析accept互斥锁机制（避免惊群）
**重点文件**：
- `src/event/ngx_event.c`
- `src/event/ngx_event_accept.c`
- `src/event/ngx_event_timer.c`
- `src/core/ngx_rbtree.h`
**代码规范观察点**：
- 事件处理函数的返回值约定
- 错误处理模式
- 函数参数的对齐方式
---
### Day 5：连接管理与I/O处理（6小时）
#### 上午（3小时）：连接池设计
**目标**：理解连接对象的生命周期管理
**学习内容**：
1. **连接对象结构**
   ```c
   struct ngx_connection_s {
       void               *data;        // 连接数据（通常是请求对象）
       ngx_event_t        *read;        // 读事件
       ngx_event_t        *write;       // 写事件
       ngx_socket_t        fd;          // 文件描述符
       ngx_recv_pt         recv;        // 接收函数指针
       ngx_send_pt         send;        // 发送函数指针
       ngx_pool_t         *pool;        // 连接内存池
   };
   ```
2. **连接池预分配**
   - 启动时预分配所有连接对象
   - 空闲连接链表管理
   - O(1)时间获取和归还连接
3. **连接复用机制**
   - keepalive连接复用
   - 连接池大小限制
   - 连接超时处理
**实践任务**：
- [ ] 阅读 `src/core/ngx_connection.h`，理解连接结构
- [ ] 阅读 `src/event/ngx_event.c:ngx_event_process_init()`
   - 连接池预分配
   - 事件对象与连接的绑定
   - 监听socket的连接对象创建
- [ ] 分析连接获取和归还的实现（`ngx_get_connection`、`ngx_free_connection`）
**重点文件**：
- `src/core/ngx_connection.h`
- `src/core/ngx_connection.c`
- `src/event/ngx_event.c` (ngx_event_process_init)
#### 下午（3小时）：I/O操作与零拷贝
**目标**：理解Nginx的I/O优化技术
**学习内容**：
1. **I/O函数抽象**
   - `ngx_recv()` / `ngx_send()`：基础I/O
   - `ngx_recv_chain()` / `ngx_send_chain()`：链式I/O
   - `ngx_writev()`：向量I/O
   - `sendfile()`：零拷贝发送
2. **零拷贝技术**
   - sendfile系统调用
   - 文件到网络的零拷贝
   - 内存拷贝次数分析
3. **非阻塞I/O**
   - EAGAIN处理
   - 事件注册机制
   - 异步I/O流程
**实践任务**：
- [ ] 阅读 `src/os/unix/ngx_readv_chain.c`
- [ ] 阅读 `src/os/unix/ngx_sendfile_chain.c`
   - 理解sendfile的实现
   - 分析零拷贝的优势
- [ ] 阅读 `src/event/ngx_event_accept.c`
   - 理解非阻塞accept
   - 分析EAGAIN的处理
**重点文件**：
- `src/os/unix/ngx_readv_chain.c`
- `src/os/unix/ngx_sendfile_chain.c`
- `src/os/unix/ngx_writev_chain.c`
- `src/event/ngx_event_accept.c`
**代码规范观察点**：
- I/O函数的错误处理
- 系统调用的封装方式
- 平台相关的条件编译
---
## 第二周：HTTP处理与模块开发
### Day 6：HTTP请求处理流程（6小时）
#### 上午（3小时）：请求接收与解析
**目标**：理解HTTP请求的完整处理流程
**学习内容**：
1. **请求接收流程**
   ```
   新连接到达
   └── ngx_event_accept()          # 接受连接
       └── ngx_http_init_connection()  # 初始化HTTP连接
           └── ngx_http_wait_request_handler()  # 等待请求
               └── ngx_http_process_request_line()  # 解析请求行
   ```
2. **请求行解析**
   - 状态机解析
   - 零拷贝解析（指针引用）
   - 请求方法识别
   - URI解析
3. **请求头解析**
   - 哈希表存储头部
   - 头部字段识别
   - 特殊头部处理（Host、Connection等）
**实践任务**：
- [ ] 阅读 `src/http/ngx_http_request.c`
   - `ngx_http_init_connection()`：初始化HTTP连接
   - `ngx_http_wait_request_handler()`：等待请求
   - `ngx_http_process_request_line()`：解析请求行
- [ ] 阅读 `src/http/ngx_http_parse.c`
   - `ngx_http_parse_request_line()`：请求行解析状态机
   - 理解零拷贝解析的实现
- [ ] 分析请求缓冲区管理
**重点文件**：
- `src/http/ngx_http_request.c`
- `src/http/ngx_http_parse.c`
- `src/http/ngx_http_request.h`
#### 下午（3小时）：11阶段处理流程
**目标**：深入理解HTTP请求处理的11个阶段
**学习内容**：
1. **11个处理阶段**
   ```c
   NGX_HTTP_POST_READ_PHASE        // 0: 读取请求头后
   NGX_HTTP_SERVER_REWRITE_PHASE   // 1: server块重写
   NGX_HTTP_FIND_CONFIG_PHASE      // 2: 查找location配置
   NGX_HTTP_REWRITE_PHASE          // 3: location块重写
   NGX_HTTP_POST_REWRITE_PHASE     // 4: 重写后处理
   NGX_HTTP_PREACCESS_PHASE        // 5: 访问控制前
   NGX_HTTP_ACCESS_PHASE           // 6: 访问控制
   NGX_HTTP_POST_ACCESS_PHASE      // 7: 访问控制后
   NGX_HTTP_TRY_FILES_PHASE        // 8: try_files处理
   NGX_HTTP_CONTENT_PHASE          // 9: 内容生成
   NGX_HTTP_LOG_PHASE              // 10: 日志记录
   ```
2. **阶段引擎实现**
   - `ngx_http_phase_handler_t`：阶段处理器
   - `ngx_http_core_run_phases()`：阶段执行引擎
   - checker和handler的分离
3. **模块挂载机制**
   - 模块注册到特定阶段
   - handler返回值控制流程
   - NGX_OK、NGX_DECLINED、NGX_AGAIN的含义
**实践任务**：
- [ ] 阅读 `src/http/ngx_http_core_module.c`
   - `ngx_http_core_run_phases()`：阶段执行引擎
   - `ngx_http_core_generic_phase()`：通用阶段检查器
- [ ] 分析一个模块如何挂载到特定阶段
   - 阅读 `src/http/modules/ngx_http_static_module.c`
   - 理解CONTENT_PHASE的处理
- [ ] 理解handler返回值对流程的影响
**重点文件**：
- `src/http/ngx_http_core_module.c`
- `src/http/ngx_http_core_module.h`
- `src/http/modules/ngx_http_static_module.c`
**代码规范观察点**：
- 阶段处理函数的返回值约定
- 模块注册的方式
- 配置指令的解析
---
### Day 7：模块系统设计（6小时）
#### 上午（3小时）：模块结构定义
**目标**：理解Nginx模块系统的设计
**学习内容**：
1. **模块数据结构**
   ```c
   struct ngx_module_s {
       ngx_uint_t            ctx_index;    // 模块上下文索引
       ngx_uint_t            index;        // 模块索引
       char                 *name;         // 模块名称
       void                 *ctx;          // 模块上下文
       ngx_command_t        *commands;     // 配置指令数组
       ngx_uint_t            type;         // 模块类型
       // 生命周期钩子函数
   };
   ```
2. **模块类型**
   - NGX_CORE_MODULE：核心模块
   - NGX_EVENT_MODULE：事件模块
   - NGX_HTTP_MODULE：HTTP模块
   - NGX_STREAM_MODULE：流模块
3. **模块上下文**
   - HTTP模块上下文：`ngx_http_module_t`
   - 配置创建和合并钩子
   - 模块初始化钩子
**实践任务**：
- [ ] 阅读 `src/core/ngx_module.h`，理解模块结构
- [ ] 阅读 `src/core/nginx.c:198-211`，分析核心模块定义
   ```c
   ngx_module_t  ngx_core_module = {
       NGX_MODULE_V1,
       &ngx_core_module_ctx,
       ngx_core_commands,
       NGX_CORE_MODULE,
       // ...
   };
   ```
- [ ] 阅读 `src/http/ngx_http_config.h`，理解HTTP模块上下文
- [ ] 分析一个简单模块的实现（如静态文件模块）
**重点文件**：
- `src/core/ngx_module.h`
- `src/core/nginx.c` (ngx_core_module定义)
- `src/http/ngx_http_config.h`
- `src/http/modules/ngx_http_static_module.c`
#### 下午（3小时）：配置系统
**目标**：理解Nginx的配置解析机制
**学习内容**：
1. **配置指令定义**
   ```c
   struct ngx_command_s {
       ngx_str_t             name;        // 指令名称
       ngx_uint_t            type;        // 指令类型和作用域
       char               *(*set)(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
       ngx_uint_t            conf;        // 配置偏移量
       ngx_uint_t            offset;      // 字段偏移量
       void                 *post;        // 后处理函数
   };
   ```
2. **配置解析流程**
   - 配置文件读取
   - 指令匹配
   - set函数调用
   - 配置存储
3. **配置继承机制**
   - main配置
   - server配置（继承main）
   - location配置（继承server）
   - merge函数实现配置合并
**实践任务**：
- [ ] 阅读 `src/core/ngx_conf_file.c`
   - `ngx_conf_parse()`：配置文件解析
   - `ngx_conf_read_token()`：读取配置令牌
- [ ] 阅读 `src/core/ngx_string.c`，理解字符串处理
- [ ] 分析一个配置指令的实现
   - 阅读 `src/http/modules/ngx_http_core_module.c`
   - 找出一个简单的指令（如root、index）
   - 理解set函数的实现
**重点文件**：
- `src/core/ngx_conf_file.h`
- `src/core/ngx_conf_file.c`
- `src/http/modules/ngx_http_core_module.c`
**代码规范观察点**：
- 配置指令的命名约定
- set函数的返回值处理
- 错误消息的输出方式
---
### Day 8：HTTP核心模块分析（6小时）
#### 上午（3小时）：静态文件服务
**目标**：深入理解静态文件服务的实现
**学习内容**：
1. **静态文件处理流程**
   ```
   NGX_HTTP_CONTENT_PHASE
   └── ngx_http_static_handler()
       ├── ngx_http_map_uri_to_path()  # URI映射到文件路径
       ├── ngx_open_file()             # 打开文件
       ├── ngx_http_set_content_type() # 设置Content-Type
       └── ngx_http_output_filter()    # 输出过滤器
   ```
2. **文件I/O优化**
   - sendfile零拷贝
   - 文件打开缓存
   - 目录索引生成
3. **MIME类型处理**
   - MIME类型映射
   - Content-Type设置
   - 字符集处理
**实践任务**：
- [ ] 阅读 `src/http/modules/ngx_http_static_module.c`
   - `ngx_http_static_handler()`：静态文件处理器
   - 理解文件路径映射
   - 分析sendfile的使用
- [ ] 阅读 `src/http/ngx_http_core_module.c`
   - `ngx_http_set_content_type()`：设置Content-Type
   - MIME类型查找
- [ ] 分析文件打开缓存机制
**重点文件**：
- `src/http/modules/ngx_http_static_module.c`
- `src/http/ngx_http_core_module.c`
- `src/core/ngx_file.h`
#### 下午（3小时）：反向代理模块
**目标**：理解反向代理的实现机制
**学习内容**：
1. **反向代理流程**
   ```
   NGX_HTTP_CONTENT_PHASE
   └── ngx_http_proxy_handler()
       ├── ngx_http_upstream_create()  # 创建upstream
       ├── ngx_http_upstream_init()    # 初始化upstream
       └── ngx_http_upstream_connect() # 连接后端服务器
   ```
2. **upstream机制**
   - 后端服务器选择
   - 负载均衡算法
   - 健康检查
   - 失败重试
3. **请求转发**
   - 请求头转发
   - 请求体转发
   - 响应接收
   - 响应转发
**实践任务**：
- [ ] 阅读 `src/http/modules/ngx_http_proxy_module.c`
   - `ngx_http_proxy_handler()`：代理处理器
   - 理解请求转发逻辑
- [ ] 阅读 `src/http/ngx_http_upstream.c`
   - `ngx_http_upstream_init()`：初始化upstream
   - 理解负载均衡机制
- [ ] 分析upstream的健康检查实现
**重点文件**：
- `src/http/modules/ngx_http_proxy_module.c`
- `src/http/ngx_http_upstream.c`
- `src/http/ngx_http_upstream.h`
**代码规范观察点**：
- 异步操作的处理方式
- 错误处理的完整性
- 资源清理的时机
---
### Day 9：过滤器系统（6小时）
#### 上午（3小时）：过滤器链
**目标**：理解Nginx的过滤器机制
**学习内容**：
1. **过滤器类型**
   - Header过滤器：处理响应头
   - Body过滤器：处理响应体
   - 过滤器链：多个过滤器串联
2. **过滤器注册**
   - 过滤器在模块初始化时注册
   - 过滤器按优先级排序
   - 过滤器链的构建
3. **过滤器执行**
   - 过滤器链遍历
   - 缓冲区的传递
   - 最后一个缓冲区的处理
**实践任务**：
- [ ] 阅读 `src/http/ngx_http_core_module.c`
   - `ngx_http_output_filter()`：输出过滤器
   - 理解过滤器链的遍历
- [ ] 阅读 `src/http/modules/ngx_http_chunked_filter_module.c`
   - 理解chunked编码过滤器
   - 分析缓冲区的处理
- [ ] 分析gzip过滤器的实现
**重点文件**：
- `src/http/ngx_http_core_module.c`
- `src/http/modules/ngx_http_chunked_filter_module.c`
- `src/http/modules/ngx_http_gzip_filter_module.c`
#### 下午（3小时）：缓冲区管理
**目标**：理解缓冲区链的设计
**学习内容**：
1. **缓冲区结构**
   ```c
   struct ngx_buf_s {
       u_char          *pos;       // 数据起始位置
       u_char          *last;      // 数据结束位置
       off_t            file_pos;  // 文件位置
       off_t            file_last; // 文件结束位置
       u_char          *start;     // 缓冲区起始
       u_char          *end;       // 缓冲区结束
       ngx_buf_tag_t    tag;       // 缓冲区标签
       ngx_file_t      *file;      // 文件对象
   };
   ```
2. **缓冲区链**
   - `ngx_chain_t`：缓冲区链节点
   - 链式传递：避免大内存拷贝
   - 最后一个缓冲区标记
3. **缓冲区操作**
   - 缓冲区创建
   - 缓冲区链合并
   - 缓冲区链发送
**实践任务**：
- [ ] 阅读 `src/core/ngx_buf.h`，理解缓冲区结构
- [ ] 阅读 `src/core/ngx_output_chain.c`
   - 理解缓冲区链的处理
   - 分析文件缓冲区的处理
- [ ] 分析write_filter的实现
   - 阅读 `src/http/modules/ngx_http_write_filter_module.c`
**重点文件**：
- `src/core/ngx_buf.h`
- `src/core/ngx_output_chain.c`
- `src/http/modules/ngx_http_write_filter_module.c`
**代码规范观察点**：
- 缓冲区管理的错误处理
- 内存对齐的考虑
- 缓冲区链的操作方式
---
### Day 10：变量系统与子请求（6小时）
#### 上午（3小时）：变量系统
**目标**：理解Nginx的变量机制
**学习内容**：
1. **变量定义**
   - 内置变量（$uri、$args等）
   - 自定义变量
   - 变量索引机制
2. **变量求值**
   - 延迟求值（Lazy Evaluation）
   - 变量缓存
   - 变量失效机制
3. **变量访问**
   - 通过索引访问变量
   - 变量值的获取
   - 变量值的设置
**实践任务**：
- [ ] 阅读 `src/http/ngx_http_variables.c`
   - `ngx_http_add_variable()`：添加变量
   - `ngx_http_get_variable_index()`：获取变量索引
   - `ngx_http_get_indexed_variable()`：获取变量值
- [ ] 分析内置变量的定义
   - 阅读 `src/http/modules/ngx_http_core_module.c`
   - 找出几个内置变量的实现
- [ ] 理解变量缓存机制
**重点文件**：
- `src/http/ngx_http_variables.h`
- `src/http/ngx_http_variables.c`
- `src/http/modules/ngx_http_core_module.c`
#### 下午（3小时）：子请求机制
**目标**：理解子请求的实现和应用
**学习内容**：
1. **子请求概念**
   - 主请求和子请求
   - 子请求的处理流程
   - 子请求的响应处理
2. **子请求实现**
   - `ngx_http_subrequest()`：创建子请求
   - 子请求的11阶段处理
   - 子请求的响应回调
3. **子请求应用**
   - SSI（Server-Side Includes）
   - 内容聚合
   - 内部重定向
**实践任务**：
- [ ] 阅读 `src/http/ngx_http_request.c`
   - `ngx_http_subrequest()`：创建子请求
   - 理解子请求的处理流程
- [ ] 阅读 `src/http/modules/ngx_http_addition_filter_module.c`
   - 理解子请求在过滤器中的应用
- [ ] 分析SSI模块的实现
**重点文件**：
- `src/http/ngx_http_request.c`
- `src/http/modules/ngx_http_addition_filter_module.c`
- `src/http/modules/ngx_http_ssi_filter_module.c`
**代码规范观察点**：
- 异步回调的处理
- 资源管理的复杂性
- 错误处理的完整性
---
## 实践项目阶段（Day 11-14）
### Day 11-12：开发简单模块（12小时）
#### 项目目标
开发一个简单的Nginx模块，实现以下功能：
- 添加一个配置指令 `hello_world`
- 在CONTENT_PHASE阶段处理请求
- 返回简单的响应内容
#### 开发步骤
**Step 1：创建模块文件结构**
```
src/http/modules/ngx_http_hello_module.c
src/http/modules/ngx_http_hello_module.h (可选)
```
**Step 2：定义模块结构**
```c
// 模块上下文
static ngx_http_module_t ngx_http_hello_module_ctx = {
    NULL,                                  // preconfiguration
    NULL,                                  // postconfiguration
    NULL,                                  // create_main_conf
    NULL,                                  // init_main_conf
    NULL,                                  // create_srv_conf
    NULL,                                  // merge_srv_conf
    ngx_http_hello_create_loc_conf,        // create_loc_conf
    ngx_http_hello_merge_loc_conf          // merge_loc_conf
};
// 模块定义
ngx_module_t ngx_http_hello_module = {
    NGX_MODULE_V1,
    &ngx_http_hello_module_ctx,
    ngx_http_hello_commands,
    NGX_HTTP_MODULE,
    NULL,                                  // init_master
    NULL,                                  // init_module
    NULL,                                  // init_process
    NULL,                                  // init_thread
    NULL,                                  // exit_thread
    NULL,                                  // exit_process
    NULL,                                  // exit_master
    NGX_MODULE_V1_PADDING
};
```
**Step 3：定义配置指令**
```c
static ngx_command_t ngx_http_hello_commands[] = {
    {
        ngx_string("hello_world"),
        NGX_HTTP_LOC_CONF | NGX_CONF_FLAG,
        ngx_conf_set_flag_slot,
        NGX_HTTP_LOC_CONF_OFFSET,
        offsetof(ngx_http_hello_loc_conf_t, enable),
        NULL
    },
    ngx_null_command
};
```
**Step 4：实现配置结构**
```c
typedef struct {
    ngx_flag_t enable;
} ngx_http_hello_loc_conf_t;
static void *
ngx_http_hello_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_hello_loc_conf_t *conf;
    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_hello_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }
    conf->enable = NGX_CONF_UNSET;
    return conf;
}
static char *
ngx_http_hello_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_hello_loc_conf_t *prev = parent;
    ngx_http_hello_loc_conf_t *conf = child;
    ngx_conf_merge_flag_value(conf->enable, prev->enable, 0);
    return NGX_CONF_OK;
}
```
**Step 5：实现请求处理函数**
```c
static ngx_int_t
ngx_http_hello_handler(ngx_http_request_t *r)
{
    ngx_http_hello_loc_conf_t *hlcf;
    ngx_buf_t *b;
    ngx_chain_t out;
    hlcf = ngx_http_get_module_loc_conf(r, ngx_http_hello_module);
    if (!hlcf->enable) {
        return NGX_DECLINED;
    }
    // 设置响应头
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_type_len = sizeof("text/plain") - 1;
    ngx_str_set(&r->headers_out.content_type, "text/plain");
    // 创建响应体
    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }
    b->pos = (u_char *) "Hello, World!";
    b->last = b->pos + sizeof("Hello, World!") - 1;
    b->memory = 1;
    b->last_buf = 1;
    out.buf = b;
    out.next = NULL;
    r->headers_out.content_length_n = b->last - b->pos;
    // 发送响应头
    ngx_http_send_header(r);
    // 发送响应体
    return ngx_http_output_filter(r, &out);
}
```
**Step 6：注册处理函数**
```c
static ngx_int_t
ngx_http_hello_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt *h;
    ngx_http_core_main_conf_t *cmcf;
    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);
    h = ngx_array_push(&cmcf->phases[NGX_HTTP_CONTENT_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }
    *h = ngx_http_hello_handler;
    return NGX_OK;
}
```
**Step 7：编译模块**
```bash
# 修改auto/modules文件，添加模块
# 或者在configure时添加
./configure --add-module=/path/to/hello_module
make
```
**Step 8：配置和测试**
```nginx
location /hello {
    hello_world on;
}
```
#### 实践任务清单
- [ ] 完成模块的基本框架
- [ ] 实现配置指令
- [ ] 实现请求处理函数
- [ ] 编译并测试模块
- [ ] 添加日志输出
- [ ] 处理错误情况
- [ ] 优化代码风格，符合Nginx规范
#### 参考资源
- `src/http/modules/ngx_http_echo_module.c`（如果可用）
- `src/http/modules/ngx_http_static_module.c`
- Nginx模块开发指南
---
### Day 13：代码重构与优化（6小时）
#### 任务目标
1. **代码审查**：检查自己编写的模块代码
2. **规范检查**：确保符合Nginx代码规范
3. **性能优化**：优化模块的性能
4. **错误处理**：完善错误处理机制
#### 代码规范检查清单
- [ ] 缩进：4空格，无TAB
- [ ] 行宽：不超过80字符
- [ ] 命名：全局符号使用`ngx_`前缀
- [ ] 类型：使用`_t`后缀
- [ ] 注释：使用C风格注释
- [ ] 函数：返回值检查完整
- [ ] 错误处理：统一的错误处理模式
- [ ] 内存管理：使用内存池，避免泄漏
#### 性能优化点
- [ ] 减少内存分配
- [ ] 使用内存池
- [ ] 避免不必要的拷贝
- [ ] 优化字符串操作
- [ ] 减少系统调用
#### 实践任务
- [ ] 重构模块代码
- [ ] 添加详细的注释
- [ ] 完善错误处理
- [ ] 性能测试和优化
- [ ] 代码审查和总结
---
### Day 14：总结与进阶学习（6小时）
#### 上午（3小时）：知识总结
**任务**：
1. **绘制架构图**
   - Master-Worker进程模型
   - 事件驱动架构
   - HTTP请求处理流程
   - 模块系统架构
2. **整理学习笔记**
   - 核心概念总结
   - 关键数据结构
   - 重要函数调用链
   - 性能优化技巧
3. **代码规范总结**
   - 命名约定
   - 代码格式
   - 注释规范
   - 错误处理模式
#### 下午（3小时）：进阶学习方向
**推荐学习路径**：
1. **深入理解特定模块**
   - 反向代理模块（upstream）
   - 缓存模块
   - SSL/TLS模块
   - 限流模块
2. **性能优化技术**
   - 零拷贝技术深入
   - 内存管理优化
   - 系统调用优化
   - 并发模型优化
3. **高级特性**
   - HTTP/2支持
   - HTTP/3（QUIC）支持
   - 流媒体代理
   - 邮件代理
4. **实践项目**
   - 开发自定义模块
   - 性能调优实践
   - 故障排查实践
   - 源码贡献
#### 学习资源推荐
1. **官方文档**
   - Nginx官方文档：https://nginx.org/en/docs/
   - 开发指南：https://nginx.org/en/docs/dev/development_guide.html
2. **源码阅读**
   - 核心模块源码
   - 第三方模块源码
   - 测试用例
3. **实践项目**
   - 开发简单模块
   - 性能测试
   - 故障排查
---
## 学习成果检查
### 知识掌握检查
- [ ] 能够解释Nginx的Master-Worker模型
- [ ] 能够说明事件驱动架构的工作原理
- [ ] 能够描述HTTP请求处理的11个阶段
- [ ] 能够理解内存池的设计和实现
- [ ] 能够解释零拷贝技术的原理
- [ ] 能够理解模块系统的设计
- [ ] 能够编写符合Nginx规范的代码
- [ ] 能够开发简单的Nginx模块
### 代码能力检查
- [ ] 能够阅读Nginx核心代码
- [ ] 能够理解关键数据结构的用途
- [ ] 能够分析函数调用链
- [ ] 能够编写符合规范的代码
- [ ] 能够开发功能模块
- [ ] 能够进行性能优化
- [ ] 能够处理错误情况
### 实践能力检查
- [ ] 能够编译和调试Nginx
- [ ] 能够开发自定义模块
- [ ] 能够进行性能测试
- [ ] 能够排查问题
- [ ] 能够优化性能
---
## 学习建议
### 每日学习建议
1. **上午**：阅读源码，理解设计
2. **下午**：实践操作，编写代码
3. **晚上**：总结笔记，整理思路
### 学习方法建议
1. **循序渐进**：从简单到复杂，从整体到细节
2. **理论与实践结合**：理解概念后立即实践
3. **多画图**：用图表帮助理解复杂流程
4. **多调试**：使用gdb调试，观察实际运行
5. **多总结**：每天总结学习内容，形成知识体系
### 遇到困难时的建议
1. **查阅文档**：先看官方文档和注释
2. **阅读测试用例**：测试用例是最好的文档
3. **调试代码**：使用gdb跟踪执行流程
4. **请教他人**：在社区提问或讨论
5. **循序渐进**：不要急于求成，扎实基础
### 持续学习建议
1. **定期复习**：每周复习一次学习内容
2. **实践项目**：通过项目巩固知识
3. **阅读源码**：持续阅读新的模块源码
4. **参与社区**：参与Nginx社区讨论
5. **贡献代码**：尝试贡献代码到Nginx项目
---
## 附录：重点文件索引
### 核心文件
- `src/core/nginx.c`：main函数，启动流程
- `src/core/ngx_cycle.c`：cycle管理
- `src/core/ngx_palloc.c`：内存池实现
- `src/core/ngx_conf_file.c`：配置解析
- `src/core/ngx_module.h`：模块定义
### 事件模块
- `src/event/ngx_event.c`：事件处理
- `src/event/ngx_event_accept.c`：连接接受
- `src/event/modules/ngx_epoll_module.c`：epoll实现
### HTTP模块
- `src/http/ngx_http_request.c`：请求处理
- `src/http/ngx_http_core_module.c`：HTTP核心模块
- `src/http/ngx_http_parse.c`：请求解析
- `src/http/modules/ngx_http_static_module.c`：静态文件模块
- `src/http/modules/ngx_http_proxy_module.c`：反向代理模块
### 工具文件
- `src/core/ngx_string.c`：字符串处理
- `src/core/ngx_hash.c`：哈希表
- `src/core/ngx_rbtree.c`：红黑树
- `src/core/ngx_array.c`：动态数组
---
**祝学习顺利！**