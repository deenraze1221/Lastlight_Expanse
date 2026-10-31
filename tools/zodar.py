#!/usr/bin/env python3
"""zodar - build, test, run and capture ZODAR apps with short, agent-friendly output.

Usage (from anywhere; paths are relative to the current directory):
  zodar.py [--preset P | --build-dir DIR] build [TARGET ...]
  zodar.py [--preset P | --build-dir DIR] test [-R REGEX] [--tail N]
  zodar.py [--preset P | --build-dir DIR] run APP [--tail N] [-- APP_ARGS ...]
  zodar.py [--preset P | --build-dir DIR] shot APP [--frames N] [--out DIR] [-- APP_ARGS ...]
  zodar.py [--preset P | --build-dir DIR] bench [-- ZODARBENCH_ARGS ...]
  zodar.py report FILE.json
  zodar.py [--preset P | --build-dir DIR] import FILE... [ZodarAssetTool import options]
  zodar.py [--preset P | --build-dir DIR] assets list|check [ZodarAssetTool options]

APP: game (LastlightExpanse), viewer (ModelViewer), bench (ZodarBench), runtime
(ZodarRuntimeSmoke), or any executable name in <build>/bin.
Default preset: $ZODAR_PRESET or gpu-tests (CMakePresets.json; build dir build/<preset>).
Output: failures and summaries only; full logs go to files whose paths are printed.
Exit code: the exit code of the underlying build, test run or app (0 = success); 124 when
run/shot/bench hit --timeout (default 600 s; 1800 s for bench), e.g. an app without --frames.
Python 3.8+, standard library only.
"""

import argparse
import json
import os
import re
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APPS = {
    "game": "LastlightExpanse",
    "viewer": "ModelViewer",
    "bench": "ZodarBench",
    "runtime": "ZodarRuntimeSmoke",
    "assets": "ZodarAssetTool",
}
MAX_DIAGNOSTICS = 30


def build_dir(args):
    if args.build_dir:
        return Path(args.build_dir).resolve()
    return ROOT / "build" / args.preset


def log_dir(args):
    d = build_dir(args) / "zodar-logs"
    d.mkdir(parents=True, exist_ok=True)
    return d


def tail(text, n):
    lines = text.rstrip("\n").splitlines()
    return "\n".join(lines[-n:]) if n > 0 else ""


TIMEOUT_EXIT = 124  # like coreutils timeout


def run_logged(cmd, log_path, env=None, cwd=None, timeout=None):
    """Runs cmd, writes stdout+stderr to log_path, returns (exit code, output, seconds).
    A run longer than `timeout` seconds is killed and returns TIMEOUT_EXIT."""
    start = time.monotonic()
    try:
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=env, cwd=cwd,
                              text=True, errors="replace", timeout=timeout)
        code, out = proc.returncode, proc.stdout
    except subprocess.TimeoutExpired as e:
        out = e.output.decode("utf-8", "replace") if isinstance(e.output, bytes) else (e.output or "")
        out += f"\nzodar: killed after {timeout} s timeout\n"
        code = TIMEOUT_EXIT
    seconds = time.monotonic() - start
    Path(log_path).write_text(out, encoding="utf-8")
    return code, out, seconds


def gpu_env():
    env = dict(os.environ)
    env.setdefault("SDL_VIDEO_DRIVER", "offscreen")
    return env


def executable(args, app):
    name = APPS.get(app, app)
    exe = build_dir(args) / "bin" / (name + (".exe" if os.name == "nt" else ""))
    if not exe.exists():
        sys.exit(f"zodar: {exe} not found (run: zodar.py build {name})")
    return exe


# --- build -----------------------------------------------------------------------------

DIAGNOSTIC = re.compile(r"(error|warning)(\s*:|\s+[A-Z]+\d+\s*:)|^FAILED:|undefined reference|CMake Error", re.I)


def cmd_build(args):
    bdir = build_dir(args)
    logs = log_dir(args) if bdir.exists() else None
    if not (bdir / "CMakeCache.txt").exists():
        if args.build_dir:
            sys.exit(f"zodar: {bdir} is not configured")
        bdir.mkdir(parents=True, exist_ok=True)
        logs = log_dir(args)
        code, out, secs = run_logged(["cmake", "--preset", args.preset], logs / "configure.log", cwd=ROOT)
        if code != 0:
            print(tail(out, 20))
            print(f"configure FAILED ({secs:.0f} s), log: {logs / 'configure.log'}")
            return code
        print(f"configured {args.preset} ({secs:.0f} s)")
    cmd = ["cmake", "--build", str(bdir)]
    if args.targets:
        cmd += ["--target", *args.targets]
    code, out, secs = run_logged(cmd, logs / "build.log")
    diagnostics = [line for line in out.splitlines() if DIAGNOSTIC.search(line)]
    for line in diagnostics[:MAX_DIAGNOSTICS]:
        print(line)
    if len(diagnostics) > MAX_DIAGNOSTICS:
        print(f"... {len(diagnostics) - MAX_DIAGNOSTICS} more")
    warnings = sum(1 for line in diagnostics if re.search(r"warning", line, re.I))
    status = "ok" if code == 0 else "FAILED"
    print(f"build {status} ({secs:.0f} s, {warnings} warning lines), log: {logs / 'build.log'}")
    return code


# --- test ------------------------------------------------------------------------------

def cmd_test(args):
    bdir = build_dir(args)
    if not (bdir / "CTestTestfile.cmake").exists():
        sys.exit(f"zodar: {bdir} has no tests (run: zodar.py build)")
    logs = log_dir(args)
    junit = logs / "ctest.xml"
    junit.unlink(missing_ok=True)  # never summarize a previous run
    cmd = ["ctest", "--test-dir", str(bdir), "--output-junit", str(junit)]
    if args.R:
        cmd += ["-R", args.R]
    code, out, secs = run_logged(cmd, logs / "test.log", env=gpu_env())
    try:
        cases = ET.parse(junit).getroot().iter("testcase")
    except (ET.ParseError, FileNotFoundError):
        print(tail(out, args.tail))
        print(f"tests FAILED (no JUnit output), log: {logs / 'test.log'}")
        return code or 1
    passed, failed = 0, []
    for case in cases:
        status = case.get("status", "")
        if case.find("failure") is not None or case.find("error") is not None or status == "fail":
            output = case.findtext("system-out") or ""
            failed.append((case.get("name"), case.get("time"), output))
        elif status != "notrun" and case.find("skipped") is None:
            passed += 1
    for name, secs_case, output in failed:
        print(f"--- FAILED {name} ({secs_case} s), last {args.tail} lines:")
        print(tail(output, args.tail))
    if code != 0 and not failed:
        print(tail(out, args.tail))
    if passed + len(failed) == 0:
        print("no tests matched")
        code = code or 1
    print(f"tests: {passed} passed, {len(failed)} failed ({secs:.0f} s), log: {logs / 'test.log'}")
    return code


# --- run / shot / bench ----------------------------------------------------------------

def cmd_run(args):
    exe = executable(args, args.app)
    log = log_dir(args) / f"run_{exe.stem}.log"
    code, out, secs = run_logged([str(exe), *args.extra], log, env=gpu_env(), timeout=args.timeout)
    print(tail(out, args.tail))
    print(f"{exe.stem} exit {code} ({secs:.1f} s), log: {log}")
    return code


def cmd_shot(args):
    exe = executable(args, args.app)
    out_dir = Path(args.out).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    png, report, log = (out_dir / f"{exe.stem}{ext}" for ext in (".png", ".json", ".log"))
    for stale in (png, report):
        stale.unlink(missing_ok=True)  # never report a previous run's files
    cmd = [str(exe), "--headless", "--deterministic", "--frames", str(args.frames), "--screenshot", str(png),
           "--report", str(report), *args.extra]
    code, out, secs = run_logged(cmd, log, env=gpu_env(), timeout=args.timeout)
    if report.exists():
        summarize(report)
    else:
        print(tail(out, 15))
    if code == 0 and not (png.exists() and report.exists()):
        code = 3  # the run must leave its evidence
    print(f"{exe.stem} exit {code} ({secs:.1f} s)")
    print(f"screenshot: {png if png.exists() else 'MISSING'}")
    print(f"report: {report if report.exists() else 'MISSING'}, log: {log}")
    return code


def cmd_bench(args):
    exe = executable(args, "bench")
    logs = log_dir(args)
    output = logs / "zodarbench.json"
    extra = args.extra
    cmd = [str(exe), *extra]
    if "--output" not in extra:
        cmd += ["--output", str(output)]
    elif extra.index("--output") + 1 < len(extra):
        output = Path(extra[extra.index("--output") + 1]).resolve()
    else:
        sys.exit("zodar: --output needs a file name")
    code, out, secs = run_logged(cmd, logs / "bench.log", env=gpu_env(), timeout=args.timeout)
    if code == 0 and output.exists():
        summarize(output)
    else:
        print(tail(out, 15))
    print(f"ZodarBench exit {code} ({secs:.1f} s), report: {output}, log: {logs / 'bench.log'}")
    return code


# --- report ----------------------------------------------------------------------------

def fmt_stats(s, unit="ms"):
    if not s:
        return "n/a"
    mean = s.get("mean", s.get("avg", 0.0))
    return f"mean {mean:.2f} p50 {s.get('p50', 0):.2f} p95 {s.get('p95', 0):.2f} p99 {s.get('p99', 0):.2f} {unit}"


def top(scopes, key, n=6):
    items = sorted(scopes.items(), key=lambda kv: -(kv[1].get(key) or 0.0))[:n]
    return ", ".join(f"{name} {v.get(key, 0):.2f}" for name, v in items)


def summarize(path):
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    if data.get("schema") == "zodar.run_report":
        r, d, v, lg = data["run"], data["device"], data["validation"], data["log"]
        print(f"{data['application']}: exit {data['exitCode']}, {r['frames']} frames {r['width']}x{r['height']}"
              f"{' headless' if r['headless'] else ''}{' deterministic' if r['deterministic'] else ''}"
              f", {r['wallSeconds']:.2f} s wall, seed {r['seed']}")
        print(f"device: {d['name']} (Vulkan {d['apiVersion']}{', software' if d['softwareRasterizer'] else ''}"
              f", validation {'on' if d['validation'] else 'off'})")
        fps = data["fps"]
        print(f"fps: mean {fps['mean']:.1f} p50 {fps['p50']:.1f} p95 {fps['p95']:.1f} p99 {fps['p99']:.1f}")
        print(f"cpu frame: {fmt_stats(data.get('cpuFrameMs'))}")
        print(f"gpu frame: {fmt_stats(data.get('gpuFrameMs'))}")
        if data.get("cpuScopes"):
            print(f"cpu scopes (mean ms): {top(data['cpuScopes'], 'mean')}")
        if data.get("gpuPasses"):
            print(f"gpu passes (mean ms): {top(data['gpuPasses'], 'mean')}")
        rs, mem = data["render"], data["memory"]
        print(f"render: {rs['drawCalls']} draws, {rs['instances']} instances, {rs['culled']} culled, "
              f"{rs['triangles']} triangles; device-local peak {mem['peakDeviceLocalUsage'] / 1048576:.0f} MiB")
        print(f"validation: {v['errors']} errors, {v['warnings']} warnings; log: {lg['warnings']} warnings, "
              f"{lg['errors']} errors")
        for c in data.get("screenshots", []):
            print(f"capture frame {c['frame']}: {c['path']} {'ok' if c['ok'] else 'FAILED'}")
    elif data.get("tool") == "ZodarBench":
        print(f"ZodarBench {data['scene']} ({data['renderPath']}), {data['resolution'][0]}x{data['resolution'][1]}, "
              f"{data['measuredFrames']} measured frames, device {data['device']}")
        if data.get("softwareRasterizer"):
            print("software rasterizer: GPU times are NOT GPU performance")
        print(f"cpu frame: {fmt_stats(data.get('cpuFrameMs'))}")
        print(f"gpu frame: {fmt_stats(data.get('gpuFrameMs'))}")
        if data.get("gpuPassesMs"):
            print(f"gpu passes (avg ms): {top(data['gpuPassesMs'], 'avg')}")
        lf, v = data["lastFrame"], data["validation"]
        print(f"last frame: {lf['drawCalls']} draws, {lf['visibleObjects']} visible, {lf['triangles']} triangles; "
              f"validation {v['errors']} errors")
    else:
        sys.exit(f"zodar: {path}: unknown report format")


def cmd_report(args):
    summarize(args.file)
    return 0


# --- asset registry (ZodarAssetTool) --------------------------------------------------------

def cmd_assets(args):
    exe = executable(args, "assets")
    action = ["import"] if args.command == "import" else [args.action]
    return subprocess.run([str(exe)] + action + args.rest + args.extra).returncode


def main(argv=None):
    parser = argparse.ArgumentParser(prog="zodar.py", description=__doc__.split("\n")[0])
    parser.add_argument("--preset", default=os.environ.get("ZODAR_PRESET", "gpu-tests"))
    parser.add_argument("--build-dir", help="use this configured build tree instead of build/<preset>")
    sub = parser.add_subparsers(dest="command", required=True)

    p = sub.add_parser("build", help="configure (first time) and build; prints diagnostics only")
    p.add_argument("targets", nargs="*")
    p.set_defaults(func=cmd_build)

    p = sub.add_parser("test", help="run ctest; prints failing tests only")
    p.add_argument("-R", help="ctest regex")
    p.add_argument("--tail", type=int, default=25, help="output lines per failing test")
    p.set_defaults(func=cmd_test)

    for name, func, helptext in (("run", cmd_run, "run an app; prints the last log lines"),
                                 ("shot", cmd_shot, "headless deterministic run: PNG + run report")):
        p = sub.add_parser(name, help=helptext)
        p.add_argument("app")
        p.add_argument("--timeout", type=float, default=600, help="kill after S seconds (exit 124)")
        if name == "run":
            p.add_argument("--tail", type=int, default=15)
        else:
            p.add_argument("--frames", type=int, default=120)
            p.add_argument("--out", default="zodar-shots")
        p.set_defaults(func=func)

    p = sub.add_parser("bench", help="run ZodarBench; prints the benchmark summary")
    p.add_argument("--timeout", type=float, default=1800, help="kill after S seconds (exit 124)")
    p.set_defaults(func=cmd_bench)

    p = sub.add_parser("import", help="add/update assets in assets.json (ZodarAssetTool import)")
    p.add_argument("rest", nargs=argparse.REMAINDER, help="FILE... [--registry F] [--id ID] [--compile] ...")
    p.set_defaults(func=cmd_assets)

    p = sub.add_parser("assets", help="list or check the asset registry (ZodarAssetTool)")
    p.add_argument("action", choices=["list", "check"])
    p.add_argument("rest", nargs=argparse.REMAINDER, help="[--registry F] [--json] [--report F] [--strict]")
    p.set_defaults(func=cmd_assets)

    p = sub.add_parser("report", help="summarize a run report or ZodarBench report")
    p.add_argument("file")
    p.set_defaults(func=cmd_report)

    # Everything after the first "--" goes to the app unchanged.
    argv = list(sys.argv[1:] if argv is None else argv)
    extra = argv[argv.index("--") + 1:] if "--" in argv else []
    argv = argv[:argv.index("--")] if "--" in argv else argv
    args = parser.parse_args(argv)
    args.extra = extra
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
