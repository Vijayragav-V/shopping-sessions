#pragma once
// Collect worker elapsed time only in a separate diagnostic build. One second
// on each of eight concurrent workers sums to eight worker-seconds.
#ifdef SHOPPING_DIAGNOSTICS
#include <cstdlib>
#include <new>
namespace metrics {
enum Part { grouping, sorting, session, attribution, aggregation, parts };
inline std::atomic<int64_t> totals[parts]{}, alloc_ns{}, alloc_calls{}, alloc_bytes{};
inline int64_t now() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}
struct Local {
    int64_t ns[parts]{}, sampled{}, calls{}, bytes{};
    ~Local() {
        for(int i=0;i<parts;++i) totals[i]+=ns[i];
        alloc_ns+=sampled; alloc_calls+=calls; alloc_bytes+=bytes;
    }
};
inline thread_local Local local;
struct Scope {
    Part part; int64_t start=now();
    ~Scope(){local.ns[part]+=now()-start;}
};
inline void report() {
    const char* names[]={"grouping","sorting","session boundaries","attribution windows","aggregation"};
    for(int i=0;i<parts;++i) std::cerr<<names[i]<<": "<<(totals[i]+local.ns[i])*1e-9<<" worker-s\n";
    std::cerr<<"ordinary C++ allocations: "<<alloc_calls+local.calls<<" calls, "<<alloc_bytes+local.bytes
        <<" requested bytes, "<<(alloc_ns+local.sampled)*128e-9<<" estimated worker-s in malloc (1/128 sampled)\n";
}
}
void* operator new(size_t n) {
    auto& m=metrics::local;
    m.bytes+=n;
    bool sample=(++m.calls%128)==0;
    auto start=sample ? metrics::now() : 0;
    void* ptr=std::malloc(n?n:1);
    if(sample) m.sampled+=metrics::now()-start;
    if(!ptr) throw std::bad_alloc();
    return ptr;
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
#define MEASURE(part) metrics::Scope measure_##part{metrics::part}
#else
#define MEASURE(part) ((void)0)
#endif
