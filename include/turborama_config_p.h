/*
 turborama_mount (Multi-source ROMs mounting utility for Turborama) is licensed under [**GPL3**](https://gnu.org/licenses/gpl.html)

 Copyright (C) 2022 Guoxin "7Ji" Pu (pugokushin@gmail.com)

 This program is free software: you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, either version * of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

 You should have received a copy of the GNU General Public License along with this program. If not, see <https://www.gnu.org/licenses/>.

*/
#include "turborama_config.h"

#include <string.h>
#include <limits.h>

#include "logging.h"
#include "alloc.h"
#include "util.h"

#define TURBORAMA_CONFIG_DIR       "/storage/.config/turborama/configs"
#define TURBORAMA_CONFIG_FILE      TURBORAMA_CONFIG_DIR "/turborama.conf"

#define TURBORAMA_CONFIG_GLOBAL_PREFIX  "global"
static const char turborama_config_global_prefix[] = TURBORAMA_CONFIG_GLOBAL_PREFIX;
static const size_t len_turborama_config_global_prefix = strlen(TURBORAMA_CONFIG_GLOBAL_PREFIX);
static const size_t offset_turborama_config_global_setting = len_turborama_config_global_prefix + 1;