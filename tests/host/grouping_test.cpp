#include <cassert>
#include <fstream>
#include <iostream>
#include <regex>
#include <string>
#ifndef ANALYZER_MAIN_SOURCE
#define ANALYZER_MAIN_SOURCE "../../main/main.cpp"
#endif
#include ANALYZER_MAIN_SOURCE
int64_t host_now_us=1000000;
uint64_t host_gptimer_count=0,host_next_alarm=0;
char host_output[2000000];
size_t host_output_used=0;
TaskHandle_t host_current_task=nullptr;

int main(int argc,char** argv) {
    assert(argc==2);
    std::ifstream input(argv[1]);assert(input.good());
    ResetAnalysis(1,1);
    capture_state.run_id=1;capture_state.trial_id=1;
    const std::regex edge(R"(ANZ\|EDGE\|(\d+)\|(\d+)\|([123])\|[^|]+\|(\d+)\|([01]))");
    std::string line;unsigned count=0;
    while(std::getline(input,line)) {
        std::smatch m;if(!std::regex_search(line,m,edge)) continue;
        EdgeEvent e{};e.run_id=std::stoull(m[1]);e.trial_id=std::stoul(m[2]);
        e.channel=std::stoul(m[3])-1;e.timestamp_us=std::stoll(m[4]);e.level=std::stoul(m[5]);
        ++capture_state.raw_counts[e.channel];capture_state.seen_mask|=1U<<e.channel;
        ProcessTrainQualification(e);++count;
    }
    EndTrial(1,1);
    std::cout<<"RAW_EDGES="<<count<<" INCOMPLETE="<<analysis_state.incomplete_groups<<" TRAINS="<<analysis_state.train_count<<"\n";
    if(analysis_state.incomplete_groups!=0) return 2;
    assert(analysis_state.train_count==3);
    const int expected[]={46,50,68};
    for(unsigned i=0;i<3;++i) {
        assert(analysis_state.trains[i].boundaries==31);
        assert(analysis_state.trains[i].start_worst_range_us==expected[i]);
    }
    assert(strstr(host_output,"boundary=0|ESP01=246651070|ESP02=246651043|ESP03=246651089|range_us=46"));
    assert(!strstr(host_output,"ANZ|MISSING"));
    assert(strstr(host_output,"incomplete_groups=0|result=PASS"));
    std::cout<<"PASS: actual Analyzer source replay; original START=46/48/68 us, 31 boundaries per train, final PASS\n";
}
