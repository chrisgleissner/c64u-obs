/*
C64U Plugin for OBS
Copyright (C) 2025 Chris Gleissner

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include "plugin-support.h"
#include "c64u-network.h" // Include network header first to avoid Windows header conflicts
#include "c64u-logging.h"
#include "c64u-protocol.h"
#include "c64u-source.h"
#include "c64u-version.h"

// Logging control - define the global variable
bool c64u_debug_logging = true;

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en")

bool obs_module_load(void)
{
    C64U_LOG_INFO("Loading %s", c64u_get_version_string());
    C64U_LOG_INFO("Build info: %s", c64u_get_build_info());

    // DEBUG: This will always be hit when the plugin loads
    // Module loading

    struct obs_source_info c64u_info = {.id = "c64u_source",
                                        .type = OBS_SOURCE_TYPE_INPUT,
                                        .output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_AUDIO,
                                        .get_name = c64u_get_name,
                                        .create = c64u_create,
                                        .destroy = c64u_destroy,
                                        .update = c64u_update,
                                        .get_defaults = c64u_defaults,
                                        .video_render = c64u_render,
                                        .get_properties = c64u_properties,
                                        .get_width = c64u_get_width,
                                        .get_height = c64u_get_height};

    obs_register_source(&c64u_info);
    C64U_LOG_INFO("C64U plugin loaded successfully");
    return true;
}

void obs_module_unload(void)
{
    C64U_LOG_INFO("Unloading C64U plugin");
    c64u_cleanup_networking();
}
