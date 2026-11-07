# Forge canonical import compiler rejection matrix

The real Forge bootstrap parser tests now cover canonical `atto.core`,
`atto.alloc`, `atto.std`, optional `atto.runtime.actor`, explicit
local `.atto`, and explicit legacy `.ores` import forms. Each source
must reject with the specific typed-module-graph admission diagnostic,
while a comment mentioning an import remains legal. These tests execute
the **actual native Forge frontend parser**, not a regex-only fixture.
The existing scalar codegen+native-link test still checks executable
output and 42→43 mutation, and the compiler-owned module registry
remains **unsupported** here until typed linking is implemented.

These cases are committed test code, not executed CI evidence; source-owned
runner admission remains blocked.
