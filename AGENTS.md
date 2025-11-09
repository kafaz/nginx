# Repository Guidelines

## Project Structure & Module Organization
Core runtime lives under `src/`, split into `core/`, `event/`, `http/`, and `stream/` modules; add new logic beside the module you extend. Build scripts and feature toggles stay in `auto/` (custom flags belong in `auto/options`). Default configs, MIME maps, and system integration assets live in `conf/` and `contrib/`, while long-form docs and schemas live in `docs/`. Generated objects and binaries land in `objs/`; keep the tree clean by ignoring that directory in commits.

## Build, Test, and Development Commands
- `auto/configure --with-http_ssl_module --with-stream=dynamic` — prepare a Makefile with the modules you need; rerun after touching `auto/` logic.
- `make -j$(nproc)` — compile the selected modules; the nginx binary will appear in `objs/nginx`.
- `sudo make install PREFIX=/usr/local/nginx` — stage artifacts for packaging or local installs.
- `objs/nginx -t -c conf/nginx.conf` — lint configuration changes before running.

## Coding Style & Naming Conventions
Follow the [NGINX development guide](https://nginx.org/en/docs/dev/development_guide.html) style: four spaces, no tabs, 80-column soft limit, and brace placement that matches surrounding files. Functions use the `ngx_*` prefix scoped to their subsystem (e.g., `ngx_http_`), macros remain upper-case (`NGX_*`), and configuration directives stay lowercase with words separated by underscores. Mirror nearby files for includes, ordering, and error-handling macros.

## Testing Guidelines
Clone https://github.com/nginx/nginx-tests alongside this repo, export `TEST_NGINX_BINARY=$(pwd)/objs/nginx`, and run `prove -v ./nginx-tests` to execute the Perl-based suite. Add focused `.t` cases for every new directive, code path, and regression. Document any platform prerequisites inside the test header comments and keep fixtures under the test's `t/` directory.

## Commit & Pull Request Guidelines
Keep commits small, rebased, and logically grouped. Subjects are limited to 67 characters, bodies to 76-character lines, and should carry a subsystem prefix such as `Core:`, `HTTP:`, or `Stream:` followed by an issue reference when applicable. Pull requests must outline the motivation, build/test matrix, configuration snippets, and any perf or interoperability notes that reviewers need.

## Security & Configuration Tips
Report vulnerabilities via the private workflow described in `SECURITY.md`, and never attach raw core dumps or secrets to issues. Use `conf/nginx.conf` as a safe baseline, rotate TLS assets outside the repo, and scrub access/error logs before sharing. When in doubt, coordinate with the security team before publishing configuration changes that alter default exposure.
