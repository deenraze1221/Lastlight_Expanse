"""Two real native clients: LAN ENet and the shared deployment's WebSocket relay.
Run from any directory: python3 scripts/test-godot-coop.py [lan|ws|all]
Requires Godot 4.6, Python 3, Node 22 and npm ci. No renderer/display required.
"""
from pathlib import Path
import os
import socket
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
GODOT = os.environ.get("GODOT_BIN", "godot")

def free_port(udp=False):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM if udp else socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]

def run(mode):
    with tempfile.TemporaryDirectory(prefix="dead-sector-coop-") as folder:
        temp = Path(folder)
        port = free_port(mode == "lan")
        children, logs, clients = [], [], []
        try:
            if mode == "ws":
                log = (temp / "server.log").open("w")
                logs.append(log)
                server = subprocess.Popen(["node", "server/index.js"], cwd=ROOT,
                    env={**os.environ, "PORT": str(port)}, stdout=log, stderr=subprocess.STDOUT)
                children.append(server)
                deadline = time.monotonic() + 5
                while time.monotonic() < deadline:
                    try:
                        with socket.create_connection(("127.0.0.1", port), timeout=.1):
                            break
                    except OSError:
                        time.sleep(.05)
                else:
                    raise RuntimeError("Relay did not listen")
            for role in ("host", "guest"):
                log = (temp / (role + ".log")).open("w")
                logs.append(log)
                env = dict(os.environ)
                for name, part in (("XDG_DATA_HOME", "data"), ("XDG_CONFIG_HOME", "config"), ("XDG_CACHE_HOME", "cache")):
                    env[name] = str(temp / role / part)
                process = subprocess.Popen([GODOT, "--headless", "--path", "godot", "--script",
                    "tests/coop_peer.gd", "--", role, mode, str(temp / "stage"), str(port)],
                    cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
                children.append(process)
                clients.append(process)
            deadline = time.monotonic() + 125
            while any(p.poll() is None for p in clients) and time.monotonic() < deadline:
                time.sleep(.2)
            for role, process in zip(("host", "guest"), clients):
                text = (temp / (role + ".log")).read_text()
                print(text, end="")
                if process.poll() != 0 or "SCRIPT ERROR" in text or "ERROR:" in text:
                    raise RuntimeError(f"{mode} {role} failed or timed out (exit {process.poll()})")
        finally:
            for process in children:
                if process.poll() is None:
                    process.terminate()
            for process in children:
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
            for log in logs:
                log.close()

if __name__ == "__main__":
    choice = sys.argv[1] if len(sys.argv) > 1 else "all"
    if choice not in ("lan", "ws", "all"):
        raise SystemExit("Choose lan, ws or all")
    for transport in ("lan", "ws") if choice == "all" else (choice,):
        run(transport)
