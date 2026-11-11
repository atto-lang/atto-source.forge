#pragma once
// Trusted host SQLite WAL/FULL inbox; NEVER guest-facing or a compiler proof.
// The matching Java/LLVM path uses the same big-endian batch framing.
#include <sqlite3.h>
#include <cstdint>
#include <climits>
#include <cstring>
#include <algorithm>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace oresforge::runtime::durable {
struct Event { std::uint32_t schema; std::vector<std::uint8_t> bytes; };
struct Receipt {
  std::uint64_t first_version, last_version;
  std::vector<std::uint8_t> reply;
  bool duplicate;
};
struct Identity { std::string tenant, actor, slot; };

class SqliteCommandInbox final {
  sqlite3* db_=nullptr;
  std::mutex mutex_;
  static void check(int result) {
    if(result!=SQLITE_OK && result!=SQLITE_DONE && result!=SQLITE_ROW)
      throw std::runtime_error("SQLITE_COMMAND_OUTCOME_UNKNOWN");
  }
  class Statement final {
   public:
    sqlite3_stmt* value=nullptr;
    Statement(sqlite3* db,const char* sql) {check(sqlite3_prepare_v2(db,sql,-1,&value,nullptr));}
    ~Statement(){if(value)sqlite3_finalize(value);}
    Statement(const Statement&)=delete;
    Statement& operator=(const Statement&)=delete;
    void text(int n,const std::string& s){check(sqlite3_bind_text(value,n,s.data(),static_cast<int>(s.size()),SQLITE_TRANSIENT));}
    void integer(int n,std::uint64_t v){if(v>INT64_MAX)throw std::invalid_argument("VERSION_EXHAUSTED");check(sqlite3_bind_int64(value,n,static_cast<sqlite3_int64>(v)));}
    void blob(int n,const std::vector<std::uint8_t>& b){check(sqlite3_bind_blob(value,n,b.empty()?static_cast<const void*>(""):b.data(),static_cast<int>(b.size()),SQLITE_TRANSIENT));}
    int step(){int rc=sqlite3_step(value);check(rc);return rc;}
  };
  void sql(const char* statement){check(sqlite3_exec(db_,statement,nullptr,nullptr,nullptr));}
  void rollback() noexcept {sqlite3_exec(db_,"ROLLBACK",nullptr,nullptr,nullptr);}
  static bool id_ok(const std::string& text){
    if(text.empty()||text.size()>128)return false;
    for(unsigned char c:text)
      if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'))return false;
    return true;
  }
  static void verify_id(const Identity& id,const std::string& op){
    if(!id_ok(id.tenant)||!id_ok(id.actor)||!id_ok(id.slot)||!id_ok(op))
      throw std::invalid_argument("INVALID_COMMAND_IDENTITY");
  }
  static void bind_identity(Statement& s,const Identity& id){
    s.text(1,id.tenant);s.text(2,id.actor);s.text(3,id.slot);
  }
  static std::vector<std::uint8_t> owned(sqlite3_stmt* st,int col,std::size_t maximum){
    const int n=sqlite3_column_bytes(st,col);
    if(sqlite3_column_type(st,col)==SQLITE_NULL||n<0||static_cast<std::size_t>(n)>maximum)
      throw std::runtime_error("CORRUPT_COMMAND_RECORD");
    const auto* p=static_cast<const std::uint8_t*>(sqlite3_column_blob(st,col));
    if(n==0)return {};
    if(!p)throw std::runtime_error("CORRUPT_COMMAND_RECORD");
    return {p,p+n};
  }
  static void put32(std::vector<std::uint8_t>& out,std::uint32_t v){
    for(int shift=24;shift>=0;shift-=8)out.push_back(static_cast<std::uint8_t>(v>>shift));
  }
  static std::uint32_t take32(const std::vector<std::uint8_t>& b,std::size_t& pos){
    if(pos>b.size()||b.size()-pos<4)throw std::runtime_error("CORRUPT_COMMAND_BATCH");
    std::uint32_t n=0;
    for(int i=0;i<4;++i)n=(n<<8)|b[pos++];
    return n;
  }
  static std::vector<std::uint8_t> encode(const std::vector<Event>& events){
    if(events.empty()||events.size()>256)throw std::invalid_argument("INVALID_EVENT_COUNT");
    std::vector<std::uint8_t> result; result.reserve(1024);
    put32(result,static_cast<std::uint32_t>(events.size()));
    for(const auto& event:events){
      if(event.schema==0||event.bytes.size()>65536||result.size()+8+event.bytes.size()>1048576)
        throw std::invalid_argument("INVALID_EVENT_BATCH");
      put32(result,event.schema);put32(result,static_cast<std::uint32_t>(event.bytes.size()));
      result.insert(result.end(),event.bytes.begin(),event.bytes.end());
    }
    return result;
  }
  static std::vector<Event> decode(const std::vector<std::uint8_t>& bytes){
    if(bytes.size()<12||bytes.size()>1048576)throw std::runtime_error("CORRUPT_COMMAND_BATCH");
    std::size_t pos=0;const auto count=take32(bytes,pos);
    if(count<1||count>256)throw std::runtime_error("CORRUPT_COMMAND_BATCH");
    std::vector<Event> result; result.reserve(count);
    for(std::uint32_t i=0;i<count;++i){
      const auto schema=take32(bytes,pos),n=take32(bytes,pos);
      if(schema==0||n>65536||pos>bytes.size()||n>bytes.size()-pos)
        throw std::runtime_error("CORRUPT_COMMAND_BATCH");
      result.push_back({schema,{bytes.begin()+static_cast<std::ptrdiff_t>(pos),bytes.begin()+static_cast<std::ptrdiff_t>(pos+n)}});
      pos+=n;
    }
    if(pos!=bytes.size())throw std::runtime_error("CORRUPT_COMMAND_BATCH");
    return result;
  }
  std::uint64_t epoch(const Identity& id){
    Statement s(db_,"SELECT epoch FROM durable_fences WHERE tenant=? AND actor=?");
    s.text(1,id.tenant);s.text(2,id.actor);
    if(s.step()==SQLITE_DONE)return 0;
    const auto n=sqlite3_column_int64(s.value,0);
    if(n<1)throw std::runtime_error("CORRUPT_FENCE");
    return static_cast<std::uint64_t>(n);
  }
  std::uint64_t version(const Identity& id){
    Statement s(db_,"SELECT version FROM durable_head WHERE tenant=? AND actor=? AND slot=?");
    bind_identity(s,id);
    if(s.step()==SQLITE_DONE)return 0;
    const auto n=sqlite3_column_int64(s.value,0);
    if(n<0)throw std::runtime_error("CORRUPT_HEAD");
    return static_cast<std::uint64_t>(n);
  }
  std::optional<Receipt> lookup(const Identity& id,std::uint64_t current_epoch,
                                const std::string& op,const std::vector<std::uint8_t>& request){
    Statement s(db_,"SELECT epoch,expected,request,first_version,last_version,event_batch,reply "
                    "FROM durable_command_inbox WHERE tenant=? AND actor=? AND slot=? AND op=?");
    bind_identity(s,id);s.text(4,op);
    if(s.step()==SQLITE_DONE)return std::nullopt;
    const auto origin=sqlite3_column_int64(s.value,0);
    const auto expected=sqlite3_column_int64(s.value,1);
    const auto first=sqlite3_column_int64(s.value,3);
    const auto last=sqlite3_column_int64(s.value,4);
    if(origin<=0||static_cast<std::uint64_t>(origin)>current_epoch||expected<0||
       expected>=INT64_MAX||first!=expected+1||last<first||last-first>=256||
       static_cast<std::uint64_t>(last)>version(id))
      throw std::runtime_error("OP_ID_CONFLICT");
    const auto saved_request=owned(s.value,2,65536);
    if(saved_request!=request)throw std::runtime_error("OP_ID_CONFLICT");
    const auto batch=decode(owned(s.value,5,1048576));
    const auto reply=owned(s.value,6,65536);
    if(batch.size()!=static_cast<std::size_t>(last-first+1))
      throw std::runtime_error("CORRUPT_COMMAND_BATCH");
    Statement events(db_,"SELECT version,schema,payload FROM durable_events "
                         "WHERE tenant=? AND actor=? AND slot=? AND version>=? AND version<=? "
                         "ORDER BY version ASC");
    bind_identity(events,id);events.integer(4,static_cast<std::uint64_t>(first));
    events.integer(5,static_cast<std::uint64_t>(last));
    for(std::size_t i=0;i<batch.size();++i){
      if(events.step()!=SQLITE_ROW||
         sqlite3_column_int64(events.value,0)!=first+static_cast<sqlite3_int64>(i)||
         sqlite3_column_int64(events.value,1)!=static_cast<sqlite3_int64>(batch[i].schema)||
         owned(events.value,2,65536)!=batch[i].bytes)
        throw std::runtime_error("INCOMPLETE_COMMAND_JOURNAL");
    }
    if(events.step()!=SQLITE_DONE)throw std::runtime_error("INCOMPLETE_COMMAND_JOURNAL");
    return Receipt{static_cast<std::uint64_t>(first),static_cast<std::uint64_t>(last),reply,true};
  }
 public:
  explicit SqliteCommandInbox(const std::string& trusted_path){
    if(trusted_path.empty()||trusted_path==":memory:"||trusted_path.rfind("file:",0)==0||trusted_path[0]!='/')
      throw std::invalid_argument("TRUSTED_ABSOLUTE_DATABASE_PATH_REQUIRED");
    if(sqlite3_open_v2(trusted_path.c_str(),&db_,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK){
      if(db_){sqlite3_close(db_);db_=nullptr;}throw std::runtime_error("SQLITE_OPEN_FAILED");
    }
    try {
      sql("PRAGMA busy_timeout=5000");
      Statement wal(db_,"PRAGMA journal_mode=WAL");
      if(wal.step()!=SQLITE_ROW||std::string(reinterpret_cast<const char*>(sqlite3_column_text(wal.value,0)))!="wal")
        throw std::runtime_error("SQLITE_WAL_UNAVAILABLE");
      sql("PRAGMA synchronous=FULL");
      sql("CREATE TABLE IF NOT EXISTS durable_fences(tenant TEXT NOT NULL,actor TEXT NOT NULL,epoch INTEGER NOT NULL,PRIMARY KEY(tenant,actor))");
      sql("CREATE TABLE IF NOT EXISTS durable_head(tenant TEXT NOT NULL,actor TEXT NOT NULL,slot TEXT NOT NULL,version INTEGER NOT NULL,PRIMARY KEY(tenant,actor,slot))");
      sql("CREATE TABLE IF NOT EXISTS durable_events(tenant TEXT NOT NULL,actor TEXT NOT NULL,slot TEXT NOT NULL,version INTEGER NOT NULL,schema INTEGER NOT NULL,payload BLOB NOT NULL,PRIMARY KEY(tenant,actor,slot,version))");
      sql("CREATE TABLE IF NOT EXISTS durable_command_inbox(tenant TEXT NOT NULL,actor TEXT NOT NULL,slot TEXT NOT NULL,op TEXT NOT NULL,epoch INTEGER NOT NULL,expected INTEGER NOT NULL,request BLOB NOT NULL,first_version INTEGER NOT NULL,last_version INTEGER NOT NULL,event_batch BLOB NOT NULL,reply BLOB NOT NULL,PRIMARY KEY(tenant,actor,slot,op))");
    } catch(...){sqlite3_close(db_);db_=nullptr;throw;}
  }
  ~SqliteCommandInbox(){if(db_)sqlite3_close(db_);}
  SqliteCommandInbox(const SqliteCommandInbox&)=delete;
  SqliteCommandInbox& operator=(const SqliteCommandInbox&)=delete;
  void grant_epoch(const std::string& tenant,const std::string& actor,std::uint64_t next){
    verify_id({tenant,actor,"main"},"valid");
    if(next==0||next>INT64_MAX)throw std::invalid_argument("INVALID_EPOCH");
    std::lock_guard<std::mutex> held(mutex_);sql("BEGIN IMMEDIATE");
    try {
      auto before=epoch({tenant,actor,"main"});
      if(next<before)throw std::runtime_error("STALE_EPOCH");
      Statement s(db_,"INSERT INTO durable_fences VALUES(?,?,?) ON CONFLICT(tenant,actor) DO UPDATE SET epoch=excluded.epoch");
      s.text(1,tenant);s.text(2,actor);s.integer(3,next);s.step();sql("COMMIT");
    }catch(...){rollback();throw;}
  }
  std::optional<Receipt> resolve(const Identity& id,std::uint64_t current_epoch,
                                 const std::string& op,const std::vector<std::uint8_t>& request){
    verify_id(id,op);
    if(current_epoch==0||current_epoch>INT64_MAX||request.size()>65536)
      throw std::invalid_argument("INVALID_COMMAND_LOOKUP");
    std::lock_guard<std::mutex> held(mutex_);sql("BEGIN");
    try {
      if(epoch(id)!=current_epoch)throw std::runtime_error("STALE_EPOCH");
      auto result=lookup(id,current_epoch,op,request);sql("COMMIT");return result;
    }catch(...){rollback();throw;}
  }
  Receipt commit_command(const Identity& id,std::uint64_t current_epoch,
                         const std::string& op,std::uint64_t expected,
                         const std::vector<std::uint8_t>& request,
                         const std::vector<Event>& events,const std::vector<std::uint8_t>& reply){
    verify_id(id,op);
    if(current_epoch==0||current_epoch>INT64_MAX||expected>INT64_MAX||
       request.size()>65536||reply.size()>65536)
      throw std::invalid_argument("INVALID_COMMAND_LIMITS");
    const auto batch=encode(events);
    if(expected>static_cast<std::uint64_t>(INT64_MAX)-events.size())
      throw std::invalid_argument("VERSION_EXHAUSTED");
    std::lock_guard<std::mutex> held(mutex_);sql("BEGIN IMMEDIATE");
    try {
      if(epoch(id)!=current_epoch)throw std::runtime_error("STALE_EPOCH");
      auto prior=lookup(id,current_epoch,op,request);
      if(prior){sql("COMMIT");return *prior;}
      if(version(id)!=expected)throw std::runtime_error("VERSION_CONFLICT");
      const auto first=expected+1,last=expected+events.size();
      Statement head(db_,"INSERT INTO durable_head VALUES(?,?,?,?) ON CONFLICT(tenant,actor,slot) DO UPDATE SET version=excluded.version");
      bind_identity(head,id);head.integer(4,last);head.step();
      Statement add(db_,"INSERT INTO durable_events VALUES(?,?,?,?,?,?)");
      for(std::size_t i=0;i<events.size();++i){
        sqlite3_reset(add.value);sqlite3_clear_bindings(add.value);
        bind_identity(add,id);add.integer(4,first+i);add.integer(5,events[i].schema);
        add.blob(6,events[i].bytes);add.step();
      }
      Statement inbox(db_,"INSERT INTO durable_command_inbox VALUES(?,?,?,?,?,?,?,?,?,?,?)");
      bind_identity(inbox,id);inbox.text(4,op);inbox.integer(5,current_epoch);
      inbox.integer(6,expected);inbox.blob(7,request);inbox.integer(8,first);
      inbox.integer(9,last);inbox.blob(10,batch);inbox.blob(11,reply);inbox.step();
      sql("COMMIT");return {first,last,reply,false};
    }catch(...){rollback();throw;}
  }
};
} // namespace oresforge::runtime::durable
