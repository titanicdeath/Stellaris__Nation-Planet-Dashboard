#pragma once

#include "config.hpp"

std::vector<fs::path> discover_saves(const Settings& st);
std::string load_gamestate_for_save(const Settings& st, const fs::path& save_path);
std::string extract_gamestate_from_sav(const fs::path& sav_path);
