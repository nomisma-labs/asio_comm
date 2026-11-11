#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

"""Exercise the live-test runner with fake executables, without network access."""

import argparse
import os
from pathlib import Path
import shlex
import shutil
import signal
import subprocess
import tempfile
import time


def executable(path, body):
    path.write_text("#!/usr/bin/env bash\n" + body + "\n")
    path.chmod(0o755)


def build_directory(root, name):
    folder = root / name
    folder.mkdir()
    (folder / "bin").mkdir()
    (folder / "CMakeCache.txt").write_text(
        f"CMAKE_PROJECT_NAME:STATIC=asio_comm\nCMAKE_CACHEFILE_DIR:INTERNAL={folder}\n")
    return folder


def report(folder):
    return next((folder / "conn_test_results").glob("*/summary.md")).read_text()


def run(script, folder, options, env):
    return subprocess.run([str(script), *options], cwd=folder, env=env,
                          text=True, capture_output=True, timeout=20)


def wait_file(path, process):
    deadline = time.monotonic() + 5
    while not path.exists():
        assert process.poll() is None, process.communicate()
        assert time.monotonic() < deadline, path
        time.sleep(0.02)


def check_interrupt(script, folder, env, during_delay):
    executable(folder / "bin/a_feeder", '''
echo 'INFO main.cpp:1 Connected: local'
echo $$ > child.pid
''' + ("" if during_delay else "trap 'exit 0' TERM\nwhile :; do sleep 0.1; done\n") + '''
echo 'CONN_STATS messages=2 payload_bytes=99' >&2
''')
    executable(folder / "bin/b_feeder", "touch should_not_run")
    process = subprocess.Popen([str(script), "--delay-sec", "10"], cwd=folder, env=env,
                               text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        wait_file(folder / "child.pid", process)
        if during_delay:
            deadline = time.monotonic() + 5
            while "| a_feeder | PASS |" not in report(folder):
                assert time.monotonic() < deadline
                time.sleep(0.02)
            time.sleep(0.1)
        process.send_signal(signal.SIGINT)
        stdout, stderr = process.communicate(timeout=8)
        assert process.returncode == 130, (stdout, stderr)
        text = report(folder)
        assert "**Interrupted.**" in text and not (folder / "should_not_run").exists(), text
        if not during_delay:
            assert "| a_feeder | INTERRUPTED | 0 |" in text, text
        try:
            os.kill(int((folder / "child.pid").read_text()), 0)
        except ProcessLookupError:
            pass
        else:
            raise AssertionError("Interrupted child still running")
    finally:
        if process.poll() is None:
            process.kill()
            process.communicate()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--script", type=Path, required=True)
    args = parser.parse_args()
    script = args.script.resolve()
    env = dict(os.environ)
    env.pop("BINANCE_SBE_API_KEY", None)
    with tempfile.TemporaryDirectory(prefix="asio-conn-runner-") as temporary:
        root = Path(temporary)
        clock_tools = root / "clock_tools"
        clock_tools.mkdir()
        executable(clock_tools / "date", f'''
# Wall-clock timestamps are allowed; durations must not use the adjustable system clock.
[[ "$*" != *'%s'* ]] || exit 99
exec {shlex.quote(shutil.which('date'))} "$@"
''')
        env['PATH'] = f"{clock_tools}:{env['PATH']}"
        assert run(script, root, [], env).returncode == 2
        assert run(script, root, ["--help"], env).returncode == 0
        empty = build_directory(root, "empty")
        assert run(script, empty, [], env).returncode == 2
        for options in (["--delay-sec"], ["--timeout-sec", "0"], ["--feeder-sec", "-1"],
                        ["--provider-sec", "1.5"], ["--provider-sec", "4294967296"], ["--unknown"]):
            assert run(script, root, options, env).returncode == 2, options

        folder = build_directory(root, "build with spaces")
        executable(folder / "bin/a_feeder", '''
[[ "$*" == '--exit-after-sec 2' ]] || exit 7
echo 'INFO main.cpp:1 Connected: local'
echo 'CONN_STATS messages=3 payload_bytes=12345' >&2
''')
        executable(folder / "bin/b_symbol_provider", '''
[[ "$*" == '--exit-after-sec 10' ]] || exit 7
echo '{"symbols":[]}'
echo 'Received 14 bytes from local' >&2
''')
        executable(folder / "bin/binance_spot_sbe_feeder", "exit 9")
        executable(folder / "bin/ignored_example", "touch should_not_run")
        start = time.monotonic()
        result = run(script, folder, ["--delay-sec", "1"], env)
        duration = time.monotonic() - start
        text = report(folder)
        assert result.returncode == 0, (result, text)
        assert 1 <= duration < 3, duration  # No wait after the last run or for a skipped app.
        assert "| a_feeder | PASS | 0 |" in text and "| 3 | 12345 |" in text, text
        assert "| b_symbol_provider | PASS | 0 |" in text, text
        assert "| binance_spot_sbe_feeder | SKIP |" in text, text
        for row in text.splitlines():
            if row.startswith(("| a_feeder |", "| b_symbol_provider |")):
                assert float(row.split('|')[4]) >= 0, row
        assert not (folder / "should_not_run").exists()

        failure = build_directory(root, "failure")
        executable(failure / "bin/a_feeder", '''
echo 'INFO main.cpp:1 Connected: local'
echo 'CONN_STATS messages=0 payload_bytes=0' >&2
''')
        executable(failure / "bin/b_feeder", "echo 'bad | <endpoint>' >&2; exit 7")
        executable(failure / "bin/c_feeder", "echo 'INFO main.cpp:1 Connected: local'")
        executable(failure / "bin/d_symbol_provider", "echo '{\"error\":\"bad\"}'")
        executable(failure / "bin/e_symbol_provider", "echo 'Received 4 bytes from local' >&2")
        executable(failure / "bin/f_feeder", "echo 'CONN_STATS messages=2 payload_bytes=99' >&2")
        result = run(script, failure, ["--delay-sec", "0"], env)
        text = report(failure)
        assert result.returncode == 1 and "1 NO_DATA" in text and "5 FAIL" in text, (result, text)
        assert "bad \\| &lt;endpoint&gt;" in text, text

        timeout = build_directory(root, "timeout")
        executable(timeout / "bin/a_feeder", "trap 'exit 0' TERM\nwhile :; do sleep 0.1; done")
        executable(timeout / "bin/b_feeder", "trap '' TERM\nwhile :; do sleep 0.1; done")
        result = run(script, timeout, ["--delay-sec", "0", "--timeout-sec", "1"], env)
        text = report(timeout)
        assert result.returncode == 1, (result, text)
        assert "| a_feeder | TIMEOUT | 0 |" in text, text
        assert "| b_feeder | TIMEOUT | 137 |" in text, text

        check_interrupt(script, build_directory(root, "interrupt"), env, False)
        check_interrupt(script, build_directory(root, "interrupt_delay"), env, True)

        built = build_directory(root, "optional_build")
        fake_tools = root / "tools"
        fake_tools.mkdir()
        executable(fake_tools / "cmake", '''
[[ "$*" == '--build . --target feeders providers --parallel 4' ]] || exit 3
cat > bin/built_symbol_provider <<'EOF'
#!/usr/bin/env bash
echo data
echo 'Received 4 bytes from local' >&2
EOF
chmod +x bin/built_symbol_provider
''')
        build_env = dict(env, PATH=f"{fake_tools}:{env['PATH']}", CMAKE_BUILD_PARALLEL_LEVEL="4")
        result = run(script, built, ["--build", "--delay-sec", "0"], build_env)
        assert result.returncode == 0 and "Build succeeded" in report(built), result
        executable(fake_tools / "cmake", "exit 9")
        broken_build = build_directory(root, "failed_build")
        assert run(script, broken_build, ["--build"], build_env).returncode == 2
        assert "Build stopped with exit code 9" in report(broken_build)
    print("conn_test runner: discovery, statistics, statuses, deadlines, signals and builds passed")


if __name__ == "__main__":
    main()
