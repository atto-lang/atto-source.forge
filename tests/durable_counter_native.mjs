import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {existsSync,mkdtempSync,readFileSync,rmSync,writeFileSync} from 'node:fs';
import {tmpdir} from 'node:os';
import {join,resolve} from 'node:path';
const compiler=resolve(process.env.FORGE_SCALAR||'build/forge_scalar');
const dir=mkdtempSync(join(tmpdir(),'atto-forge-durable-'));
const source=`define durable actor Counter as
    state count: int = 0;
    receive(ActorMail<int> mail) -> void {
        self.count = self.count + mail.value;
    }
end
`;
function run(command,args,input) {
  const r=spawnSync(command,args,{cwd:dir,encoding:'utf8',input,timeout:60000,maxBuffer:1<<20});
  assert.equal(r.error,undefined);return r;
}
function compile(text,mode=true,ok=true){
  const input=join(dir,'counter.atto'),output=join(dir,'counter.cpp');
  writeFileSync(input,text);rmSync(output,{force:true});
  const r=run(compiler,[...(mode?['--durable-counter-actor-v1']:[]),input,'-o',output]);
  assert.equal(r.status,ok?0:2,r.stderr);
  assert.equal(existsSync(output),ok);
  if(!ok){assert.ok(r.stderr.trim());if(mode)assert.match(r.stderr,/E_DURABLE_COUNTER_ACTOR_ABI/);return;}
  return readFileSync(output);
}
function metadata(field) {
  const fnv=bytes=>{let h=0xcbf29ce484222325n;for(const b of bytes)h=BigInt.asUintN(64,(h^BigInt(b))*0x100000001b3n);return h;};
  const le=(n,size)=>{const b=Buffer.alloc(size);for(let i=0;i<size;i++)b[i]=Number((BigInt(n)>>BigInt(8*i))&255n);return b;};
  const actor=Buffer.from('Counter'),name=Buffer.from(field);
  const id=fnv(Buffer.concat([Buffer.from('atto.actor.state.schema.v1'),Buffer.from([0]),le(actor.length,4),actor,le(name.length,4),name,Buffer.from([1])]));
  const data=Buffer.concat([Buffer.from('ATTOST01'),Buffer.from([1]),le(id,8),le(1,2),Buffer.from([1]),le(0,8)]);
  const checkpoint=Buffer.concat([data,le(fnv(data),8)]);
  return Buffer.concat([Buffer.from([1,actor.length]),actor,Buffer.from([name.length]),name,Buffer.from([1,checkpoint.length]),checkpoint]);
}
const driver=`#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
extern "C" std::int64_t atto_durable_transition_v1(std::int64_t,std::int64_t,std::int64_t) noexcept;
extern "C" const unsigned char* atto_durable_state_v1(std::size_t*) noexcept;
int main(){
  std::size_t n=0;const auto* p=atto_durable_state_v1(&n);
  if(!p||n>4096||atto_durable_state_v1(nullptr)!=nullptr)return 2;
  for(std::size_t i=0;i<n;i++)std::printf("%02x",static_cast<unsigned>(p[i]));
  std::puts("");
  std::int64_t s,o,d;
  while(std::cin>>s>>o>>d)std::cout<<atto_durable_transition_v1(s,o,d)<<'\\n';
  return std::cin.eof()?0:3;
}
`;
try{
  const first=compile(source);assert.deepEqual(first,compile(source));
  const renamed=compile(source.replaceAll('count','balance').replaceAll('mail','event'));
  assert.notDeepEqual(first,renamed);
  writeFileSync(join(dir,'driver.cpp'),driver);
  function execute(field,text,inputs){
    compile(text);
    const r=run(process.env.CXX||'c++',['-std=c++20','-O2','-Wall','-Wextra','-Werror','-pedantic','-fsanitize=undefined','-fno-sanitize-recover=all','counter.cpp','driver.cpp','-o','guest']);
    assert.equal(r.status,0,r.stderr);
    const g=run(join(dir,'guest'),[],inputs.map(x=>x.join(' ')).join('\n')+'\n');
    assert.equal(g.status,0,g.stderr);assert.equal(g.stderr,'');
    const [hex,...values]=g.stdout.trim().split('\n');
    assert.equal(hex,metadata(field).toString('hex'));
    assert.equal(values.length,inputs.length);
    return values.map(BigInt);
  }
  const vectors=[],expected=[];
  const add=(s,o,d)=>{vectors.push([s,o,d]);expected.push(s<0n||s>1000000000n?-1n:o===2n?(d===0n?s:-1n):o!==1n||d<=0n||d>1000000000n||s+d>1000000000n?-1n:s+d);};
  for(let i=0n;i<100n;i++)add(i,1n,1n);
  for(const s of [-1n,0n,100n,999999999n,1000000000n,1000000001n])
    for(const o of [0n,1n,2n,3n])
      for(const d of [-1n,0n,1n,42n,1000000000n,1000000001n])add(s,o,d);
  assert.equal(vectors.length,244);
  assert.deepEqual(execute('count',source,vectors),expected);
  assert.deepEqual(execute('balance',source.replaceAll('count','balance').replaceAll('mail','event'),[[1n,1n,2n]]),[3n]);
  for(const bad of [
    source.replace('durable actor','actor'),
    source.replace('durable actor','persistent actor'),
    source.replace('= 0;','= 1;'),
    source.replace('state count','pub state count'),
    source.replace('self.count + mail.value','self.count - mail.value'),
    source.replace('mail.value','9'),
    source.replace('ActorMail<int>','ActorMail<bool>'),
    source.replace('self.count =','self.unknown ='),
    source.replace('receive(','pub receive('),
    source.replace('self.count = self.count + mail.value;','self.count = self.count + mail.value; self.count = 0;'),
    source+'\npub fnc main()->int {return 0;}',
    source+'//\u0000',
    source.replaceAll('count','self')
  ])compile(bad,true,false);
  compile(source,false,false);
  console.log('FORGE_NATIVE_DURABLE_SCHEMA_AND_244_TRANSITIONS_PASS');
  console.log('FORGE_DESCRIPTOR_HEX='+metadata('count').toString('hex'));
}finally{rmSync(dir,{recursive:true,force:true});}
