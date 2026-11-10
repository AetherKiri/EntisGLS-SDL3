// Included after the imported geometry helpers by motion_prepare_player.py.
// A composite mask is a dependency, not a visible draw in Z order. In
// particular, an opacity-zero mask can follow the image it clips. Build its
// coverage separately without borrowing the published surface's color alpha.
void renderSceneItems(bool priorDraw, iTVPTexture2D* targetTexture,
    const D3DTargetTextureGetter_guess& targetTextureGetter,
    const tTVPRect& targetRect,
    const D3DSourceTextureGetter_guess& sourceTextureGetter,
    detail::PreparedRenderItemList& mainList, float xOffset, float yOffset) {
    auto* manager = ::motion::render_backend_guess::getPrivateOpenGLRenderManager_guess();
    manager->BeginStencil(targetTexture);
    struct ResetStencil {
        iTVPRenderManager* manager;
        ~ResetStencil() { glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); manager->EndStencil(); }
    } reset{manager};
    ::motion::render_backend_guess::TriangleBatch_guess batch(manager);

    auto draw = [&](PreparedRenderItem& item, bool mask) {
        if (!item.hasOwnSource || item.rawFlag16 || item.skipFlag0 || item.sourceState->blank) return;
        int opacity = mask ? 255 : item.opacity;
        if (!mask && priorDraw) opacity /= 2;
        if (opacity <= 0) return;
        std::array<float, 4> clip;
        if (!computeD3DClip_guess(item, targetRect.right, targetRect.bottom, clip)) return;
        const tTVPRect clipped(std::max(targetRect.left, int(clip[0])),
            std::max(targetRect.top, int(clip[1])),
            std::min(targetRect.right, int(std::ceil(clip[2]))),
            std::min(targetRect.bottom, int(std::ceil(clip[3]))));
        auto* source = sourceTextureGetter(item);
        const auto rect = item.sourceState->textureRect;
        if (rect[2] <= rect[0] || rect[3] <= rect[1]) return;
        const auto color = mask ? 0xffffffffu : d3dPackedColorWithOpacity(item, opacity);
        auto* method = batch.selectMethod_guess(mask ? 0 : item.blendMode & 15, color, true, mask);
        if (!method) return;
        if (item.meshType == 0) {
            appendD3DAffine_guess(batch, method, targetTextureGetter, clipped,
                item, source, rect, xOffset, yOffset, color);
        } else if (item.meshType == 1) {
            const auto divisions = renderBezierPatchCellDivisions_guess(item.commandPatchDivision,
                item.sourceState->width, item.sourceState->height);
            const auto bounds = buildOffsetMeshPoints(item.meshPoints, xOffset, yOffset);
            const auto mesh = ::motion::render_backend_guess::tessellateBezierPatch_guess(
                bounds, divisions[0], divisions[1]);
            appendD3DMesh_guess(batch, method, targetTextureGetter, clipped, source, rect,
                bounds, mesh, divisions[0], divisions[1], color);
        } else if (item.meshType == 2) {
            const auto mesh = buildOffsetMeshPoints(item.commandCompositeMeshPoints, xOffset, yOffset);
            appendD3DMesh_guess(batch, method, targetTextureGetter, clipped, source, rect,
                mesh, mesh, item.meshDivX, item.meshDivY, color);
        }
    };

    PreparedRenderItem* previousMask = nullptr;
    glDisable(GL_STENCIL_TEST);
    for (auto* item : mainList) {
        if (shouldSkipD3DRenderItem_guess(*item, priorDraw) || item->opacity <= 0) continue;
        auto* parent = priorDraw ? nullptr : item->parentItem;
        if (parent != previousMask) {
            batch.flush_guess();
            if (!parent) {
                glDisable(GL_STENCIL_TEST);
            } else {
                std::vector<PreparedRenderItem*> chain;
                for (auto* ancestor = parent; ancestor; ancestor = ancestor->parentItem) {
                    if (chain.size() == 8 || std::find(chain.begin(), chain.end(), ancestor) != chain.end())
                        throw std::runtime_error("Motion mask ancestry exceeds the stencil buffer or contains a cycle");
                    chain.push_back(ancestor);
                }
                glDisable(GL_SCISSOR_TEST);
                glStencilMask(255); glClearStencil(0); glClear(GL_STENCIL_BUFFER_BIT);
                glEnable(GL_STENCIL_TEST);
                glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
                unsigned bits = 0, reference = 0;
                for (auto at = chain.rbegin(); at != chain.rend(); ++at) {
                    auto* group = *at;
                    const unsigned bit = bits + 1;
                    glStencilMask(bit);
                    glStencilFunc(GL_EQUAL, reference | bit, bits);
                    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
                    if (group->childItems.empty()) draw(*group, true);
                    else for (auto* shape : group->childItems) draw(*shape, true);
                    batch.flush_guess();
                    bits |= bit;
                    if ((group->stencilComposite & 3) != 2) reference |= bit;
                }
                glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
                glStencilMask(0); glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
                glStencilFunc(GL_EQUAL, reference, bits);
            }
            previousMask = parent;
        }
        draw(*item, false);
    }
    batch.flush_guess();
}
