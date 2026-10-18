"""Turn the personal copy of the mod into the version published to Windhawk.

Adds the GitHub/homepage/license metadata and the preview GIF, clears the
personal birthday so every new cat counts from its own first day, and drops
the code that only the standalone harness uses (WH_STANDALONE).

Usage: python tools/make_public.py <input.wh.cpp> <output.wh.cpp> <preview gif url> [--keep-harness]

--keep-harness keeps the WH_STANDALONE code, for the source repository.
"""

import sys
from pathlib import Path

GITHUB = "https://github.com/viraj-rgb"
HOMEPAGE = "https://github.com/viraj-rgb/windhawk-taskbar-cat"


def replace_once(text, old, new):
    if old not in text:
        sys.exit(f"error: expected text not found: {old[:60]!r}")
    return text.replace(old, new, 1)


def strip_standalone(text):
    """Resolve #ifdef/#ifndef WH_STANDALONE blocks as if it were not defined."""
    out, stack = [], []  # stack of "keep this branch?" for WH_STANDALONE blocks
    depth = 0  # other #if blocks nested inside ours
    for line in text.split("\n"):
        stripped = line.strip()
        if stripped in ("#ifdef WH_STANDALONE", "#ifndef WH_STANDALONE"):
            stack.append(stripped.startswith("#ifndef"))
            continue
        if stack and depth == 0 and stripped == "#else":
            stack[-1] = not stack[-1]
            continue
        if stack and depth == 0 and stripped == "#endif":
            stack.pop()
            continue
        if stack and stripped.startswith("#if"):
            depth += 1
        elif stack and stripped == "#endif":
            depth -= 1
        if all(stack):
            out.append(line)
    if stack:
        sys.exit("error: unbalanced WH_STANDALONE block")
    return "\n".join(out)


def main():
    source, target, gif_url = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
    s = source.read_text(encoding="utf-8")
    s = replace_once(s, "// @author          viraj-rgb\n",
                     f"// @author          viraj-rgb\n// @github          {GITHUB}\n"
                     f"// @homepage        {HOMEPAGE}\n// @license         MIT\n")
    s = replace_once(s, "# Taskbar Cat\n", f"# Taskbar Cat\n\n![Taskbar Cat]({gif_url})\n")
    s = replace_once(s, '- birthday: "2026-10-17"', '- birthday: ""')
    s = replace_once(s, "    .birthday = cat::DaysFromCivil(2026, 10, 17),", "    .birthday = 0,")
    if "--keep-harness" in sys.argv[4:]:
        target.write_text(s, encoding="utf-8", newline="\n")
        print(f"wrote {target} (with harness code)")
        return
    s = strip_standalone(s)
    s = replace_once(s, "// The harness has no Windhawk settings store, so it uses the defaults.\n"
                        "[[maybe_unused]] Settings ReadWindhawkSettings() {",
                     "Settings ReadWindhawkSettings() {")
    if "WH_STANDALONE" in s:
        sys.exit("error: WH_STANDALONE left in the public copy")
    target.write_text(s, encoding="utf-8", newline="\n")
    print(f"wrote {target}")


if __name__ == "__main__":
    main()
