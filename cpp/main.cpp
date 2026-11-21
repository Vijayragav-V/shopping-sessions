#include "input.hpp"
#include "metrics.hpp"
#include "output.hpp"
#include <map>
#include <regex>
#include <span>

constexpr size_t shards=128;
size_t bucket(Key k) { return k.hash()%shards; }
struct Event { Time ts; int32_t price, category; };
struct Click { Str id; Time ts; int32_t campaign; };
struct Candidate { Key id,user; Time ingest; Event event; };
template<class T> using Bins=std::array<std::vector<T>,shards>;
struct Worker {
    Bins<Candidate> candidates;
    Bins<std::pair<Key,Event>> events;
    Bins<std::pair<Key,Click>> clicks;
    KeyPool pool;
};
struct Catalog {
    Dict<int> product,campaign,category;
    std::vector<Str> categories{"unknown"}, campaigns{"organic"}, channels{"organic"};
    template<class F> void read(Input& input,const fs::path& file,F f) {
        auto s=input.map(file); auto pos=s.find('\n');
        if(pos!=Str::npos) rows<2>(s.substr(pos+1),f);
    }
    Catalog(Input& input,const fs::path& dir) {
        read(input,dir/"products.csv",[&](auto& r) {
            auto [it,added]=category.try_emplace(r[1],categories.size());
            if(added) categories.push_back(r[1]);
            product.emplace(r[0],it->second);
        });
        read(input,dir/"campaigns.csv",[&](auto& r) {
            campaign.emplace(r[0],campaigns.size());
            campaigns.push_back(r[0]); channels.push_back(r[1]);
        });
    }
};
struct Window {
    Time start,end;
    explicit Window(Str json) {
        auto get=[&](std::string key) {
            std::regex re("\""+key+"\"\\s*:\\s*\"([^\"]+)\"");
            std::match_results<Str::const_iterator> match;
            if(!std::regex_search(json.begin(),json.end(),match,re)) throw std::runtime_error("missing "+key);
            return timestamp(match[1].str());
        };
        start=get("window_start"); end=get("window_end");
        if(start==bad || end<=start) throw std::runtime_error("invalid manifest window");
    }
};
Event validate(const std::array<Str,7>& r, Time ingest, const Catalog& catalog, Window w) {
    Event drop{bad,0,0};
    if(r[1].empty() || r[5].empty()) return drop;
    Time ts=timestamp(r[2]);
    if(ts<w.start || ts>=w.end || ingest-ts>day_ms) return drop;
    if(r[4]=="view" || r[4]=="cart") return {ts,0,0};
    if(r[4]!="purchase") return drop;
    Time price=number(r[6],10000000);
    if(price<1) return drop;
    auto it=catalog.product.find(r[5]);
    return {ts,int32_t(price),it==catalog.product.end()?0:it->second};
}
using Totals=std::map<std::pair<int,int32_t>,Counts>;
int32_t utc_day(Time ts) { return int32_t(ts/day_ms-(ts%day_ms<0)); }
struct Result {
    std::vector<UserDay> users;
    Totals sparse;
    std::vector<Counts> table;
    int32_t first_day{};
    size_t campaigns{};
    void configure(Window w,size_t count,unsigned threads) {
        first_day=utc_day(w.start); campaigns=count;
        size_t days=size_t(utc_day(w.end-1)-first_day)+1;
        if(days<=4'000'000/threads/count) table.resize(days*count);
    }
    Counts& at(int campaign,int32_t day) {
        if(table.empty()) return sparse[{campaign,day}];
        return table[size_t(day-first_day)*campaigns+campaign];
    }
    void merge_into(Totals& merged) const {
        auto add=[&](int campaign,int32_t day,const Counts& c){
            auto& dest=merged[{campaign,day}];
            dest.sessions+=c.sessions; dest.purchases+=c.purchases;
            dest.revenue+=c.revenue; dest.assists+=c.assists;
        };
        for(auto& [k,c]:sparse) add(k.first,k.second,c);
        for(size_t i=0;i<table.size();++i) if(table[i].sessions || table[i].assists)
            add(i%campaigns,first_day+int32_t(i/campaigns),table[i]);
    }
};

void summarize(Key user, std::span<Event> events,std::span<Click> clicks,const Catalog& cat,Result& result) {
    if(events.empty()) return;
    {
    MEASURE(sorting);
    std::sort(events.begin(),events.end(),[](auto a,auto b){return a.ts<b.ts;});
    std::sort(clicks.begin(),clicks.end(),[](const auto& a,const auto& b){
        return a.ts==b.ts ? a.id>b.id : a.ts<b.ts;
    });
    }
    // Exact click copies have no effect on the winner or distinct active campaigns.
    size_t left=0,right=0;
    std::unordered_map<int,size_t> active;
    std::unordered_map<int,int64_t> spend;
    UserDay daily{user,0,0,0,0,{}};
    auto finish_day=[&] {
        MEASURE(aggregation);
        int64_t best=0;
        for(auto [k,v]:spend) {
            if(v>best || (v==best && cat.categories[k]<*daily.category)) {
                best=v; daily.category=cat.categories[k];
            }
        }
        result.users.push_back(daily); spend.clear();
    };
    for(size_t i=0;i<events.size();) {
        auto start=events[i].ts;
        auto day=utc_day(start);
        if(daily.sessions && day!=daily.day) { finish_day(); daily={user,day,0,0,0,{}}; }
        daily.day=day;
        size_t first=i;
        int64_t revenue=0;
        {
        MEASURE(session);
        do {
            auto e=events[i++];
            if(e.price) { spend[e.category]+=e.price; revenue+=e.price; }
        } while(i<events.size() && events[i].ts-events[i-1].ts<=1800000);
        }
        ++daily.sessions; daily.events+=i-first; daily.revenue+=revenue;
        {
        MEASURE(attribution);
        while(right<clicks.size() && clicks[right].ts<=start) ++active[clicks[right++].campaign];
        while(left<right && clicks[left].ts<start-reach) {
            auto it=active.find(clicks[left++].campaign);
            if(--it->second==0) active.erase(it);
        }
        }
        MEASURE(aggregation);
        int winner=left<right ? clicks[right-1].campaign : 0;
        auto& counts=result.at(winner,day);
        ++counts.sessions; counts.purchases+=revenue!=0; counts.revenue+=revenue;
        for(auto [campaign,_]:active) ++result.at(campaign,day).assists;
    }
    finish_day();
}
struct Timer {
    using Clock=std::chrono::steady_clock;
    Clock::time_point last=Clock::now();
    void mark(const char* name) {
        auto now=Clock::now();
        if(std::getenv("PROFILE")) std::cerr<<name<<" "<<std::chrono::duration<double>(now-last).count()<<" s\n";
        last=now;
    }
};
void run(const fs::path& data,const fs::path& output,unsigned threads) {
    Timer timer;
    Input input;
    Window window(input.map(data/"manifest.json"));
    Catalog cat(input,data);
    Identities ids;
    for(auto part:input.parts(data/"links")) rows<2>(part,[&](auto& r){ids.join(r[0],r[1]);});
    ids.finish();
    timer.mark("catalogs and linked identities");
    std::vector<Worker> workers(threads);
    auto events=input.parts(data/"events");
    parallel(events.size(),threads,[&](size_t i,unsigned t){
        rows<7>(events[i],[&](auto& r){
            if(r[0].empty()) return;
            Time ingest=ingest_time(r[3]);
            // Invalid revisions still take part in earliest-ingest selection.
            Key id=workers[t].pool.encode(r[0],'e');
            Event e=validate(r,ingest,cat,window);
            Key user=e.ts==bad ? Key{} : workers[t].pool.encode(r[1],'u');
            workers[t].candidates[bucket(id)].push_back({id,user,ingest,e});
        });
    });
    timer.mark("event CSV, validation and partition");
    parallel(shards,threads,[&](size_t b,unsigned t){
        size_t count=0;
        for(auto& w:workers) count+=w.candidates[b].size();
        // The low hash bits selected this shard. Use the remaining bits
        // for open addressing and compare the original bytes on a hash match.
        size_t capacity=1;
        while(capacity<count+count/2) capacity*=2;
        std::vector<Candidate*> best(capacity,nullptr);
        for(auto& w:workers) for(auto& e:w.candidates[b]) {
            size_t slot=(e.id.hash()/shards)&(capacity-1);
            while(best[slot] && !(best[slot]->id==e.id))
                slot=(slot+1)&(capacity-1);
            if(!best[slot]) best[slot]=&e;
            else if(best[slot]->ingest>e.ingest) {
                best[slot]->event.ts=bad; best[slot]=&e;
            } else e.event.ts=bad;
        }
        for(auto& w:workers) for(auto& e:w.candidates[b]) if(e.event.ts!=bad) {
            auto user=ids.resolve(e.user);
            workers[t].events[bucket(user)].emplace_back(user,e.event);
        }
        for(auto& w:workers) std::vector<Candidate>().swap(w.candidates[b]);
    });
    timer.mark("global event dedup and person partition");
    auto clicks=input.parts(data/"clicks");
    parallel(clicks.size(),threads,[&](size_t i,unsigned t){
        rows<4>(clicks[i],[&](auto& r){
            if(r[0].empty() || r[1].empty()) return;
            Time ts=timestamp(r[3]); auto c=cat.campaign.find(r[2]);
            if(ts<window.start-reach || ts>=window.end || c==cat.campaign.end()) return;
            Key user=ids.resolve(workers[t].pool.encode(r[1],'u'));
            workers[t].clicks[bucket(user)].emplace_back(user,Click{r[0],ts,c->second});
        });
    });
    timer.mark("click CSV and person partition");
    std::vector<Result> results(threads);
    for(auto& result:results) result.configure(window,cat.campaigns.size(),threads);
    parallel(shards,threads,[&](size_t b,unsigned t){
        struct Group { Key user; size_t event_end{},click_end{}; };
        std::vector<Group> groups;
        std::vector<Event> ordered_events;
        std::vector<Click> ordered_clicks;
        {
        MEASURE(grouping);
        std::unordered_map<Key,size_t,KeyHash> ids;
        for(auto& w:workers) {
            for(auto& [u,e]:w.events[b]) {
                auto [it,added]=ids.try_emplace(u,groups.size());
                if(added) groups.push_back({u});
                ++groups[it->second].event_end;
                u.bits=it->second;
            }
        }
        for(auto& w:workers) {
            for(auto& [u,c]:w.clicks[b]) {
                auto it=ids.find(u);
                if(it==ids.end()) u.bits=UINTPTR_MAX;
                else {u.bits=it->second; ++groups[it->second].click_end;}
            }
        }
        size_t ne=0,nc=0;
        for(auto& g:groups) {
            size_t events=g.event_end, clicks=g.click_end;
            g.event_end=ne; g.click_end=nc;
            ne+=events; nc+=clicks;
        }
        ordered_events.resize(ne); ordered_clicks.resize(nc);
        for(auto& w:workers) {
            for(auto& [u,e]:w.events[b]) ordered_events[groups[u.bits].event_end++]=e;
            for(auto& [u,c]:w.clicks[b]) if(u.bits!=UINTPTR_MAX) ordered_clicks[groups[u.bits].click_end++]=c;
            decltype(w.events)::value_type().swap(w.events[b]);
            decltype(w.clicks)::value_type().swap(w.clicks[b]);
        }
        }
        size_t ep=0,cp=0;
        for(auto& g:groups) {
            summarize(g.user,std::span(ordered_events).subspan(ep,g.event_end-ep),
                std::span(ordered_clicks).subspan(cp,g.click_end-cp),cat,results[t]);
            ep=g.event_end; cp=g.click_end;
        }
    });
    timer.mark("person grouping, sorting, sessions, attribution and aggregation");
    fs::create_directories(output/"user_daily");
    fs::create_directories(output/"campaign_daily");
    parallel(results.size(),threads,[&](size_t i,unsigned){
        write_users(output/"user_daily"/("part-"+std::to_string(i)+".parquet"),results[i].users);
    });
    Totals merged;
    for(auto& r:results) r.merge_into(merged);
    std::vector<CampaignDay> campaigns;
    for(auto& [key,c]:merged) campaigns.push_back({cat.campaigns[key.first],cat.channels[key.first],key.second,c});
    write_campaigns(output/"campaign_daily"/"part-0.parquet",campaigns);
    timer.mark("merge and Parquet output");
#ifdef SHOPPING_DIAGNOSTICS
    metrics::report();
#endif
}
int main(int argc,char** argv) {
    auto env=[](const char* key,const char* fallback){ auto s=std::getenv(key); return s?s:fallback; };
    try {
        run(argc>1?argv[1]:env("DATA_DIR","/data"),argc>2?argv[2]:env("OUTPUT_DIR","/output"),
            std::clamp(std::stoi(env("NCPU","8")),1,64));
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
