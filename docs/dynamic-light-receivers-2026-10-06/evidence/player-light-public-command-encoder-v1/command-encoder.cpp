#include "livedbg.hpp"
#include <charconv>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
static uint32_t number(const char* text, uint32_t max) {
    const std::string_view v(text); uint32_t value=0;
    const auto r=std::from_chars(v.data(),v.data()+v.size(),value);
    if(r.ec!=std::errc{} || r.ptr!=v.data()+v.size() || value>max)
        throw std::invalid_argument("invalid unsigned decimal argument");
    return value;
}
int main(int argc,char** argv) {
    try {
        if(argc==3 && std::string_view(argv[1])=="snapshot") {
            livedbg::Snapshot s;
            if(!livedbg::readSnapshot(argv[2],s)) return 3;
            std::cout<<"SNAPSHOT seq="<<s.seq<<" frame="<<s.frame<<" scene="<<s.scene<<" halted="<<s.halted<<" objects="<<s.objects.size()<<'\n';
            for(const auto& o:s.objects) {
                std::cout<<"WATCH index="<<o.index<<" samples="<<o.samples.size();
                if(!o.samples.empty()) {
                    const auto& v=o.samples.back();
                    std::cout<<" frame="<<v.frame<<" pos="<<v.pos[0]<<','<<v.pos[1]<<','<<v.pos[2]<<" active="<<v.active<<" visible="<<v.visible;
                }
                std::cout<<'\n';
            }
            return 0;
        }
        if(argc<4 || std::string_view(argv[1])!="write") {
            std::cerr<<"Usage: command-encoder write PATH NONZERO_SEQ [WATCH_INDEX...] | snapshot PATH\n";
            return 2;
        }
        livedbg::Command command;
        command.seq=number(argv[3],std::numeric_limits<uint32_t>::max());
        if(!command.seq) throw std::invalid_argument("seq must be nonzero and distinct from the prior command");
        for(int i=4;i<argc;++i) command.watchObjects.push_back(static_cast<uint16_t>(number(argv[i],65535)));
        const auto error=livedbg::writeCommand(argv[2],command);
        if(!error.empty()) { std::cerr<<error<<'\n'; return 1; }
        std::cout<<"WROTE_NORMAL_LIVEDBG_COMMAND seq="<<command.seq<<" watches="<<command.watchObjects.size()<<" halt=0 capture=0\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 2; }
}
