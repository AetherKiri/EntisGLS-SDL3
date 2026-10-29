#pragma once
#include <sakuraglx/sprite/sglx_sprite.h>
#include <cmath>

// GLS3 has two independent focal lengths: the parent's screen projects the
// position, while SpriteParam.rZScale projects the image size. GLS4's single
// scale cannot express this. Keep script parameters in world coordinates so
// animation, hit tests and saved contexts all use the same projection.
bool GetLegacySpriteZScale(const SakuraGL::SGLSprite*, double& focal, bool& fixed);
struct LegacySpriteCamera {
    bool enabled = false;
    SakuraGL::S3DDVector position{0, 0, 0}, target{0, 0, 1};
    double roll = 0; // GLS3 camera angles are radians, sprite angles degrees.
};
bool GetLegacySpriteCamera(const SakuraGL::SGLSprite*, LegacySpriteCamera&);
bool ReadLegacySpriteCamera(class ECSSprite&, LegacySpriteCamera&);
inline bool ProjectLegacySprite(SakuraGL::SGLSprite::Parameter& param,
    double focal, bool fixed, const SakuraGL::SGLSprite::Virtual3DParam* view,
    const LegacySpriteCamera* camera = nullptr) {
    using namespace SakuraGL;
    auto& position = param.vDst;
    if (view) {
        position.x -= view->m_vProjectScreen.x;
        position.y -= view->m_vProjectScreen.y;
        if (camera && camera->enabled) {
            S3DDMatrix matrix(1, 1, 1);
            matrix.RevolveOnZ(std::sin(camera->roll), std::cos(camera->roll));
            matrix.RevolveByAngleOn(camera->target - camera->position);
            position -= camera->position;
            matrix.RevolveVector(position);
            param.zAngle -= camera->roll * (180.0 / 3.14159265358979323846);
        }
    }
    if (!std::isfinite(position.z) || position.z < 1 || !std::isfinite(focal)) return false;
    param.vZoom.x *= focal / position.z;
    param.vZoom.y *= focal / position.z;
    if (view) {
        const double scale = view->m_vProjectScreen.z / position.z;
        position.x = position.x * scale + view->m_vProjectScreen.x;
        position.y = position.y * scale + view->m_vProjectScreen.y;
    }
    if (!std::isfinite(position.x) || !std::isfinite(position.y) ||
        std::abs(position.x) >= 16384 || std::abs(position.y) >= 16384) return false;
    const double unit = fixed ? 65536 : 1;
    position.x = std::round(position.x * unit) / unit;
    position.y = std::round(position.y * unit) / unit;
    return true;
}
