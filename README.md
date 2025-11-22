# esp-settings

Settings component for ESP-IDF providing a small, structured settings
subsystem with support for typed settings, default values, and
persistence to NVS. It includes helpers for printing and HTTP-based
exposure of settings via the ESP HTTP server.

**Features**
- Typed settings: boolean, integer, one-of (options), text, color, and
	optional date/time and timezone types (config guarded).
- Default value helpers and pack-level initialization.
- Read/write/erase persistence using ESP NVS.
- Simple HTTP handler integration to expose/update settings over HTTP.
- Extensible handler registration for custom processing on load/apply.

**Quick start**

- Add this component to your ESP-IDF project (as a component directory).
- Enable any optional features via your project's `sdkconfig` (e.g.
	`CONFIG_SETTINGS_DATETIME_SUPPORT`).
- Include headers in your code:

```
#include "settings-defs.h"
#include "settings.h"
```

**Basic usage**

- Initialize defaults for a settings pack:

```
settings_pack_set_defaults(my_settings_pack);
```

- Read persisted values from NVS (overwrites in-memory values):

```
settings_nvs_read(my_settings_pack);
```

- Persist current values to NVS:

```
settings_nvs_write(my_settings_pack);
```

- Erase persisted settings (use with care):

```
settings_nvs_erase();
```

- Register a handler to be called by the settings subsystem:

```
void my_handler(const settings_group_t *grp, void *arg) { /* ... */ }
settings_handler_register(my_handler, NULL);
```

- Serve settings over HTTP by registering `settings_httpd_handler`
	with the ESP HTTP server (see ESP HTTPD docs for handler registration).

**Configuration**

Optional features are controlled by Kconfig options (configured in
`sdkconfig`). Examples:

- `CONFIG_SETTINGS_DATETIME_SUPPORT` — enable time/date/datetime types
- `CONFIG_SETTINGS_TIMEZONE_SUPPORT` — enable timezone text type
- `CONFIG_SETTINGS_COLOR_SUPPORT` — enable color type


