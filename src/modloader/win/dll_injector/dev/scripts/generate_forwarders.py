#!/usr/bin/env python3
"""
Export forwarder generator for an OpenGL32 proxy DLL.

Parses a function list file and produces a header with
#pragma comment(linker, "/export:...") directives that redirect calls
to the original library.

Supported input formats (auto-detected):

  Format A (with DLL path, e.g. dumpbin output):
      N/A, 79 (0x0000004f), glEnable, C:\\WINDOWS\\system32\\OPENGL32.dll, False, None

  Format B (ordinal + RVA, "additional exports" style):
      12 (0x000c),  (0x), glBindTexture, 0x000214e0, None

  Format B (first entry, with leading ordinal):
      1 (0x0001),  (0x), GlmfBeginGlsBlock, 0x0006bea0, None

In both formats the function name is the third comma-separated field.
The parser also accepts lines with trailing commas / empty fields.

Functions listed in EXCLUDE_FUNCTIONS are skipped - it is assumed you
will implement them yourself (e.g. via MinHook).
"""

import argparse
import re
import sys
from pathlib import Path

# ============================================================
#  Functions we do NOT forward - we hook them ourselves.
#  Add entries here as needed.
# ============================================================
EXCLUDE_FUNCTIONS: set[str] = {
    # "glDrawElements",
    # "glDrawArrays",
}

# Name of the original DLL to forward to.
# IMPORTANT: at runtime your proxy DLL will be loaded as OPENGL32.dll,
# so the original must be renamed, e.g. to OPENGL32_orig.dll,
# or loaded via an explicit full path with LoadLibrary.
ORIGINAL_DLL_NAME = "OPENGL32"

# Regex used to validate a candidate function name (a C identifier).
_IDENT_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")


def parse_functions_file(path: Path) -> list[str]:
    """
    Parses a function list file. Two layouts are supported out of the box:

      Format A (with DLL path):
          N/A, 79 (0x0000004f), glEnable, C:\\WINDOWS\\system32\\OPENGL32.dll, False, None

      Format B (ordinal + RVA):
          12 (0x000c),  (0x), glBindTexture, 0x000214e0, None
          1 (0x0001),  (0x), GlmfBeginGlsBlock, 0x0006bea0, None

    In both formats the function name is the third comma-separated column.
    Returns a list of unique function names in file order.
    """
    functions: list[str] = []
    seen: set[str] = set()

    with path.open("r", encoding="utf-8", errors="replace") as f:
        for raw_line in f:
            line = raw_line.strip()
            if not line:
                continue

            # Both supported formats have no embedded commas inside their
            # fields, so a plain split is sufficient and robust.
            parts = [p.strip() for p in line.split(",")]

            # Need at least 3 fields to reach the function name.
            if len(parts) < 3:
                continue

            candidate = parts[2]
            if not _IDENT_RE.match(candidate):
                # Not a valid identifier - skip header lines or garbage.
                continue

            if candidate in seen:
                continue
            seen.add(candidate)
            functions.append(candidate)

    return functions


def generate_header(
    functions: list[str],
    exclude: set[str],
    original_dll: str,
    out_path: Path,
    input_name: str,
) -> None:
    """Generates a .h file with linker forwarder directives."""

    forwarded = [fn for fn in functions if fn not in exclude]
    excluded_present = [fn for fn in functions if fn in exclude]
    excluded_missing = [fn for fn in exclude if fn not in functions]

    lines: list[str] = []
    lines.append("// ============================================================")
    lines.append("//  AUTO-GENERATED FILE - DO NOT EDIT MANUALLY!")
    lines.append(f"//  Source:          {input_name}")
    lines.append(f"//  Original DLL:    {original_dll}")
    lines.append(f"//  Total functions: {len(functions)}")
    lines.append(f"//  Forwarded:       {len(forwarded)}")
    lines.append(f"//  Excluded:        {len(excluded_present)}")
    lines.append("// ============================================================")
    lines.append("")
    lines.append("#pragma once")
    lines.append("")

    if not forwarded:
        lines.append("// No functions to forward.")
    else:
        # Sort alphabetically for readability and deterministic output.
        # MSVC linker does not care about the order.
        for fn in sorted(forwarded):
            lines.append(
                f'#pragma comment(linker, "/export:{fn}={original_dll}.{fn}")'
            )

    if excluded_present:
        lines.append("")
        lines.append("// --- Excluded (implemented manually) ---")
        for fn in sorted(excluded_present):
            lines.append(f"// {fn}")

    if excluded_missing:
        lines.append("")
        lines.append("// --- WARNING: listed in EXCLUDE but not found in the input ---")
        for fn in sorted(excluded_missing):
            lines.append(f"// ??? {fn}")

    lines.append("")

    out_path.parent.mkdir(parents=True, exist_ok=True)

    new_content = "\n".join(lines)

    # Do not touch the file if the content has not changed (helps incremental builds).
    if out_path.exists():
        try:
            if out_path.read_text(encoding="utf-8") == new_content:
                print(f"[generate_forwarders] No changes: {out_path}")
                return
        except OSError:
            pass

    out_path.write_text(new_content, encoding="utf-8")
    print(
        f"[generate_forwarders] Generated {out_path} "
        f"({len(forwarded)} forwarders, {len(excluded_present)} excluded)"
    )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Forwarder generator for OpenGL32 proxy DLL"
    )
    parser.add_argument(
        "--input",
        type=Path,
        required=True,
        help="Path to the function list file (Format A or Format B)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        required=True,
        help="Path to the output .h file",
    )
    parser.add_argument(
        "--original",
        default=ORIGINAL_DLL_NAME,
        help=f"Name of the original DLL (default: {ORIGINAL_DLL_NAME})",
    )
    parser.add_argument(
        "--exclude",
        default="",
        help="Comma-separated list of functions NOT to forward "
             "(appended to the built-in EXCLUDE_FUNCTIONS list)",
    )

    args = parser.parse_args()

    if not args.input.is_file():
        print(
            f"[generate_forwarders] ERROR: file not found: {args.input}",
            file=sys.stderr,
        )
        return 1

    exclude = set(EXCLUDE_FUNCTIONS)
    if args.exclude:
        exclude |= {f.strip() for f in args.exclude.split(",") if f.strip()}

    functions = parse_functions_file(args.input)
    if not functions:
        print(
            f"[generate_forwarders] ERROR: could not extract functions "
            f"from {args.input}",
            file=sys.stderr,
        )
        return 1

    generate_header(
        functions=functions,
        exclude=exclude,
        original_dll=args.original,
        out_path=args.output,
        input_name=args.input.name,
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())