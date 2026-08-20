#!/usr/bin/env python3
"""Flatten exchange-distance benchmark JSON to one row per discard/config."""

import argparse
import csv
import json
from pathlib import Path


def tile_name(tile):
    if tile < 9:
        return f"{tile + 1}m"
    if tile < 18:
        return f"{tile - 8}p"
    if tile < 27:
        return f"{tile - 17}s"
    if tile < 34:
        return f"{tile - 26}z"
    return ("0m", "0p", "0s")[tile - 34]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    data = json.loads(args.input.read_text(encoding="utf-8"))
    fields = [
        "hand", "config", "discard", "ev", "win_prob", "tenpai_prob",
        "searched", "wall_time_ms", "engine_time_ms", "graph_build_ms",
        "csr_build_ms", "dp_ms", "draw_vertices", "discard_vertices", "edges",
    ]
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, lineterminator="\n")
        writer.writeheader()
        for result in data:
            profile = result.get("profile") or {}
            common = {
                "hand": result["hand"], "config": result["config"],
                "searched": result["searched"],
                "wall_time_ms": result["wall_time_ms"],
                "engine_time_ms": result["engine_time_ms"],
                "graph_build_ms": profile.get("graph_build_us", 0) / 1000,
                "csr_build_ms": profile.get("csr_build_us", 0) / 1000,
                "dp_ms": profile.get("dp_us", 0) / 1000,
                "draw_vertices": profile.get("draw_vertices", 0),
                "discard_vertices": profile.get("discard_vertices", 0),
                "edges": profile.get("edges", 0),
            }
            for row in result["rows"]:
                writer.writerow({**common, "discard": tile_name(row["tile"]),
                                 "ev": row["ev"], "win_prob": row["win_prob"],
                                 "tenpai_prob": row["tenpai_prob"]})


if __name__ == "__main__":
    main()
