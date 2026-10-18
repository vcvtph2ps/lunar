#pragma once

#include <common/init.h>
#include <stdbool.h>

void cmdline_parse(const char* cmdline);

[[nodiscard]] bool cmdline_get_stop_at_stage(init_stage_t* stage);
