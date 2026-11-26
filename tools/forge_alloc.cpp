// Restricted experimental .atto allocation guest compiler, not a general Forge frontend.
// Admits a deliberately closed vocabulary of alloc.ByteRegion/std.byte_ops calls.
// Canonical .atto library module bodies are NOT compiled by this stage.
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace {
struct Token { std::string_view value; std::size_t at; };
struct Variable { std::string name; bool mutable_ = false; };
struct Step { enum Kind { allocate, write } kind; int variable; std::int64_t a, b; };
struct Return { enum Kind { read, read_eq, length, equal } kind; int left, right = -1; std::int64_t argument = 0, compared = 0, yes = 1, no = 0; };
struct Plan { std::vector<Variable> vars; std::vector<Step> steps; Return result{}; };

std::optional<std::vector<Token>> scan(std::string_view source) {
  if (source.empty() || source.size() > 16384) return std::nullopt;
  std::vector<Token> out;
  for (std::size_t i = 0; i < source.size();) {
    const unsigned char ch = static_cast<unsigned char>(source[i]);
    if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') { ++i; continue; }
    if (source.substr(i, 2) == "//") {
      while (i < source.size() && source[i] != '\n') ++i;
      continue;
    }
    if (out.size() >= 255 || ch < 32 || ch > 126) return std::nullopt;
    const auto start = i;
    if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == '_') {
      while (i < source.size()) {
        const auto c = source[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '_')) break;
        ++i;
      }
    } else if (ch >= '0' && ch <= '9') {
      while (i < source.size() && source[i] >= '0' && source[i] <= '9') ++i;
    } else if (ch == '"') {
      ++i;
      while (i < source.size() && source[i] != '"') {
        // No interpolation, backslashes, or unbounded/embedded control bytes.
        if (source[i] == '\\' || static_cast<unsigned char>(source[i]) < 32 ||
            i - start > 96) return std::nullopt;
        ++i;
      }
      if (i >= source.size()) return std::nullopt;
      ++i;
    } else {
      if (std::string_view(".():;={},?-").find(ch) == std::string_view::npos)
        return std::nullopt;
      ++i;
    }
    out.push_back({source.substr(start, i-start), start});
  }
  out.push_back({"<eof>", source.size()});
  return out;
}

struct Parser {
  const std::vector<Token>& ts;
  std::size_t pos = 0;
  std::string error = "E_ALLOC_PROFILE";
  bool region_import = false, bytes_import = false;
  std::string region_alias, bytes_alias;
  Plan plan;
  explicit Parser(const std::vector<Token>& tokens): ts(tokens) {}
  bool take(std::string_view s) { if (pos>=ts.size() || ts[pos].value != s) return false; ++pos; return true; }
  bool require(std::string_view s) { return take(s); }
  bool identifier(std::string& s) {
    if (pos >= ts.size()) return false;
    auto word = ts[pos].value;
    if (word.empty() || !((word[0] >= 'a' && word[0] <= 'z') || word[0]=='_')) return false;
    for (const char c : word) if (!((c >= 'a' && c <= 'z') ||
       (c >= '0' && c <= '9') || c == '_')) return false;
    if (word.size() > 48) return false;
    s = std::string(word); ++pos; return true;
  }
  bool number(std::int64_t& value) {
    bool negative = take("-");
    if (pos >= ts.size()) return false;
    auto token = ts[pos].value;
    if (token.empty() || token[0] < '0' || token[0] > '9') return false;
    std::string digits = (negative ? "-" : "") + std::string(token);
    auto [end,ec] = std::from_chars(digits.data(),digits.data()+digits.size(),value);
    if (ec != std::errc{} || end != digits.data()+digits.size()) return false;
    ++pos; return true;
  }
  int variable(std::string_view name) const {
    for (std::size_t i=0;i<plan.vars.size();++i)
      if(plan.vars[i].name==name) return static_cast<int>(i);
    return -1;
  }
  bool import_module() {
    if (!take("import") || !require("module")) return false;
    std::string alias;
    if(!identifier(alias) || !require("from") || !require("pkg")) return false;
    if (pos >= ts.size() - 1) return false;
    const auto module=ts[pos++].value;
    if(!require(";")) return false;
    if(module=="\"alloc.byte_region\"" && !region_import && alias != bytes_alias) {
      region_import=true;region_alias=alias;return true;
    }
    if(module=="\"std.byte_ops\"" && !bytes_import && alias != region_alias) {
      bytes_import=true;bytes_alias=alias;return true;
    }
    error="E_ALLOC_IMPORT";return false;
  }
  bool parse_return() {
    std::string target;
    if(!identifier(target) || !require(".")) return false;
    if(bytes_import && target==bytes_alias && take("bytes_equal")) {
      std::string a,b;
      if(!require("(") || !identifier(a) || !require(",") || !identifier(b) ||
         !require(")") || !require("?")) return false;
      auto i=variable(a),j=variable(b);
      if (i<0 || j<0) return false;
      std::int64_t yes,no;
      if(!number(yes) || !require(":") || !number(no) || !require(";"))return false;
      plan.result={Return::equal,i,j,0,0,yes,no};return true;
    }
    const int v=variable(target);
    if(v<0) return false;
    if(take("length")) {
      if(!require("(") || !require(")") || !require(";"))return false;
      plan.result={Return::length,v};return true;
    }
    if(!take("read_byte") || !require("("))return false;
    std::int64_t offset;
    if(!number(offset)||!require(")"))return false;
    if(take("eq")) {
      std::int64_t compare,yes,no;
      if(!number(compare)||!require("?")||!number(yes)||!require(":")||
         !number(no)||!require(";"))return false;
      plan.result={Return::read_eq,v,-1,offset,compare,yes,no};return true;
    }
    if(!require(";"))return false;
    plan.result={Return::read,v,-1,offset};return true;
  }
  bool parse() {
    if(!import_module() || !region_import) return false;
    // One optional std import; order is checked but not assumed.
    if(ts[pos].value=="import" && !import_module()) return false;
    if(!require("pub")||!require("fnc")||!require("main")||
       !require("(")||!require(")")||!require(":")||!require("int")||
       !require("throws")||!require("Error")||!require("{")) return false;
    while(ts[pos].value!="return" && ts[pos].value!="<eof>") {
      if(plan.steps.size()>=48)return false;
      if(take("val")||take("let")) {
        const bool mutable_=take("mut");
        std::string name;
        if(!identifier(name)||variable(name)>=0||name==region_alias||name==bytes_alias||
           !require("="))return false;
        std::string qualifier;
        if(!identifier(qualifier)||qualifier!=region_alias||!require(".")||
           !require("ByteRegion")||!require(".")||!require("allocate_zeroed")||
           !require("("))return false;
        std::int64_t size;
        if(!number(size)||!require(")")||!require(";")||plan.vars.size()>=16)return false;
        int index=static_cast<int>(plan.vars.size());
        plan.vars.push_back({name,mutable_});
        plan.steps.push_back({Step::allocate,index,size,0});
      } else {
        std::string name;
        if(!identifier(name))return false;
        if(name=="OwnedBytes") {error="E_SEALED_ALLOC";return false;}
        int v=variable(name);
        if(v<0||!require(".")||!require("write_byte")||!require("("))return false;
        if(!plan.vars[v].mutable_){error="E_ALLOC_IMMUTABLE";return false;}
        std::int64_t offset,value;
        if(!number(offset)||!require(",")||!number(value)||
           !require(")")||!require(";"))return false;
        plan.steps.push_back({Step::write,v,offset,value});
      }
    }
    if(!require("return")||!parse_return()||!require("}")||
       ts[pos].value!="<eof>" || plan.vars.empty())return false;
    return true;
  }
};

// C++ tokenization treats the magnitude of INT64_MIN as unsigned. Emit
// signed, parenthesized constants so strict native builds preserve Atto i64.
std::string i64_literal(std::int64_t value) {
  if (value == std::numeric_limits<std::int64_t>::min())
    return "(-9223372036854775807LL - 1LL)";
  return "(" + std::to_string(value) + "LL)";
}

std::string emit(const Plan& plan) {
  std::string output=R"(#include <atto/tenant_bytes_v1.hpp>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <utility>
using namespace atto::tenant_bytes_v1;
static int fail(Code code) {
  switch(code) {
    case Code::bounds: std::fputs("E_BUFFER_BOUNDS\n",stderr);return 72;
    case Code::value: std::fputs("E_BUFFER_VALUE\n",stderr);return 73;
    case Code::invalid_size: std::fputs("E_BUFFER_ALLOCATION\n",stderr);return 74;
    case Code::quota: std::fputs("E_TENANT_QUOTA\n",stderr);return 75;
    default: std::fputs("E_ALLOC_RUNTIME\n",stderr);return 76;
  }
}
int main() {
  // Test-only trusted principal. This is NOT a production TenantVM bootstrap.
  Arena arena({1,1,1}, 65536);
)";
  auto v=[](int i){return "v"+std::to_string(i);};
  for(const auto& s:plan.steps){
    if(s.kind==Step::allocate){
      output += "  auto "+v(s.variable)+"=arena.allocate_zeroed("+i64_literal(s.a)+");\n";
      output += "  if("+v(s.variable)+".code!=Code::ok) return fail("+v(s.variable)+".code);\n";
    }else{
      output += "  { auto status="+v(s.variable)+".value->write_byte(arena,"+
                i64_literal(s.a)+","+i64_literal(s.b)+");"+
                " if(status!=Code::ok) return fail(status); }\n";
    }
  }
  const auto& r=plan.result;
  switch(r.kind){
    case Return::length:
      output += "  std::printf(\"%llu\\n\",static_cast<unsigned long long>("+
                v(r.left)+".value->length()));\n";
      break;
    case Return::read:
    case Return::read_eq:
      output += "  std::uint8_t byte=0;\n";
      output += "  { auto status="+v(r.left)+".value->read_byte(arena,"+
                i64_literal(r.argument)+",byte); if(status!=Code::ok) return fail(status); }\n";
      if(r.kind==Return::read)
        output += "  std::printf(\"%u\\n\",static_cast<unsigned>(byte));\n";
      else
        output += "  std::printf(\"%lld\\n\",static_cast<long long>(static_cast<std::int64_t>(byte)=="+
                  i64_literal(r.compared)+"?"+i64_literal(r.yes)+":"+
                  i64_literal(r.no)+"));\n";
      break;
    case Return::equal:
      output += "  bool same="+v(r.left)+".value->length()=="+v(r.right)+".value->length();\n";
      output += "  if(same) for(std::size_t i=0;i<"+v(r.left)+".value->length();++i) {\n";
      output += "    std::uint8_t left=0,right=0;\n";
      // The access always stays scoped to the originating arena; no raw pointers.
      output += "    auto lc="+v(r.left)+".value->read_byte(arena,static_cast<std::int64_t>(i),left);\n";
      output += "    if(lc!=Code::ok) return fail(lc);\n";
      output += "    auto rc="+v(r.right)+".value->read_byte(arena,static_cast<std::int64_t>(i),right);\n";
      output += "    if(rc!=Code::ok) return fail(rc);\n";
      output += "    if(left!=right){same=false;break;}\n  }\n";
      output += "  std::printf(\"%lld\\n\",static_cast<long long>(same?"+
                i64_literal(r.yes)+":"+i64_literal(r.no)+"));\n";
      break;
  }
  output += "  return 0;\n}\n";
  return output;
}

int deny(std::string_view code) {std::cerr<<code<<'\n';return 2;}
} // namespace

int main(int argc,char**argv) {
  if(argc!=5 || std::string_view(argv[1])!="--experimental-host-profile" ||
     std::string_view(argv[3])!="-o")return deny("E_ALLOC_EXPERIMENTAL_REQUIRED");
  const fs::path input(argv[2]),output(argv[4]);
  if(input.extension()!=".atto"||output.extension()!=".cpp")return deny("E_ALLOC_PATH");
  std::error_code ec;
  const auto source_absolute=fs::absolute(input,ec);
  if(ec)return deny("E_ALLOC_PATH");
  fs::path source_parent=source_absolute.lexically_normal().root_path();
  for(const auto&part:source_absolute.lexically_normal().relative_path().parent_path()) {
    source_parent/=part;
    ec.clear(); const auto state=fs::symlink_status(source_parent,ec);
    if(ec||fs::is_symlink(state)||!fs::is_directory(state))return deny("E_ALLOC_PATH");
  }
  ec.clear(); const auto source_state=fs::symlink_status(source_absolute,ec);
  if(ec||fs::is_symlink(source_state)||!fs::is_regular_file(source_state))
    return deny("E_ALLOC_SOURCE");
  const auto size=fs::file_size(input,ec);
  if(ec||size>16384)return deny("E_ALLOC_LIMIT");
  std::ifstream file(input,std::ios::binary);
  if(!file)return deny("E_ALLOC_SOURCE");
  std::string source(static_cast<std::size_t>(size),'\0');
  if(!source.empty())file.read(source.data(),static_cast<std::streamsize>(size));
  if(!file||file.peek()!=std::char_traits<char>::eof())return deny("E_ALLOC_SOURCE");
  auto tokens=scan(source);
  if(!tokens)return deny("E_ALLOC_LEXICAL");
  for(const auto& token:*tokens) if(token.value=="OwnedBytes")return deny("E_SEALED_ALLOC");
  Parser parser{*tokens};
  if(!parser.parse())return deny(parser.error);
  const auto cpp=emit(parser.plan);
  if(cpp.size()>32768)return deny("E_ALLOC_LIMIT");
  const auto absolute=fs::absolute(output,ec);
  if(ec)return deny("E_ALLOC_PATH");
  const auto normalized=absolute.lexically_normal();
  fs::path parent=normalized.root_path();
  for(const auto&part:normalized.relative_path().parent_path()) {
    parent/=part;
    const auto status=fs::symlink_status(parent,ec);
    if(ec||fs::is_symlink(status)||!fs::is_directory(status))return deny("E_ALLOC_PATH");
  }
  // Never overwrite a preexisting output or its temporary staging path.
  const auto part=fs::path(output.string()+".part");
  for(const auto&p:{output,part}) {
    ec.clear(); const auto status=fs::symlink_status(p,ec);
    if(ec==std::errc::no_such_file_or_directory){ec.clear();continue;}
    if(ec||fs::exists(status)||fs::is_symlink(status))return deny("E_ALLOC_PATH");
  }
  std::ofstream out(part,std::ios::binary|std::ios::trunc);
  if(!out)return deny("E_ALLOC_OUTPUT");
  out.write(cpp.data(),static_cast<std::streamsize>(cpp.size()));out.flush();
  const bool valid=static_cast<bool>(out);out.close();
  if(!valid||!out){fs::remove(part,ec);return deny("E_ALLOC_OUTPUT");}
  fs::rename(part,output,ec);
  if(ec){fs::remove(part,ec);return deny("E_ALLOC_OUTPUT");}
  return 0;
}
