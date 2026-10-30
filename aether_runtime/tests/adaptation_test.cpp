#include "input.h"
#include "font_discovery.h"
#include "game_hooks.h"
#include "compatibility/sdk/legacy/gls.h"
#include <sakuragl/erisa/sgl_erisa_archive_file.h>
#include "launcher/config_encoding.h"
#include "launcher/game_config.h"
#include "platform/sdl/system.h"
#include "platform/sdl/runtime_control.h"
#include "platform/sdl/sdl_pcm_stream.h"
#include "runtime/cotopha_port/legacy_sprite.h"
#include "runtime/cotopha_port/legacy_sprite_draw.h"
#include "runtime/cotopha_port/legacy_sprite_projection.h"
#include "runtime/cotopha_port/legacy_message.h"
#include <SDL3/SDL.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sakuraglx/sakuraglx.h>
#include <sakuraglx/render/sglx3d_scene_item.h>
#include <sakuraglx/render/sglx3d_scene_instancing.h>
#include <sakuraglx/render/sglx3d_scene_item_curve.h>
#include <filesystem>
#include <future>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
unsigned checks=0;
void require(bool value,const char* name) {++checks;if(!value)throw std::runtime_error(name);}
void near(double value,double expected,const char* name) {require(std::abs(value-expected)<0.0001,name);}
template<class F> void rejects(F f) {bool rejected=false;try{f();}catch(const entis::launcher::ConfigError&){rejected=true;}require(rejected,"invalid configuration must be rejected");}
void encoding() {
    using entis::launcher::DecodeConfigurationText;
    const std::string jp="\x93\xfa\x96\x7b\x8c\xea";
    for(const auto* name:{"Shift_JIS","shift-jis","CP932","Windows-31J","sjis"}) {
        const auto actual=DecodeConfigurationText(std::string("<?xml version='1.0' encoding='")+name+"'?><script caption='"+jp+"'/>");
        require(actual==u8"<?xml version='1.0' encoding='utf-8'?><script caption='日本語'/>","CP932 metadata survives decoding");
    }
    const std::u16string unicode=u"<?xml version='1.0' encoding='UTF-16'?><script caption='中文🌸'/>";
    for(bool little:{true,false}) {
        std::string bytes=little?"\xff\xfe":"\xfe\xff";
        for(auto c:unicode){bytes+=char(little?c:c>>8);bytes+=char(little?c>>8:c);}
        require(DecodeConfigurationText(bytes)==u8"<?xml version='1.0' encoding='utf-8'?><script caption='中文🌸'/>","UTF16 endian and supplementary scalar");
        bytes.pop_back();rejects([&]{DecodeConfigurationText(bytes);});
    }
    require(DecodeConfigurationText(u8"<script title='中文🌸'/>")==u8"<script title='中文🌸'/>","UTF8 unchanged");
    rejects([&]{DecodeConfigurationText("<?xml encoding='koi8-r'?><script/>");});
    rejects([&]{DecodeConfigurationText(std::string("\xff\xfe\x00\xd8",4));});
    rejects([&]{DecodeConfigurationText(std::string("\xfe\xff\xdc\x00",4));});
    rejects([&]{DecodeConfigurationText("<?xml encoding='Shift_JIS'?>\x81");});
    rejects([&]{DecodeConfigurationText("<?xml encoding='Shift_JIS'?>\x81\x20");});
    rejects([&]{DecodeConfigurationText(std::string(1024*1024+1,'x'));});
}
void input() {
    using aether::entisgls::TranslateInput;
    engine_input_event_t event{};event.struct_size=sizeof(event);SDL_Event out{};std::string text;
    event.type=ENGINE_INPUT_EVENT_POINTER_DOWN;event.x=320.25;event.y=240.5;
    for(int button=0;button<3;++button){event.button=button;require(TranslateInput(event,out,text),"pointer translation");require(out.button.button==(button==0?SDL_BUTTON_LEFT:button==1?SDL_BUTTON_RIGHT:SDL_BUTTON_MIDDLE),"host button ordering");near(out.button.x,320.25,"logical x");}
    event.button=3;require(!TranslateInput(event,out,text),"invalid button rejected");event.button=0;
    event.x=std::numeric_limits<double>::quiet_NaN();require(!TranslateInput(event,out,text),"nonfinite coordinates rejected");event.x=12;
    event.type=ENGINE_INPUT_EVENT_POINTER_UP;event.modifiers=ENGINE_INPUT_MODIFIER_POINTER_CANCEL;require(TranslateInput(event,out,text)&&out.button.x<0,"cancel avoids an accidental click");
    event.modifiers=0;event.type=ENGINE_INPUT_EVENT_POINTER_SCROLL;event.delta_y=-2;
    require(TranslateInput(event,out,text)&&out.wheel.y==-2,"wheel direction and units");
    event.type=ENGINE_INPUT_EVENT_KEY_DOWN;
    for(int key='A';key<='Z';++key){event.key_code=key;require(TranslateInput(event,out,text)&&out.key.scancode==SDL_SCANCODE_A+key-'A',"letter virtual key");}
    for(int key=0x70;key<=0x7b;++key){event.key_code=key;require(TranslateInput(event,out,text)&&out.key.scancode==SDL_SCANCODE_F1+key-0x70,"function virtual key");}
    for(int key:{8,9,13,16,17,18,27,32,33,34,35,36,37,38,39,40,45,46}){event.key_code=key;require(TranslateInput(event,out,text),"navigation virtual key");}
    event.type=ENGINE_INPUT_EVENT_TEXT_INPUT;event.unicode_codepoint=0x1f338;
    require(TranslateInput(event,out,text)&&text==u8"🌸"&&out.text.text==text.c_str(),"text scalar owns UTF8 payload");
    for(unsigned cp:{0u,0xd800u,0xdfffu,0x110000u}){event.unicode_codepoint=cp;require(!TranslateInput(event,out,text),"invalid scalar rejected");}
    event.struct_size=0;require(!TranslateInput(event,out,text),"short ABI input rejected");
}
ECSStructure* vector(double x,double y,double z) {
    auto* value=new ECSStructure;value->m_pwszTag=L"Vector";
    value->SetMemberAsReal(L"x",x);value->SetMemberAsReal(L"y",y);value->SetMemberAsReal(L"z",z);return value;
}
INT64 invoke(ECSContext& context,ECSSprite& sprite,const wchar_t* name,std::initializer_list<ECSObject*> values) {
    ECSObjArray<ECSObject> args;args.Add(new ECSReference(&sprite));for(auto* value:values)args.Add(value);
    int index;require(!sprite.GetFunction(context,index,name),"method resolves");
    const auto error=sprite.CallFunction(context,index,args);
    if(error)throw std::runtime_error(GetESLErrorMsg(error));
    std::unique_ptr<ECSObject> result(context.PopObject());INT64 number=0;
    require(result&&!result->OperateInteger(number),"native call returns integer");return number;
}
ECSArray* curve(double x0,double x1,double z) {
    auto* values=new ECSArray;for(int i=0;i<4;++i)values->m_varArray.Add(vector(x0+(x1-x0)*i/3.,0,z));return values;
}
void imageCache() {
    using namespace SakuraGL;
    ECSContext context;
    for(uint32_t background:{0xff112233u,0u}) {
        ECSSprite canvas,longSource,shortSource,hint;
        require(!canvas.NativeSprite().CreateBuffer(24,12),"cached tooltip canvas");
        canvas.NativeSprite().SetFillBackColor(background,true);
        SGLImage longImage,shortImage;
        SGLImageInfo info;info.width=12;info.height=6;info.depth=32;info.format=formatImageARGB;
        info.ptOrigin=SGLPoint(-5,-3);
        require(!longImage.CreateBuffer(info)&&!longImage.FillImage(SGLPalette(0xffffffffu)),"wide tooltip image");
        info.width=4;info.ptOrigin=SGLPoint(-9,-3);
        require(!shortImage.CreateBuffer(info)&&!shortImage.FillImage(SGLPalette(0xffff0000u)),"narrow tooltip image");
        longSource.NativeSprite().AttachImage(&longImage);shortSource.NativeSprite().AttachImage(&shortImage);
        canvas.NativeSprite().AddChild(&hint.NativeSprite());
        auto pixel=[&](int x,int y) {
            canvas.NativeSprite().BeforeDraw();SGLPalette result;
            require(!canvas.GetImage()->GetPixelRGBA(result,x,y),"read cached tooltip pixel");return result.ui32;
        };
        for(int pass=0;pass<3;++pass) {
            require(!invoke(context,hint,L"AttachImage",{new ECSReference(&longSource)}),"attach wide tooltip");
            require(pixel(6,4)==0xffffffffu,"old glyph pixels rendered");
            require(!invoke(context,hint,L"AttachImage",{new ECSReference(&shortSource)}),"replace with narrow tooltip");
            require(pixel(10,4)==0xffff0000u,"replacement glyph pixels rendered");
            require(pixel(6,4)==background,"shrinking tooltip clears old pixels outside new bounds");
        }
        require(!hint.Release(),"release tooltip");
        require(pixel(10,4)==background,"releasing an owned image clears its former bounds");
    }
}
void cameraCache() {
    using namespace SakuraGL;
    for(bool nested:{false,true}) {
        ECSContext context;ECSSprite canvas,group,left,right;
        require(!canvas.NativeSprite().CreateBuffer(64,32),"camera canvas");
        canvas.NativeSprite().SetFillBackColor(0xff112233u,true);
        canvas.NativeSprite().SetProjectionScreen(S3DVector(32,16,64));
        auto& scene=nested?group:canvas;
        if(nested) {
            require(!group.NativeSprite().CreateBuffer(64,32),"nested cached camera canvas");
            group.NativeSprite().SetFillBackColor(0xff112233u,true);
            group.NativeSprite().SetProjectionScreen(S3DVector(32,16,64));
            canvas.NativeSprite().AddChild(&group.NativeSprite());
        }
        for(auto* child:{&left,&right}) {
            require(!child->NativeSprite().CreateBuffer(8,8),"projected image");
            require(!child->GetImage()->FillImage(SGLPalette(child==&left?0xffff0000u:0xff00ff00u)),"projected pixels");
            auto* parameter=new ECSStructure;parameter->m_pwszTag=L"SpriteParam";
            parameter->SetMemberAsInt(L"nFlags",0x8001);parameter->SetMemberAsReal(L"rZScale",64);
            parameter->SetMemberAsReal(L"rZOrder",64);
            auto* position=new ECSStructure;position->m_pwszTag=L"Point";
            position->SetMemberAsInt(L"x",child==&left?20:40);position->SetMemberAsInt(L"y",12);
            parameter->AddNewVariable(L"ptDstPos",position);
            invoke(context,*child,L"SetParameter",{parameter});
            scene.NativeSprite().AddChild(&child->NativeSprite());
        }
        auto pixel=[&](int x,int y) {
            canvas.NativeSprite().BeforeDraw();SGLPalette result;
            require(!canvas.GetImage()->GetPixelRGBA(result,x,y),"read camera pixel");return result.ui32;
        };
        auto colorNear=[](uint32_t actual,uint32_t expected) {
            // Fractional scaling in the software rasterizer rounds premultiplied
            // channels. Allow those few levels while still rejecting stale pixels.
            for(int shift=0;shift<32;shift+=8)
                if(std::abs(int((actual>>shift)&255)-int((expected>>shift)&255))>3)return false;
            return true;
        };
        require(pixel(22,15)==0xffff0000u&&pixel(42,15)==0xff00ff00u,"both original projected sprites render");
        auto* positions=new ECSArray;
        for(int i=0;i<4;++i)positions->m_varArray.Add(vector(0,0,32.*i/3));
        invoke(context,canvas,L"SetCameraCurve",{positions,curve(0,0,64)});
        invoke(context,canvas,L"BeginActivation",{new ECSInteger(1000)});
        canvas.NativeSprite().BeforeDraw();
        canvas.NativeSprite().AdvanceTime(500);
        require(colorNear(pixel(17,15),0xffff0000u),"camera animation refreshes left sprite pixels");
        require(colorNear(pixel(51,15),0xff00ff00u),"camera animation refreshes right sprite pixels");
        require(pixel(40,15)==0xff112233u,"camera animation erases previous sprite position");
        EMemoryFile saved;require(!saved.Create(4096)&&!SaveLegacySpriteAnimation(saved,canvas),"save moving camera");
        invoke(context,canvas,L"CancelActivation",{});saved.Seek(0,ESLFileObject::FromBegin);
        require(!LoadLegacySpriteAnimation(saved,canvas)&&
                !CommitLegacySpriteDrawing(context,canvas,nullptr,0,SGLRect(),nullptr),"restore moving camera");
        canvas.NativeSprite().AdvanceTime(250);
        require(colorNear(pixel(56,15),0xff00ff00u),"restored camera action invalidates rendered children");
        invoke(context,canvas,L"FlushActivation",{});
        require(colorNear(pixel(60,15),0xff00ff00u),"flushing camera animation refreshes final frame");
        invoke(context,canvas,L"Enable3DViewCamera",{new ECSInteger(0)});
        require(pixel(42,15)==0xff00ff00u&&pixel(60,15)==0xff112233u,"disabling camera refreshes inherited cached views");
        invoke(context,canvas,L"Set3DViewCamera",{vector(0,0,16),vector(0,0,64)});
        require(colorNear(pixel(51,15),0xff00ff00u),"setting camera refreshes inherited cached views");
    }
}
void graphics() {
    using namespace SakuraGL;
    class TestFont final : public SGLFontObject {
    public:
        SGLFontObject* NewFont(const SGLFontStyle&) override { return new TestFont; }
        SGLError SetStyle(const SGLFontStyle&) override { return sglErrSuccess; }
        SGLError GetMetrics(uint8_t* raster,size_t bytes,SGLFontMetrics& metrics,uint32_t) override {
            metrics={};metrics.nWidth=metrics.nHeight=metrics.nAscent=2;
            metrics.rctExterior.w=metrics.rctExterior.h=1;
            if(raster){if(!bytes)return sglErrInvalidParam;raster[0]=255;}
            return sglErrSuccess;
        }
    };
    SGLFont::RegisterStockFont(SGLFontStyle::StandardFont,new TestFont);
    SGLFont::RegisterStockFont(L"Fixture Face & 中文",new TestFont);
    require(CheckLegacyMessage(),"message pixels, ruby and typewriter lifecycle");
    {
        ECSContext context;
        EMemoryFile saved;require(!saved.Create(4096),"message save buffer");
        {
            ECSMessageSprite message;
            require(!message.CreateMessage(512,160),"message canvas");
            require(invoke(context,message,L"SetFontFace",{new ECSString(L"Fixture Face & 中文")})==0,"custom font");
            size_t consumed=0;
            require(!message.OutputMessage(L"中文 text",consumed)&&consumed==7,"message before save");
            require(!message.Save(saved,context),"save custom font message");
        }
        saved.Seek(0,ESLFileObject::FromBegin);
        ECSMessageSprite restored;
        require(!restored.Load(saved,context),"load custom font message");
        const auto commit=restored.CommitAllReference(context);
        if(commit)throw std::runtime_error(GetESLErrorMsg(commit));
        require(restored.NativeMessage().GetMessageCharacterCount()==6,"restored message glyphs (space has no raster)");
        require(!restored.NativeMessage().IsMessagePending(),"restored text is fully visible");
        EMemoryFile again;require(!again.Create(4096)&&!restored.Save(again,context),"restored message can save again");
    }
    require(CheckLegacySpriteDraw(),"existing pixel, animation and hit-test regressions");
    {
        struct OwnedItem : SGLSprite {
            int& destroyed;
            explicit OwnedItem(int& count):destroyed(count){}
            ~OwnedItem() override {++destroyed;}
        };
        int destroyed=0;
        ECSSprite scriptChild;
        {
            ECSSprite form;
            form.NativeSprite().AddChild(&scriptChild.NativeSprite());
            form.NativeSprite().AddSmartChild(new OwnedItem(destroyed));
            require(!form.Release(),"release form resources");
            require(destroyed==1,"release destroys owned native skin items");
            require(scriptChild.NativeSprite().GetParent()==&form.NativeSprite(),"release retains script child order and ownership");
            auto* nested=new OwnedItem(destroyed);
            nested->AddSmartChild(new OwnedItem(destroyed));
            form.NativeSprite().AddSmartChild(nested);
        }
        require(destroyed==3,"destructor releases nested native items");
        require(scriptChild.NativeSprite().GetParent()==nullptr,"script child survives parent destruction");
    }
    SGLSprite::Parameter param;param.vDst=S3DDVector(840,460,2000);param.vZoom=S2DDVector(1,1);
    SGLSprite::Virtual3DParam screen;screen.m_vProjectScreen=S3DVector(640,360,1000);
    require(ProjectLegacySprite(param,500,false,&screen),"visible projection");
    near(param.vDst.x,740,"screen position focal length");near(param.vDst.y,410,"screen position y");near(param.vZoom.x,.25,"independent image focal length");
    for(double z:{-1.,0.,.5}){auto hidden=param;hidden.vDst.z=z;require(!ProjectLegacySprite(hidden,500,false,&screen),"near plane clips");}
    LegacySpriteCamera camera;camera.enabled=true;camera.position=S3DDVector(0,0,1000);camera.target=S3DDVector(0,0,2000);
    param.vDst=S3DDVector(840,460,2000);param.vZoom=S2DDVector(1,1);
    require(ProjectLegacySprite(param,500,false,&screen,&camera),"camera translation");near(param.vDst.x,840,"camera depth affects projection");near(param.vZoom.x,.5,"camera depth affects zoom");
    ECSContext context;
    ECSSprite parent,child,restored;parent.NativeSprite().AddChild(&child.NativeSprite());
    {
        auto* parameter=new ECSStructure;parameter->m_pwszTag=L"SpriteParam";
        parameter->SetMemberAsInt(L"nFlags",0x8040);parameter->SetMemberAsReal(L"rZScale",1000);
        parameter->SetMemberAsReal(L"rZOrder",1000);
        auto* center=new ECSStructure;center->m_pwszTag=L"Point";
        center->SetMemberAsInt(L"x",int(331.25*65536));center->SetMemberAsInt(L"y",int(84.5*65536));
        parameter->AddNewVariable(L"ptRevCenter",center);
        auto* destination=new ECSStructure;destination->m_pwszTag=L"Point";
        destination->SetMemberAsInt(L"x",int(331.25*65536));destination->SetMemberAsInt(L"y",int(84.5*65536));
        parameter->AddNewVariable(L"ptDstPos",destination);
        invoke(context,child,L"SetParameter",{parameter});
        near(child.NativeSprite().GetParameter().vCenter.x,331.25,"fixed-point rotation center uses pixel units");
        EMemoryFile saved;require(!saved.Create(4096)&&!SaveLegacySpriteVisual(saved,child),"fixed-center save");saved.Seek(0,ESLFileObject::FromBegin);
        require(!LoadLegacySpriteVisual(saved,restored),"fixed-center load");RestoreLegacySpriteVisual(restored);
        near(restored.NativeSprite().GetParameter().vCenter.y,84.5,"fixed-center survives old binary save format");
    }
    invoke(context,parent,L"Set3DViewCamera",{vector(11,22,33),vector(44,55,66),new ECSReal(.125)});
    LegacySpriteCamera observed;require(GetLegacySpriteCamera(&child.NativeSprite(),observed),"camera inherited");near(observed.position.y,22,"camera y distinct from x");near(observed.target.z,66,"camera target z");near(observed.roll,.125,"camera roll radians");
    EMemoryFile bytes;require(!bytes.Create(4096),"save buffer");require(!SaveLegacySpriteVisual(bytes,parent),"camera visual save");bytes.Seek(0,ESLFileObject::FromBegin);
    require(!LoadLegacySpriteVisual(bytes,restored),"camera visual load");RestoreLegacySpriteVisual(restored);
    require(GetLegacySpriteCamera(&restored.NativeSprite(),observed),"camera restored");near(observed.position.y,22,"saved camera y");near(observed.roll,.125,"saved camera roll");
    invoke(context,parent,L"Enable3DViewCamera",{new ECSInteger(0)});
    require(!GetLegacySpriteCamera(&child.NativeSprite(),observed),"disabled camera not inherited");
    invoke(context,parent,L"SetCameraCurve",{curve(0,100,0),curve(0,100,1000)});
    invoke(context,parent,L"BeginActivation",{new ECSInteger(1000)});
    parent.NativeSprite().AdvanceTime(500);
    require(GetLegacySpriteCamera(&parent.NativeSprite(),observed),"camera curve enables camera");near(observed.position.x,50,"camera curve midpoint");near(observed.target.x,50,"independent target curve midpoint");
    EMemoryFile animation;require(!animation.Create(4096),"animation buffer");require(!SaveLegacySpriteAnimation(animation,parent),"camera curve save");animation.Seek(0,ESLFileObject::FromBegin);
    require(!LoadLegacySpriteAnimation(animation,restored),"camera curve load");
    invoke(context,parent,L"FlushActivation",{});GetLegacySpriteCamera(&parent.NativeSprite(),observed);near(observed.position.x,100,"camera curve finishes exactly");
}
class StateImage final:public ECSExecutionImage {
public:
    StateImage() {
        const BYTE code[]={4,0,0,0,0,0,0,0,0,18,0};
        std::memset(&m_exiHeader,0,sizeof(m_exiHeader));
        m_exiHeader.nVersion=1;m_exiHeader.nIntBase=64;m_exiHeader.nStackSize=4096;m_exiHeader.nHeapSize=4096;
        m_exiHeader.fnStaticInitialize=UINT32_MAX;m_exiHeader.fnResumePrepare=UINT32_MAX;
        std::memcpy(m_bufImage.PutBuffer(sizeof(code)),code,sizeof(code));m_bufImage.Flush(sizeof(code));
        m_pImage=static_cast<BYTE*>(m_bufImage.ModifyBuffer(0,sizeof(code)));m_dwImageSize=sizeof(code);
    }
};

template<class Parse>
void sceneBinaryRecord(uint32_t type, size_t offset, size_t entrySize, Parse parse) {
    using namespace SSystem;
    using Header=SakuraGL::S3DSceneComposer::BinaryHeader;
    const uint32_t count=2;
    std::vector<uint8_t> bytes(sizeof(Header)+offset+count*entrySize,0);
    Header header{};header.nType=type;
    std::memcpy(bytes.data(),&header,sizeof(header));
    std::memcpy(bytes.data()+sizeof(Header),&count,sizeof(count));
    SString encoded;SArray<uint8_t> decoded;
    const auto decode=[&] {
        Charset::EncodeBase64(encoded,bytes.data(),bytes.size());
        return parse(decoded,encoded);
    };
    const auto* valid=decode();
    require(valid&&valid->count==count,"scene binary accepts two complete records");
    bytes.pop_back();require(!decode(),"scene binary rejects a truncated final record");
    bytes.push_back(0);
    const uint32_t oversized=UINT32_MAX;
    std::memcpy(bytes.data()+sizeof(Header),&oversized,sizeof(oversized));
    require(!decode(),"scene binary rejects an oversized count without overflow");
}
void sceneBinary() {
    using namespace SakuraGL;
    sceneBinaryRecord(S3DSceneComposer::binaryInstancing,
        offsetof(S3DSceneComposer::BinaryInstancingData,entries),
        sizeof(S3DSceneComposer::BinaryInstancingEntry),
        [](SSystem::SArray<uint8_t>& bytes,const wchar_t* text){return S3DItemInstancingSerializer::ParseData(bytes,text);});
    sceneBinaryRecord(S3DSceneComposer::binaryBezierCurve,
        offsetof(S3DSceneComposer::BinaryBezierCurveData,points),
        sizeof(S3DSceneComposer::BinaryBezierCurvePoint),
        [](SSystem::SArray<uint8_t>& bytes,const wchar_t* text){return S3DItemBezierCurveSerializer::ParseData(bytes,text);});
}

void serialization() {
    sceneBinary();
    ECSEnvironment environment;require(!environment.InitEnvironment(),"serialization environment");
    StateImage image;image.AttachCSEnvironment(&environment);ECSContext context;
    require(!context.InitializeContext(&image),"serialization context");
    struct Cleanup { ECSContext& context;~Cleanup(){context.ReleaseContext(true);} } cleanup{context};
    auto* hash=new ECSHash;auto* nested=new ECSArray;
    nested->m_varArray.Add(new ECSInteger(42));
    hash->m_varArray.SetAs(L"nested 中文",nested);
    hash->m_varArray.SetAs(L"empty",nullptr);
    image.m_csgGlobal.AddVariable(L"hash",hash);
    image.m_csgGlobal.AddVariable(L"reference",new ECSReference(nested->m_varArray.GetAt(0)));
    image.m_csgGlobal.IndexAllMember();
    EMemoryFile bytes;require(!bytes.Create(4096)&&!image.m_csgGlobal.Save(bytes,context),"serialize nested hash and cross-container reference");
    image.m_csgGlobal.CleanupAllReference(context);image.m_csgGlobal.RemoveAllVariable();
    bytes.Seek(0,ESLFileObject::FromBegin);
    require(!image.m_csgGlobal.Load(bytes,context)&&!image.m_csgGlobal.CommitAllReference(context),"rebuild nested reference graph");
    auto* restored=ESLTypeCast<ECSHash>(image.m_csgGlobal.GetVariableAt(0));
    require(restored&&restored->m_varArray.GetSize()==2,"hash slot count survives load");
    auto* array=ESLTypeCast<ECSArray>(restored->m_varArray.GetAs(L"nested 中文"));
    auto* ref=ESLTypeCast<ECSReference>(image.m_csgGlobal.GetVariableAt(1));
    require(array&&ref&&ref->m_pRef==array->m_varArray.GetAt(0),"reference retains target identity");
    INT64 value=0;require(!ref->OperateInteger(value)&&value==42,"restored reference usable");
    // A truncated native record must fail, rather than accept a partial hash.
    EMemoryFile hashBytes;require(!hashBytes.Create(128)&&!restored->Save(hashBytes,context),"hash fixture");
    for(unsigned length:{0u,1u,3u,unsigned(hashBytes.GetLength()-1)}) {
        EMemoryFile cut;require(!cut.Create(128),"truncation fixture");cut.Write(hashBytes.GetBuffer(),length);cut.Seek(0,ESLFileObject::FromBegin);
        ECSHash invalid;require(invalid.Load(cut,context)!=eslErrSuccess,"truncated hash rejected");
    }
}

void fonts(const std::filesystem::path& root) {
    namespace fs=std::filesystem;
    fs::create_directories(root/"setup"/"fonts");fs::create_directories(root/".hidden");
    for(const auto* path:{"setup/fonts/中文.OTF","b.ttf","a.ttc",".hidden/ignored.otf","not-a-font.txt"})
        std::ofstream(root/path)<<"fixture";
    std::ofstream(root/"empty.otf");
    std::error_code error;fs::create_symlink(root/"b.ttf",root/"linked.ttf",error);
    fs::create_directory_symlink(root/"setup",root/"linked-dir",error);
    const auto found=aether::entisgls::DiscoverBundledFonts(root);
    require(found==std::vector<std::string>({"a.ttc","b.ttf",u8"setup/fonts/中文.OTF"}),"font scan is bounded, relative, deterministic, case-insensitive and ignores symlinks/empty/hidden files");
}
void gameHooks(const std::filesystem::path& root) {
    namespace fs = std::filesystem;
    using namespace SSystem;
    auto wide = [](const std::string &text) { SString s; s.FromUTF8(reinterpret_cast<const uint8_t *>(text.c_str())); return s; };
    fs::create_directories(root / "savedata");
    std::ofstream(root / "savedata/slot.dat") << "progress";
    std::ofstream(root / "font.ttf") << "font selection fixture";
    {
        const std::vector<std::pair<const wchar_t *, std::string>> members = {
            {L"message.bin", "原生中文"}, {L"keep.bin", "original"}};
        ERISA::SGLArchiveFile::SDirectory directory;
        ERISA::SGLArchiveFile::FILE_ENTRY_EX entry{};
        entry.nEncodeType = ERISA::SGLArchiveFile::encodeRaw;
        for (const auto &member : members) directory.AddFileEntry(member.first, entry);
        ERISA::SGLArchiveFile::SDirectory graphics;
        graphics.AddFileEntry(L"base.eri", entry);
        entry.nAttribute = ERISA::SGLArchiveFile::attrDirectory;
        directory.AddFileEntry(L"gfx", entry);
        SStandardFileOpener files;
        std::unique_ptr<SFileInterface> file(files.NewOpenFile(wide((root / "fixture.noa").u8string()), SFile::modeCreate));
        ERISA::SGLArchiveFile archive;
        require(file && archive.OpenArchive(file.get(), false, SFile::modeCreate, &directory) == errSuccess, "synthetic NOA create");
        for (const auto &member : members) {
            require(archive.DescendFile(member.first) == errSuccess, "NOA member create");
            require(archive.Write(member.second.data(), member.second.size()) == member.second.size(), "NOA member write");
            require(archive.AscendFile() == errSuccess, "NOA member close");
        }
        require(archive.DescendDirectory(L"gfx", &graphics) == errSuccess, "NOA subdirectory create");
        require(archive.DescendFile(L"base.eri") == errSuccess, "NOA reference create");
        require(archive.Write("reference", 9) == 9 && archive.AscendFile() == errSuccess, "NOA reference write");
        require(archive.AscendDirectory() == errSuccess, "NOA subdirectory close");
        require(archive.CloseArchive() == errSuccess, "NOA close");
    }
    ECSEnvironment environment;
    require(environment.InitEnvironment() == errSuccess, "hook environment initialize");
    require(environment.AddFileArchive(wide((root / "fixture.noa").u8string()), "") == eslErrSuccess, "native archive mounted");
    environment.SetWritableFileOpener(new SOffsetFileOpener(wide((root / "savedata").u8string()), L'/', new SStandardFileOpener, true));
    auto read = [&](const wchar_t *path) {
        std::unique_ptr<SFileInterface> file(environment.NewOpenFile(path, SFile::shareRead));
        require(bool(file), "environment resource opens");
        std::string value(static_cast<size_t>(file->GetLength()), '\0');
        require(file->Read(value.data(), value.size()) == value.size(), "environment resource bytes");
        return value;
    };
    require(read(L"message.bin") == "原生中文", "native translation works without Lua");
    require(read(L"gfx/base.eri") == "reference", "native archive reference is readable before overrides");
    std::ofstream(root / "aether.lua") << R"(
        assert(aether.engine == 'entisgls' and aether.api_version == 1)
        aether.add_file('MESSAGE.BIN', '兜底中文')
        aether.add_file('empty.bin', '')
        aether.add_file('gfx/replacement.eri', 'image')
        aether.add_file('slot.dat', 'must not replace saves')
        aether.add_file('savedata/slot.dat', 'must not replace saves')
        aether.set_font('font.ttf')
    )";
    auto hooks = std::make_shared<aether::hooks::GameHooks>();
    hooks->Load(root, "entisgls");
    entis::launcher::GameLaunchConfig config;
    config.openTypeFonts.push_back({"original.ttf", "Default"});
    aether::entisgls::ApplyGameHooks(root, root / "cache", hooks, config);
    require(config.fontOverride == "storage://game/font.ttf", "selected physical font reaches the session configuration");
    {
        using namespace SakuraGL;
        class FixtureFont final : public SGLFontObject {
        public:
            SGLFontObject *NewFont(const SGLFontStyle &style) override {
                require(std::wstring(style.pszFace) == L"@test:lua", "selected font wins over the script's requested family");
                require(style.nSize == 24, "font override retains the requested style");
                return new FixtureFont;
            }
            SGLError SetStyle(const SGLFontStyle &) override { return sglErrSuccess; }
            SGLError GetMetrics(uint8_t *, size_t, SGLFontMetrics &metrics, uint32_t) override {
                metrics.nWidth = 7;
                return sglErrSuccess;
            }
        };
        SGLFont::RegisterStockFont(L"@test:lua", new FixtureFont);
        study::platform::sdl::SetFontOverride(L"@test:lua");
        SGLFont font; SGLFontStyle style; style.pszFace = L"Uninstalled game font"; style.nSize = 24;
        require(font.SetStyle(style) == sglErrSuccess, "font rendering uses explicit session selection");
        SGLFontMetrics metrics{};
        require(font.GetMetrics(nullptr, 0, metrics, 0x4e2d) == sglErrSuccess && metrics.nWidth == 7, "glyph queries reach the selected font");
        study::platform::sdl::SetFontOverride({});
        require(font.SetStyle(style) != sglErrSuccess, "clearing font selection restores native lookup");
    }
    const auto cache = hooks->CacheDirectory(root / "cache");
    environment.SetResourceOverrides(config.resourceOverrides);
    require(read(L"message.bin") == "兜底中文" && read(L"storage://game/message.bin") == "兜底中文", "Lua wins over archive through logical and game URLs");
    require(read(L"keep.bin") == "original", "unregistered archive resource retained");
    require(environment.IsExistingFile(L"empty.bin") && read(L"empty.bin").empty(), "zero-length resource is present");
    SFileOpener::State state{};
    require(environment.QueryFileState(L"message.bin", state) == errSuccess && state.nFileSize == std::string("兜底中文").size(), "metadata sees overridden bytes");
    {
        std::unique_ptr<ESLFileObject> file(environment.OpenFileObject("message.bin"));
        require(file && file->GetLength() == state.nFileSize, "traditional File API shares the override");
        std::unique_ptr<SFileInterface> image(environment.NewOpenFile(L"gfx/replacement.eri", SFile::shareRead));
        std::unique_ptr<SFileInterface> duplicate(image->Duplicate());
        std::unique_ptr<SFileInterface> reference(duplicate->NewOpenFile(L"base.eri", SFile::shareRead));
        require(reference && reference->GetLength() == 9, "duplicated override streams resolve reference assets in native archives");
        reference.reset(duplicate->NewOpenFile(L"gfx/base.eri", SFile::shareRead));
        require(reference && reference->GetLength() == 9, "archive-root relative references keep their original meaning");
        image.reset(environment.NewOpenFile(L"storage://game/gfx/replacement.eri", SFile::shareRead));
        reference.reset(image->NewOpenFile(L"base.eri", SFile::shareRead));
        require(reference && reference->GetLength() == 9, "game URL replacements retain archive references");
    }
    require(read(L"slot.dat") == "progress", "unqualified existing saves retain native precedence");
    require(environment.QueryFileState(L"slot.dat", state) == errSuccess && state.nFileSize == 8, "save metadata matches the native bytes");
    require(!config.resourceOverrides->NewOpenFile(L"message.bin", SFile::modeCreate), "override opener rejects writes");
    require(!config.resourceOverrides->IsExisting(L"savedata/slot.dat"), "explicit saves excluded from overrides");
    environment.SetResourceOverrides(nullptr); config.resourceOverrides.reset(); hooks.reset();
    require(!fs::exists(cache), "resource cache removed at session teardown");
    fs::remove(root / "aether.lua");
    require(read(L"message.bin") == "原生中文", "native translation restored after hook removal");
    std::ofstream(root / "aether.lua") << "aether.set_encoding('GBK')";
    hooks = std::make_shared<aether::hooks::GameHooks>(); hooks->Load(root, "entisgls");
    bool rejected = false;
    try { aether::entisgls::ApplyGameHooks(root, root / "cache", hooks, config); }
    catch (const std::runtime_error &e) { rejected = std::string(e.what()).find("aether.lua") != std::string::npos; }
    require(rejected, "unsupported CSX encoding is reported, not silently ignored");
    fs::remove(root / "aether.lua");
}
void control() {
    auto* mutex=ECSSakura2Processor::mutexGlobalAtomic;
    auto* signal=ECSSakura2Processor::signalLeave;
    ECSSakura2Processor::Initialize();
    require(mutex==ECSSakura2Processor::mutexGlobalAtomic&&signal==ECSSakura2Processor::signalLeave,"nested processor initialization shares synchronization objects");
    ECSSakura2Processor::Close();
    require(mutex==ECSSakura2Processor::mutexGlobalAtomic&&signal==ECSSakura2Processor::signalLeave,"one processor client cannot tear down another client");
    using namespace study::platform::sdl;
    ResetRuntimeControl();SetRuntimePaused(true);const auto before=RuntimeMilliseconds();
    const auto sdkBefore=SSystem::CurrentMilliSec();
    auto waiting=std::async(std::launch::async,[]{WaitForRuntimeResume();return true;});
    require(waiting.wait_for(std::chrono::milliseconds(30))==std::future_status::timeout,"script waits during pause");
    require(RuntimeMilliseconds()==before,"game time frozen during pause");
    require(SSystem::CurrentMilliSec()==sdkBefore,"SDK animation and audio-envelope clock frozen during pause");
    SetRuntimePaused(false);require(waiting.wait_for(std::chrono::seconds(1))==std::future_status::ready,"script resumes");
    require(RuntimeMilliseconds()-before<20,"resume excludes wall time spent paused");
    SetRuntimePaused(true);auto shutdown=std::async(std::launch::async,[]{WaitForRuntimeResume();});ResumeRuntimeForShutdown();
    require(shutdown.wait_for(std::chrono::seconds(1))==std::future_status::ready,"shutdown wakes paused workers");ResetRuntimeControl();
}
void audio() {
    using namespace studysteady::platform;
    require(SDL_InitSubSystem(SDL_INIT_AUDIO),"host audio lifetime");
    std::vector<std::int16_t> samples(48000,1200);
    {SdlPcmStream stream;require(stream.Open({SdlPcmStream::SampleType::signed16,48000,1}),"open PCM");
    require(stream.WriteStatic(samples.data(),samples.size()*2)&&stream.Play(true),"play PCM");
    SDL_Delay(80);require(stream.GetPlayingPosition()>0,"device consumes samples");
    require(SetSdlRuntimeAudioPaused(true),"host pause audio");SDL_Delay(20);auto position=stream.GetPlayingPosition();SDL_Delay(50);require(stream.GetPlayingPosition()==position,"host pause preserves audio position");
    require(stream.Pause(),"script can pause while host paused");require(SetSdlRuntimeAudioPaused(false),"host audio resumes");SDL_Delay(40);require(stream.GetPlayingPosition()==position,"host resume preserves script pause");
    require(stream.Restart(),"script resumes audio");SDL_Delay(80);require(stream.GetPlayingPosition()>position,"playback continues after both pauses");
    require(SetSdlMasterVolume(.4f),"master gain");float gain=0;require(GetSdlMasterVolume(gain)&&std::abs(gain-.4f)<.001,"master gain verified");}
    std::vector<std::future<bool>> jobs;
    for(int i=0;i<4;++i)jobs.emplace_back(std::async(std::launch::async,[&]{for(int j=0;j<8;++j){SdlPcmStream s;if(!s.Open({SdlPcmStream::SampleType::signed16,48000,1})||!s.WriteStatic(samples.data(),1024)||!s.Play(false)||!s.Close())return false;}return true;}));
    for(auto& job:jobs)require(job.get(),"concurrent audio open/close");
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}
}
int main(int argc,char** argv) {
    if(argc!=3)return 2;
    bool initialized=false;int result=0;
    try {
        require(SDL_Init(0),"SDL initialize");
        const auto root=std::filesystem::absolute(argv[2]);std::filesystem::create_directories(root);
        study::platform::sdl::SystemPaths paths;paths.assetsRoot=paths.storageRoot=paths.localRoot=paths.gameRoot=root.string();
        require(study::platform::sdl::ConfigureSystemPaths(paths),"fixture paths");SakuraGL::Initialize();ECotophaScript::Initialize(0);ECotophaScript::MultithreadReference(true);initialized=true;
        const std::string mode=argv[1];
        if(mode=="game-hooks")gameHooks(root);else if(mode=="encoding")encoding();else if(mode=="input")input();else if(mode=="graphics")graphics();else if(mode=="image-cache")imageCache();else if(mode=="camera-cache")cameraCache();else if(mode=="control")control();else if(mode=="audio")audio();else if(mode=="fonts")fonts(root);else if(mode=="serialization")serialization();else throw std::runtime_error("unknown test mode");
        std::printf("EntisGLS %s PASS (%u checks)\n",argv[1],checks);
    }catch(const std::exception& error){std::fprintf(stderr,"EntisGLS adaptation FAIL: %s\n",error.what());result=1;}
    study::platform::sdl::ResumeRuntimeForShutdown();
    if(initialized){ECotophaScript::Release();SakuraGL::Finalize();}SDL_Quit();return result;
}
