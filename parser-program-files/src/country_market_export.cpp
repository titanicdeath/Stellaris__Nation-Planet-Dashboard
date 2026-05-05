#include "country_export_helpers.hpp"

struct MarketResourceInfo {
    int index = 0;
    std::string resource;
    std::string category;
};


MarketResourceInfo market_resource_info(int index) {
    switch (index) {
        case 0: return {index, "energy", "basic"};
        case 1: return {index, "minerals", "basic"};
        case 2: return {index, "food", "basic"};
        case 9: return {index, "consumer_goods", "advanced"};
        case 10: return {index, "alloys", "advanced"};
        case 11: return {index, "volatile_motes", "strategic"};
        case 12: return {index, "exotic_gases", "strategic"};
        case 13: return {index, "rare_crystals", "strategic"};
        case 14: return {index, "sr_living_metal", "rare"};
        case 15: return {index, "sr_zro", "rare"};
        case 16: return {index, "sr_dark_matter", "rare"};
        default: return {index, "unknown_" + std::to_string(index), "unknown"};
    }
}

std::vector<double> numeric_array_values(const PdxValue* v) {
    std::vector<double> values;
    if (!v || v->kind != PdxValue::Kind::Container) return values;
    for (const auto& e : v->entries) {
        if (!e.key.empty()) continue;
        if (auto amount = scalar_double(e.value)) values.push_back(*amount);
    }
    return values;
}

std::map<std::string, double> market_named_resource_values(const PdxValue* resources) {
    std::map<std::string, double> out;
    if (!resources || resources->kind != PdxValue::Kind::Container) return out;
    for (const auto& e : resources->entries) {
        if (e.key.empty()) continue;
        if (auto amount = scalar_double(e.value)) out[e.key] = *amount;
    }
    return out;
}

void parse_market_activity_array(const PdxValue* market,
                                 const std::string& block_name,
                                 std::map<std::string, std::map<std::string, double>>& by_country,
                                 std::map<std::string, double>& totals,
                                 size_t& max_amount_length) {
    for (const PdxValue* block : children(market, block_name)) {
        const std::string country_id = scalar_or(child(block, "country"));
        if (country_id.empty()) continue;
        const PdxValue* amount = child(block, "amount");
        const std::vector<double> values = numeric_array_values(amount);
        max_amount_length = std::max(max_amount_length, values.size());
        std::map<std::string, double>& country_values = by_country[country_id];
        for (size_t i = 0; i < values.size(); ++i) {
            const std::string resource = market_resource_info(static_cast<int>(i)).resource;
            country_values[resource] += values[i];
            totals[resource] += values[i];
        }
    }
}

void write_market_number_map(JsonWriter& j, const std::map<std::string, double>& values) {
    j.begin_object();
    constexpr double epsilon = 0.000000001;
    for (const auto& [resource, amount] : values) {
        if (std::abs(amount) <= epsilon) continue;
        j.key(resource);
        j.raw_number(json_number(amount));
    }
    j.end_object();
}

void write_market(JsonWriter& j,
                  const PdxValue* market,
                  const std::string& country_id,
                  MarketExportStats& stats) {
    if (!market || market->kind != PdxValue::Kind::Container) return;

    const std::vector<double> fluctuations = numeric_array_values(child(market, "fluctuations"));
    const std::vector<double> galactic_resources = numeric_array_values(child(market, "galactic_market_resources"));

    std::map<std::string, std::map<std::string, double>> bought_by_country;
    std::map<std::string, std::map<std::string, double>> sold_by_country;
    std::map<std::string, double> total_bought;
    std::map<std::string, double> total_sold;
    size_t bought_length = 0;
    size_t sold_length = 0;
    parse_market_activity_array(market, "resources_bought", bought_by_country, total_bought, bought_length);
    parse_market_activity_array(market, "resources_sold", sold_by_country, total_sold, sold_length);

    std::map<std::string, std::map<std::string, double>> internal_by_country;
    for (const PdxValue* block : children(market, "internal_market_fluctuations")) {
        const std::string activity_country_id = scalar_or(child(block, "country"));
        if (activity_country_id.empty()) continue;
        internal_by_country[activity_country_id] = market_named_resource_values(child(block, "resources"));
    }

    size_t max_index_count = std::max(fluctuations.size(), galactic_resources.size());
    max_index_count = std::max(max_index_count, bought_length);
    max_index_count = std::max(max_index_count, sold_length);

    std::set<size_t> array_lengths;
    if (!fluctuations.empty()) array_lengths.insert(fluctuations.size());
    if (!galactic_resources.empty()) array_lengths.insert(galactic_resources.size());
    if (bought_length > 0) array_lengths.insert(bought_length);
    if (sold_length > 0) array_lengths.insert(sold_length);
    stats.market_array_length_warnings = array_lengths.size() > 1 ? array_lengths.size() - 1 : 0;

    std::vector<MarketResourceInfo> resource_order;
    for (size_t i = 0; i < max_index_count; ++i) resource_order.push_back(market_resource_info(static_cast<int>(i)));

    const std::map<std::string, double> player_bought =
        bought_by_country.count(country_id) ? bought_by_country[country_id] : std::map<std::string, double>{};
    const std::map<std::string, double> player_sold =
        sold_by_country.count(country_id) ? sold_by_country[country_id] : std::map<std::string, double>{};
    const std::map<std::string, double> player_internal =
        internal_by_country.count(country_id) ? internal_by_country[country_id] : std::map<std::string, double>{};

    std::map<std::string, double> total_net;
    for (const auto& [resource, amount] : total_bought) total_net[resource] += amount;
    for (const auto& [resource, amount] : total_sold) total_net[resource] -= amount;

    std::set<std::string> countries_with_activity;
    for (const auto& [id, _] : bought_by_country) countries_with_activity.insert(id);
    for (const auto& [id, _] : sold_by_country) countries_with_activity.insert(id);

    stats.market_resource_count = resource_order.size();
    stats.market_unknown_resource_indices = 0;
    for (const MarketResourceInfo& info : resource_order) {
        if (starts_with_ci(info.resource, "unknown_")) stats.market_unknown_resource_indices++;
    }

    j.key("market");
    j.begin_object();
    j.key("enabled"); j.value(pdx_truthy(child(market, "enabled")));
    j.key("galactic_market_country"); write_id(j, scalar_or(child(market, "country")));
    j.key("next_monthly_trade_item_id");
    if (const PdxValue* next_id = child(market, "next_monthly_trade_item_id")) json_scalar(j, scalar_or(next_id));
    else j.value(nullptr);

    j.key("resource_order");
    j.begin_array();
    for (const MarketResourceInfo& info : resource_order) j.value(info.resource);
    j.end_array();

    j.key("resources");
    j.begin_object();
    for (const MarketResourceInfo& info : resource_order) {
        const std::string& resource = info.resource;
        const double bought = player_bought.count(resource) ? player_bought.at(resource) : 0.0;
        const double sold = player_sold.count(resource) ? player_sold.at(resource) : 0.0;

        j.key(resource);
        j.begin_object();
        j.key("index"); j.raw_number(std::to_string(info.index));
        j.key("resource"); j.value(resource);
        j.key("category"); j.value(info.category);
        j.key("galactic_market_resource");
        if (static_cast<size_t>(info.index) < galactic_resources.size()) j.value(galactic_resources[info.index] != 0.0);
        else j.value(nullptr);
        j.key("global_fluctuation");
        if (static_cast<size_t>(info.index) < fluctuations.size()) j.raw_number(json_number(fluctuations[info.index]));
        else j.value(nullptr);
        j.key("player_bought"); j.raw_number(json_number(bought));
        j.key("player_sold"); j.raw_number(json_number(sold));
        j.key("player_net_bought"); j.raw_number(json_number(bought - sold));
        j.key("player_internal_fluctuation");
        if (player_internal.count(resource)) j.raw_number(json_number(player_internal.at(resource)));
        else j.value(nullptr);
        j.key("observed_buy_price"); j.value(nullptr);
        j.key("observed_sell_price"); j.value(nullptr);
        j.end_object();
    }
    j.end_object();

    j.key("player_market_activity");
    j.begin_object();
    j.key("country_id"); write_id(j, country_id);
    j.key("resources_bought"); write_market_number_map(j, player_bought);
    j.key("resources_sold"); write_market_number_map(j, player_sold);
    std::map<std::string, double> player_net;
    for (const auto& [resource, amount] : player_bought) player_net[resource] += amount;
    for (const auto& [resource, amount] : player_sold) player_net[resource] -= amount;
    j.key("net_bought"); write_market_number_map(j, player_net);
    j.key("internal_market_fluctuations"); write_market_number_map(j, player_internal);
    j.end_object();

    j.key("all_country_market_activity_summary");
    j.begin_object();
    j.key("total_bought_by_resource"); write_market_number_map(j, total_bought);
    j.key("total_sold_by_resource"); write_market_number_map(j, total_sold);
    j.key("net_bought_by_resource"); write_market_number_map(j, total_net);
    j.key("country_count_with_market_activity"); j.raw_number(std::to_string(countries_with_activity.size()));
    j.end_object();

    j.key("price_derivation_status");
    j.begin_object();
    j.key("available"); j.value(false);
    j.key("reason"); j.value("buy/sell prices are not directly emitted in save; derivation deferred");
    j.end_object();
    j.end_object();
}
