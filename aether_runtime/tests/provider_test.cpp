#include "entisgls/provider.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <cstdio>

namespace {
const auto& api=*AetherEntisGLSProvider();
unsigned checks=0, exits=0;
void require(bool ok,const char* text){++checks;if(!ok)throw std::runtime_error(text);}
struct Runtime {
    void* value=nullptr;
    ~Runtime(){if(value)api.destroy(value);}
};
nlohmann::json state(void* value){
    uint32_t length=0;require(api.get_plugin_debug_info(value,nullptr,0,&length)==ENGINE_RESULT_OK,"snapshot size");
    std::vector<char> json(length);require(api.get_plugin_debug_info(value,json.data(),length,&length)==ENGINE_RESULT_OK,"snapshot copy");
    return nlohmann::json::parse(json.data());
}
void waitFinished(void* value){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while(std::chrono::steady_clock::now()<deadline){
        require(api.tick(value,16)==ENGINE_RESULT_OK,"empty VM tick");
        if(state(value).at("finished").get<bool>())return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("VM did not terminate");
}
}
int main(int argc,char** argv){
    if(argc!=3)return 2;
    try{
        namespace fs=std::filesystem;
        const auto fixture=fs::absolute(argv[1]),root=fs::absolute(argv[2]);
        fs::create_directories(root);
        const auto writable=(root/"user").string(),cache=(root/"cache").string(),game=fixture.string();
        engine_runtime_host_v1_t host{};host.struct_size=sizeof(host);host.api_version=ENGINE_RUNTIME_PROVIDER_API_VERSION;
        host.platform_request=[](void*,const char* op,const char*){if(std::string(op)=="runtime_end")++exits;};
        engine_create_desc_t desc{};desc.struct_size=sizeof(desc);desc.api_version=ENGINE_API_VERSION;
        desc.writable_path_utf8=writable.c_str();desc.cache_path_utf8=cache.c_str();
        require(api.probe(nullptr,nullptr)==0&&api.probe(nullptr,root.c_str())==0,"unrelated roots are not claimed");
        require(api.probe(nullptr,game.c_str())>=90,"synthetic Cotopha detected");
        require(api.create(nullptr,&host,&desc,nullptr)==ENGINE_RESULT_INVALID_ARGUMENT,"null output rejected");
        {Runtime bad;auto shortDesc=desc;shortDesc.struct_size=1;require(api.create(nullptr,&host,&shortDesc,&bad.value)==ENGINE_RESULT_INVALID_ARGUMENT&&!bad.value,"short ABI rejected");}
        const auto open=[&](Runtime& instance){
            require(api.create(nullptr,&host,&desc,&instance.value)==ENGINE_RESULT_OK,"provider create");
            require(api.open_game(instance.value,game.c_str(),nullptr)==ENGINE_RESULT_OK,"provider open");
            engine_frame_desc_t frame{};frame.struct_size=sizeof(frame);
            require(api.get_frame_desc(instance.value,&frame)==ENGINE_RESULT_OK,"initial frame metadata");
            std::vector<uint8_t> pixels(size_t(frame.stride_bytes)*frame.height);
            require(api.read_frame_rgba(instance.value,pixels.data(),pixels.size())==ENGINE_RESULT_OK,"frame readable before first VM tick");
            require(pixels.size()>=4&&pixels[0]==0&&pixels[3]==255,"initial frame is opaque black");
            const auto json=state(instance.value);
            require(fs::u8path(json.at("save_directory").get<std::string>())==fs::canonical(fixture)/"savedata","game-local save directory");
        };
        fs::create_directories(fixture/"savedata");
        std::ofstream(fixture/"savedata/existing.dat") << "preserved";
        // The first paused VM also exercises cancellation before executing any
        // instructions. Its abort flag must not poison subsequent sessions.
        for(int i=0;i<8;++i){
            Runtime first;open(first);
            if(i==0){
                require(api.pause(first.value)==ENGINE_RESULT_OK,"pause before first tick");
                require(api.tick(first.value,16)==ENGINE_RESULT_OK,"start suspended");
                require(state(first.value).at("paused").get<bool>(),"initial pause retained");
                Runtime second;open(second);
                require(api.tick(second.value,16)!=ENGINE_RESULT_OK,"concurrent VM rejected");
            }else{
                const auto before=exits;waitFinished(first.value);
                require(exits==before+1,"successful completion emits runtime_end once");
                require(api.tick(first.value,16)==ENGINE_RESULT_OK&&exits==before+1,"completion is idempotent");
            }
        }
        std::ifstream existing(fixture/"savedata/existing.dat");std::string contents;existing >> contents;
        require(contents=="preserved","repeated provider starts preserve existing saves");
        require(!fs::exists(root/"user/entisgls"),"provider does not create an alternate save tree");
        std::printf("EntisGLS provider lifecycle PASS (%u checks)\n",checks);return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"Provider FAIL: %s\n",error.what());return 1;}
}
