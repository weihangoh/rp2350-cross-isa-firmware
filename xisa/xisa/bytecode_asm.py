from __future__ import annotations
from dataclasses import dataclass
from pathlib import Path
import re

OPCODES = {
    "NOP": 0x00,
    "LOADI": 0x01,
    "LOAD": 0x02,
    "STORE": 0x03,
    "LOADR": 0x04,
    "STORER": 0x05,
    "MOV": 0x06,
    "ADDI": 0x07,
    "ADD": 0x08,
    "XOR": 0x09,
    "AND": 0x0A,
    "SHL": 0x0B,
    "SHR": 0x0C,
    "ROL": 0x0D,
    "CMP": 0x0E,
    "JNZ": 0x0F,
    "JMP": 0x10,
    "HALT": 0x11,
}

MACROS = {
    "NOP": "BCDEMO_NOP()",
    "LOADI": "BCDEMO_LOADI({a}, {imm16})",
    "LOAD": "BCDEMO_LOAD({a}, {b})",
    "STORE": "BCDEMO_STORE({a}, {b})",
    "LOADR": "BCDEMO_LOADR({a}, {b}, {c})",
    "STORER": "BCDEMO_STORER({a}, {b}, {c})",
    "MOV": "BCDEMO_MOV({a}, {b})",
    "ADDI": "BCDEMO_ADDI({a}, {b}, {c})",
    "ADD": "BCDEMO_ADD({a}, {b}, {c})",
    "XOR": "BCDEMO_XOR({a}, {b}, {c})",
    "AND": "BCDEMO_AND({a}, {b}, {c})",
    "SHL": "BCDEMO_SHL({a}, {b}, {c})",
    "SHR": "BCDEMO_SHR({a}, {b}, {c})",
    "ROL": "BCDEMO_ROL({a}, {b}, {c})",
    "CMP": "BCDEMO_CMP({a}, {b})",
    "JNZ": "BCDEMO_JNZ({target})",
    "JMP": "BCDEMO_JMP({target})",
    "HALT": "BCDEMO_HALT()",
}


DEFAULT_DEMO_KEY_WORDS = (0x243F6A88, 0x85A308D3, 0x13198A2E, 0x03707344)


def u32(v: int) -> int:
    return v & 0xFFFFFFFF


def fnv1a_mix_u32(h: int, v: int) -> int:
    for i in range(4):
        h ^= (v >> (8 * i)) & 0xFF
        h = u32(h * 16777619)
    return h


def key_id(key_words: tuple[int, int, int, int]) -> int:
    h = 2166136261
    for w in key_words:
        h = fnv1a_mix_u32(h, w)
    return h


def xtea_encrypt_block(v0: int, v1: int, key_words: tuple[int, int, int, int]) -> tuple[int, int]:
    total = 0
    delta = 0x9E3779B9
    for _ in range(32):
        v0 = u32(v0 + ((((v1 << 4) ^ (v1 >> 5)) + v1) ^ (total + key_words[total & 3])))
        total = u32(total + delta)
        v1 = u32(v1 + ((((v0 << 4) ^ (v0 >> 5)) + v0) ^ (total + key_words[(total >> 11) & 3])))
    return v0, v1


def inst_to_bytes(ins: AssembledInst) -> bytes:
    return bytes((OPCODES[ins.op], ins.a & 0xFF, ins.b & 0xFF, ins.c & 0xFF))


def program_plain_bytes(prog: Program) -> bytes:
    return b"".join(inst_to_bytes(ins) for ins in prog.insts)


def program_image_id(prog: Program) -> int:
    h = 2166136261
    h = fnv1a_mix_u32(h, len(prog.insts))
    h = fnv1a_mix_u32(h, prog.directives["input_base"])
    h = fnv1a_mix_u32(h, prog.directives["input_len"])
    h = fnv1a_mix_u32(h, prog.directives["output_addr"])
    h = fnv1a_mix_u32(h, prog.directives.get("expected_output", -1) & 0xFFFFFFFF)
    for b in program_plain_bytes(prog):
        h ^= b
        h = u32(h * 16777619)
    return h


def instruction_keystream(pc: int, image_id: int, key_words: tuple[int, int, int, int]) -> bytes:
    v0 = 0x42434445 ^ image_id
    v1 = 0x31584953 ^ pc
    k0, _k1 = xtea_encrypt_block(v0, v1, key_words)
    return k0.to_bytes(4, "little")


def encrypted_program_bytes(prog: Program, key_words: tuple[int, int, int, int]) -> bytes:
    image_id = program_image_id(prog)
    out = bytearray()
    for pc, ins in enumerate(prog.insts):
        pt = inst_to_bytes(ins)
        ks = instruction_keystream(pc, image_id, key_words)
        out.extend((pt[i] ^ ks[i]) & 0xFF for i in range(4))
    return bytes(out)


def parse_key_words(text: str | None) -> tuple[int, int, int, int]:
    if text is None:
        return DEFAULT_DEMO_KEY_WORDS
    t = text.strip().lower().replace("0x", "").replace("_", "").replace(":", "").replace(",", "")
    if len(t) != 32 or not re.fullmatch(r"[0-9a-f]{32}", t):
        raise BytecodeAsmError("key must be 128 bits as 32 hex digits, interpreted as four 32-bit words")
    return tuple(int(t[i:i+8], 16) for i in range(0, 32, 8))  # type: ignore[return-value]

@dataclass
class SourceInst:
    op: str
    operands: list[str]
    line_no: int
    text: str

@dataclass
class AssembledInst:
    op: str
    a: int
    b: int
    c: int
    line_no: int
    text: str

@dataclass
class Program:
    source: Path
    symbols: dict[str, int]
    directives: dict[str, int]
    insts: list[AssembledInst]


class BytecodeAsmError(Exception):
    pass


def strip_comment(line: str) -> str:
    # RP2350A Bytecode Demonstrator comments start with ';' or '#'.
    for marker in (";", "#"):
        idx = line.find(marker)
        if idx >= 0:
            line = line[:idx]
    return line.strip()


def tokenize_operands(rest: str) -> list[str]:
    rest = rest.replace(",", " ")
    return [x for x in rest.split() if x]


def parse_reg(tok: str, line_no: int) -> int:
    m = re.fullmatch(r"[rR]([0-7])", tok)
    if not m:
        raise BytecodeAsmError(f"line {line_no}: expected register r0..r7, got {tok!r}")
    return int(m.group(1))


def parse_int(tok: str, symbols: dict[str, int], line_no: int) -> int:
    if tok in symbols:
        return symbols[tok]
    if tok.upper() in symbols:
        return symbols[tok.upper()]
    try:
        return int(tok, 0)
    except ValueError:
        raise BytecodeAsmError(f"line {line_no}: unknown symbol or integer {tok!r}")


def check_range(v: int, lo: int, hi: int, what: str, line_no: int) -> int:
    if not (lo <= v <= hi):
        raise BytecodeAsmError(f"line {line_no}: {what} out of range {lo}..{hi}: {v}")
    return v


def assemble_file(path: str | Path) -> Program:
    path = Path(path)
    raw = path.read_text(encoding="utf-8").splitlines()

    symbols: dict[str, int] = {}
    directives = {
        "input_base": 100,
        "input_len": 69,
        "output_addr": 10,
        "expected_output": -1,
    }
    insts_src: list[SourceInst] = []

    # Constants derived from directives; kept in sync while parsing.
    symbols.update({
        "INPUT_BASE": directives["input_base"],
        "INPUT_LEN": directives["input_len"],
        "OUTPUT_ADDR": directives["output_addr"],
    })

    for line_no, orig in enumerate(raw, 1):
        line = strip_comment(orig)
        if not line:
            continue

        # Label-only or label + instruction on one line.
        while True:
            m = re.match(r"^([A-Za-z_][A-Za-z0-9_]*):\s*(.*)$", line)
            if not m:
                break
            label, rest = m.group(1), m.group(2).strip()
            if label in symbols:
                raise BytecodeAsmError(f"line {line_no}: duplicate label/symbol {label!r}")
            symbols[label] = len(insts_src)
            symbols[label.upper()] = len(insts_src)
            line = rest
            if not line:
                break
        if not line:
            continue

        if line.startswith("."):
            parts = line.split()
            name = parts[0].lower()
            if name == ".const":
                if len(parts) != 3:
                    raise BytecodeAsmError(f"line {line_no}: .const requires NAME VALUE")
                val = parse_int(parts[2], symbols, line_no)
                symbols[parts[1]] = val
                symbols[parts[1].upper()] = val
            elif name in (".input_base", ".input_len", ".output_addr", ".expected_output"):
                if len(parts) != 2:
                    raise BytecodeAsmError(f"line {line_no}: {name} requires one value")
                key = name[1:]
                val = parse_int(parts[1], symbols, line_no)
                directives[key] = val
                if key == "input_base": symbols["INPUT_BASE"] = val
                if key == "input_len": symbols["INPUT_LEN"] = val
                if key == "output_addr": symbols["OUTPUT_ADDR"] = val
                if key == "expected_output": symbols["EXPECTED_OUTPUT"] = val
            else:
                raise BytecodeAsmError(f"line {line_no}: unknown directive {name}")
            continue

        parts = line.split(None, 1)
        op = parts[0].upper()
        if op not in OPCODES:
            raise BytecodeAsmError(f"line {line_no}: unknown opcode {op!r}")
        operands = tokenize_operands(parts[1] if len(parts) > 1 else "")
        insts_src.append(SourceInst(op, operands, line_no, orig.strip()))

    assembled: list[AssembledInst] = []
    for s in insts_src:
        op, ops, ln = s.op, s.operands, s.line_no
        a = b = c = 0
        if op in ("NOP", "HALT"):
            if ops:
                raise BytecodeAsmError(f"line {ln}: {op} takes no operands")
        elif op == "LOADI":
            if len(ops) != 2: raise BytecodeAsmError(f"line {ln}: LOADI rX, imm")
            a = parse_reg(ops[0], ln)
            imm = check_range(parse_int(ops[1], symbols, ln), 0, 65535, "imm16", ln)
            b = imm & 0xff
            c = (imm >> 8) & 0xff
        elif op in ("LOAD", "STORE"):
            if len(ops) != 2: raise BytecodeAsmError(f"line {ln}: {op} rX, addr")
            a = parse_reg(ops[0], ln)
            b = check_range(parse_int(ops[1], symbols, ln), 0, 255, "address", ln)
        elif op in ("LOADR", "STORER", "ADD", "XOR", "AND"):
            if len(ops) != 3: raise BytecodeAsmError(f"line {ln}: {op} rA, rB, rC")
            a = parse_reg(ops[0], ln); b = parse_reg(ops[1], ln); c = parse_reg(ops[2], ln)
        elif op == "MOV":
            if len(ops) != 2: raise BytecodeAsmError(f"line {ln}: MOV rD, rA")
            a = parse_reg(ops[0], ln); b = parse_reg(ops[1], ln)
        elif op in ("ADDI", "SHL", "SHR", "ROL"):
            if len(ops) != 3: raise BytecodeAsmError(f"line {ln}: {op} rD, rA, imm8")
            a = parse_reg(ops[0], ln); b = parse_reg(ops[1], ln)
            c = check_range(parse_int(ops[2], symbols, ln), 0, 255, "imm8", ln)
        elif op == "CMP":
            if len(ops) != 2: raise BytecodeAsmError(f"line {ln}: CMP rA, rB")
            a = parse_reg(ops[0], ln); b = parse_reg(ops[1], ln)
        elif op in ("JNZ", "JMP"):
            if len(ops) != 1: raise BytecodeAsmError(f"line {ln}: {op} target")
            target = check_range(parse_int(ops[0], symbols, ln), 0, 65535, "jump target", ln)
            a = target & 0xff
            b = (target >> 8) & 0xff
        else:
            raise AssertionError(op)
        assembled.append(AssembledInst(op, a, b, c, ln, s.text))


    return Program(path, symbols, directives, assembled)


def to_c_header(prog: Program, *, encrypted: bool = False, key_words: tuple[int, int, int, int] = DEFAULT_DEMO_KEY_WORDS) -> str:
    lines = []
    lines.append("#pragma once")
    lines.append("#include \"bcdemo_shared.h\"")
    lines.append("")
    lines.append(f"/* Generated from {prog.source.as_posix()} by xisa assemble. */")
    lines.append(f"#define BCDEMO_PROGRAM_SIZE {len(prog.insts)}u")
    lines.append(f"#define BCDEMO_PROGRAM_INPUT_BASE {prog.directives['input_base']}u")
    lines.append(f"#define BCDEMO_PROGRAM_INPUT_LEN {prog.directives['input_len']}u")
    lines.append(f"#define BCDEMO_PROGRAM_OUTPUT_ADDR {prog.directives['output_addr']}u")
    if prog.directives.get('expected_output', -1) >= 0:
        lines.append(f"#define BCDEMO_PROGRAM_EXPECTED_OUTPUT {prog.directives['expected_output']}u")

    if encrypted:
        enc = encrypted_program_bytes(prog, key_words)
        image_id = program_image_id(prog)
        kid = key_id(key_words)
        lines.append("#define BCDEMO_PROGRAM_ENCRYPTED 1")
        lines.append(f"#define BCDEMO_PROGRAM_IMAGE_ID 0x{image_id:08x}u")
        lines.append(f"#define BCDEMO_PROGRAM_KEY_ID 0x{kid:08x}u")
        lines.append("")
        lines.append("/* Encrypted bytecode: plaintext instructions and source comments intentionally omitted. */")
        lines.append("static const uint8_t BCDEMO_PROGRAM_ENC[BCDEMO_PROGRAM_SIZE * 4u] = {")
        for i in range(0, len(enc), 12):
            chunk = enc[i:i+12]
            vals = ", ".join(f"0x{b:02x}" for b in chunk)
            lines.append(f"    {vals},")
        lines.append("};")
        lines.append("")
        return "\n".join(lines)

    lines.append("")
    lines.append("static const bcdemo_inst_t BCDEMO_PROGRAM[BCDEMO_PROGRAM_SIZE] = {")
    for idx, ins in enumerate(prog.insts):
        op = ins.op
        if op == "LOADI":
            imm = ins.b | (ins.c << 8)
            expr = MACROS[op].format(a=ins.a, imm16=imm)
        elif op in ("JNZ", "JMP"):
            target = ins.a | (ins.b << 8)
            expr = MACROS[op].format(target=target)
        elif op in ("NOP", "HALT"):
            expr = MACROS[op]
        else:
            expr = MACROS[op].format(a=ins.a, b=ins.b, c=ins.c)
        comment = ins.text.replace("*/", "* /")
        lines.append(f"    {expr}, /* {idx:03d}: {comment} */")
    lines.append("};")
    lines.append("")
    return "\n".join(lines)


def assemble_to_header(src: str | Path, out: str | Path, *, encrypted: bool = False, key_words: tuple[int, int, int, int] = DEFAULT_DEMO_KEY_WORDS) -> Program:
    prog = assemble_file(src)
    out = Path(out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(to_c_header(prog, encrypted=encrypted, key_words=key_words), encoding="utf-8")
    return prog
