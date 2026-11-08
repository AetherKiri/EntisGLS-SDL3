#include "runtime/witch/script.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace entis::witch {
namespace {
[[noreturn]] void Error(const std::string& text) { throw std::runtime_error("WitchWizard: " + text); }
bool NameChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.'; }
class Expression {
    const std::string& source;
    const std::map<std::string, double>& vars;
    std::size_t at = 0, depth = 0;
    bool active = true;
public:
    Expression(const std::string& text, const std::map<std::string, double>& values) : source(text), vars(values) {
        if (text.size() > 65536) Error("expression exceeds 64 KiB");
    }
    void Space() { while (at < source.size() && std::isspace(static_cast<unsigned char>(source[at]))) ++at; }
    bool Take(const std::string& token) {
        Space();
        if (source.compare(at, token.size(), token) != 0) return false;
        at += token.size(); return true;
    }
    std::string Name() {
        Space(); const auto start = at;
        if (at == source.size() || (!std::isalpha(static_cast<unsigned char>(source[at])) && source[at] != '_'))
            Error("expected variable name");
        while (at < source.size() && NameChar(source[at])) ++at;
        return source.substr(start, at - start);
    }
    void End() { Space(); if (at != source.size()) Error("unsupported expression near " + source.substr(at, 24)); }
    double Primary() {
        if (++depth > 128) Error("expression nesting exceeds 128");
        struct Pop { std::size_t& d; ~Pop() { --d; } } pop{depth};
        if (Take("!")) return !Primary();
        if (Take("-")) return -Primary();
        if (Take("+")) return Primary();
        if (Take("(")) { const auto v = Or(); if (!Take(")")) Error("missing closing parenthesis"); return v; }
        Space();
        if (at < source.size() && (std::isdigit(static_cast<unsigned char>(source[at])) || source[at] == '.')) {
            char* end = nullptr; const auto value = std::strtod(source.c_str() + at, &end);
            if (end == source.c_str() + at || !std::isfinite(value)) Error("invalid number");
            at = static_cast<std::size_t>(end - source.c_str()); return value;
        }
        const auto name = Name();
        if (name == "true") return 1;
        if (name == "false") return 0;
        const auto found = vars.find(name); return found == vars.end() ? 0 : found->second;
    }
    double Mul() {
        auto v = Primary();
        for (;;) {
            if (Take("*")) v *= Primary();
            else if (Take("/")) { const auto r = Primary(); if (active && !r) Error("division by zero"); v = active ? v / r : 0; }
            else if (Take("%")) { const auto r = Primary(); if (active && !r) Error("division by zero"); v = active ? std::fmod(v, r) : 0; }
            else return v;
        }
    }
    double Add() { auto v = Mul(); for (;;) { if (Take("+")) v += Mul(); else if (Take("-")) v -= Mul(); else return v; } }
    double Compare() {
        auto v = Add();
        for (;;) {
            if (Take("<=")) v = v <= Add(); else if (Take(">=")) v = v >= Add();
            else if (Take("<")) v = v < Add(); else if (Take(">")) v = v > Add(); else return v;
        }
    }
    double Equal() { auto v = Compare(); for (;;) { if (Take("==")) v = v == Compare(); else if (Take("!=")) v = v != Compare(); else return v; } }
    double And() {
        auto v = Equal();
        while (Take("&&")) { const bool before = active; active = active && v; const auto r = Equal(); active = before; v = v && r; }
        return v;
    }
    double Or() {
        auto v = And();
        while (Take("||")) { const bool before = active; active = active && !v; const auto r = And(); active = before; v = v || r; }
        return v;
    }
};
}
std::string Command::Get(const std::string& key, const std::string& fallback) const {
    const auto it = attributes.find(key); return it == attributes.end() ? fallback : it->second;
}
double Command::Number(const std::string& key, double fallback) const {
    const auto text = Get(key); return text.empty() ? fallback : Evaluate(text, {});
}
double Evaluate(const std::string& text, const std::map<std::string, double>& variables) {
    Expression parser(text, variables); const auto v = parser.Or(); parser.End();
    if (!std::isfinite(v)) Error("expression result is not finite");
    return v;
}
void Assign(const std::string& text, std::map<std::string, double>& variables) {
    Expression parser(text, variables); const auto name = parser.Name();
    if (!parser.Take("=")) Error("expected assignment");
    const auto value = parser.Or(); parser.Take(";"); parser.End();
    if (!std::isfinite(value)) Error("assignment result is not finite");
    variables[name] = value;
}
std::string Script::ModuleName(std::string name) {
    if (name.size() >= 7 && name.compare(name.size() - 7, 7, ".srcxml") == 0) name.resize(name.size() - 7);
    if (name.empty() || name.size() > 240 || name.find_first_of("/\\:") != std::string::npos || name == "." || name == ".." ||
        std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32; }))
        Error("invalid script module name");
    return name;
}
Script::Script(Loader loader) : loader_(std::move(loader)) {}
const Module& Script::Load(const std::string& name) {
    auto found = modules_.find(name);
    if (found != modules_.end()) return found->second;
    auto module = loader_(name);
    if (module.code.size() > 1000000) Error("script exceeds one million commands");
    return modules_.emplace(name, std::move(module)).first->second;
}
void Script::Start(const std::string& entry) {
    const auto name = ModuleName(entry); Load(name);
    stack_ = {{name, 0}}; choice_.reset();
    for (auto i = variables_.begin(); i != variables_.end();) {
        if (i->first.rfind("s.", 0) == 0) ++i; else i = variables_.erase(i);
    }
}
void Script::Jump(Frame& frame, const std::string& label) {
    if (label.empty()) return;
    const auto& code = Load(frame.module).code;
    for (std::size_t i = 0; i < code.size(); ++i)
        if (code[i].tag == "label" && code[i].Get("id") == label) { frame.pc = i + 1; return; }
    Error("label not found: " + frame.module + ":" + label);
}
std::optional<Command> Script::Next() {
    if (choice_) Error("a selection must be answered before advancing");
    for (std::size_t budget = 0; budget < 10000; ++budget) {
        if (stack_.empty()) return {};
        auto& frame = stack_.back(); const auto& code = Load(frame.module).code;
        if (frame.pc >= code.size()) { stack_.pop_back(); continue; }
        auto command = code[frame.pc++]; const auto& tag = command.tag;
        if (tag == "label" || tag == "description") continue;
        if (tag == "eval") { Assign(command.Get("expr"), variables_); continue; }
        if (tag == "goto" || tag == "call") {
            if (!command.Get("cond").empty() && !Evaluate(command.Get("cond"), variables_)) continue;
            auto target = command.Get("target");
            Frame next{target.empty() ? frame.module : ModuleName(target), target.empty() ? frame.pc : 0};
            Load(next.module); Jump(next, command.Get("label"));
            if (tag == "call") {
                if (stack_.size() >= 128) Error("call depth exceeds 128");
                stack_.push_back(next);
            } else frame = next;
            continue;
        }
        if (tag == "return") { stack_.pop_back(); continue; }
        if (tag == "select") {
            command.children.erase(std::remove_if(command.children.begin(), command.children.end(), [&](const Command& child) {
                return child.tag != "menu" || (!child.Get("cond").empty() && !Evaluate(child.Get("cond"), variables_));
            }), command.children.end());
            if (command.children.empty()) Error("selection contains no available menu");
            choice_ = command;
        }
        return command;
    }
    Error("script did not yield within 10000 control instructions at " + Location());
}
void Script::Choose(std::size_t index) {
    if (!choice_ || index >= choice_->children.size() || stack_.empty()) Error("invalid choice");
    auto next = stack_.back(); const auto menu = choice_->children[index];
    if (!menu.Get("target").empty()) { next.module = ModuleName(menu.Get("target")); next.pc = 0; Load(next.module); }
    Jump(next, menu.Get("label")); stack_.back() = next; choice_.reset();
}
nlohmann::json Script::Save() const {
    if (choice_) Error("cannot save an unanswered selection");
    auto frames = nlohmann::json::array();
    for (const auto& frame : stack_) frames.push_back({{"module", frame.module}, {"pc", frame.pc}});
    return {{"version", 1}, {"frames", frames}, {"variables", variables_}};
}
void Script::Restore(const nlohmann::json& saved) {
    if (saved.at("version") != 1 || !saved.at("frames").is_array() || saved.at("frames").size() > 128)
        Error("invalid saved script state");
    std::vector<Frame> frames;
    auto variables = saved.at("variables").get<std::map<std::string, double>>();
    if (variables.size() > 65536) Error("too many saved variables");
    for (const auto& [name, value] : variables) if (name.empty() || !std::isfinite(value)) Error("invalid saved variable");
    for (const auto& item : saved.at("frames")) {
        if (!item.at("pc").is_number_unsigned() && (!item.at("pc").is_number_integer() || item.at("pc").get<int64_t>() < 0))
            Error("invalid saved program counter");
        Frame frame{ModuleName(item.at("module")), item.at("pc").get<std::size_t>()};
        if (frame.pc > Load(frame.module).code.size()) Error("saved program counter is outside the script");
        frames.push_back(frame);
    }
    // A failed restore leaves the currently running story intact.
    stack_ = std::move(frames); variables_ = std::move(variables); choice_.reset();
}
std::string Script::Location() const {
    return stack_.empty() ? "<end>" : stack_.back().module + ":" + std::to_string(stack_.back().pc);
}
} // namespace entis::witch
