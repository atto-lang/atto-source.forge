use std::{
    env, fs,
    os::unix::process::ExitStatusExt,
    path::{Path, PathBuf},
    process::{Command, Output},
};

fn run(program: &Path, args: &[&Path]) -> Result<Output, String> {
    Command::new(program)
        .args(args)
        .output()
        .map_err(|e| format!("failed to run {}: {e}", program.display()))
}

fn require(value: bool, message: impl Into<String>) -> Result<(), String> {
    if value { Ok(()) } else { Err(message.into()) }
}

fn emit_and_link(
    forge: &Path,
    cc: &Path,
    entry: &Path,
    library: &Path,
    root: &Path,
    label: &str,
) -> Result<PathBuf, String> {
    let emitted = Command::new(forge)
        .args(["emit-call-module-asm", "x86_64"])
        .arg(entry)
        .arg(library)
        .output()
        .map_err(|e| e.to_string())?;
    require(
        emitted.status.success() && emitted.stderr.is_empty(),
        format!("{label}: Forge assembly emission failed: {}",
            String::from_utf8_lossy(&emitted.stderr)),
    )?;
    let assembly = String::from_utf8(emitted.stdout).map_err(|e| e.to_string())?;
    require(
        assembly.contains("  call .Lores_fn_") &&
        assembly.contains("jo .Lores_overflow") &&
        assembly.len() < 5_000_000,
        format!("{label}: linked assembly lost calls, checked overflow, or budget"),
    )?;
    let asm = root.join(format!("{label}.s"));
    fs::write(&asm, assembly).map_err(|e| e.to_string())?;
    let driver = root.join(format!("{label}.c"));
    fs::write(
        &driver,
        "#include <inttypes.h>\n#include <stdint.h>\n#include <stdio.h>\n\
         extern int64_t oresforge_entry(void);\n\
         int main(void){const int64_t v=oresforge_entry();\
         if(printf(\"%\" PRId64 \"\\n\",v)<0){return 3;}\
         return fflush(stdout)==0?0:4;}\n",
    ).map_err(|e| e.to_string())?;
    let executable = root.join(format!("{label}-native"));
    let linked = Command::new(cc)
        .args(["-std=c11", "-Wall", "-Wextra", "-Werror"])
        .arg(&driver).arg(&asm).arg("-o").arg(&executable)
        .output().map_err(|e| e.to_string())?;
    require(
        linked.status.success(),
        format!("{label}: native link failed: {}", String::from_utf8_lossy(&linked.stderr)),
    )?;
    Ok(executable)
}

fn main() {
    if let Err(error) = real_main() {
        eprintln!("Forge Math module native proof FAILED: {error}");
        std::process::exit(1);
    }
}

fn real_main() -> Result<(), String> {
    let args: Vec<_> = env::args_os().collect();
    require(args.len() == 6,
        "usage: native_math_module <forge> <normal.ores> <overflow.ores> <math.ores> <cc>")?;
    let forge = PathBuf::from(&args[1]);
    let normal = PathBuf::from(&args[2]);
    let overflow = PathBuf::from(&args[3]);
    let library = PathBuf::from(&args[4]);
    let cc = PathBuf::from(&args[5]);

    let root = env::temp_dir().join(format!("oresforge-math-module-{}", std::process::id()));
    let _ = fs::remove_dir_all(&root);
    fs::create_dir(&root).map_err(|e| e.to_string())?;
    struct Cleanup(PathBuf);
    impl Drop for Cleanup {
        fn drop(&mut self) { let _ = fs::remove_dir_all(&self.0); }
    }
    let _cleanup = Cleanup(root.clone());

    let interpreted = Command::new(&forge)
        .arg("run-call-module").arg(&normal).arg(&library)
        .output().map_err(|e| e.to_string())?;
    require(
        interpreted.status.success() && interpreted.stderr.is_empty() &&
        interpreted.stdout == b"42\n",
        format!("linked interpreter expected 42, got status={:?}, stdout={}, stderr={}",
            interpreted.status.code(),
            String::from_utf8_lossy(&interpreted.stdout),
            String::from_utf8_lossy(&interpreted.stderr)),
    )?;

    let normal_native = emit_and_link(&forge, &cc, &normal, &library, &root, "normal")?;
    let native = run(&normal_native, &[])?;
    require(
        native.status.success() && native.stderr.is_empty() && native.stdout == b"42\n",
        format!("linked native Math expected 42, got {:?}", native.status),
    )?;

    let checked = Command::new(&forge)
        .arg("run-call-module").arg(&overflow).arg(&library)
        .output().map_err(|e| e.to_string())?;
    require(
        checked.status.code() == Some(1) && checked.stdout.is_empty() &&
        String::from_utf8_lossy(&checked.stderr).contains("i64 sub overflow"),
        format!("linked interpreter MIN overflow used wrong failure: {}",
            String::from_utf8_lossy(&checked.stderr)),
    )?;

    let overflow_native = emit_and_link(&forge, &cc, &overflow, &library, &root, "overflow")?;
    let trapped = run(&overflow_native, &[])?;
    require(
        trapped.status.signal() == Some(4) && trapped.stdout.is_empty(),
        format!("linked native Math MIN overflow did not SIGILL: {:?}", trapped.status),
    )?;

    println!("PASS Forge exact stdlib Math: linked interpreter/native result 42 and checked MIN trap");
    Ok(())
}
