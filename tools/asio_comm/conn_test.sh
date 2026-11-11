#!/usr/bin/env bash
# SPDX-FileCopyrightText: 2026 Alexey Tkachenko
# SPDX-License-Identifier: LicenseRef-Sustainable-Use-1.0

# Live, sequential smoke test. Run from a configured asio_comm build directory.
set -euo pipefail
export LC_ALL=C

usage() {
    cat <<'EOF'
Usage: conn_test.sh [--build] [--feeder-sec N] [--provider-sec N]
                    [--delay-sec N] [--timeout-sec N]

Run from a configured asio_comm build directory, e.g.:
  cd build/dev
  ../../tools/asio_comm/conn_test.sh --build

  --build           Build the feeders and providers group targets first.
  --feeder-sec N    Feeder time after connection (default: 2).
  --provider-sec N  Provider time after connection (default: 10).
  --delay-sec N     Pause between applications (default: 10).
  --timeout-sec N   Total process deadline including setup (default: 60).
                    SIGTERM on expiry, SIGKILL after another 5 seconds.
  --help            Show this help.

Durations are unsigned integer seconds; timeout must be positive.
Results: conn_test_results/<UTC timestamp>-<unique suffix>/summary.md and raw logs.
No JSON parsing. Payload counts may include application acknowledgements/heartbeats.
Exit: 0 checks passed (credential skips allowed), 1 failed/no data/timed out,
      2 usage/build error, 130 interrupted by SIGINT, 143 by SIGTERM.
EOF
}

die() { printf 'conn_test: %s\n' "$*" >&2; exit 2; }

feeder_sec=2
provider_sec=10
delay_sec=10
timeout_sec=60
build=false
while (($#)); do
    case "$1" in
        --help) usage; exit 0 ;;
        --build) build=true; shift ;;
        --feeder-sec|--provider-sec|--delay-sec|--timeout-sec)
            option=$1
            (($# >= 2)) || die "Missing value for $option"
            [[ $2 =~ ^[0-9]{1,10}$ ]] || die "$option requires unsigned integer seconds"
            value=$((10#$2))
            ((value <= 4294967295)) || die "$option exceeds UINT_MAX"
            case "$option" in
                --feeder-sec) feeder_sec=$value ;;
                --provider-sec) provider_sec=$value ;;
                --delay-sec) delay_sec=$value ;;
                --timeout-sec) timeout_sec=$value ;;
            esac
            shift 2 ;;
        *) die "Unknown argument: $1 (use --help)" ;;
    esac
done
((timeout_sec > 0)) || die '--timeout-sec must be positive'

[[ -f CMakeCache.txt ]] && grep -qx 'CMAKE_PROJECT_NAME:STATIC=asio_comm' CMakeCache.txt ||
    die 'Run from a configured asio_comm build directory, e.g. cd build/dev'
build_dir=$(pwd -P)
cache_dir=$(sed -n 's/^CMAKE_CACHEFILE_DIR:INTERNAL=//p' CMakeCache.txt)
[[ -d $cache_dir && $(cd "$cache_dir" && pwd -P) == "$build_dir" ]] ||
    die 'CMakeCache.txt belongs to a different build directory; reconfigure it first'
for command in awk date mktemp sort; do
    command -v "$command" >/dev/null || die "Required command not found: $command"
done
timeout_command=
for candidate in timeout gnutimeout; do
    if command -v "$candidate" >/dev/null && "$candidate" --version | grep -q 'GNU coreutils'; then
        timeout_command=$(command -v "$candidate")
        break
    fi
done
[[ -n $timeout_command ]] || die 'GNU timeout is required (timeout or gnutimeout)'
[[ -r /proc/uptime ]] || die 'Linux /proc/uptime is required for monotonic elapsed times'

monotonic_ms() {
    # Linux uptime is independent of wall-clock corrections (and includes system suspension).
    awk '{printf "%.0f\n", $1 * 1000}' /proc/uptime
}

mkdir -p conn_test_results
report_dir=$(mktemp -d "conn_test_results/$(date -u +%Y%m%dT%H%M%SZ)-XXXXXX")
summary=$report_dir/summary.md
started_ms=$(monotonic_ms)
cat >"$summary" <<EOF
# Connectivity test

- Started (UTC): $(date -u +%FT%TZ)
- Build directory: \`$build_dir\`
- Build requested: $build
- Feeder/provider runtime after connection: ${feeder_sec}s / ${provider_sec}s
- Delay between applications: ${delay_sec}s
- Total process deadline: ${timeout_sec}s, then up to 5s for termination
- Elapsed time uses monotonic uptime; UTC timestamps are informational.

Payload bytes exclude logging and transport overhead. Feeder counts cover relayed application
payloads, possibly including acknowledgements/heartbeats; they do not prove market-data readiness.
Binary/compressed payloads use their received size. Provider bytes describe the completed catalog.
An empty short sample is NO_DATA, not proof of a broken connection.

EOF
printf 'Report: %s/%s\n' "$build_dir" "$summary"

active_pid=
interrupted=0
interrupt() {
    interrupted=$1
    # GNU timeout owns the child process group and forwards TERM, including during a build.
    if [[ -n $active_pid ]]; then kill -TERM "$active_pid" 2>/dev/null || true; fi
}
trap 'interrupt 130' INT
trap 'interrupt 143' TERM

wait_active() {
    process_code=0
    wait "$active_pid" || process_code=$?
    # A trapped signal interrupts wait before the child has drained its cancellations.
    if ((interrupted)); then
        process_code=0
        wait "$active_pid" || process_code=$?
    fi
    active_pid=
}

if $build; then
    command -v cmake >/dev/null || die 'Required command not found: cmake'
    printf 'Building feeders and providers...\n'
    "$timeout_command" --preserve-status --signal=TERM --kill-after=5 0 \
        cmake --build . --target feeders providers --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}" \
        >"$report_dir/build.log" 2>&1 &
    active_pid=$!
    wait_active
    if ((interrupted || process_code)); then
        printf 'Build stopped with exit code %s. See [build.log](build.log).\n' "$process_code" >>"$summary"
        ((interrupted)) && exit "$interrupted"
        die "Build failed; see $report_dir/build.log"
    fi
    printf 'Build succeeded; see [build.log](build.log).\n\n' >>"$summary"
fi

[[ -d bin ]] || die 'No bin directory; use --build to build feeders and providers'
shopt -s nullglob
apps=()
feeder_count=0
provider_count=0
while IFS= read -r executable; do
    [[ -f $executable && -x $executable ]] || continue
    name=${executable##*/}
    [[ $name =~ ^[a-z0-9_]+$ ]] || die "Unsupported executable name: $name"
    apps+=("$executable")
    if [[ $name == *_feeder ]]; then ((feeder_count+=1)); else ((provider_count+=1)); fi
done < <(printf '%s\n' bin/*_feeder bin/*_symbol_provider | sort -u)
((${#apps[@]})) || die 'No feeder/provider executables in bin; use --build'

printf 'Found %s feeders and %s providers; executing sequentially.\n' "$feeder_count" "$provider_count"
printf 'Discovered **%s feeders** and **%s providers**. Only existing binaries are tested.\n\n' \
    "$feeder_count" "$provider_count" >>"$summary"
printf '| Application | Result | Exit | Elapsed (s) | Messages | Payload bytes | Details | Logs |\n' >>"$summary"
printf '| --- | --- | ---: | ---: | ---: | ---: | --- | --- |\n' >>"$summary"

passed=0
failed=0
no_data=0
timed_out=0
skipped=0
tested=0
have_run=false
for executable in "${apps[@]}"; do
    ((interrupted == 0)) || break
    name=${executable##*/}
    stdout_file=$report_dir/$name.stdout.log
    stderr_file=$report_dir/$name.stderr.log
    messages=—
    payload_bytes=—
    elapsed=0.000
    process_code=—
    reason=
    result=

    if [[ $name == binance_spot_sbe_feeder && -z ${BINANCE_SBE_API_KEY:-} ]]; then
        printf '[%s/%s] %s (skipped)\n' "$((tested+1))" "${#apps[@]}" "$name"
        result=SKIP
        reason='BINANCE_SBE_API_KEY is not set'
        ((skipped+=1))
        : >"$stdout_file"
        printf '%s\n' "$reason" >"$stderr_file"
    else
        if $have_run && ((delay_sec > 0)); then
            sleep "$delay_sec" &
            active_pid=$!
            wait_active
        fi
        ((interrupted == 0)) || break
        have_run=true
        runtime=$provider_sec
        [[ $name != *_feeder ]] || runtime=$feeder_sec
        printf '[%s/%s] %s (--exit-after-sec %s)\n' "$((tested+1))" "${#apps[@]}" "$name" "$runtime"
        begin_ms=$(monotonic_ms)
        "$timeout_command" --verbose --preserve-status --signal=TERM --kill-after=5 "$timeout_sec" \
            "$executable" --exit-after-sec "$runtime" >"$stdout_file" 2>"$stderr_file" &
        active_pid=$!
        wait_active
        end_ms=$(monotonic_ms)
        elapsed=$(awk -v begin="$begin_ms" -v end="$end_ms" 'BEGIN {printf "%.3f", (end-begin)/1000}')

        if [[ $name == *_feeder ]]; then
            read -r stats_count messages payload_bytes < <(awk '
                /^CONN_STATS messages=[0-9]+ payload_bytes=[0-9]+$/ {
                    count++; split($2, m, "="); split($3, b, "="); messages=m[2]; bytes=b[2]
                }
                END {print count+0, count ? messages : "—", count ? bytes : "—"}' "$stderr_file")
        else
            read -r stats_count payload_bytes < <(awk '
                /^Received [0-9]+ bytes from / {count++; bytes=$2}
                END {print count+0, count ? bytes : "—"}' "$stderr_file")
            ((stats_count == 0)) || messages=1
        fi

        if ((interrupted)); then
            result=INTERRUPTED
            reason='Run interrupted; remaining applications were not started'
        elif grep -q "^${timeout_command##*/}: sending signal " "$stderr_file"; then
            result=TIMEOUT
            reason="Exceeded ${timeout_sec}s total process deadline"
            ((timed_out+=1))
        elif ((process_code != 0)); then
            result=FAIL
            reason=$(awk '!/^CONN_STATS / && NF {print substr($0,1,240); exit}' "$stderr_file")
            if [[ -z $reason ]]; then
                reason=$(awk '/ ERROR[[:space:]]| WARN[[:space:]]/ {print substr($0,1,240); exit}' "$stdout_file")
            fi
            reason=${reason:-Application exited unsuccessfully; inspect logs}
            ((failed+=1))
        elif ((stats_count != 1)); then
            result=FAIL
            reason='Missing or ambiguous payload summary; rebuild the application'
            ((failed+=1))
        elif [[ $name == *_feeder ]] && ! grep -q ' Connected: ' "$stdout_file"; then
            result=FAIL
            reason='No completed connection reported'
            ((failed+=1))
        elif [[ $payload_bytes == 0 || $messages == 0 ]]; then
            if [[ $name == *_feeder ]]; then
                result=NO_DATA
                reason='Connected, but no application payload within the sample'
                ((no_data+=1))
            else
                result=FAIL
                reason='Empty catalog'
                ((failed+=1))
            fi
        elif [[ $name == *_symbol_provider && ! -s $stdout_file ]]; then
            result=FAIL
            reason='Catalog completion reported without output'
            ((failed+=1))
        else
            result=PASS
            reason='Connected and received application payload'
            [[ $name != *_symbol_provider ]] || reason='Catalog written successfully'
            ((passed+=1))
        fi
    fi

    # Keep diagnostics literal within Markdown table cells.
    reason=${reason//\\/\\\\}
    reason=${reason//|/\\|}
    reason=${reason//</\&lt;}
    reason=${reason//>/\&gt;}
    reason=${reason//$'\r'/}
    printf '| %s | %s | %s | %s | %s | %s | %s | [out](%s.stdout.log), [err](%s.stderr.log) |\n' \
        "$name" "$result" "$process_code" "$elapsed" "$messages" "$payload_bytes" "$reason" "$name" "$name" >>"$summary"
    ((tested+=1))
    printf '  %s: exit=%s, messages=%s, bytes=%s, elapsed=%ss\n' \
        "$result" "$process_code" "$messages" "$payload_bytes" "$elapsed"
done

printf '\n**Results:** %s PASS, %s FAIL, %s NO_DATA, %s TIMEOUT, %s SKIP; %s/%s rows completed.\n' \
    "$passed" "$failed" "$no_data" "$timed_out" "$skipped" "$tested" "${#apps[@]}" >>"$summary"
printf '\nFinished (UTC): %s. Total elapsed: %ss.\n' "$(date -u +%FT%TZ)" \
    "$(awk -v start="$started_ms" -v end="$(monotonic_ms)" 'BEGIN {printf "%.3f", (end-start)/1000}')" >>"$summary"
if ((interrupted)); then
    printf '\n**Interrupted.** This is a partial report; script exit code %s.\n' "$interrupted" >>"$summary"
fi
printf '\n'
cat "$summary"
printf '\nReport: %s/%s\n' "$build_dir" "$summary"
((interrupted == 0)) || exit "$interrupted"
((failed + no_data + timed_out == 0)) || exit 1
