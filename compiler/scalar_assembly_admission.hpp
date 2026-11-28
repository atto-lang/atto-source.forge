#pragma once
#include <charconv>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace oresforge::cache {
// Closed x86-64 runtime emitter grammar, not a general assembler, source
// equivalence proof or machine-code verifier. No assembler directives other
// than the exact fixed header/footer; sequential initialized stack slots.
inline void audit_scalar_assembly(std::string_view input) {
    auto deny=[](){throw std::runtime_error("E_ASM_ADMISSION: restricted scalar assembly");};
    if(input.empty()||input.size()>4*1024*1024||input.back()!='\n')deny();
    std::vector<std::string_view> lines;
    while(!input.empty()){
        auto end=input.find('\n');
        if(end==std::string_view::npos||end>128||lines.size()>=16400)deny();
        lines.push_back(input.substr(0,end));input.remove_prefix(end+1);
    }
    std::size_t pos=0;
    auto next=[&](){if(pos>=lines.size())deny();return lines[pos++];};
    auto exact=[&](std::string_view want){if(next()!=want)deny();};
    auto integer=[&](std::string_view v){
        std::int64_t n=0;
        auto r=std::from_chars(v.data(),v.data()+v.size(),n);
        if(v.empty()||r.ec!=std::errc{}||r.ptr!=v.data()+v.size()||std::to_string(n)!=v)deny();
        return n;
    };
    auto middle=[&](std::string_view line,std::string_view pre,std::string_view post){
        if(!line.starts_with(pre)||!line.ends_with(post)||line.size()<pre.size()+post.size())deny();
        return line.substr(pre.size(),line.size()-pre.size()-post.size());
    };
    exact(".text");exact(".globl oresforge_entry");
    exact(".type oresforge_entry, @function");exact("oresforge_entry:");
    auto frame=integer(middle(next(),"  subq $",", %rsp"));
    if(frame<16||frame>32768||frame%16)deny();
    std::int64_t count=0;
    auto load=[&](std::string_view line,std::string_view pre){
        auto n=integer(middle(line,pre,"(%rsp), %rax"));
        if(n<0||n%8||n/8>=count)deny();
    };
    auto store=[&](std::string_view line,std::string_view pre){
        auto n=integer(middle(line,pre,"(%rsp)"));
        if(count>=4095||n!=count*8||n>=frame)deny();
        ++count;
    };
    for(;;){
        auto line=next();
        if(line.starts_with("  movabs $")){
            (void)integer(middle(line,"  movabs $",", %rax"));
            store(next(),"  movq %rax, ");
            continue;
        }
        bool parameter=false;
        for(auto pre:{"  movq %rdi, ","  movq %rsi, ","  movq %rdx, ","  movq %rcx, "})
            if(line.starts_with(pre)){store(line,pre);parameter=true;break;}
        if(parameter)continue;
        load(line,"  movq ");
        auto op=next();
        if(op.starts_with("  addq $")){
            if(integer(middle(op,"  addq $",", %rsp"))!=frame||
               ((count+1)*8+15)/16*16!=frame)deny();
            exact("  ret");break;
        }
        if(op.starts_with("  addq "))load(op,"  addq ");
        else if(op.starts_with("  subq "))load(op,"  subq ");
        else if(op.starts_with("  imulq "))load(op,"  imulq ");
        else deny();
        exact("  jo .Loresforge_overflow");
        store(next(),"  movq %rax, ");
    }
    exact(".Loresforge_overflow:");exact("  ud2");
    exact(".size oresforge_entry, .-oresforge_entry");
    exact(".section .note.GNU-stack,\"\",@progbits");
    if(pos!=lines.size())deny();
}
} // namespace oresforge::cache
