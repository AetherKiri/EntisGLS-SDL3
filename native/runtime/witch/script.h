#pragma once

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

// WitchWizard's native xscript VM. Presentation is deliberately outside the
// interpreter: a command is yielded once, then the host owns its wait/input.
namespace entis::witch {
struct Command {
    std::string tag;
    std::map<std::string, std::string> attributes;
    std::vector<Command> children;
    std::string Get(const std::string& key, const std::string& fallback = {}) const;
    double Number(const std::string& key, double fallback = 0) const;
};
struct Module { std::vector<Command> code; };
class Script {
public:
    using Loader = std::function<Module(const std::string&)>;
    explicit Script(Loader loader);
    void Start(const std::string& entry);
    std::optional<Command> Next();
    void Choose(std::size_t index);
    nlohmann::json Save() const;
    void Restore(const nlohmann::json& saved);
    std::map<std::string, double>& Variables() { return variables_; }
    const std::map<std::string, double>& Variables() const { return variables_; }
    std::string Location() const;
    static std::string ModuleName(std::string name);
private:
    struct Frame { std::string module; std::size_t pc = 0; };
    Loader loader_;
    std::map<std::string, Module> modules_;
    std::vector<Frame> stack_;
    std::map<std::string, double> variables_;
    std::optional<Command> choice_;
    const Module& Load(const std::string& name);
    void Jump(Frame& frame, const std::string& label);
};
double Evaluate(const std::string& expression, const std::map<std::string, double>& variables);
void Assign(const std::string& expression, std::map<std::string, double>& variables);
} // namespace entis::witch
