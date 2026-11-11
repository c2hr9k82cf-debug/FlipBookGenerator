// json_min.h — minimal read-only JSON parser (v0.9.1 template loading)
// 极简只读 JSON 解析器（v0.9.1 模板加载）
//
// Deliberately tiny: objects, arrays, strings, integers, booleans and null —
// exactly what the template / item JSON files contain. No floats, no writer,
// no DOM mutation, zero new dependencies. If this stops being enough, the plan
// allows swapping in a single-header library later.
// 刻意极简：对象、数组、字符串、整数、布尔、null —— 恰好覆盖模板/部件 JSON 的
// 内容。无浮点、无写出、无 DOM 修改、零新依赖。真不够用时，计划允许日后换单头库。
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace fb {

// A parsed JSON value. Object members keep file order (the palette list order
// is meaningful to humans reading errors).
// 一个解析后的 JSON 值。对象成员保留文件顺序（配色列表顺序对读报错的人有意义）。
struct JsonValue {
    enum Type { Null, Bool, Int, String, Array, Object };
    Type type = Null;
    bool b = false;
    long long i = 0;
    std::string s;
    std::vector<JsonValue> arr;
    std::vector<std::pair<std::string, JsonValue>> obj;

    // Object member lookup; nullptr when absent or not an object.
    // 对象成员查找；不存在或非对象时为 nullptr。
    const JsonValue* find(const std::string& key) const;
    bool has(const std::string& key) const { return find(key) != nullptr; }

    // Convenience typed reads with defaults. / 带默认值的便捷类型化读取。
    std::string asString(const std::string& def = "") const;
    long long asInt(long long def = 0) const;
};

// Parse `text` fully (trailing non-whitespace is an error). Returns false and
// fills `err` (with byte offset) on any syntax problem.
// 完整解析 text（结尾有多余非空白即报错）。任何语法问题返回 false 并在 err
// 里给出字节偏移。
bool jsonParse(const std::string& text, JsonValue& out, std::string& err);

}  // namespace fb
