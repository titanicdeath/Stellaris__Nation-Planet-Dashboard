#pragma once

#include "config.hpp"

struct CliOverrides {
    std::vector<std::string> save_files;
    bool latest_save = false;
    bool include_autosaves = false;
    bool force_reparse = false;
};

void apply_cli_overrides(Settings& st, const CliOverrides& cli);
