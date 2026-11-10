#include "extensions/emote/tjs_runtime/gles_render_manager.h"
#include "extensions/emote/tjs_runtime/gles_scene_bridge.h"
#include <SDL3/SDL.h>
#include <cstdio>
#include <stdexcept>

namespace {
using Item = motion::detail::PreparedRenderItem;
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void rectangle(Item& item, motion::detail::MotionNode::SourceState& source,
               float left, float top, float right, float bottom) {
    item.sourceState = &source;
    item.hasOwnSource = true;
    item.drawFlag = true; item.rawFlag16 = item.skipFlag0 = item.skipFlag1 = false;
    item.parentItem = nullptr; item.opacity = 255; item.blendMode = 0; item.meshType = 0;
    item.packedColors.fill(0xff808080);
    item.corners = {left, top, right, top, right, bottom, left, bottom};
    item.paintBox = {left, top, right, bottom}; item.viewport = {1, 1, -1, -1};
}
void run() {
    using namespace studysteady::motion;
    GlesRenderManager renderer; useRenderManager(&renderer);
    const uint8_t white[] = {255, 255, 255, 255};
    auto* sourceTexture = renderer.CreateTexture2D(white, 4, 1, 1, TVPTextureFormat::RGBA, 0);
    auto* target = renderer.CreateTexture2D(nullptr, 0, 8, 8, TVPTextureFormat::RGBA, 0);
    motion::detail::MotionNode::SourceState source{};
    source.blank = false; source.valid = true; source.texture = sourceTexture;
    source.width = source.height = 1; source.textureRect = {0, 0, 1, 1};
    Item iris, mask, group;
    rectangle(iris, source, 2, 2, 6, 6);
    rectangle(mask, source, 2, 4, 6, 6);
    rectangle(group, source, 2, 2, 6, 6);
    group.rawFlag16 = group.skipFlag0 = true; group.stencilComposite = 5;
    group.childItems = {&mask}; iris.parentItem = &group;
    mask.opacity = 0;
    motion::detail::PreparedRenderItemList items{&iris, &mask};
    auto render = [&] {
        renderer.clearTarget(target);
        renderSceneItems(false, target,
            [target](bool, const tTVPRect&) { return motion::D3DTargetTexturePair_guess(target, target); },
            {0, 0, 8, 8}, [](Item& item) { return item.sourceState->texture; }, items, 0, 0);
        return renderer.readTarget(target);
    };
    auto pixels = render();
    auto alpha = [&](int x, int y) { return pixels[(y * 8 + x) * 4 + 3]; };
    require(alpha(3, 3) == 0, "an invisible mask drawn later must cover the upper iris");
    require(alpha(3, 5) == 255, "the iris inside the invisible mask remains visible");
    require(alpha(1, 5) == 0, "mask-only geometry must not paint color");
    group.stencilComposite = 6;
    pixels = render();
    require(alpha(3, 3) == 255 && alpha(3, 5) == 0, "outer masks invert the covered region");
    Item ancestor, outerShape;
    rectangle(ancestor, source, 2, 2, 6, 6);
    rectangle(outerShape, source, 2, 2, 4, 6);
    ancestor.rawFlag16 = ancestor.skipFlag0 = true;
    ancestor.stencilComposite = 5; ancestor.childItems = {&outerShape};
    group.parentItem = &ancestor; group.stencilComposite = 5;
    pixels = render();
    require(alpha(3, 5) == 255 && alpha(5, 5) == 0 && alpha(3, 3) == 0,
        "nested inner masks intersect their coverage");
    ancestor.stencilComposite = 6;
    pixels = render();
    require(alpha(3, 5) == 0 && alpha(5, 5) == 255 && alpha(5, 3) == 0,
        "an inner mask inside an outer mask preserves both requirements");
    group.parentItem = nullptr;
    group.childItems.clear(); group.stencilComposite = 5;
    pixels = render();
    require(alpha(3, 3) == 0 && alpha(3, 5) == 0, "an empty inner mask cannot leak the full image");
    iris.parentItem = nullptr;
    pixels = render();
    require(alpha(3, 3) == 255 && alpha(3, 5) == 255, "a later unmasked frame resets stencil state");
    target->Release(); sourceTexture->Release();
}
}
int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) { std::fprintf(stderr, "No video device: %s\n", SDL_GetError()); return 77; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, 0);
    auto* window = SDL_CreateWindow("Motion mask test", 8, 8, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    auto context = window ? SDL_GL_CreateContext(window) : nullptr;
    int result = context ? 0 : 77;
    if (!context) std::fprintf(stderr, "No GL context: %s\n", SDL_GetError());
    try { if(context) { run(); std::puts("Motion mask pixels PASS"); } }
    catch (const std::exception& error) { std::fprintf(stderr, "%s\n", error.what()); result = 1; }
    if (context) SDL_GL_DestroyContext(context);
    if (window) SDL_DestroyWindow(window);
    SDL_Quit(); return result;
}
