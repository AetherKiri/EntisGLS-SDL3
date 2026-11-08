#include "runtime/witch/script.h"
#include <iostream>
#include <stdexcept>
using namespace entis::witch;
namespace {
void Require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
template<class F> void Reject(F action) {
    try { action(); } catch (const std::exception&) { return; }
    throw std::runtime_error("invalid script/state accepted");
}
Command Op(const std::string& tag, std::map<std::string, std::string> args = {}, std::vector<Command> children = {}) {
    return {tag, std::move(args), std::move(children)};
}
}
int main() {
    try {
        std::map<std::string, double> variables;
        Require(Evaluate("!s.PLAYED_OPENING\r\n", variables) == 1, "undefined flags must be zero");
        Assign("s.PLAYED_OPENING = 1\r\n", variables);
        Require(Evaluate("!s.PLAYED_OPENING", variables) == 0, "saved flag was ignored");
        Require(Evaluate("2 + 3 * 4 == 14 && (5 % 3) == 2", variables) == 1, "operator precedence");
        Require(Evaluate("0 && 1 / 0", variables) == 0 && Evaluate("1 || 1 / 0", variables) == 1, "logical operators short circuit evaluation");
        Reject([&] { Evaluate("0 || 1 / 0", variables); });
        Assign("local = -1.25 * 4;", variables);
        Require(variables.at("local") == -5, "signed real assignment");
        for (const auto& text : {"1 / 0", "1 +", "a()", "(1", "1e999", "1 extra"}) Reject([&] { Evaluate(text, variables); });
        Reject([&] { Assign("x == 1", variables); });
        Reject([&] { Evaluate(std::string(200, '!') + "1", variables); });
        for (const auto& name : {"", "..", "../scene", "a/b", "a\\b", "x:y"}) Reject([&] { Script::ModuleName(name); });
        Require(Script::ModuleName("序章.srcxml") == "序章", "Unicode module name");

        std::map<std::string, Module> modules{
            {"main", {{Op("call", {{"target", "scene"}}), Op("msg", {{"text", "after"}})}}},
            {"scene", {{Op("msg", {{"text", "first"}}), Op("wait", {{"time", "20"}}),
                        Op("msg", {{"text", "second"}}), Op("return")}}}
        };
        auto loader = [&](const std::string& name) { return modules.at(name); };
        Script script(loader); script.Start("main.srcxml");
        Require(script.Next()->Get("text") == "first", "call into scene");
        const auto saved = script.Save();
        Require(script.Next()->tag == "wait", "wait must yield exactly once");
        Require(script.Next()->Get("text") == "second", "resume after wait");
        Require(script.Next()->Get("text") == "after", "return address");
        Require(!script.Next(), "script completion");
        Script restored(loader); restored.Restore(nlohmann::json::parse(saved.dump()));
        Require(restored.Next()->tag == "wait", "load must resume after the displayed line");
        Require(restored.Next()->Get("text") == "second", "load lost nested module");
        Require(restored.Next()->Get("text") == "after", "load lost caller");

        auto bad = saved; bad["frames"][1]["pc"] = 900;
        const auto before = restored.Save(); Reject([&] { restored.Restore(bad); });
        Require(restored.Save() == before, "invalid restore changed live state");
        bad = saved; bad["frames"][0]["pc"] = -1; Reject([&] { restored.Restore(bad); });
        bad = saved; bad["frames"][0]["module"] = "../escape"; Reject([&] { restored.Restore(bad); });
        bad = saved; bad["version"] = 99; Reject([&] { restored.Restore(bad); });
        modules["choice"].code = {
            Op("select", {}, {Op("menu", {{"text", "hidden"}, {"cond", "false"}}),
                              Op("menu", {{"text", "yes"}, {"label", "yes"}}), Op("menu", {{"text", "no"}})}),
            Op("msg", {{"text", "no"}}), Op("return"), Op("label", {{"id", "yes"}}), Op("msg", {{"text", "yes"}})
        };
        Script choice(loader); choice.Start("choice");
        Require(choice.Next()->children.size() == 2, "conditional choices");
        Reject([&] { choice.Next(); }); Reject([&] { choice.Save(); }); Reject([&] { choice.Choose(2); });
        choice.Choose(0); Require(choice.Next()->Get("text") == "yes", "choice label");
        choice.Start("choice"); choice.Next(); choice.Choose(1);
        Require(choice.Next()->Get("text") == "no", "fall-through choice");

        modules["loop"].code = {Op("label", {{"id", "top"}}), Op("goto", {{"label", "top"}})};
        Script loop(loader); loop.Start("loop"); Reject([&] { loop.Next(); });
        modules["recursive"].code = {Op("call", {{"target", "recursive"}})};
        Script recursion(loader); recursion.Start("recursive"); Reject([&] { recursion.Next(); });
        modules["missing"].code = {Op("goto", {{"label", "absent"}})};
        Script missing(loader); missing.Start("missing"); Reject([&] { missing.Next(); });
        script.Variables() = {{"s.complete", 1}, {"local", 9}}; script.Start("main");
        Require(script.Variables().count("s.complete") && !script.Variables().count("local"), "new game variable lifetime");
        std::cout << "WitchWizard script PASS: expressions, calls, choices, waits, save/resume, invalid state, bounded execution\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
