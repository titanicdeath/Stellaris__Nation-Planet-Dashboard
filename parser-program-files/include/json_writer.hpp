#pragma once

#include "common.hpp"

class JsonWriter {
public:
    explicit JsonWriter(std::ostream& os, bool pretty);

    void begin_object();
    void end_object();
    void begin_array();
    void end_array();
    void key(const std::string& k);
    void value(const std::string& v);
    void value(const char* v);
    void value(bool v);
    void value(std::nullptr_t);
    void raw_number(const std::string& s);

private:
    struct Frame { bool is_object; bool first; };
    std::ostream& os_;
    bool pretty_ = true;
    std::vector<Frame> stack_;
    bool expecting_value_after_key_ = false;

    void before_value();
    void mark_value_done();
    void newline_indent();
    void newline_before_close();
    void write_escaped(const std::string& s);
};

bool looks_int(const std::string& s);
bool looks_float(const std::string& s);
void json_scalar(JsonWriter& j, const std::string& raw);
std::string json_number(double v);
