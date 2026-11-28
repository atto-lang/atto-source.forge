#include "compiler/topic_publish_source_gate.hpp"
#include "compiler/frontend.hpp"
#include "compiler/call_native.hpp"
#include <fstream>
#include <iterator>
#include <random>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void expect_denied(std::string_view input) {
  auto probe=[&](auto&& fn, const char* name) {
    try { fn(); }
    catch(const std::runtime_error& e) {
      if(std::string_view(e.what()).find("TOPIC_PUBLISH_BACKEND_UNSUPPORTED")!=std::string_view::npos)return;
      throw std::runtime_error(std::string(name)+" gave unrelated denial: "+e.what());
    }
    throw std::runtime_error(std::string(name)+" accepted unlowered topic member");
  };
  probe([&] {oresforge::source::reject_unlowered_topic_publish(input);},"gate");
  probe([&] {(void)oresforge::parse_source(input);},"Forge scalar frontend");
  probe([&] {(void)oresforge::parse_runtime_source(input);},"Forge runtime source frontend");
  probe([&] {(void)oresforge::calls::parse(input);},"Forge callable frontend");
}
void expect_inert(std::string_view input) {
  oresforge::source::reject_unlowered_topic_publish(input);
}
}
int main(int argc, char** argv) {
  if(argc!=2)throw std::runtime_error("expected canonical AttoLang fixture path");
  std::ifstream file(argv[1],std::ios::binary);
  if(!file)throw std::runtime_error("canonical fixture missing");
  const std::string fixture((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
  expect_denied(fixture);
  for(const auto& input: {
    "pub fnc main():int{return topic.try_publish(share! event);}",
    "pub fnc main():int{return imported_topic.try_publish(rt share event);}",
    "pub fnc main():int{return local /* outer /* inner */ finish */ . try_publish(share! event);}",
    "pub fnc main():int{return maybe?.try_publish(share! event);}",
    "pub fnc main():int{return actor.topic.try_publish;}",
    "pub fnc main():int{return alias.\ntry_publish(event);}",
  }) expect_denied(input);
  // The shared source UTF-8 validator forbids disguised Unicode line breaks
  // *before* this typed-source fence. Do not weaken that stronger guard.
  for(const auto& input: {
    "// ignored foo.try_publish(value)\xC2\x85" "bar.try_publish(value)",
    "// ignored foo.try_publish(value)\xE2\x80\xA8" "bar.try_publish(value)"
  }) {
    try { oresforge::parse_source(input); }
    catch(const std::runtime_error& e) {
      if(std::string_view(e.what()).find("source contains forbidden Unicode control")!=std::string_view::npos)continue;
      throw;
    }
    throw std::runtime_error("Unicode line separator escaped UTF-8 validator");
  }
  for(const auto& input: {
    "pub fnc main():int{return 42;} // alias.try_publish(share! event)",
    "/* nested /* alias.try_publish(event); */ completed */ pub fnc main():int{return 42;}",
    "pub fnc try_publish(int value):int {return value;}",
    "pub fnc main():int{return try_publish(42);}",
    "pub fnc main():int{return topic.try_publisher(42);}",
    "pub fnc main():int{return \"topic.try_publish(event)\";}",
    "pub fnc main():int{return 'topic.try_publish(event)';}",
    "pub fnc main():int{return \x60topic.try_publish(event)\x60;}",
    "// alias.try_publish(event)\xC2\x85" "pub fnc main():int{return 42;}",
  }) expect_inert(input);
  // Deterministic randomized source-only audit: trivia and nested comments
  // must never hide a member call, and a comment-only mention is inert.
  std::mt19937 rng(0xA7702026);
  const std::string trivia[]={" ","\t","\n","/* simple */",
    "/* outer /* nested */ end */","// comment\n"," /* inert .try_publish() */"};
  for(int case_id=0;case_id<12000;++case_id) {
    std::string source="fnc main():int{return imported_topic";
    for(int n=0;n<5;++n)source+=trivia[rng()%7];
    source+='.';
    for(int n=0;n<5;++n)source+=trivia[rng()%7];
    source+="try_publish(share! event);}";
    try { oresforge::source::reject_unlowered_topic_publish(source); }
    catch(const std::runtime_error& e) {
      if(std::string_view(e.what()).find("TOPIC_PUBLISH_BACKEND_UNSUPPORTED")!=std::string_view::npos) {
        oresforge::source::reject_unlowered_topic_publish(
          "fnc main():int{return 42;} /* imported_topic.try_publish(event) */");
        continue;
      }
      throw;
    }
    throw std::runtime_error("randomized Topic member denial escaped");
  }
  auto parse_ok=oresforge::parse_source("fnc main():int{return 42;}");
  auto runtime_ok=oresforge::parse_runtime_source("fnc main():int{return 42;}");
  if(parse_ok.instructions.empty()||runtime_ok.instructions.empty())
    throw std::runtime_error("scalar positive frontends failed");
  std::cout << "FORGE_TYPED_PUBSUB_SOURCE_ADMISSION_PASS negative_cases=7 unicode_control_cases=2 inert_cases=9 scalar_frontends=2 randomized_cases=12000\n";
}
