#include "country_export_helpers.hpp"

void accumulate_balance_resources(const PdxValue* v, std::map<std::string, double>& totals) {
    if (!v || v->kind != PdxValue::Kind::Container) return;
    for (const auto& e : v->entries) {
        if (e.key.empty()) {
            accumulate_balance_resources(e.value, totals);
            continue;
        }
        if (auto amount = scalar_double(e.value)) {
            totals[e.key] += *amount;
        } else {
            accumulate_balance_resources(e.value, totals);
        }
    }
}

void write_budget_child_or_empty(JsonWriter& j, const PdxValue* current_month, const std::string& key) {
    j.key(key);
    if (const PdxValue* v = child(current_month, key)) write_pdx_as_schema_json(j, v, key);
    else { j.begin_object(); j.end_object(); }
}

void write_nat_finance_economy(JsonWriter& j, const PdxValue* country, const PdxValue* stored_resources) {
    const PdxValue* current_month = nested_child(country, {"budget", "current_month"});
    const PdxValue* balance = child(current_month, "balance");
    std::map<std::string, double> net_monthly;
    accumulate_balance_resources(balance, net_monthly);

    j.key("nat_finance_economy");
    j.begin_object();
    j.key("budget");
    j.begin_object();
    write_budget_child_or_empty(j, current_month, "income");
    write_budget_child_or_empty(j, current_month, "expenses");
    write_budget_child_or_empty(j, current_month, "balance");
    j.end_object();

    j.key("net_monthly_resource");
    j.begin_object();
    constexpr double epsilon = 0.000000001;
    for (const auto& [resource, amount] : net_monthly) {
        if (std::abs(amount) <= epsilon) continue;
        j.key(resource);
        j.raw_number(json_number(amount));
    }
    j.end_object();

    j.key("stored_resources");
    if (stored_resources) write_pdx_as_schema_json(j, stored_resources, "stored_resources");
    else { j.begin_object(); j.end_object(); }
    j.end_object();
}

