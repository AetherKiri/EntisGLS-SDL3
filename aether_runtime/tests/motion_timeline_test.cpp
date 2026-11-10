#include "extensions/emote/tjs_runtime/motion_apk_runtime.h"
#include "extensions/emote/tjs_runtime/timeline_playback.h"
#include "ResourceManager.h"
#include "ncbind.hpp"
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
        std::puts("Motion SDK timeline semantics PASS");
    } catch(const eTJSError& e){std::fprintf(stderr,"%s\n",e.GetMessage().AsStdString().c_str());result=1;}
      catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());result=1;}
    study_motion_destroy(runtime,error,sizeof(error));return result;
}
