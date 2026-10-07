#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#include <vector>
#ifndef ANALYZER_MAIN_SOURCE
#define ANALYZER_MAIN_SOURCE "../../main/main.cpp"
#endif
#include ANALYZER_MAIN_SOURCE
int64_t host_now_us=1000000;
uint64_t host_gptimer_count=0,host_next_alarm=0;
char host_output[2000000];
size_t host_output_used=0;
TaskHandle_t host_current_task=nullptr;

// v10.1 review: the longer bootstrap group expiry must not hide real faults.
// Mutations of the original 2026-10-07 capture:
//   none        unchanged                                   -> PASS, no MISSING
//   drop_start  ESP03's train-1 START edge removed          -> MISSING, FAIL
//   late_start  ESP03's train-3 START delayed by 10 ms      -> MISSING, FAIL
//   skip_mid    one ESP01 COMMIT in train 2 removed         -> MISSING, FAIL
struct RawEdge {uint64_t run;uint32_t trial;uint32_t channel;int64_t ts;uint32_t level;std::string name;};

static std::string TripwireLine() {
    const char* p=strstr(host_output,"ANZ|TRIPWIRE|");
    assert(p!=nullptr);
    const char* e=strpbrk(p,"\r\n");
    return std::string(p,e?static_cast<size_t>(e-p):strlen(p));
}

int main(int argc,char** argv) {
    assert(argc==3);
    const std::string mode=argv[2];
    std::ifstream input(argv[1]);assert(input.good());
    const std::regex edge(R"(ANZ\|EDGE\|(\d+)\|(\d+)\|([123])\|([^|]+)\|(\d+)\|([01]))");
    std::vector<RawEdge> edges;std::string line;
    while(std::getline(input,line)) {
        std::smatch m;if(!std::regex_search(line,m,edge)) continue;
        edges.push_back({std::stoull(m[1]),static_cast<uint32_t>(std::stoul(m[2])),
                         static_cast<uint32_t>(std::stoul(m[3])-1),std::stoll(m[5]),
                         static_cast<uint32_t>(std::stoul(m[6])),m[4]});
    }
    assert(edges.size()==285);
    auto find=[&](const char* name,int64_t after)->size_t {
        for(size_t i=0;i<edges.size();++i) if(edges[i].name==name && edges[i].ts>after) return i;
        assert(false);return 0;
    };
    if(mode=="drop_start") edges.erase(edges.begin()+static_cast<long>(find("ESP03_COMMIT",0)));
    else if(mode=="late_start") edges[find("ESP03_COMMIT",333000000)].ts+=10000;
    else if(mode=="skip_mid") edges.erase(edges.begin()+static_cast<long>(find("ESP01_COMMIT",303000000)));
    else assert(mode=="none");
    std::stable_sort(edges.begin(),edges.end(),[](const RawEdge& a,const RawEdge& b){return a.ts<b.ts;});

    ResetAnalysis(1,1);
    capture_state.run_id=1;capture_state.trial_id=1;
    for(const RawEdge& r:edges) {
        EdgeEvent e{};e.run_id=r.run;e.trial_id=r.trial;e.channel=r.channel;e.timestamp_us=r.ts;e.level=r.level;
        ++capture_state.raw_counts[e.channel];capture_state.seen_mask|=1U<<e.channel;
        ProcessTrainQualification(e);
    }
    EndTrial(1,1);
    const std::string tripwire=TripwireLine();
    const bool missing=strstr(host_output,"ANZ|MISSING")!=nullptr;
    std::cout<<"MODE="<<mode<<" INCOMPLETE="<<analysis_state.incomplete_groups<<" TRAINS="<<analysis_state.train_count
             <<" MISSING_RECORDS="<<(missing?1:0)<<" "<<tripwire<<"\n";
    if(mode=="none") {
        assert(analysis_state.incomplete_groups==0 && !missing);
        assert(tripwire.find("result=PASS")!=std::string::npos);
    } else {
        assert(analysis_state.incomplete_groups>=1 && missing);
        assert(tripwire.find("result=FAIL")!=std::string::npos);
    }
    std::cout<<"PASS: analyzer "<<mode<<" -> "<<(mode=="none"?"PASS":"FAIL with MISSING")<<"\n";
}
