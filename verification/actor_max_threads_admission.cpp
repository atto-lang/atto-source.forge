#include "compiler/frontend.hpp"
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
using oresforge::parse_source;
static void check(bool ok) { if (!ok) std::abort(); }
static void reject(std::string_view source, std::string_view reason) {
  bool denied = false;
  try { (void)parse_source(source); }
  catch (const std::runtime_error& e) {
    denied = std::string_view(e.what()).find(reason) != std::string_view::npos;
  }
  check(denied);
}
int main() {
  for (auto tier : {"LOW", "MEDIUM", "HIGH"}) {
    reject(std::string("@ActorMaxThreads(MaxActorThreads.")+tier+
           ") define actor HungryActor as end",
           "@ActorMaxThreads requires actor source lowering");
  }
  reject("@ActorMaxThreads(3) define actor A as end",
         "@ActorMaxThreads requires actor source lowering");
  reject("define enum MaxActorThreads as LOW int = 1 MEDIUM int = 2 HIGH int = 3 end"
         " define actor HungryActor as end", "MaxActorThreads enum requires actor source lowering");
  reject("define actor Ordinary as end", "source actor execution unsupported");
  (void)parse_source("fnc main():int{return 42;} // @ActorMaxThreads(MaxActorThreads.HIGH)");
  return 0;
}
