from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

from .bytecode_asm import BytecodeAsmError, assemble_to_header, parse_key_words


def default_output_path() -> Path:
    """Return the usual firmware header path for either repo root or xisa/ cwd."""
    if Path("arm_host").is_dir():
        return Path("arm_host/bcdemo_program.h")
    if Path("../arm_host").is_dir():
        return Path("../arm_host/bcdemo_program.h")
    return Path("arm_host/bcdemo_program.h")


class XisaError(Exception):
    pass


def ensure_input_file(path: Path) -> Path:
    path = path.expanduser()
    if not path.exists():
        raise XisaError(f"input file not found: {path}")
    if not path.is_file():
        raise XisaError(f"input path is not a file: {path}")
    if path.suffix.lower() != ".bc":
        raise XisaError(f"expected a .bc file, got: {path}")
    return path


def ensure_output_path(path: Path) -> Path:
    path = path.expanduser()
    if path.exists() and path.is_dir():
        raise XisaError(f"output path is a directory, expected a file: {path}")
    path.parent.mkdir(parents=True, exist_ok=True)
    return path


def assemble_bytecode(source: Path, output: Path, *, encrypted: bool = False, key_hex: str | None = None):
    source = ensure_input_file(source)
    output = ensure_output_path(output)
    key_words = parse_key_words(key_hex)
    prog = assemble_to_header(source, output, encrypted=encrypted, key_words=key_words)
    if not output.exists():
        raise XisaError(f"assembler did not create output file: {output}")
    if output.stat().st_size == 0:
        raise XisaError(f"assembler created an empty output file: {output}")
    return prog


def cmd_assemble(args: argparse.Namespace) -> int:
    output = Path(args.output) if args.output else default_output_path()
    prog = assemble_bytecode(Path(args.source), output, encrypted=args.encrypted, key_hex=args.key_hex)
    print(f"Wrote {output}")
    print(f"instructions: {len(prog.insts)}")
    print(f"input_base: {prog.directives['input_base']}")
    print(f"input_len: {prog.directives['input_len']}")
    print(f"output_addr: {prog.directives['output_addr']}")
    if prog.directives.get("expected_output", -1) >= 0:
        print(f"expected_output: 0x{prog.directives['expected_output']:08x}")
    print(f"encoding: {'encrypted' if args.encrypted else 'plaintext'}")
    return 0


def cmd_check(args: argparse.Namespace) -> int:
    source = ensure_input_file(Path(args.source))
    with tempfile.TemporaryDirectory() as td:
        out = Path(td) / "bcdemo_program.h"
        prog = assemble_bytecode(source, out)
    print(f"OK: {source}")
    print(f"instructions: {len(prog.insts)}")
    return 0


def cmd_dump(args: argparse.Namespace) -> int:
    source = ensure_input_file(Path(args.source))
    with tempfile.TemporaryDirectory() as td:
        out = Path(td) / "bcdemo_program.h"
        assemble_bytecode(source, out, encrypted=args.encrypted, key_hex=args.key_hex)
        print(out.read_text(encoding="utf-8"), end="")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="xisa",
        description="RP2350A Bytecode Demonstrator assembler for rp2350-xisa-lab. Converts .bc source into bcdemo_program.h.",
    )
    sub = parser.add_subparsers(dest="command")

    p = sub.add_parser("assemble", help="assemble a .bc program into a C header")
    p.add_argument("source", help="input .bc file")
    p.add_argument(
        "-o",
        "--output",
        default=None,
        help=(
            "output header path; default: arm_host/bcdemo_program.h from "
            "repo root, or ../arm_host/bcdemo_program.h from xisa/"
        ),
    )
    p.add_argument(
        "--encrypted",
        action="store_true",
        help="emit encrypted bytecode storage for protected firmware builds; still assembler-only",
    )
    p.add_argument(
        "--key-hex",
        default=None,
        help="optional 128-bit demo key as 32 hex digits / four 32-bit words; defaults to firmware demo key",
    )
    p.set_defaults(func=cmd_assemble)

    p = sub.add_parser("check", help="parse and assemble a .bc file without keeping output")
    p.add_argument("source", help="input .bc file")
    p.set_defaults(func=cmd_check)

    p = sub.add_parser("dump", help="assemble a .bc file and print the generated C header")
    p.add_argument("source", help="input .bc file")
    p.add_argument("--encrypted", action="store_true", help="print encrypted bytecode storage instead of plaintext program macros")
    p.add_argument("--key-hex", default=None, help="optional 128-bit demo key as 32 hex digits / four 32-bit words")
    p.set_defaults(func=cmd_dump)

    return parser


def main(argv: list[str] | None = None) -> int:
    argv = list(sys.argv[1:] if argv is None else argv)

    # Short form: xisa program.bc -o ../arm_host/bcdemo_program.h
    if argv and argv[0] not in {"assemble", "check", "dump", "-h", "--help"}:
        argv.insert(0, "assemble")

    parser = build_parser()
    args = parser.parse_args(argv)
    if not hasattr(args, "func"):
        parser.print_help()
        return 0

    try:
        return args.func(args)
    except (XisaError, BytecodeAsmError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
