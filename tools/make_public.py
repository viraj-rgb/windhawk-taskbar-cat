"""Turn the personal copy of the mod into the version published to Windhawk.

Adds the GitHub/homepage/license metadata and the preview GIF, and clears the
personal birthday so every new cat counts from its own first day.

Usage: python tools/make_public.py <input.wh.cpp> <output.wh.cpp> <preview gif url>
"""

import sys
from pathlib import Path

GITHUB = "https://github.com/viraj-rgb"
HOMEPAGE = "https://github.com/viraj-rgb/windhawk-taskbar-cat"


def replace_once(text, old, new):
    if old not in text:
        sys.exit(f"error: expected text not found: {old[:60]!r}")
    return text.replace(old, new, 1)


def main():
    source, target, gif_url = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
    s = source.read_text(encoding="utf-8")
    s = replace_once(s, "// @author          viraj-rgb\n",
                     f"// @author          viraj-rgb\n// @github          {GITHUB}\n"
                     f"// @homepage        {HOMEPAGE}\n// @license         MIT\n")
    s = replace_once(s, "# Taskbar Cat\n", f"# Taskbar Cat\n\n![Taskbar Cat]({gif_url})\n")
    s = replace_once(s, '- birthday: "2026-10-17"', '- birthday: ""')
    s = replace_once(s, "    .birthday = cat::DaysFromCivil(2026, 10, 17),", "    .birthday = 0,")
    target.write_text(s, encoding="utf-8", newline="\n")
    print(f"wrote {target}")


if __name__ == "__main__":
    main()
