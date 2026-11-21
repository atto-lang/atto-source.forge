//! Source-local Rust runner for the real Forge native mailbox executable.
//! This validates host-runtime behavior, not compiled .atto guest semantics.
use std::{
    env,
    fs,
    path::PathBuf,
    process::Command,
    time::{SystemTime, UNIX_EPOCH},
};

struct TempBuild(PathBuf);
impl Drop for TempBuild {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}

#[test]
fn bounded_try_send_native_at_o1_and_o2() {
    let root = PathBuf::from(env!("PWD"));
    let source = root.join("verification/actor_mailbox_limits.cpp");
    let runtime = root.join("runtime/actor.cpp");
    assert!(source.is_file() && runtime.is_file(), "run from Forge repository root");
    let scratch = env::temp_dir().join(format!(
        "forge-try-send-{}-{}",
        std::process::id(),
        SystemTime::now().duration_since(UNIX_EPOCH).unwrap().as_nanos()
    ));
    fs::create_dir(&scratch).expect("isolated build directory");
    let _cleanup = TempBuild(scratch.clone());
    let compiler = env::var_os("CXX").unwrap_or_else(|| "g++".into());
    for flag in ["-O1", "-O2"] {
        let binary = scratch.join(if flag == "-O1" { "try-send-o1" } else { "try-send-o2" });
        let compile = Command::new(&compiler)
            .args(["-std=c++20", "-g", "-pthread", "-Wall", "-Wextra",
                   "-Wconversion", "-Werror", "-D_GLIBCXX_ASSERTIONS", flag])
            .arg("-I").arg(&root).arg(&runtime).arg(&source).arg("-o").arg(&binary)
            .output().expect("invoke real C++ compiler");
        assert!(compile.status.success(),
            "native Forge mailbox {flag} compile failed: {}",
            String::from_utf8_lossy(&compile.stderr));
        let execution = Command::new(&binary).output().expect("run real Forge mailbox test");
        assert!(execution.status.success(), "mailbox {flag} exited {:?}: {}",
            execution.status.code(), String::from_utf8_lossy(&execution.stderr));
        let output = String::from_utf8_lossy(&execution.stdout);
        assert!(output.lines().any(|line| line == "forge actor mailbox bounds: pass"),
            "missing real mailbox execution marker at {flag}: {output}");
    }
}
