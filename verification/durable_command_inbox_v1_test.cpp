// SQLite host provider, not execution of generated AttoLang/Cranelift code.
#include "runtime/durable_command_inbox_v1.hpp"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <cstdlib>
using namespace oresforge::runtime::durable;
static std::vector<std::uint8_t> b(const std::string& s){return {s.begin(),s.end()};}
template<class F>static bool rejects(F f){try{f();return false;}catch(const std::exception&){return true;}}
int main(){
  char name[]="/tmp/forge-durable-command-XXXXXX";
  const char* dir=mkdtemp(name);if(!dir)throw std::runtime_error("mkdtemp failed");
  const auto path=std::string(dir)+"/commands.sqlite";
  Identity id{"tenant-a","bank-42","main"};
  try{
    {
      SqliteCommandInbox store(path);
      store.grant_epoch(id.tenant,id.actor,1);
      const auto first=store.commit_command(id,1,"op-42",0,b("Deposit(100)"),
        {{1,b("MoneyDeposited(60)")},{1,b("MoneyDeposited(40)")}},b("Ok(balance=100)"));
      assert(!first.duplicate&&first.first_version==1&&first.last_version==2);
      // Exact event batch framing must match Java/LLVM/Wasm/.NET:
      // u32 count; per event u32 schema, u32 payload length, bytes (big endian).
      sqlite3* inspect=nullptr;
      if(sqlite3_open(path.c_str(),&inspect)!=SQLITE_OK)
        throw std::runtime_error("open golden wire inspector failed");
      sqlite3_stmt* inspect_stmt=nullptr;
      if(sqlite3_prepare_v2(inspect,
          "SELECT hex(event_batch) FROM durable_command_inbox WHERE actor='bank-42' AND op='op-42'",
          -1,&inspect_stmt,nullptr)!=SQLITE_OK)
        throw std::runtime_error("prepare golden wire inspector failed");
      assert(sqlite3_step(inspect_stmt)==SQLITE_ROW);
      const auto* golden_wire=reinterpret_cast<const char*>(sqlite3_column_text(inspect_stmt,0));
      assert(golden_wire!=nullptr);
      assert(std::string(golden_wire)==
          "0000000200000001000000124D6F6E65794465706F736974656428363029"
          "00000001000000124D6F6E65794465706F736974656428343029");
      sqlite3_finalize(inspect_stmt);
      sqlite3_close(inspect);
      const auto duplicate=store.commit_command(id,1,"op-42",0,b("Deposit(100)"),
        {{1,b("do not rerun decide")}},b("WRONG"));
      assert(duplicate.duplicate&&duplicate.last_version==2&&duplicate.reply==first.reply);
      assert(rejects([&]{(void)store.resolve(id,1,"op-42",b("Deposit(200)"));}));
      assert(rejects([&]{(void)store.commit_command(id,1,"oversized",2,b("cmd"),
          {{1,std::vector<std::uint8_t>(65537)}},b("ok"));}));
      assert(rejects([&]{(void)store.commit_command(id,1,"too-many",2,b("cmd"),
          std::vector<Event>(257,Event{1,b("e")}),b("ok"));}));
      assert(!store.resolve(id,1,"unknown",b("Deposit(100)")));
      store.grant_epoch(id.tenant,id.actor,2);
      assert(rejects([&]{(void)store.resolve(id,1,"op-42",b("Deposit(100)"));}));
      assert(rejects([&]{(void)store.commit_command(id,1,"op-43",2,b("Deposit(5)"),{{1,b("5")}},b("OK"));}));
      const auto retry=store.resolve(id,2,"op-42",b("Deposit(100)"));
      assert(retry&&retry->duplicate&&retry->last_version==2&&retry->reply==first.reply);
      assert(rejects([&]{(void)store.commit_command(id,2,"op-42",2,b("Deposit(200)"),{{1,b("200")}},b("BAD"));}));
      const auto new_op=store.commit_command(id,2,"op-43",2,b("Deposit(5)"),{{1,b("5")}},b("Ok(balance=105)"));
      assert(new_op.last_version==3);
    }
    {
      SqliteCommandInbox reopened(path);
      auto replay=reopened.resolve(id,2,"op-42",b("Deposit(100)"));
      assert(replay&&replay->reply==b("Ok(balance=100)"));
      replay->reply[0]=0;
      assert(reopened.resolve(id,2,"op-42",b("Deposit(100)"))->reply==b("Ok(balance=100)"));
      Identity other{"tenant-a","bank-43","main"};
      reopened.grant_epoch(other.tenant,other.actor,1);
      assert(!reopened.resolve(other,1,"op-42",b("Deposit(100)")));
    }
    {
      SqliteCommandInbox store(path);
      Identity damaged{"tenant-a","bank-fault","main"};
      store.grant_epoch(damaged.tenant,damaged.actor,1);
      sqlite3* connection=nullptr;
      if(sqlite3_open(path.c_str(),&connection)!=SQLITE_OK)throw std::runtime_error("fault DB open failed");
      auto apply=[&](const char* sql){char* error=nullptr;int rc=sqlite3_exec(connection,sql,nullptr,nullptr,&error);sqlite3_free(error);if(rc!=SQLITE_OK)throw std::runtime_error("fault injection failed");};
      apply("CREATE TRIGGER abort_second BEFORE INSERT ON durable_events WHEN NEW.actor='bank-fault' AND NEW.version=2 BEGIN SELECT RAISE(ABORT,'fault'); END");
      assert(rejects([&]{(void)store.commit_command(damaged,1,"op-fault",0,b("Deposit(10)"),{{1,b("6")},{1,b("4")}},b("Ok(10)"));}));
      assert(!store.resolve(damaged,1,"op-fault",b("Deposit(10)")));
      apply("DROP TRIGGER abort_second");
      assert(store.commit_command(damaged,1,"op-fault",0,b("Deposit(10)"),{{1,b("6")},{1,b("4")}},b("Ok(10)")).last_version==2);
      sqlite3_close(connection);
    }
    // A command record without its journal is never a valid durable reply.
    {
      sqlite3* raw=nullptr;
      if(sqlite3_open(path.c_str(),&raw)!=SQLITE_OK)throw std::runtime_error("corrupt open failed");
      sqlite3_exec(raw,"DELETE FROM durable_events WHERE actor='bank-42' AND version=1",nullptr,nullptr,nullptr);
      sqlite3_close(raw);
      SqliteCommandInbox store(path);
      assert(rejects([&]{(void)store.resolve(id,2,"op-42",b("Deposit(100)"));}));
    }
  }catch(...){std::filesystem::remove_all(dir);throw;}
  std::filesystem::remove_all(dir);
  std::cout<<"FORGE_DURABLE_HOST_PASS|atomic_events=2|cross_epoch_reply=1|rollback=1|guest_atto=0\n";
}
