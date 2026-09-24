"""Render the exact individual COM56 verifier sends from its JSON trace."""

from __future__ import annotations

import argparse
import json
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    report = json.loads(args.report.read_text(encoding="utf-8"))
    lines = [
        "# Exact individual HRT commands sent on COM56",
        "",
        f"Source: `{args.report.name}`. All {len(report['checks'])} checks passed: "
        f"{all(report['checks'].values())}. Outer CRC errors: "
        f"{report['stats']['bad_outer_crc']}.",
        "",
        "Each COMMAND line gives the exact **105-byte DICE command payload**. "
        "Each REQUEST line gives the complete **14-byte STP packet**. "
        "Do not paste a 14-byte request into the 105-byte command field. "
        "The source JSON also records each complete 120-byte command wire packet.",
        "",
    ]
    for index, step in enumerate(report["wire_steps"], 1):
        if step.get("tx") == "command":
            lines.extend([
                f"{index}. **COMMAND** phase={step['phase']} opcode="
                f"{step['opcode']} seq={step['seq']} args={step['args_hex'] or 'none'}",
                f"   DICE payload: `{step['dice_command_payload_hex'].upper()}`",
            ])
        elif step.get("tx") == "request":
            lines.extend([
                f"{index}. **REQUEST** phase={step['phase']} type={step['type']}",
                f"   Full packet: `{step['wire_hex'].upper()}`",
            ])
        else:
            lines.append(
                f"{index}. **MATCHING LRT** phase={step['phase']} opcode="
                f"{step['opcode']} seq={step['seq']} result="
                f"{step['result']} gate={int(bool(step['gate_open']))}"
            )
        lines.append("")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote {args.output}")


if __name__ == "__main__":
    main()
