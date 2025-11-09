# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## About NGINX

NGINX is a high-performance web server, reverse proxy, load balancer, and API gateway written in C. The codebase uses a custom build system based on shell scripts (`auto/configure`) rather than traditional build tools like CMake or autotools.

## Building NGINX

### Prerequisites

On Ubuntu/Debian-based systems, install required dependencies:

```bash
sudo apt update
sudo apt install gcc make libpcre3-dev zlib1g-dev
```

For SSL/TLS support (highly recommended):

```bash
sudo apt install libssl-dev
```

### Configure and Build

NGINX uses a custom configure script located in `auto/configure`:

```bash
# Basic build with defaults
auto/configure

# Build with SSL support
auto/configure --with-http_ssl_module

# Build with HTTP/2 support
auto/configure --with-http_v2_module --with-http_ssl_module

# Build with HTTP/3 (QUIC) support
auto/configure --with-http_v3_module --with-http_ssl_module

# View all available configuration options
auto/configure --help

# Common development build with debug symbols
auto/configure --with-debug --with-http_ssl_module --with-http_v2_module
```

After configuring, compile the binary:

```bash
make
```

The compiled binary will be in `objs/nginx`.

### Installation

To install the compiled binary to `/usr/local/nginx/`:

```bash
sudo make install
```

### Running and Testing

Run the installed binary:

```bash
sudo /usr/local/nginx/sbin/nginx
```

Test it works:

```bash
curl localhost
```

Control the running instance:

```bash
# Stop
sudo /usr/local/nginx/sbin/nginx -s stop

# Reload configuration
sudo /usr/local/nginx/sbin/nginx -s reload

# Test configuration
sudo /usr/local/nginx/sbin/nginx -t
```

## Testing

NGINX tests are maintained in a separate repository:

```bash
git clone https://github.com/nginx/nginx-tests.git
```

Run tests after building NGINX (refer to the nginx-tests repository documentation for detailed instructions).

## Architecture Overview

### Source Code Organization

```
src/
├── core/           # Core functionality: event loop, memory pools, data structures
├── event/          # Event handling mechanisms (epoll, kqueue, select, etc.)
│   ├── modules/    # Event method implementations
│   └── quic/       # QUIC/HTTP3 protocol implementation
├── http/           # HTTP module implementation
│   └── modules/    # HTTP feature modules (SSL, gzip, proxy, auth, etc.)
├── mail/           # Mail proxy module (IMAP, POP3, SMTP)
├── stream/         # Stream (TCP/UDP) proxy module
├── os/             # OS-specific implementations
└── misc/           # Miscellaneous utilities
```

### Key Architecture Concepts

**Master-Worker Process Model**: NGINX runs as a master process that manages multiple worker processes. The master reads and validates configuration, while workers handle actual requests. Worker process count is typically set to match CPU cores.

**Event-Driven Architecture**: NGINX uses non-blocking I/O with platform-specific event notification mechanisms (epoll on Linux, kqueue on BSD, etc.) implemented in `src/event/`. This allows a single worker to handle thousands of concurrent connections efficiently.

**Modular Design**: NGINX is built from modules that can be compiled statically (at build time) or dynamically (loaded at runtime). Modules are configured via `auto/configure` flags and defined in `auto/modules`.

**Memory Management**: Custom memory pool allocator (`src/core/ngx_palloc.*`) provides efficient request-scoped memory allocation without fragmentation. Memory is allocated from pools and freed in bulk when the request completes.

**Configuration System**: Configuration parsing is handled by `src/core/ngx_conf_file.*`. Each module defines configuration directives via `ngx_command_t` structures that specify parsing handlers and validation.

### Core Data Structures

- **ngx_cycle_t**: Global server state and configuration lifecycle
- **ngx_connection_t**: Represents a network connection (`src/core/ngx_connection.*`)
- **ngx_event_t**: Event object for I/O operations
- **ngx_pool_t**: Memory pool for efficient allocation
- **ngx_buf_t / ngx_chain_t**: Buffer and buffer chain for data handling
- **ngx_http_request_t**: HTTP request state (`src/http/ngx_http_request.h`)
- **ngx_module_t**: Module definition and interface

### Module Types

1. **Core modules** (`src/core/`): Basic building blocks (events, memory, config)
2. **Event modules** (`src/event/modules/`): Platform-specific event implementations
3. **HTTP modules** (`src/http/modules/`): HTTP-specific features (proxy, SSL, gzip, auth, etc.)
4. **Mail modules** (`src/mail/`): Mail proxy functionality
5. **Stream modules** (`src/stream/`): TCP/UDP load balancing

### Build System

The `auto/` directory contains the custom build system:

- `auto/configure`: Main configuration script (entry point)
- `auto/options`: Parses command-line build options
- `auto/modules`: Defines which modules to compile
- `auto/cc/`: Compiler detection and configuration
- `auto/os/`: OS-specific feature detection
- `auto/lib/`: External library detection (PCRE, zlib, OpenSSL)
- `auto/make`: Generates the Makefile

Configuration results are written to `objs/` directory including the generated Makefile.

## Code Style

NGINX follows strict code style conventions documented at https://nginx.org/en/docs/dev/development_guide.html#code_style

Key points:
- 4-space indentation (not tabs)
- Braces on same line for functions and control structures
- Naming: `ngx_` prefix for global symbols, `ngx_http_` for HTTP module symbols
- Maximum line length: 80 characters preferred
- C89 compatibility (no C99/C11 features like `//` comments)

## Commit Message Format

When making commits:
- Single-line subject (max 67 chars) with module prefix (e.g., "HTTP:", "Core:", "Stream:")
- Blank line, then verbose description (max 76 chars per line)
- Reference GitHub issues in subject line when applicable
- Example prefixes: "Core:", "HTTP:", "Stream:", "Mail:", "SSL:", "QUIC:", "Upstream:"

## Platform Support

NGINX supports a wide range of platforms. See https://nginx.org/en/#tested_os_and_platforms for the full list. The codebase includes platform-specific code in `src/os/` for Linux, FreeBSD, Windows, etc.

## Development Guide

For detailed information on NGINX internals, module development, and architecture, refer to:
- https://nginx.org/en/docs/dev/development_guide.html
- https://nginx.org/en/docs/ (main documentation)

## Code Explanation and Documentation Guidelines

When explaining code functionality, follow these principles:

### Role and Perspective

Act as a senior distributed backend engineer with expertise in:
- C/C++ language features and coding style
- Large-scale backend system design
- High-concurrency systems
- Microservices and distributed architectures
- Thinking like Linus Torvalds: direct, concise, technically-focused responses

### Code Commenting Standards

When asked to explain code functionality:

1. **Add detailed Chinese comments** to all code segments being explained
2. **Function header comments** must include:
   - Function purpose and behavior
   - Parameter descriptions
   - Return value description
   - Side effects and error conditions
   - Usage examples when relevant

3. **Inline comments** should explain:
   - Non-obvious logic and algorithms
   - Design decisions and trade-offs
   - Interactions with other modules or data structures
   - Performance considerations

### Response Format

Responses must be:
- **Concise and elegant**: No filler words, encouragement, or conversational elements
- **Technically precise**: Focus solely on technical content
- **Structured**: Include the following sections:
  1. **Code explanation**: What the code does, line by line or section by section
  2. **Purpose**: The functional role in the overall system
  3. **Design advantages**: Why this design is effective (performance, maintainability, scalability)
  4. **Interactions**: Which modules, data structures, or subsystems interact with this code and how

### Example Structure

When explaining a function or code block:

```c
/**
 * 函数功能：简要描述函数的主要作用
 * 
 * 参数说明：
 *   @param param1: 参数1的说明
 *   @param param2: 参数2的说明
 * 
 * 返回值：
 *   @return: 返回值的含义和可能的值
 * 
 * 副作用：
 *   - 可能修改的状态
 *   - 可能产生的副作用
 * 
 * 交互模块：
 *   - 与哪些模块交互
 *   - 使用哪些核心数据结构
 * 
 * 设计优势：
 *   - 性能考虑
 *   - 可维护性
 *   - 扩展性
 */
```

### Technical Focus Areas

When analyzing NGINX code, pay special attention to:

1. **Concurrency Model**: How code handles multi-process/thread scenarios
2. **Memory Management**: Pool allocation, reference counting, cleanup mechanisms
3. **Event Loop Integration**: How code integrates with the event-driven architecture
4. **Module System**: Module initialization, configuration parsing, lifecycle management
5. **Performance**: Lock-free algorithms, zero-copy techniques, cache-friendly data structures
6. **Error Handling**: Error propagation, resource cleanup, failure recovery

### Interaction Analysis

When explaining code interactions, identify:

- **Data structures**: Which `ngx_*` structures are used (e.g., `ngx_cycle_t`, `ngx_connection_t`, `ngx_pool_t`)
- **Module dependencies**: Which modules depend on or are depended upon
- **Configuration system**: How configuration directives are parsed and applied
- **Event system**: How events are registered, handled, and cleaned up
- **Memory pools**: How memory is allocated and when it's freed
- **Process model**: Master process vs worker process behavior

### Code Style Consistency

Maintain NGINX code style:
- Use existing NGINX comment style (C-style `/* */` comments, not `//`)
- Follow existing naming conventions
- Match indentation and formatting of surrounding code
- Preserve existing code structure and organization

### Output Format Requirements

**CRITICAL**: All generated code and markdown documentation must follow these strict formatting rules:

1. **No Empty Lines**: Generated code blocks and markdown content MUST NOT contain any empty lines. All lines must contain actual content (code, comments, or text).
2. **Code Blocks**: Every line in code blocks must have content - no blank lines between function definitions, no blank lines between statements (unless absolutely necessary for syntax), no blank lines in markdown sections.
3. **Markdown Files**: Markdown documents must not have empty lines between sections, paragraphs, or list items. Use continuous text flow.
4. **Exceptions**: Only allow empty lines when they are:
   - Required by language syntax (e.g., between function definitions in Python class)
   - Required for code readability in specific contexts (e.g., separating logical blocks in complex algorithms)
   - Explicitly requested by the user

5. **Content Richness and Line Length**: All generated content must be comprehensive and detailed:
   - **Minimum Line Length**: Each line must contain substantial content. Single lines with only a few words (less than 20 characters) are prohibited unless absolutely necessary for syntax or formatting.
   - **Rich Content**: Code comments, documentation, and explanations must be thorough and informative. Avoid brief, minimal descriptions.
   - **Detailed Explanations**: When explaining code or concepts, provide comprehensive details rather than brief summaries.
   - **Code Comments**: Comments should be descriptive and informative, explaining not just what the code does, but why and how it works.
   - **Markdown Content**: Paragraphs and sections should contain detailed information. Avoid single-sentence paragraphs unless they are part of a structured list.
   - **Examples**: When providing examples, include comprehensive code samples with detailed comments and explanations.
6. **Prohibited Patterns**: The following patterns are strictly prohibited:
   - Single-word lines (except in specific contexts like dictionary keys or configuration values)
   - Lines with only punctuation or symbols
   - Brief one-line explanations that could be expanded
   - Minimal comments like "// init" or "/* variable */" - these must be expanded with detailed descriptions
   - Short function descriptions - always include parameter details, return values, and behavior explanations

**Enforcement**: When generating any code or markdown content, ensure that:
- All code files have no empty lines
- All markdown documentation has no empty lines
- All lines contain substantial content (minimum 20 characters per line, unless syntax requires otherwise)
- All comments and documentation are comprehensive and detailed
- All explanations provide thorough context and information
- All output follows these rules unless explicitly overridden by user request
