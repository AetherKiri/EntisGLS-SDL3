#include "extensions/emote/tjs_runtime/motion_apk_runtime.h"
#include "extensions/emote/tjs_runtime/timeline_playback.h"
#include "ResourceManager.h"
#include "ncbind.hpp"
#include "EmoteVarController.h"
#include "EmoteSelectorController.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>

namespace {
void require(bool value, const char* message) { if(!value)throw std::runtime_error(message); }
}
int main() {
    char error[512]{};
    auto* runtime=study_motion_create(error,sizeof(error));
    if(!runtime){std::fprintf(stderr,"%s\n",error);return 1;}
    int result=0;
    try {
        // Real timeline ownership/selection without copyrighted model assets.
        auto* manager=ncbInstanceAdaptor<::motion::ResourceManager>::CreateAdaptor(new ::motion::ResourceManager());
        tTJSVariant owner(manager,manager);manager->Release();
        ::motion::EmoteEngine engine{owner};
        for(const char* name:{"idle","pose","idle2"}) {
            auto& state=engine._timelineStates[ttstr(name)];
            state.timelineData=std::make_unique<::motion::detail::EmoteTimelineData>();
        }
        auto play=[&](const char* name,uint32_t flags){studysteady::motion::PlaySdkTimeline(engine,ttstr(name),flags);};
        auto playing=[&](const char* name){return engine.isTimelinePlaying_guess(ttstr(name));};
        play("idle",2); play("pose",1);
        require(playing("idle")&&playing("pose"),"SDK parallel playback must retain the idle differential timeline");
        play("idle2",2);
        require(playing("idle2")&&!playing("idle")&&!playing("pose"),"replacing the default timeline must remove the previous loops");
        require((engine._timelineStates.at(ttstr("idle2")).flags&2)!=0,"differential playback keeps its controller mode");
        play("pose",0);
        require(playing("pose")&&!playing("idle2"),"ordinary SDK playback replaces active timelines");
        engine.stopTimeline_guess(ttstr());
        require(!engine.isTimelinePlaying_guess(ttstr()),"explicit stop clears all timelines");
        // Timeline decoding stores easing powers, while SetVariable accepts a
        // signed easing parameter. Exercise seek AND the next-keyframe route
        // through a real pose controller, not just the conversion helper.
        const ttstr poseLabel("body_LR");
        engine._auxVarDeque8.emplace_back(new ::motion::EmoteVarController(1), poseLabel, 1);
        engine._variableControllerRefs[poseLabel] = {7, 0};
        auto* controller = engine._auxVarDeque8.back().ctl.get();
        for (const double power : {1.0, 2.0, 0.5}) {
            float zero = 0;
            ::motion::EmoteVarController_setTarget_guess(controller, &zero, 0, 1, false);
            ::motion::detail::EmoteTimelineState state;
            state.timelineData = std::make_unique<::motion::detail::EmoteTimelineData>();
            state.timelineData->variableList.emplace_back();
            auto& track = state.timelineData->variableList.back();
            track.label = poseLabel;
            track.frameList = {{0, false, 100, power}, {13, false, 0, power}, {26, true, 0, 0}};
            engine.seekTimeline_guess(state, 0);
            float value = 0;
            ::motion::EmoteVarController_step(controller, &value, 6);
            const double fraction = std::pow(0.5, power);
            require(std::abs(value - 100 * fraction) < 0.001, "pose seek must retain authored easing");
            ::motion::EmoteVarController_step(controller, &value, 6);
            require(std::abs(value - 100) < 0.001, "pose transition reaches its endpoint");
            engine.applyTimelineWindow_guess(state, true, 13);
            ::motion::EmoteVarController_step(controller, &value, 6);
            require(std::abs(value - 100 * (1 - fraction)) < 0.001, "next pose keyframe must retain authored easing");
        }
        // A selector changes the discrete arm index immediately, but its two
        // visibility controllers must crossfade for the authored duration.
        ::motion::EmoteVarController oldArm(1), newArm(1);
        auto* selector = new ::motion::EmoteSelectorController({{&oldArm, 1, 0}, {&newArm, 1, 0}});
        engine._vectorVarDeque9.emplace_back(selector, ttstr("arm_type"));
        engine._vectorVarDeque9.back().flag = 1;
        engine._variableControllerRefs[ttstr("arm_type")] = {8, 0};
        ::motion::detail::EmoteTimelineState arm;
        arm.timelineData = std::make_unique<::motion::detail::EmoteTimelineData>();
        arm.timelineData->variableList.emplace_back();
        arm.timelineData->variableList.back().label = ttstr("arm_type");
        arm.timelineData->variableList.back().frameList = {{0, false, 1, 1}, {16, true, 0, 0}};
        engine.seekTimeline_guess(arm, 0);
        float selected = 0, oldFade = 0, newFade = 0;
        ::motion::EmoteSelectorController_step(selector, &selected, 0);
        ::motion::EmoteVarController_step(&oldArm, &oldFade, 5);
        ::motion::EmoteVarController_step(&newArm, &newFade, 5);
        require(selected == 1 && std::abs(oldFade - 1.0f/3) < 0.001 && std::abs(newFade - 2.0f/3) < 0.001,
            "arm pose must crossfade, not replace both visibility values on its first frame");
        engine._vectorVarDeque9.clear(); // borrowed controllers must outlive their selector
        std::puts("Motion SDK timeline semantics PASS");
    } catch(const eTJSError& e){std::fprintf(stderr,"%s\n",e.GetMessage().AsStdString().c_str());result=1;}
      catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());result=1;}
    study_motion_destroy(runtime,error,sizeof(error));return result;
}
