#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
using Str = std::string_view;
using Time = int64_t;
constexpr Time day_ms = 86400000, reach = 7 * day_ms, bad = INT64_MIN;
template<class V> using Dict = std::unordered_map<Str,V>;

struct Input {
    std::vector<Str> mappings;
    ~Input() { for (auto s : mappings) munmap(const_cast<char*>(s.data()), s.size()); }
    Str map(const fs::path& p) {
        int fd = open(p.c_str(), O_RDONLY);
        if (fd < 0) throw std::runtime_error("open " + p.string());
        struct stat st{};
        if (fstat(fd,&st)) { close(fd); throw std::runtime_error("stat " + p.string()); }
        if (!st.st_size) { close(fd); return {}; }
        auto ptr = mmap(nullptr,st.st_size,PROT_READ,MAP_PRIVATE,fd,0);
        close(fd);
        if (ptr == MAP_FAILED) throw std::runtime_error("mmap " + p.string());
        mappings.emplace_back(static_cast<const char*>(ptr),st.st_size);
        return mappings.back();
    }
    std::vector<Str> parts(const fs::path& dir) {
        std::vector<Str> chunks;
        for (auto& file : fs::directory_iterator(dir)) {
            if (file.path().extension() != ".csv") continue;
            Str data = map(file.path());
            auto nl = data.find('\n');
            if (nl == Str::npos) continue;
            data.remove_prefix(nl+1);
            while (!data.empty()) {
                auto split = data.find('\n', 512*1024);
                auto n = split == Str::npos ? data.size() : split+1;
                chunks.push_back(data.substr(0,n));
                data.remove_prefix(n);
            }
        }
        return chunks;
    }
};

template<size_t N, class F> void scalar_rows(Str data, F&& callback) {
    std::array<Str,N> fields;
    while (!data.empty()) {
        size_t end = data.find('\n');
        Str line = data.substr(0,end);
        data.remove_prefix(line.size() + (end != Str::npos));
        for (size_t i=0; i<N; ++i) {
            auto comma = line.find(',');
            if ((comma == Str::npos) != (i+1 == N)) break;
            Str s = line.substr(0,comma);
            if (s.size() >= 2 && s.front()=='"' && s.back()=='"') {
                s.remove_prefix(1); s.remove_suffix(1);
            }
            if (s.find('"') != Str::npos) break;
            fields[i]=s;
            if (i+1==N) callback(fields);
            else line.remove_prefix(comma+1);
        }
    }
}

#if defined(__SSE2__)
#include <emmintrin.h>
inline std::pair<unsigned,unsigned> delimiters(const char* p) {
    auto v=_mm_loadu_si128(reinterpret_cast<const __m128i*>(p));
    auto ends=_mm_or_si128(_mm_cmpeq_epi8(v,_mm_set1_epi8(',')),_mm_cmpeq_epi8(v,_mm_set1_epi8('\n')));
    return {_mm_movemask_epi8(ends),_mm_movemask_epi8(_mm_cmpeq_epi8(v,_mm_set1_epi8('"')))};
}
#elif defined(__aarch64__)
#include <arm_neon.h>
inline unsigned mask(uint8x16_t matches) {
    constexpr uint8_t weights[16]={1,2,4,8,16,32,64,128,1,2,4,8,16,32,64,128};
    auto bits=vandq_u8(matches,vld1q_u8(weights));
    return vaddv_u8(vget_low_u8(bits)) | (unsigned(vaddv_u8(vget_high_u8(bits)))<<8);
}
inline std::pair<unsigned,unsigned> delimiters(const char* p) {
    auto v=vld1q_u8(reinterpret_cast<const uint8_t*>(p));
    auto ends=vorrq_u8(vceqq_u8(v,vdupq_n_u8(',')),vceqq_u8(v,vdupq_n_u8('\n')));
    return {mask(ends),mask(vceqq_u8(v,vdupq_n_u8('"')))};
}
#endif
template<size_t N,class F> void rows(Str data,F&& callback) {
#if defined(__SSE2__) || defined(__aarch64__)
    if(data.empty()) return;
    std::array<Str,N> fields;
    size_t column=0, expected_quotes=0, actual_quotes=0;
    const char* field=data.data();
    auto finish_field=[&](const char* end) {
        Str s(field,end-field);
        if(s.size()>=2 && s.front()=='"' && s.back()=='"') {
            s.remove_prefix(1); s.remove_suffix(1); expected_quotes+=2;
        }
        if(column<N) fields[column]=s;
        ++column;
    };
    for(size_t off=0;off<data.size();off+=16) {
        const char* base=data.data()+off;
        char tail[16]{};
        const char* block=base;
        if(data.size()-off<16) { std::memcpy(tail,base,data.size()-off); block=tail; }
        auto [ends,quotes]=delimiters(block);
        while(ends) {
            unsigned at=std::countr_zero(ends);
            ends &= ends-1;
            finish_field(base+at);
            field=base+at+1;
            if(base[at]=='\n') {
                unsigned before=(1u<<at)-1;
                actual_quotes+=std::popcount(quotes&before);
                quotes &= ~before;
                if(column==N && expected_quotes==actual_quotes) callback(fields);
                column=actual_quotes=expected_quotes=0;
            }
        }
        actual_quotes+=std::popcount(quotes);
    }
    if(data.back()!='\n') {
        finish_field(data.data()+data.size());
        if(column==N && expected_quotes==actual_quotes) callback(fields);
    }
#else
    scalar_rows<N>(data,callback);
#endif
}

inline Time number(Str s, Time limit) {
    Time n=0;
    if (s.empty()) return bad;
    for (unsigned char c:s) {
        unsigned d = unsigned(c)-'0';
        if (d>9 || n>(limit-static_cast<Time>(d))/10) return bad;
        n=n*10+d;
    }
    return n;
}
inline int pair_digits(const char* p) {
    unsigned a=unsigned(static_cast<unsigned char>(p[0]))-'0';
    unsigned b=unsigned(static_cast<unsigned char>(p[1]))-'0';
    return (a|b) < 16 && a<10 && b<10 ? int(10*a+b) : -1000;
}
inline Time timestamp(Str s) {
    if ((s.size()!=20 && s.size()!=25) || s[4]!='-') return number(s,INT64_MAX);
    if (s[7]!='-' || s[10]!='T' || s[13]!=':' || s[16]!=':') return bad;
    auto p=s.data();
    int century=pair_digits(p), yy=pair_digits(p+2), m=pair_digits(p+5), d=pair_digits(p+8);
    int h=pair_digits(p+11), minute=pair_digits(p+14), sec=pair_digits(p+17);
    int y=century*100+yy;
    if (century<0 || yy<0 || m<1 || m>12 || d<1 || unsigned(h)>23 || unsigned(minute)>59 || unsigned(sec)>59) return bad;
    constexpr int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if (d>lengths[m-1]+(m==2 && y%4==0 && (y%100!=0 || y%400==0))) return bad;
    int offset=0;
    if (s.size()==25) {
        int oh=pair_digits(p+20), om=pair_digits(p+23);
        if ((p[19]!='+' && p[19]!='-') || p[22]!=':' || unsigned(oh)>23 || unsigned(om)>59) return bad;
        offset=(oh*60+om)*(p[19]=='+' ? 1 : -1);
    } else if (p[19]!='Z') return bad;
    y-=m<=2;
    int era=(y>=0 ? y : y-399)/400, year=y-era*400;
    int days=era*146097+year*365+year/4-year/100+(153*(m+(m>2?-3:9))+2)/5+d-1-719468;
    return days*day_ms+((h*60+minute-offset)*60+sec)*1000LL;
}
struct Identities {
    Dict<int> ids;
    std::vector<int> parent, sizes;
    std::vector<Str> names;
    int id(Str s) {
        auto [it,new_id]=ids.try_emplace(s,parent.size());
        if (new_id) { parent.push_back(it->second); sizes.push_back(1); names.push_back(s); }
        return it->second;
    }
    int root(int x) {
        while (parent[x]!=x) { parent[x]=parent[parent[x]]; x=parent[x]; }
        return x;
    }
    void join(Str a, Str b) {
        if (a.empty() || b.empty()) return;
        int x=root(id(a)), y=root(id(b));
        if (x==y) return;
        if (sizes[x]<sizes[y]) std::swap(x,y);
        parent[y]=x; sizes[x]+=sizes[y]; names[x]=std::min(names[x],names[y]);
    }
    void finish() { for (auto& [_,v]:ids) v=root(v); }
    Str resolve(Str s) const {
        auto it=ids.find(s);
        return it==ids.end() ? s : names[it->second];
    }
};

template<class F> void parallel(size_t jobs, unsigned threads, F&& f) {
    std::atomic<size_t> next{0};
    std::exception_ptr error;
    std::mutex lock;
    std::vector<std::jthread> workers;
    for (unsigned t=0; t<std::min<size_t>(threads,jobs); ++t) workers.emplace_back([&,t] {
        try {
            for (size_t i; (i=next.fetch_add(1,std::memory_order_relaxed))<jobs;) f(i,t);
        } catch (...) { std::lock_guard guard(lock); error=std::current_exception(); next.store(jobs); }
    });
    workers.clear();
    if (error) std::rethrow_exception(error);
}
