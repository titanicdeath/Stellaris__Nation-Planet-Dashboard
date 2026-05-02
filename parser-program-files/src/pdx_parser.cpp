#include "pdx_parser.hpp"
#include "utils.hpp"

struct Token {
    enum class Type { End, Atom, LBrace, RBrace, Equal } type = Type::End;
    std::string text;
    size_t line = 1;
};

class Tokenizer {
public:
    explicit Tokenizer(std::string_view data) : data_(data) {}

    Token peek(size_t n = 0) {
        while (buffer_.size() <= n) buffer_.push_back(next_token_internal());
        return buffer_[n];
    }

    Token next() {
        if (!buffer_.empty()) {
            Token t = buffer_.front();
            buffer_.erase(buffer_.begin());
            return t;
        }
        return next_token_internal();
    }

private:
    std::string_view data_;
    size_t pos_ = 0;
    size_t line_ = 1;
    std::vector<Token> buffer_;

    void skip_ws_and_comments() {
        while (pos_ < data_.size()) {
            char c = data_[pos_];
            if (c == '\n') { ++line_; ++pos_; continue; }
            if (std::isspace(static_cast<unsigned char>(c))) { ++pos_; continue; }
            // Game definition files use comments. Save files usually do not.
            if (c == '#') {
                while (pos_ < data_.size() && data_[pos_] != '\n') ++pos_;
                continue;
            }
            break;
        }
    }

    Token next_token_internal() {
        skip_ws_and_comments();
        if (pos_ >= data_.size()) return {Token::Type::End, "", line_};
        const size_t tok_line = line_;
        char c = data_[pos_];
        if (c == '{') { ++pos_; return {Token::Type::LBrace, "{", tok_line}; }
        if (c == '}') { ++pos_; return {Token::Type::RBrace, "}", tok_line}; }
        if (c == '=') { ++pos_; return {Token::Type::Equal, "=", tok_line}; }
        if (c == '"') {
            ++pos_;
            std::string out;
            while (pos_ < data_.size()) {
                char ch = data_[pos_++];
                if (ch == '"') break;
                if (ch == '\\' && pos_ < data_.size()) {
                    char esc = data_[pos_++];
                    switch (esc) {
                        case 'n': out.push_back('\n'); break;
                        case 'r': out.push_back('\r'); break;
                        case 't': out.push_back('\t'); break;
                        case '"': out.push_back('"'); break;
                        case '\\': out.push_back('\\'); break;
                        default: out.push_back(esc); break;
                    }
                } else {
                    if (ch == '\n') ++line_;
                    out.push_back(ch);
                }
            }
            return {Token::Type::Atom, out, tok_line};
        }

        std::string out;
        while (pos_ < data_.size()) {
            char ch = data_[pos_];
            if (std::isspace(static_cast<unsigned char>(ch)) || ch == '{' || ch == '}' || ch == '=') break;
            if (ch == '#') break;
            out.push_back(ch);
            ++pos_;
        }
        return {Token::Type::Atom, out, tok_line};
    }
};

class PdxParser {
public:
    explicit PdxParser(std::string_view data) : tok_(data) {}

    PdxDocument parse_document() {
        PdxDocument doc;
        doc.root.kind = PdxValue::Kind::Container;
        doc.root.line_start = 1;
        while (tok_.peek().type != Token::Type::End) {
            Token k = tok_.next();
            if (k.type != Token::Type::Atom) throw std::runtime_error("Expected top-level key at line " + std::to_string(k.line));
            Token eq = tok_.next();
            if (eq.type != Token::Type::Equal) throw std::runtime_error("Expected '=' after key '" + k.text + "' at line " + std::to_string(k.line));
            PdxValue* val = parse_value(doc);
            doc.root.entries.push_back({k.text, val, k.line});
        }
        doc.root.line_end = tok_.peek().line;
        return doc;
    }

private:
    Tokenizer tok_;

    PdxValue* parse_value(PdxDocument& doc) {
        Token t = tok_.next();
        if (t.type == Token::Type::Atom) return doc.make_scalar(t.text, t.line);
        if (t.type == Token::Type::LBrace) {
            PdxValue* c = doc.make_container(t.line);
            while (true) {
                Token p = tok_.peek();
                if (p.type == Token::Type::End) throw std::runtime_error("Unclosed '{' starting at line " + std::to_string(t.line));
                if (p.type == Token::Type::RBrace) {
                    Token r = tok_.next();
                    c->line_end = r.line;
                    break;
                }
                if (p.type == Token::Type::LBrace) {
                    PdxValue* anon = parse_value(doc);
                    c->entries.push_back({"", anon, anon->line_start});
                    continue;
                }
                if (p.type == Token::Type::Atom) {
                    Token a = tok_.next();
                    if (tok_.peek().type == Token::Type::Equal) {
                        tok_.next();
                        PdxValue* val = parse_value(doc);
                        c->entries.push_back({a.text, val, a.line});
                    } else {
                        c->entries.push_back({"", doc.make_scalar(a.text, a.line), a.line});
                    }
                    continue;
                }
                throw std::runtime_error("Unexpected token inside container at line " + std::to_string(p.line));
            }
            return c;
        }
        throw std::runtime_error("Expected value at line " + std::to_string(t.line));
    }
};

PdxDocument parse_document(std::string_view data) {
    PdxParser parser(data);
    return parser.parse_document();
}
