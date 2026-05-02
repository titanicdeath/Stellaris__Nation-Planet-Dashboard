#include "json_writer.hpp"
#include "utils.hpp"

JsonWriter::JsonWriter(std::ostream& os, bool pretty) : os_(os), pretty_(pretty) {}

void JsonWriter::begin_object() { before_value(); os_ << "{"; stack_.push_back({true, true}); }
void JsonWriter::end_object() { newline_before_close(); os_ << "}"; stack_.pop_back(); mark_value_done(); }
void JsonWriter::begin_array() { before_value(); os_ << "["; stack_.push_back({false, true}); }
void JsonWriter::end_array() { newline_before_close(); os_ << "]"; stack_.pop_back(); mark_value_done(); }

void JsonWriter::key(const std::string& k) {
    if (stack_.empty() || !stack_.back().is_object) throw std::runtime_error("JSON key outside object");
    if (!stack_.back().first) os_ << ",";
    newline_indent();
    write_escaped(k);
    os_ << (pretty_ ? ": " : ":");
    stack_.back().first = false;
    expecting_value_after_key_ = true;
}

void JsonWriter::value(const std::string& v) { before_value(); write_escaped(v); mark_value_done(); }
void JsonWriter::value(const char* v) { value(std::string(v)); }
void JsonWriter::value(bool v) { before_value(); os_ << (v ? "true" : "false"); mark_value_done(); }
void JsonWriter::value(std::nullptr_t) { before_value(); os_ << "null"; mark_value_done(); }
void JsonWriter::raw_number(const std::string& s) { before_value(); os_ << s; mark_value_done(); }

void JsonWriter::before_value() {
    if (expecting_value_after_key_) { expecting_value_after_key_ = false; return; }
    if (!stack_.empty() && !stack_.back().is_object) {
        if (!stack_.back().first) os_ << ",";
        newline_indent();
        stack_.back().first = false;
    }
}

void JsonWriter::mark_value_done() {}

void JsonWriter::newline_indent() {
    if (!pretty_) return;
    os_ << "\n";
    for (size_t i = 0; i < stack_.size(); ++i) os_ << "  ";
}

void JsonWriter::newline_before_close() {
    if (!pretty_) return;
    if (!stack_.empty() && !stack_.back().first) {
        os_ << "\n";
        for (size_t i = 1; i < stack_.size(); ++i) os_ << "  ";
    }
}

void JsonWriter::write_escaped(const std::string& s) {
    os_ << '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': os_ << "\\\""; break;
            case '\\': os_ << "\\\\"; break;
            case '\b': os_ << "\\b"; break;
            case '\f': os_ << "\\f"; break;
            case '\n': os_ << "\\n"; break;
            case '\r': os_ << "\\r"; break;
            case '\t': os_ << "\\t"; break;
            default:
                if (c < 0x20) {
                    os_ << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c) << std::dec;
                } else {
                    os_ << static_cast<char>(c);
                }
        }
    }
    os_ << '"';
}

bool looks_int(const std::string& s) {
    if (s.empty()) return false;
    size_t i = (s[0] == '-') ? 1 : 0;
    if (i == s.size()) return false;
    for (; i < s.size(); ++i) if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    return true;
}

bool looks_float(const std::string& s) {
    if (s.empty()) return false;
    bool dot = false, digit = false;
    size_t i = (s[0] == '-') ? 1 : 0;
    for (; i < s.size(); ++i) {
        char c = s[i];
        if (std::isdigit(static_cast<unsigned char>(c))) { digit = true; continue; }
        if (c == '.' && !dot) { dot = true; continue; }
        return false;
    }
    return dot && digit;
}

void json_scalar(JsonWriter& j, const std::string& raw) {
    const std::string low = lower_copy(raw);
    if (low == "yes" || low == "true") { j.value(true); return; }
    if (low == "no" || low == "false") { j.value(false); return; }
    if (raw == "none" || raw == "null") { j.value(nullptr); return; }
    // Dates such as 2343.06.20 contain two dots and must remain strings.
    if (looks_int(raw) || looks_float(raw)) { j.raw_number(raw); return; }
    j.value(raw);
}
