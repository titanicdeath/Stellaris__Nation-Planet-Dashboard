#pragma once

#include "country_export_helpers.hpp"

void write_market(JsonWriter& j,
                  const PdxValue* market,
                  const std::string& country_id,
                  MarketExportStats& stats);
