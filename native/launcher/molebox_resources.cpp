#include "launcher/molebox_resources.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <set>
#include <stdexcept>
#include <zlib.h>

namespace entis::launcher {
namespace {
using Bytes = std::vector<uint8_t>;
constexpr size_t maxBytes = 256 * 1024 * 1024;
[[noreturn]] void Invalid() { throw std::runtime_error("Invalid or unsupported Molebox resource package"); }
void Range(const Bytes& b, size_t p, size_t n) { if (p > b.size() || n > b.size() - p) Invalid(); }
uint16_t U16(const Bytes& b, size_t p) { Range(b,p,2); return b[p] | uint16_t(b[p+1]) << 8; }
uint32_t U32(const Bytes& b, size_t p) { return U16(b,p) | uint32_t(U16(b,p+2)) << 16; }
Bytes Slice(const Bytes& b, size_t p, size_t n) { Range(b,p,n); return {b.begin()+p,b.begin()+p+n}; }
bool Match(const Bytes& b, size_t p, const char* s, size_t n) {
    return p <= b.size() && n <= b.size()-p && !std::memcmp(b.data()+p,s,n);
}
// Robert Scott's NewDES substitution permutation. This older package revision
// uses independent 64-bit blocks and the first 15 bytes of each catalog key.
constexpr uint8_t substitution[] = {
32,137,239,188,102,125,221,72,212,68,81,37,86,237,147,149,70,229,17,124,115,207,33,20,122,143,25,215,51,183,138,142,
146,211,110,173,1,228,189,14,103,78,162,36,253,167,116,255,158,45,185,50,98,168,250,235,54,141,195,247,240,63,148,2,
224,169,214,180,62,22,117,108,19,172,161,159,160,47,43,171,194,175,178,56,196,112,23,220,89,21,164,130,157,8,85,251,
216,44,94,179,226,38,90,119,40,202,34,206,35,69,231,246,29,109,74,71,176,6,60,145,65,13,77,151,12,127,95,199,57,101,
5,232,150,210,129,24,181,10,121,187,48,193,139,252,219,64,88,233,96,128,80,53,191,144,218,11,106,132,155,104,91,136,
31,42,243,66,126,135,30,26,87,186,182,154,242,123,82,166,208,39,152,190,113,205,114,105,225,84,73,163,99,111,204,61,
200,217,170,15,198,28,192,254,134,234,222,7,236,248,201,41,177,156,92,131,67,249,245,184,203,9,241,0,27,46,133,174,75,18,
93,209,100,120,76,213,16,83,4,107,140,52,58,55,3,244,97,197,238,227,118,49,79,230,223,165,153,59};
void Decipher(Bytes& bytes, const uint8_t* password) {
    std::array<uint8_t,60> key{};
    unsigned pos=11, at=0;
    for (unsigned round=0; round<9; ++round) {
        for (unsigned j=0;j<4;++j) {key[at++]=password[pos];pos=(pos+1)%15;}
        if(round==8)break;
        pos=(pos+8)%15;
        for(unsigned j=0;j<3;++j) {key[at++]=password[pos];pos=(pos+1)%15;}
        pos=(pos+8)%15;
    }
    for(size_t p=0;p+8<=bytes.size();p+=8) {
        auto* b=bytes.data()+p;at=0;
        for(unsigned round=0;round<9;++round) {
            for(unsigned j=0;j<4;++j)b[4+j]^=substitution[b[j]^key[at++]];
            if(round==8)break;
            b[1]^=substitution[b[4]^key[at++]];
            b[2]^=substitution[b[4]^b[5]];
            b[3]^=substitution[b[6]^key[at++]];
            b[0]^=substitution[b[7]^key[at++]];
        }
    }
}
void CheckCRC(const Bytes& b,uint32_t expected) {
    if (crc32(0,b.data(),static_cast<uInt>(b.size()))!=expected)Invalid();
}
uint16_t CRC16(const uint8_t* data,size_t count) {
    uint16_t crc=0;
    for(size_t i=0;i<count;++i) {
        crc^=data[i];
        for(unsigned bit=0;bit<8;++bit)crc=(crc>>1)^((crc&1)?0xa001:0);
    }
    return crc;
}
std::string String8(const Bytes& b,size_t at) {
    Range(b,at,1);auto end=std::find(b.begin()+at,b.end(),0);
    if(end==b.end() || end-(b.begin()+at)>4096)Invalid();
    return {b.begin()+at,end};
}
std::string Name(const Bytes& b,size_t at,size_t length) {
    if(length>4096)Invalid();Range(b,at,length*2);
    std::string out;
    for(size_t i=0;i<length;++i) {
        uint32_t c=U16(b,at+2*i);
        if(c>=0xd800 && c<=0xdbff) {
            if(++i>=length)Invalid();const auto low=U16(b,at+2*i);
            if(low<0xdc00 || low>0xdfff)Invalid();c=0x10000+((c-0xd800)<<10)+(low-0xdc00);
        } else if(c>=0xdc00 && c<=0xdfff)Invalid();
        if(!c || c=='/' || c=='\\' || c==':')Invalid();
        if(c<0x80)out+=char(c);
        else if(c<0x800) {out+=char(0xc0|(c>>6));out+=char(0x80|(c&63));}
        else if(c<0x10000) {out+=char(0xe0|(c>>12));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}
        else {out+=char(0xf0|(c>>18));out+=char(0x80|((c>>12)&63));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}
    }
    if(out=="." || out=="..")Invalid();return out;
}
}

io::ReadOnlyFiles ReadMoleboxResources(const Bytes& exe) {
    io::ReadOnlyFiles files;
    if(!Match(exe,0,"MZ",2) || exe.size()<64)return files;
    const size_t pe=U32(exe,60);
    if(!Match(exe,pe,"PE\0\0",4))return files;
    const unsigned sections=U16(exe,pe+6);if(sections>96)Invalid();
    Range(exe,pe,24);
    const size_t table=pe+24+U16(exe,pe+20);Range(exe,table,sections*40);
    size_t overlay=table+sections*40;
    for(unsigned i=0;i<sections;++i) {
        const size_t pos=table+i*40,offset=U32(exe,pos+20),size=U32(exe,pos+16);
        Range(exe,offset,size);overlay=std::max(overlay,offset+size);
    }
    if(!Match(exe,overlay,"XOJUMANJ",8))return files;
    if(exe.size()>maxBytes || exe.size()<8)Invalid();
    const size_t count=U32(exe,exe.size()-8);
    if(!count || count>65536 || count%8 || count>exe.size()-8)Invalid();
    Bytes catalog=Slice(exe,exe.size()-8-count,count);
    constexpr uint8_t catalogKey[]={0xb7,0x64,0x67,0xe0,0x63,0x22,0xea,0xef,0xd8,0x03,0x2e,0x7e,0x6c,0x2c,0x38,0x17};
    Decipher(catalog,catalogKey);CheckCRC(catalog,U32(exe,exe.size()-4));
    const size_t packages=U32(catalog,0),packageCount=U32(catalog,4),mounts=U32(catalog,8),mountCount=U32(catalog,12);
    if(packageCount>1024 || mountCount>1024)Invalid();Range(catalog,packages,packageCount*8);Range(catalog,mounts,mountCount*24);
    size_t total=0, totalNodes=0;
    for(size_t m=0;m<mountCount;++m) {
        const auto mount=mounts+m*24;
        if(!(U32(catalog,mount)&1))continue; // External packages remain physical files.
        const auto mask=String8(catalog,U32(catalog,mount+4));
        const auto* key=catalog.data()+mount+8;
        for(size_t p=0;p<packageCount;++p) {
            const auto record=packages+p*8;
            if(mask!="*" && mask!=String8(catalog,U32(catalog,record+4)))continue;
            size_t offset=U32(catalog,record);
            auto header=Slice(exe,offset,48);Decipher(header,key);
            if(!Match(header,40,"STELPACK",8))Invalid();
            const size_t treeSize=U32(header,4),root=U32(header,8),back=U32(header,12);
            if(treeSize>16*1024*1024 || treeSize>offset || back>offset || treeSize%8)Invalid();
            const size_t base=offset-back;
            auto tree=Slice(exe,offset-treeSize,treeSize);Decipher(tree,key);CheckCRC(tree,U32(header,16));
            std::set<size_t> visited;
            std::function<void(size_t,const std::string&,unsigned)> walk;
            walk=[&](size_t node,const std::string& parent,unsigned depth) {
                if(depth>32 || !visited.insert(node).second || ++totalNodes>8192)Invalid();
                Range(tree,node,52);
                const auto flags=U32(tree,node+40),size=U32(tree,node+44),blocks=U32(tree,node+48);
                const bool directory=(flags&0x80000000u)!=0;
                if(node==root && !directory)Invalid();
                std::string path=parent;
                if(node!=root) {
                    if((flags&1) && !directory)return; // Hash-only names cannot be enumerated.
                    const auto name=Name(tree,U32(tree,node+8),U32(tree,node+12));if(name.empty())Invalid();
                    path+=name;
                }
                if(directory) {
                    if(blocks>8192)Invalid();
                    Range(tree,node+52,size_t(blocks)*4);
                    for(size_t j=0;j<blocks;++j)walk(U32(tree,node+52+j*4),path.empty()?path:path+"/",depth+1);
                    return;
                }
                if(path.empty() || path=="savedata" || path.rfind("savedata/",0)==0)return;
                if(size>maxBytes-total || blocks>8192)Invalid();Range(tree,node+52,size_t(blocks)*12);
                auto output=std::make_shared<Bytes>();output->reserve(size);
                for(size_t j=0;j<blocks;++j) {
                    const size_t bp=node+52+j*12,packed=U16(tree,bp),real=U16(tree,bp+2),bf=U16(tree,bp+4);
                    if(real>48*1024 || real>size-output->size())Invalid();
                    const size_t blockOffset=U32(tree,bp+8);
                    if(blockOffset>exe.size()-base || (bf&4 && packed%8))Invalid();
                    auto input=Slice(exe,base+blockOffset,packed);
                    if(bf&4)Decipher(input,key);
                    const size_t at=output->size();
                    if(bf&2) {
                        output->resize(at+real);uLongf actual=real;
                        if(uncompress(output->data()+at,&actual,input.data(),input.size())!=Z_OK || actual!=real)Invalid();
                    } else {
                        if(input.size()<real)Invalid();output->insert(output->end(),input.begin(),input.begin()+real);
                    }
                    if(CRC16(output->data()+at,real)!=U16(tree,bp+6))Invalid();
                }
                if(output->size()!=size)Invalid();total+=size;files[path]=std::move(output);
            };
            walk(root,{},0);
        }
    }
    return files;
}
}
