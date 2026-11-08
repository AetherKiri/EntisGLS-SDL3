#include "compatibility/sdk/legacy/gls.h"
#include "runtime/witch/player.h"
#include "runtime/witch/script.h"
#include "runtime/witch/sdk.h"
#include "launcher/game_config.h"
#include "platform/log.h"
#include "platform/sdl/game_font_aliases.h"
#include "platform/sdl/runtime_control.h"
#include <sakuraglx/sprite/sglx_resource_manager.h>
#include <sakuraglx/sprite/sglx_sprite_formed.h>
#include <sakuraglx/sprite/sglx_sprite_button.h>
#include <sakuraglx/sprite/sglx_sprite_window.h>
#include <sakuraglx/sprite/sglx_sprite_filter.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <ctime>
#include <memory>
#include <mutex>
#include <thread>

namespace entis::witch {
namespace {
using namespace SakuraGL;
using namespace SSystem;
using Json = nlohmann::json;
struct Clock {
    using duration = std::chrono::milliseconds;
    using time_point = std::chrono::time_point<Clock, duration>;
    static time_point now() { return time_point(duration(study::platform::sdl::RuntimeMilliseconds())); }
};
std::mutex snapshotMutex;
Json snapshot;
struct Lock { Lock() { SSystem::Lock(); } ~Lock() { SSystem::Unlock(); } };
using namespace sdk;
Module ReadModule(ECSEnvironment& environment, const std::string& name) {
    std::unique_ptr<SFileInterface> file(environment.NewOpenFile(Wide(name + ".srcxml"), SFileOpener::shareRead));
    if (!file) Fail("cannot read script " + name);
    return sdk::ReadModule(*file, name);
}

// Callbacks enqueue commands; UI trees are only changed by the worker outside
// SDK input dispatch. This also avoids deleting a button from its own callback.
class GameWindow final : public SGLWindowSprite {
public:
    std::deque<std::string> input;
    Json lastCommand;
    struct Mouse final : SGLSpriteMouseListener {
        GameWindow& owner;
        explicit Mouse(GameWindow& window) : owner(window) {}
        bool OnLButtonUp(SGLSprite&, double, double, int64_t) override { owner.input.push_back("advance"); return true; }
        bool OnRButtonUp(SGLSprite&, double, double, int64_t) override { owner.input.push_back("back"); return true; }
        bool OnMouseWheel(SGLSprite&, int32_t delta, double, double, int64_t) override {
            owner.input.push_back(delta > 0 ? "wheel-up" : "wheel-down"); return true;
        }
    } mouse{*this};
    struct Key final : SGLSpriteKeyListener {
        GameWindow& owner;
        explicit Key(GameWindow& window) : owner(window) {}
        bool OnCommand(SGLSprite&, const wchar_t* id, int64_t parameter, int64_t notification,
                       int priority, bool overwrite) override {
            return owner.NotifyCommand(id, parameter, notification, priority, overwrite);
        }
        bool OnKeyDown(SGLSprite&, int64_t key, int64_t) override {
            if (key == 13 || key == 32) owner.input.push_back("advance");
            else if (key == 27) owner.input.push_back("back");
            else return false;
            return true;
        }
    } key{*this};
    GameWindow() {
        SetWindowUIThreadMutex(SSystem::g_mutexGlobal);
        SetOptionalFlags(Window::flagBlackBack);
        AttachMousePostListener(&mouse); AttachKeyPostListener(&key);
    }
    bool NotifyCommand(const wchar_t* id, int64_t parameter, int64_t notification,
                       int priority, bool overwrite) override {
        lastCommand = {{"id", Utf8(SString(id))}, {"parameter", parameter}, {"notification", notification}};
        // Skin status notifications carry parameter=1. Only activations drive
        // navigation; hover must never open a page or advance the story.
        if (parameter == 0 && notification == 0) {
            auto command = Utf8(SString(id));
            const auto separator = command.find_last_of('\\');
            input.push_back(separator == std::string::npos ? command : command.substr(separator + 1));
        }
        return true;
    }
};

class Player {
    ECSEnvironment& env;
    const launcher::GameLaunchConfig& config;
    Script script;
    GameWindow window;
    SGLSkinManager titleSkin, windowSkin;
    SGLSprite *scene = nullptr, *ui = nullptr;
    SGLSpriteFormed *page = nullptr, *message = nullptr, *name = nullptr, *dialog = nullptr;
    std::map<std::string, SGLSprite*> layers;
    std::map<std::string, std::unique_ptr<SGLImage>> images;
    std::vector<std::unique_ptr<SGLImage>> thumbnails;
    std::map<std::string, std::unique_ptr<SGLAudioPlayer>> sounds;
    std::map<std::string, Clock::time_point> fadingSounds;
    std::deque<std::string> voiceQueue;
    Json layerState = Json::object(), soundState = Json::object(), history = Json::array();
    Command current;
    std::vector<Command> choices;
    std::string mode = "title", previous = "title", waitSound;
    Clock::time_point until{}, nextAuto{};
    Clock::time_point animationUntil{};
    struct Shake { std::string id, type; S2DDVector base; Clock::time_point start; double duration, amplitude, period; };
    std::vector<Shake> shakes;
    Clock::time_point flashStart{};
    double flashFade = 0, flashCount = 0;
    std::string tone;
    bool shade = false, keySkip = false;
    int shadeDegree = 0;
    bool waiting = false, automatic = false, skipping = false, hidden = false, done = false;
    int width = 1280, height = 720, view = 0, savePage = 0;
    double transition = 0;
    std::size_t messageCount = 0;
    std::size_t logEnd = 0;
    std::string pendingConfirmation;

    std::unique_ptr<SFileInterface> Open(const std::string& path) {
        return std::unique_ptr<SFileInterface>(env.NewOpenFile(Wide(path), SFileOpener::shareRead));
    }
    void Skin(SGLSkinManager& skin, const char* file) {
        auto input = Open(file);
        if (!input || skin.ReadSkinFile(*input)) Fail(std::string("cannot load skin ") + file);
        // Native Windows applications request installed MS faces. The host's
        // packaged CJK font supplies absent families; registered game faces win.
        using namespace study::platform::sdl;
        RegisterFontAlias(L"Default", L"@aether:default");
        const auto& styles = skin.GetStyleDefinitions();
        for (std::size_t i = 0; i < styles.GetLength(); ++i) {
            auto* style = styles.GetAt(i); auto* font = style ? style->GetElementTagAs(L"font") : nullptr;
            if (font) RegisterFontAlias(static_cast<const wchar_t*>(font->GetAttrStringAs(L"face")), L"@aether:default");
        }
    }
    SGLSpriteFormed* Form(SGLSkinManager& skin, const std::string& id, int priority = 0) {
        auto* result = static_cast<SGLSpriteFormed*>(skin.CreateFormedSprite(Wide(id)));
        if (!result) Fail("skin page not found: " + id);
        result->ChangePriority(priority); ui->AddSmartChild(result); return result;
    }
    void ClearUI() { ClearChildren(*ui); thumbnails.clear(); page = message = name = dialog = nullptr; hidden = false; }
    void StopSounds() { sounds.clear(); fadingSounds.clear(); voiceQueue.clear(); soundState = Json::object(); waitSound.clear(); }
    void Title() {
        mode = "title"; waiting = false; automatic = skipping = false; choices.clear();
        StopSounds(); ClearUI(); ClearChildren(*scene); layers.clear(); images.clear(); layerState = Json::object();
        shakes.clear(); flashFade = 0; scene->RemoveAllFilter(); tone.clear(); shade = false; keySkip = false;
        page = Form(titleSkin, "ID_TITLE_PAGE");
        // These pages need application-specific native tables, which are not
        // part of xscript. Do not expose clickable controls with no handler.
        for (const auto* id : {L"ID_EXTRA_CG", L"ID_EXTRA_SCENE", L"ID_CONFIG"}) page->SetSpriteEnable(id, false);
    }
    void StoryUI() {
        ClearUI(); mode = "story";
        const auto suffix = view ? "1" : "";
        message = Form(windowSkin, "ID_MESSAGE_WINDOW" + std::string(suffix), -100);
        message->SetSpriteEnable(L"ID_CONFIG", false);
        name = Form(windowSkin, "ID_MESSAGE_NAME_WINDOW" + std::string(suffix), -101);
        message->SetSpriteText(L"ID_MESSAGE", Wide(current.Get("text")));
        name->SetSpriteText(L"ID_NAME", Wide(current.Get("name")));
        name->SetVisible(!current.Get("name").empty());
    }
    void Begin() {
        ClearChildren(*scene); layers.clear(); images.clear(); layerState = Json::object(); StopSounds();
        current = {}; history = Json::array(); view = 0; messageCount = 0;
        script.Start(config.entryScript); waiting = false; until = {}; transition = 0;
        StoryUI();
    }
    void ShowMessage(const Command& command, bool remember = true) {
        const auto prior = current;
        current = command;
        if (command.Get("add") == "true") {
            current.attributes["text"] = prior.Get("text") + command.Get("text");
            if (command.Get("name").empty()) current.attributes["name"] = prior.Get("name");
        }
        if (!message) StoryUI();
        message->SetVisible(true); hidden = false;
        message->SetSpriteText(L"ID_MESSAGE", Wide(current.Get("text")));
        name->SetSpriteText(L"ID_NAME", Wide(current.Get("name")));
        name->SetVisible(!current.Get("name").empty());
        if (remember) {
            history.push_back(command.attributes);
            if (history.size() > 1000) history.erase(history.begin());
            ++messageCount;
        }
        Voices(command.Get("voices"));
        waiting = command.Get("async") != "true";
        nextAuto = Clock::now() + std::chrono::milliseconds(2000 + command.Get("text").size() * 20);
        study::platform::LogPrint(study::platform::LogPriority::Debug, "WitchWizard", "Message %zu at %s", messageCount, script.Location().c_str());
    }
    void Voices(const std::string& sources) {
        sounds.erase("voice"); soundState.erase("voice"); voiceQueue.clear();
        std::size_t begin = 0;
        while (begin < sources.size()) {
            auto end = sources.find('|', begin); if (end == std::string::npos) end = sources.size();
            if (end > begin) voiceQueue.push_back(sources.substr(begin, end - begin));
            begin = end + 1;
        }
        if (!voiceQueue.empty()) { auto first = voiceQueue.front(); voiceQueue.pop_front(); Play("voice", first, false, 1); }
    }
    void Play(const std::string& id, std::string source, bool loop, double volume) {
        fadingSounds.erase(id);
        if (source.empty()) { sounds.erase(id); soundState.erase(id); return; }
        auto sound = std::make_unique<SGLAudioPlayer>();
        if (source.find('.') == std::string::npos) source += ".mio";
        if (sound->Open(Wide(source), SGLAudioPlayer::modeOpenDynamicRead, &env)) Fail("cannot open audio " + source);
        if (volume > 1) volume /= 100;
        const auto gain = static_cast<float>(std::clamp(volume, 0.0, 1.0));
        sound->SetStereoVolume(gain, gain); sound->SetLoop(loop);
        if (sound->Play()) Fail("cannot play audio " + source);
        sounds[id] = std::move(sound);
        soundState[id] = {{"src", source}, {"loop", loop}, {"volume", gain}};
    }
    void Stop(const std::string& id, double fade) {
        const auto it = sounds.find(id);
        if (it != sounds.end()) {
            if (fade > 0) {
                float volume[2] = {0, 0}; it->second->BeginFadeVolume(volume, 2, static_cast<uint32_t>(fade));
                fadingSounds[id] = Clock::now() + std::chrono::milliseconds(static_cast<int64_t>(fade));
            }
            else sounds.erase(it);
        }
        soundState.erase(id);
    }
    bool Playing(const std::string& id) const {
        const auto it = sounds.find(id); return it != sounds.end() && it->second->IsPlaying();
    }
    void Remove(const std::string& id) {
        const auto found = layers.find(id);
        if (found != layers.end()) { delete scene->DetachChild(found->second); layers.erase(found); }
        layerState.erase(id);
        images.erase(id);
    }
    void Image(const std::string& id, const Command& command, const std::string& category) {
        std::string source = command.Get("src");
        if (source.empty()) { Remove(id); return; }
        auto image = std::make_unique<SGLImage>();
        if (source == "black" || source == "white") {
            SGLImageInfo info; info.width = width; info.height = height; info.format = formatImageARGB; info.depth = 32;
            image->CreateBuffer(info);
            image->FillImage(SGLPalette(source == "white" ? 0xffffffff : 0xff000000));
        } else {
            if (source.find('.') == std::string::npos) source += ".eri";
            auto input = Open(source);
            if (!input || image->ReadImage(input.get())) Fail("cannot read image " + source);
        }
        auto sprite = std::make_unique<SGLSprite>();
        sprite->AttachImage(image.get());
        const auto size = sprite->GetImageSize();
        double x = command.Number("x"), y = command.Number("y");
        if (category == "char") {
            const auto position = command.Get("pos", "auto");
            x = (position == "left" ? width * .25 : position == "right" ? width * .75 : width * .5) - size.w * .5 + x;
            y += height - size.h;
        }
        if (category == "face") { x += 0; y += height - size.h; }
        sprite->SetPosition(x, y);
        const int priority = category == "bg" ? 10000 : category == "ev" ? 9000 : category == "char" ? 5000 : category == "face" ? -500 : 0;
        sprite->ChangePriority(priority + static_cast<int>(command.Number("z")));
        sprite->ModifyUIFlag(SGLSprite::uiDisabled);
        Remove(id); scene->AddSmartChild(sprite.get()); layers[id] = sprite.release();
        layerState[id] = {{"tag", category}, {"attributes", command.attributes}};
        images[id] = std::move(image);
        const auto duration = command.Number("time", transition);
        if (duration > 0 && !skipping) {
            auto* item = layers.at(id); item->SetTransparency(256);
            auto* action = new SGLSpriteAction; action->SetTransparencyTo(*item, 0); action->SetDuration(static_cast<uint32_t>(duration));
            item->AddAction(action);
            animationUntil = std::max(animationUntil, Clock::now() + std::chrono::milliseconds(static_cast<int64_t>(duration)));
        }
    }
    void Clear(const Command& command) {
        const auto id = command.Get("layer"), category = command.Get("class");
        std::vector<std::string> selected;
        for (const auto& [key, state] : layerState.items())
            if (id == "all" || key == id || (!category.empty() && state.at("tag") == category)) selected.push_back(key);
        for (const auto& key : selected) Remove(key);
    }
    void SetFilters() {
        scene->RemoveAllFilter();
        if (!tone.empty()) {
            auto input = Open(tone + ".tct"); auto filter = std::make_unique<SGLSpriteFilterTone>();
            if (!input || filter->ReadFilterFile(*input)) Fail("cannot read tone filter " + tone);
            scene->AddSmartFilter(filter.release());
        }
        if (shade) scene->AddSmartFilter(new SGLSpriteFilterShadingOff);
        scene->SetFilterParameter(shadeDegree);
    }
    void ShakeLayers(const Command& command) {
        for (const auto& [id, item] : layerState.items()) {
            if (command.Get("layer") != id && command.Get("class") != "all" && command.Get("class") != item.at("tag")) continue;
            const auto found = layers.find(id); if (found == layers.end()) continue;
            shakes.push_back({id, command.Get("type", "random"), found->second->GetPosition2D(), Clock::now(),
                command.Number("duration", 200), command.Number("amp", 10), std::max(command.Number("cycle", 100), 1.0)});
        }
    }
    void Effects() {
        const auto now = Clock::now();
        for (auto it = fadingSounds.begin(); it != fadingSounds.end();) {
            if (now >= it->second) { sounds.erase(it->first); it = fadingSounds.erase(it); }
            else ++it;
        }
        if (!Playing("voice") && !voiceQueue.empty()) {
            auto next = voiceQueue.front(); voiceQueue.pop_front(); Play("voice", next, false, 1);
        }
        for (auto i = shakes.begin(); i != shakes.end();) {
            const auto sprite = layers.find(i->id);
            const double elapsed = std::chrono::duration<double, std::milli>(now - i->start).count();
            if (sprite == layers.end() || elapsed >= i->duration) {
                if (sprite != layers.end()) sprite->second->SetPosition(i->base.x, i->base.y);
                i = shakes.erase(i); continue;
            }
            const auto phase = elapsed / i->period;
            const double x = i->type == "random" ? std::sin(std::floor(phase) * 12.9898) :
                i->type == "cosine" ? std::cos(phase * 6.283185307) : std::sin(phase * 6.283185307);
            const double y = i->type == "random" ? std::cos(std::floor(phase) * 78.233) : 0;
            sprite->second->SetPosition(i->base.x + x * i->amplitude, i->base.y + y * i->amplitude); ++i;
        }
        if (flashFade > 0 && layers.count("@flash")) {
            const double phase = std::chrono::duration<double, std::milli>(now - flashStart).count() / flashFade;
            if (phase >= flashCount) { Remove("@flash"); flashFade = 0; }
            else layers.at("@flash")->SetTransparency(static_cast<uint32_t>((phase - std::floor(phase)) * 256));
        }
    }
    void Move(const Command& command) {
        const auto id = command.Get("layer"); const auto found = layers.find(id);
        if (found == layers.end()) Fail("move refers to missing layer " + id);
        auto* sprite = found->second;
        const auto position = sprite->GetPosition2D();
        const bool offset = command.Get("pos") == "offset";
        const auto x = command.Number("x", offset ? 0 : position.x) + (offset ? position.x : 0);
        const auto y = command.Number("y", offset ? 0 : position.y) + (offset ? position.y : 0);
        const auto duration = skipping ? 0 : command.Number("time");
        if (duration > 0) {
            auto* action = new SGLSpriteAction; action->SetMoveTo(*sprite, x, y); action->SetDuration(duration); sprite->AddAction(action);
            animationUntil = Clock::now() + std::chrono::milliseconds(static_cast<int64_t>(duration));
        } else sprite->SetPosition(x, y);
        Command stored{layerState.at(id).at("tag"), layerState.at(id).at("attributes").get<std::map<std::string, std::string>>(), {}};
        stored.attributes["x"] = std::to_string(stored.Number("x") + x - position.x);
        stored.attributes["y"] = std::to_string(stored.Number("y") + y - position.y);
        layerState[id]["attributes"] = stored.attributes;
    }
    Json SaveState() {
        Json thumbnail = Json::array();
        auto* buffer = scene->GetFrameBuffer();
        if (auto* image = buffer ? buffer->GetImage() : nullptr) {
            for (int y = 0; y < 83; ++y) for (int x = 0; x < 146; ++x) {
                SGLPalette pixel;
                if (image->GetPixelRGBA(pixel, x * width / 146, y * height / 83)) Fail("cannot capture save thumbnail");
                thumbnail.push_back(pixel.ui32);
            }
        }
        const auto now = std::time(nullptr); char date[32] = {};
        const auto* local = std::localtime(&now);
        if (local) std::strftime(date, sizeof(date), "%Y-%m-%d %H:%M", local);
        return {{"format", "aether-witch-1"}, {"script", script.Save()}, {"layers", layerState},
            {"sounds", soundState}, {"message", current.attributes}, {"view", view}, {"history", history}, {"message_count", messageCount},
            {"thumbnail", thumbnail}, {"date", date}, {"tone", tone}, {"shade", shade}, {"shade_degree", shadeDegree}};
    }
    std::string SaveName(int slot) { return "witch-slot-" + std::to_string(slot) + ".json"; }
    Json ReadSave(int slot) {
        auto* opener = env.GetWritableFileOpener();
        std::unique_ptr<SFileInterface> input(opener ? opener->NewOpenFile(Wide(SaveName(slot)), SFileOpener::shareRead) : nullptr);
        if (!input) return {};
        const auto size = input->GetLength();
        if (size <= 0 || size > 16 * 1024 * 1024) Fail("invalid save length");
        std::string text(static_cast<std::size_t>(size), '\0');
        if (input->Read(text.data(), text.size()) != text.size()) Fail("truncated save");
        return Json::parse(text);
    }
    void WriteSave(int slot) {
        const auto text = SaveState().dump(); auto* opener = env.GetWritableFileOpener();
        const auto destination = SaveName(slot), temporary = destination + ".tmp";
        if (!opener) Fail("save directory is unavailable");
        {
            std::unique_ptr<SFileInterface> output(opener->NewOpenFile(Wide(temporary), SFileOpener::modeCreate));
            if (!output || output->Write(text.data(), text.size()) != text.size()) Fail("cannot write save");
        }
        if (opener->RenameSubFile(Wide(temporary), Wide(destination))) Fail("cannot replace save");
    }
    void LoadSave(int slot) {
        const auto saved = ReadSave(slot); if (saved.is_null()) return;
        if (saved.at("format") != "aether-witch-1") Fail("unsupported save format");
        script.Restore(saved.at("script"));
        StopSounds(); ClearChildren(*scene); layers.clear(); images.clear(); layerState = Json::object();
        shakes.clear(); flashFade = 0; animationUntil = {};
        const auto savedLayers = saved.at("layers"), savedSounds = saved.at("sounds");
        transition = 0; skipping = true;
        for (const auto& [id, item] : savedLayers.items()) {
            Command command{item.at("tag"), item.at("attributes").get<std::map<std::string, std::string>>(), {}};
            Image(id, command, command.tag);
        }
        skipping = automatic = false; until = {}; waiting = true;
        view = saved.at("view"); history = saved.at("history"); messageCount = saved.value("message_count", std::size_t(0));
        current = {"msg", saved.at("message").get<std::map<std::string, std::string>>(), {}};
        tone = saved.value("tone", ""); shade = saved.value("shade", false); shadeDegree = saved.value("shade_degree", 0); SetFilters();
        StoryUI();
        Voices(current.Get("voices"));
        for (const auto& [id, item] : savedSounds.items())
            if (id != "voice" && item.value("loop", false)) Play(id, item.at("src"), true, item.at("volume"));
    }
    void SaveMenu(bool load) {
        previous = mode == "title" ? "title" : "story"; ClearUI(); mode = load ? "load" : "save";
        RefreshSaves();
    }
    int SlotsPerPage() const {
        int count = 0;
        auto* form = windowSkin.GetFormAs(mode == "load" ? L"ID_LOAD_PAGE" : L"ID_SAVE_PAGE");
        for (std::size_t i = 0; form && i < form->GetElementsCount(); ++i) {
            auto* item = form->GetElementAt(i);
            if (item && item->GetTag() == L"button" && Utf8(item->GetAttrStringAs(L"id")).rfind("ID_SAVE_", 0) == 0) ++count;
        }
        return std::max(count, 1);
    }
    void RefreshSaves() {
        ClearUI();
        page = Form(windowSkin, mode == "load" ? "ID_LOAD_PAGE" : "ID_SAVE_PAGE");
        const auto count = SlotsPerPage();
        for (int i = 0; i < count; ++i) {
            const auto slot = savePage * count + i; const auto suffix = std::to_string(i);
            Json saved;
            try { saved = ReadSave(slot); } catch (const std::exception&) { saved = nullptr; }
            const bool valid = saved.is_object() && saved.value("format", "") == "aether-witch-1";
            page->SetSpriteText(Wide("ID_SAVE_NUM_" + suffix), Wide("No. " + std::to_string(slot + 1)));
            page->SetSpriteText(Wide("ID_COMMENT_" + suffix), Wide(valid ? saved.at("message").value("text", "") : ""));
            page->SetSpriteText(Wide("ID_DATE_" + suffix), Wide(valid ? saved.value("date", "") : ""));
            if (valid) {
                const auto id = Wide("ID_NODATA_" + suffix);
                if (auto* placeholder = page->GetItemAs(id)) {
                    const auto position = placeholder->GetPosition2D();
                    delete page->DetachChild(placeholder);
                    const auto pixels = saved.value("thumbnail", Json::array());
                    if (pixels.is_array() && pixels.size() == 146 * 83) {
                        auto image = std::make_unique<SGLImage>();
                        SGLImageInfo info; info.width = 146; info.height = 83; info.format = formatImageARGB; info.depth = 32;
                        image->CreateBuffer(info);
                        for (int y = 0; y < 83; ++y) for (int x = 0; x < 146; ++x)
                            image->SetPixelRGBA(x, y, SGLPalette(pixels[y * 146 + x].get<uint32_t>()));
                        auto* sprite = new SGLSprite; sprite->AttachImage(image.get());
                        sprite->SetID(Wide("@thumbnail:" + suffix)); sprite->SetPosition(position.x, position.y);
                        sprite->ChangePriority(-100); sprite->ModifyUIFlag(SGLSprite::uiDisabled);
                        page->AddSmartChild(sprite); thumbnails.push_back(std::move(image));
                    }
                }
            }
            page->SetSpriteEnable(Wide("ID_SAVE_" + suffix), mode == "save" || valid);
        }
    }
    void Back() {
        pendingConfirmation.clear();
        if (mode == "story") { hidden = !hidden; message->SetVisible(!hidden); name->SetVisible(!hidden && !current.Get("name").empty()); }
        else if (previous == "title") Title(); else StoryUI();
    }
    void Confirm(const std::string& action, const std::string& form) {
        pendingConfirmation = action;
        dialog = Form(windowSkin, form, -1000);
        dialog->ModifyUIFlag(SGLSprite::uiModalFirst | SGLSprite::uiModalEnd);
    }
    void Log() {
        if (mode != "log") { previous = mode; logEnd = history.size(); }
        ClearUI(); mode = "log"; page = Form(windowSkin, "ID_MESSAGE_LOG");
        const auto start = logEnd > 3 ? logEnd - 3 : 0;
        page->SetSpriteScrollRange(L"ID_SCROLL_LOG", std::max<int>(history.size() - std::min<std::size_t>(history.size(), 3), 0));
        page->SetSpriteScrollPos(L"ID_SCROLL_LOG", start);
        for (std::size_t i = start; i < logEnd; ++i) {
            auto* entry = Form(windowSkin, "ID_MESSAGE_LOG_ENTRY", -10);
            entry->SetPosition(width * .17, height * .05 + (i - start) * height * .27);
            entry->SetSpriteText(L"ID_NAME", Wide(history[i].value("name", "")));
            entry->SetSpriteText(L"ID_MESSAGE", Wide(history[i].value("text", "")));
            if (auto* voice = entry->GetItemAs(L"ID_VOICE")) {
                voice->SetID(Wide("logvoice:" + std::to_string(i)));
                entry->SetSpriteEnable(Wide("logvoice:" + std::to_string(i)), !history[i].value("voices", "").empty());
            }
        }
    }
    void Choice(const Command& command) {
        choices = command.children;
        for (std::size_t i = 0; i < choices.size(); ++i) {
            auto* button = new SGLSpriteButton;
            button->SetID(Wide("choice:" + std::to_string(i)));
            SGLImageRect rect(0, 0, width * .6, 48); button->SetClickableRect(rect);
            button->SetPosition(width * .2, height * .3 + i * 60); button->ChangePriority(-1000);
            auto* text = new SGLSpriteText; text->SetTextFont(L"Default", 28); text->SetText(Wide(choices[i].Get("text")));
            text->ModifyUIFlag(SGLSprite::uiDisabled); button->AddSmartChild(text); ui->AddSmartChild(button);
        }
        waiting = true;
    }
    void CommandInput(const std::string& id) {
        if (!pendingConfirmation.empty()) {
            if (id == "ID_YES" || id == "ID_NO" || id == "back") {
                const auto action = pendingConfirmation; pendingConfirmation.clear();
                if (dialog) { delete ui->DetachChild(dialog); dialog = nullptr; }
                if (id == "ID_YES") {
                    if (action == "title") Title();
                    else if (action == "exit") done = true;
                    else if (action.rfind("save:", 0) == 0) { WriteSave(std::stoi(action.substr(5))); RefreshSaves(); }
                    else if (action.rfind("load:", 0) == 0) LoadSave(std::stoi(action.substr(5)));
                }
            }
            return;
        }
        if (id == "wheel-up" || id == "wheel-down") {
            if (mode == "log") {
                if (id == "wheel-up" && logEnd > 3) --logEnd;
                if (id == "wheel-down" && logEnd < history.size()) ++logEnd;
                Log();
            } else if (mode == "story") { if (id == "wheel-up") Log(); else CommandInput("advance"); }
            return;
        }
        if (id.rfind("logvoice:", 0) == 0 && mode == "log") {
            const auto index = std::stoul(id.substr(9));
            if (index < history.size()) Voices(history[index].value("voices", ""));
            return;
        }
        if (id.rfind("choice:", 0) == 0) { script.Choose(std::stoul(id.substr(7))); choices.clear(); waiting = false; StoryUI(); return; }
        if (id == "ID_START" && mode == "title") { Begin(); return; }
        if (id == "ID_EXIT") { Confirm("exit", "ID_MSGBOX_EXIT"); return; }
        if (id == "ID_TITLE") { Confirm("title", "ID_MSGBOX_TITLE"); return; }
        if (id == "back" || id == "ID_RETURN" || id == "ID_MSG_CLOSE") { Back(); return; }
        if (id == "ID_SAVE" && mode == "story" && waiting && choices.empty()) { SaveMenu(false); return; }
        if (id == "ID_LOAD") { SaveMenu(true); return; }
        if (id.rfind("ID_PAGE_", 0) == 0 && (mode == "save" || mode == "load")) { savePage = std::stoi(id.substr(8)); RefreshSaves(); return; }
        if (id.rfind("ID_SAVE_", 0) == 0 && (mode == "save" || mode == "load")) {
            const auto slot = savePage * SlotsPerPage() + std::stoi(id.substr(8));
            if (mode == "save") {
                if (!ReadSave(slot).is_null()) Confirm("save:" + std::to_string(slot), "ID_MSGBOX_OVERWRITE");
                else { WriteSave(slot); RefreshSaves(); }
            } else Confirm("load:" + std::to_string(slot), "ID_MSGBOX_LOAD");
            return;
        }
        if (id == "ID_LOG" && mode == "story") { Log(); return; }
        if (id == "ID_AUTO") automatic = !automatic;
        if (id == "ID_SKIP") skipping = !skipping;
        if (id == "advance" && mode == "story" && choices.empty()) {
            if (hidden) { hidden = false; StoryUI(); }
            else if (waiting) waiting = false;
            else until = {};
        }
    }
    void Execute(const Command& command) {
        const auto& tag = command.tag;
        if (tag == "msg") { ShowMessage(command); return; }
        if (tag == "select") { Choice(command); return; }
        if (tag == "bg" || tag == "ev" || tag == "char" || tag == "face" || tag == "layer") {
            if (tag == "bg") Remove("ev");
            Image(tag == "char" || tag == "layer" ? command.Get("id") : tag, command, tag); return;
        }
        if (tag == "clear") { Clear(command); return; }
        if (tag == "wait") { if (!skipping) until = Clock::now() + std::chrono::milliseconds(static_cast<int64_t>(command.Number("time"))); return; }
        if (tag == "waitse") { waitSound = "se:" + command.Get("buf", "0"); return; }
        if (tag == "bgm" || tag == "se" || tag == "bgv") {
            Play(tag == "bgm" ? tag : tag + ":" + command.Get("buf", "0"), command.Get("src"),
                 tag == "bgm" || command.Get("loop") == "true", command.Number("volume", 1)); return;
        }
        if (tag == "stopbgm" || tag == "stopse" || tag == "stopbgv") {
            const auto kind = tag.substr(4); Stop(kind == "bgm" ? kind : kind + ":" + command.Get("buf", "0"), command.Number("fadeout")); return;
        }
        if (tag == "begin_trans") { transition = command.Number("time"); return; }
        if (tag == "end_trans") { transition = 0; return; }
        if (tag == "chgmview") { view = command.Number("mode") != 0; StoryUI(); return; }
        if (tag == "msgshow") { const bool show = command.Get("show") != "false"; message->SetVisible(show); name->SetVisible(show && !current.Get("name").empty()); return; }
        if (tag == "pause") { waiting = true; return; }
        if (tag == "begin_keyskip") { keySkip = true; return; }
        if (tag == "end_keyskip") { keySkip = false; return; }
        if (tag == "wait_chgmv") { if (!skipping) until = std::max(until, animationUntil); return; }
        if (tag == "move") { Move(command); return; }
        if (tag == "shake") { if (!skipping) ShakeLayers(command); return; }
        if (tag == "flash") {
            if (!skipping) {
                Command flash{"layer", {{"src", command.Get("image", "white")}, {"z", "-10000"}, {"time", "0"}}, {}};
                Image("@flash", flash, "layer"); flashStart = Clock::now(); flashFade = std::max(command.Number("fade", 200), 1.0); flashCount = command.Number("count", 1);
            }
            return;
        }
        if (tag == "scrnfilter") { tone = command.Get("src"); SetFilters(); return; }
        if (tag == "set_filter") {
            if (command.Get("type") != "shading_off") Fail("unsupported sprite filter " + command.Get("type"));
            shade = true; shadeDegree = 0; SetFilters(); return;
        }
        if (tag == "fade_filter") {
            shadeDegree = std::clamp<int>(command.Number("degree"), 0, 256);
            auto* action = new SGLSpriteAction; action->SetFilterTo(*scene, shadeDegree); action->SetDuration(command.Number("time")); scene->AddAction(action); return;
        }
        if (tag == "clear_filter") { shade = false; shadeDegree = 0; SetFilters(); return; }
        if (tag == "begin_replay" || tag == "end_replay" || tag == "end_scene" || tag == "msgcount") return;
        Fail("unsupported command <" + tag + "> at " + script.Location());
    }
public:
    Player(ECSEnvironment& environment, const launcher::GameLaunchConfig& launch)
        : env(environment), config(launch), script([&](const std::string& module) { return ReadModule(env, module); }) {}
    void Initialize() {
        const auto* scriptTag = env.GetXMLDocumnet().GetElementTagAs(L"script");
        const auto* display = scriptTag ? scriptTag->GetElementTagAs(L"display") : nullptr;
        if (display) { width = display->GetAttrIntegerAs(L"width", width); height = display->GetAttrIntegerAs(L"height", height); }
        if (width <= 0 || height <= 0 || width > 16384 || height > 16384) Fail("invalid display dimensions");
        if (window.CreateDisplay(Wide(config.title), Window::modeWindow, width, height)) Fail("cannot create display");
        Skin(titleSkin, "wm_title.noa"); Skin(windowSkin, "wm_window.noa");
        scene = new SGLSprite; scene->ChangePriority(1000); scene->ModifyUIFlag(SGLSprite::uiDisabled);
        scene->CreateBuffer(width, height);
        ui = new SGLSprite; ui->ChangePriority(0);
        window.AddSmartChild(scene); window.AddSmartChild(ui);
        // Validate the entry before displaying a title that could not start.
        script.Start(config.entryScript); Title();
    }
    bool Tick() {
        Effects();
        while (!window.input.empty()) { auto id = window.input.front(); window.input.pop_front(); CommandInput(id); }
        if (mode == "log" && page) {
            const auto end = std::min<std::size_t>(std::max(page->GetSpriteScrollPos(L"ID_SCROLL_LOG"), 0) + 3, history.size());
            if (end != logEnd) { logEnd = end; Log(); }
        }
        if (mode == "story" && pendingConfirmation.empty()) {
            if (waiting && choices.empty() && (skipping || (automatic && Clock::now() >= nextAuto && !Playing("voice")))) waiting = false;
            if (!waitSound.empty() && !Playing(waitSound)) waitSound.clear();
            for (unsigned budget = 0; !waiting && waitSound.empty() && Clock::now() >= until && budget < 100; ++budget) {
                const auto command = script.Next();
                if (!command) { Title(); break; }
                Execute(*command);
            }
        }
        Json state = {{"mode", mode}, {"location", script.Location()}, {"waiting", waiting},
            {"messages", messageCount}, {"message_characters", mode == "story" && message && message->IsVisible() ? Wide(current.Get("text")).GetLength() : 0},
            {"layers", layerState}, {"sounds", soundState}, {"view", view}, {"choices", choices.size()}, {"last_command", window.lastCommand}};
        { std::lock_guard<std::mutex> guard(snapshotMutex); snapshot = std::move(state); }
        return !done;
    }
    void Close() { StopSounds(); ClearChildren(window); window.CloseDisplay(); }
};
}
Json Snapshot() { std::lock_guard<std::mutex> guard(snapshotMutex); return snapshot; }
int Run(ECSEnvironment& environment, const launcher::GameLaunchConfig& launch, const std::function<bool()>& cancelled) {
    auto* previous = SEnvironmentInterface::GetInstance();
    SEnvironmentInterface::AttachInstance(&environment);
    struct Restore { SEnvironmentInterface* previous; ~Restore() { SEnvironmentInterface::AttachInstance(previous); } } restore{previous};
    Player player(environment, launch);
    try {
        { Lock lock; player.Initialize(); }
        while (!cancelled()) {
            study::platform::sdl::WaitForRuntimeResume();
            if (cancelled()) break;
            { Lock lock; if (!player.Tick()) break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
        { Lock lock; player.Close(); }
    } catch (...) { Lock lock; player.Close(); throw; }
    return 0;
}
} // namespace entis::witch
