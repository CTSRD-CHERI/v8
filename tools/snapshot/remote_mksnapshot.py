#!/usr/bin/env python3
"""
Copy necessary files to run mksnapshot into a remote host, execute mksnapshot,
then copy the results back out. This script reads all the possible input and
output files:
mksnapshot, gen/snapshot.cc, gen/embedded.S, snapshot_blob.bin
Lastly, it reads the command to run on the remote.

1. SCPs mksnapshot, gen/snapshot.cc and gen/embedded.S to the remote if present.
2. Runs mksnapshot with the requested flags inside the remote.
3. SCPs snapshot_blob.bin, embedded.S and snapshot.cc back to the local directory.

This script calls external `ssh` and `scp` binaries rather than using libraries, for simplicity.
"""

import argparse
import subprocess
import sys
import os
from pathlib import Path
from typing import Iterable

def clean_and_expand_path(s: str) -> Path:
    s = s.replace('\\~', '~')
    return Path(os.path.expanduser(s))

def run(cmd: Iterable[str], check: bool = True, **kwargs) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, check=check, **kwargs)

def scp_to_remote(files: Iterable[Path], ssh_key: Path, host: str, port: int, remote_dir: str = "~") -> None:
    # ensure remote_dir exists
    mkdir_cmd = [
        "ssh", "-i", ssh_key, "-p", str(port),
        host,
        f"mkdir -p {remote_dir}"
    ]
    run(mkdir_cmd)
    for f in files:
        if f.exists():
            run([
                "scp",
                "-i", str(ssh_key),
                "-P", str(port),
                "-o", "StrictHostKeyChecking=no",
                "-o", "UserKnownHostsFile=/dev/null",
                str(f), f"{host}:{remote_dir}/"
            ])
        else:
            print(f"[WARN] File does not exist, skipping: {f}")


def scp_from_remote(files: Iterable[str], ssh_key: Path, host: str, port: int, local_dir: str) -> None:
    # ensure local_dir exists
    run(["mkdir", "-p",
         f"{local_dir}"
        ])
    for f in files:
        run([
            "scp",
            "-i", str(ssh_key),
            "-P", str(port),
            "-o", "StrictHostKeyChecking=no",
            "-o", "UserKnownHostsFile=/dev/null",
            f"{host}:{f}", f"{local_dir}/"
        ], check=False)


def ssh_run(cmd: str, ssh_key: Path, host: str, port: int, check: bool = True) -> subprocess.CompletedProcess:
    return run([
        "ssh",
        "-i", str(ssh_key),
        "-p", str(port),
        "-o", "StrictHostKeyChecking=no",
        "-o", "UserKnownHostsFile=/dev/null",
        f"{host}", cmd
    ], check=check)


# NOTE(zyj20): assume we are in root_build_dir
def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("hostname", help="Hostname or user@hostname")
    parser.add_argument("port", type=int, help="Port number to use for SSH", default=22)
    parser.add_argument("keyfile", type=clean_and_expand_path, help="Path to SSH private key")
    parser.add_argument("remote_dir", type=clean_and_expand_path, help="Path in the remote runner to store and pull artifacts",
                        default=Path("~"))
    parser.add_argument("mksnapshot", type=clean_and_expand_path, help="Path to mksnapshot executable")
    parser.add_argument("snapshot_blob", type=clean_and_expand_path, help="Path to snapshot_blob.bin")
    parser.add_argument("embedded_s", type=clean_and_expand_path, help="Path to embedded.S")
    parser.add_argument("snapshot_cc", type=clean_and_expand_path, help="Path to snapshot.cc")
    parser.add_argument("command", nargs=argparse.REMAINDER, help="Command to run on the remote host")

    args = parser.parse_args()

    for path in (args.mksnapshot, args.keyfile):
        if not path.exists():
            parser.error(f"Path does not exist: {path}")

    if not args.command:
        parser.error("Error: You must provide a command to run.")
    remote_cmd = f"cd {args.remote_dir} && " + " ".join(args.command)

    try:
        # Push files
        scp_to_remote([args.mksnapshot], args.keyfile, args.hostname, args.port, f"{args.remote_dir}/{args.mksnapshot.parent}")
        # Ensure these directories exist in remote even though we don't scp anything there
        ssh_run(f"mkdir -p {args.remote_dir}/{args.snapshot_blob.parent}", args.keyfile, args.hostname, args.port)
        ssh_run(f"mkdir -p {args.remote_dir}/{args.snapshot_cc.parent}", args.keyfile, args.hostname, args.port)
        ssh_run(f"mkdir -p {args.remote_dir}/{args.embedded_s.parent}", args.keyfile, args.hostname, args.port)
        if args.snapshot_blob.exists():
            scp_to_remote([args.snapshot_blob], args.keyfile, args.hostname, args.port, f"{args.remote_dir}/{args.snapshot_blob.parent}")
        if args.snapshot_cc.exists():
            scp_to_remote([args.snapshot_cc], args.keyfile, args.hostname, args.port, f"{args.remote_dir}/{args.snapshot_cc.parent}")
        if args.embedded_s.exists():
            scp_to_remote([args.embedded_s], args.keyfile, args.hostname, args.port, f"{args.remote_dir}/{args.embedded_s.parent}")

        # Ensure mksnapshot is executable
        ssh_run(f"chmod +x {args.remote_dir}/{args.mksnapshot}", args.keyfile, args.hostname, args.port)

        # Run mksnapshot
        ssh_run(remote_cmd, args.keyfile, args.hostname, args.port)

        # Pull back artifacts from remote
        scp_from_remote([
            f"{args.remote_dir}/{args.snapshot_blob}",
        ], args.keyfile, args.hostname, args.port, str(args.snapshot_blob.parent))
        scp_from_remote([
            f"{args.remote_dir}/{args.embedded_s}",
        ], args.keyfile, args.hostname, args.port, str(args.embedded_s.parent))
        scp_from_remote([
            f"{args.remote_dir}/{args.snapshot_cc}",
        ], args.keyfile, args.hostname, args.port, str(args.snapshot_cc.parent))

        return 0
    except Exception as e:
        print(f"[ERROR] {e}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
