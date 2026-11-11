// json_min.cpp — recursive-descent parser for the read-only JSON subset.
// 递归下降解析只读 JSON 子集。
#include "core/json_min.h"

#include <cctype>
#include <cstdlib>

namespace fb {
namespace {

// Cursor over the input; `pos` is the byte offset reported in errors.
// 输入上的游标；pos 是报错用的字节偏移。
struct Parser {
    const std::string& t;
    size_t pos = 0;
    std::string err;

    explicit Parser(const std::string& text) : t(text) {}

    void fail(const std::string& what) {
        if (err.empty())
            err = "JSON offset " + std::to_string(pos) + ": " + what;
    }
    bool atEnd() const { return pos >= t.size(); }
    char peek() const { return t[pos]; }

    void skipWs() {
        while (pos < t.size() &&
               (t[pos] == ' ' || t[pos] == '\t' || t[pos] == '\n' || t[pos] == '\r'))
            ++pos;
    }

    bool expect(char c, const char* what) {
        skipWs();
        if (pos < t.size() && t[pos] == c) { ++pos; return true; }
        fail(std::string("expected ") + what);
        return false;
    }

    // JSON string with the standard escapes. \uXXXX is decoded to UTF-8
    // (surrogate pairs included); a lone surrogate becomes U+FFFD.
    // JSON 字符串，支持标准转义。\uXXXX 解码为 UTF-8（含代理对）；
    // 孤立代理项变为 U+FFFD。
    bool parseString(std::string& out) {
        if (!expect('"', "'\"'")) return false;
        out.clear();
        while (true) {
            if (atEnd()) { fail("unterminated string"); return false; }
            char c = t[pos++];
            if (c == '"') return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (atEnd()) { fail("unterminated escape"); return false; }
            char e = t[pos++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case 'r': out += '\r'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'u': {
                    unsigned cp = 0;
                    if (!parseHex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        // High surrogate: expect \uXXXX low surrogate.
                        // 高代理项：期待 \uXXXX 低代理项。
                        if (pos + 1 < t.size() && t[pos] == '\\' && t[pos + 1] == 'u') {
                            pos += 2;
                            unsigned lo = 0;
                            if (!parseHex4(lo)) return false;
                            if (lo >= 0xDC00 && lo <= 0xDFFF)
                                cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            else
                                cp = 0xFFFD;
                        } else {
                            cp = 0xFFFD;
                        }
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        cp = 0xFFFD;
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default:
                    fail("bad escape");
                    return false;
            }
        }
    }

    bool parseHex4(unsigned& out) {
        out = 0;
        for (int i = 0; i < 4; ++i) {
            if (atEnd()) { fail("truncated \\u escape"); return false; }
            char c = t[pos++];
            out <<= 4;
            if (c >= '0' && c <= '9') out |= (unsigned)(c - '0');
            else if (c >= 'a' && c <= 'f') out |= (unsigned)(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') out |= (unsigned)(c - 'A' + 10);
            else { fail("bad \\u hex digit"); return false; }
        }
        return true;
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += (char)cp;
        } else if (cp < 0x800) {
            out += (char)(0xC0 | (cp >> 6));
            out += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += (char)(0xE0 | (cp >> 12));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        } else {
            out += (char)(0xF0 | (cp >> 18));
            out += (char)(0x80 | ((cp >> 12) & 0x3F));
            out += (char)(0x80 | ((cp >> 6) & 0x3F));
            out += (char)(0x80 | (cp & 0x3F));
        }
    }

    bool parseValue(JsonValue& out) {
        skipWs();
        if (atEnd()) { fail("unexpected end of input"); return false; }
        char c = peek();
        switch (c) {
            case '{': return parseObject(out);
            case '[': return parseArray(out);
            case '"': out.type = JsonValue::String; return parseString(out.s);
            case 't':
                if (t.compare(pos, 4, "true") == 0) {
                    pos += 4;
                    out.type = JsonValue::Bool;
                    out.b = true;
                    return true;
                }
                fail("bad literal");
                return false;
            case 'f':
                if (t.compare(pos, 5, "false") == 0) {
                    pos += 5;
                    out.type = JsonValue::Bool;
                    out.b = false;
                    return true;
                }
                fail("bad literal");
                return false;
            case 'n':
                if (t.compare(pos, 4, "null") == 0) {
                    pos += 4;
                    out.type = JsonValue::Null;
                    return true;
                }
                fail("bad literal");
                return false;
            default:
                return parseNumber(out);
        }
    }

    // Integers only — the template / item files contain no floats. A fractional
    // part or exponent is rejected so a stray float cannot silently truncate.
    // 只支持整数 —— 模板/部件文件里没有浮点。出现小数或指数直接报错，
    // 免得意外截断。
    bool parseNumber(JsonValue& out) {
        const size_t start = pos;
        if (!atEnd() && (peek() == '-' || peek() == '+')) ++pos;
        bool anyDigit = false;
        while (!atEnd() && std::isdigit((unsigned char)peek())) { ++pos; anyDigit = true; }
        if (!anyDigit) { fail("bad number"); return false; }
        if (!atEnd() && (peek() == '.' || peek() == 'e' || peek() == 'E')) {
            fail("float numbers are not supported");
            return false;
        }
        out.type = JsonValue::Int;
        out.i = std::strtoll(t.substr(start, pos - start).c_str(), nullptr, 10);
        return true;
    }

    bool parseArray(JsonValue& out) {
        out.type = JsonValue::Array;
        if (!expect('[', "'['")) return false;
        skipWs();
        if (!atEnd() && peek() == ']') { ++pos; return true; }
        while (true) {
            out.arr.emplace_back();
            if (!parseValue(out.arr.back())) return false;
            skipWs();
            if (atEnd()) { fail("unterminated array"); return false; }
            if (peek() == ',') { ++pos; continue; }
            if (peek() == ']') { ++pos; return true; }
            fail("expected ',' or ']' in array");
            return false;
        }
    }

    bool parseObject(JsonValue& out) {
        out.type = JsonValue::Object;
        if (!expect('{', "'{'")) return false;
        skipWs();
        if (!atEnd() && peek() == '}') { ++pos; return true; }
        while (true) {
            skipWs();
            std::string key;
            if (!parseString(key)) return false;
            if (!expect(':', "':'")) return false;
            out.obj.emplace_back(std::move(key), JsonValue{});
            if (!parseValue(out.obj.back().second)) return false;
            skipWs();
            if (atEnd()) { fail("unterminated object"); return false; }
            if (peek() == ',') { ++pos; continue; }
            if (peek() == '}') { ++pos; return true; }
            fail("expected ',' or '}' in object");
            return false;
        }
    }
};

}  // namespace

const JsonValue* JsonValue::find(const std::string& key) const {
    if (type != Object) return nullptr;
    for (const auto& kv : obj)
        if (kv.first == key) return &kv.second;
    return nullptr;
}

std::string JsonValue::asString(const std::string& def) const {
    return type == String ? s : def;
}

long long JsonValue::asInt(long long def) const {
    return type == Int ? i : def;
}

bool jsonParse(const std::string& text, JsonValue& out, std::string& err) {
    out = JsonValue{};
    Parser p(text);
    if (!p.parseValue(out)) {
        err = p.err;
        return false;
    }
    p.skipWs();
    if (!p.atEnd()) {
        err = "JSON offset " + std::to_string(p.pos) + ": trailing data after value";
        return false;
    }
    return true;
}

}  // namespace fb
