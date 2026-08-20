#!/usr/bin/env python3
"""Benchmark exchange-distance caps and turn-yaku on the three known hands."""

import argparse
import json
from pathlib import Path

from benchmark_nanikiru_pruning import DEFAULT_SETTINGS, Engine, build_request
from run_known_pruning_ablation import HANDS, value_at


CONFIGS = [
    ("BASE", -1, False, True),
    ("D1", 1, False, True),
    ("D2", 2, False, True),
    ("D3", 3, False, True),
    ("D1+010", 1, True, True),
    ("D2+010", 2, True, True),
    ("D3+010", 3, True, True),
    ("BASE_TY_OFF", -1, False, False),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--port", type=int, default=50128)
    args = parser.parse_args()

    engine = Engine(None, args.engine, args.port)
    results = []
    try:
        for hand in HANDS:
            problem = {"hand": hand, "settings": {"turn": 6}}
            for name, distance, prune_010, turn_yaku in CONFIGS:
                settings = {**DEFAULT_SETTINGS,
                            "simulator_auto_disable_deep_search": False}
                request, turn = build_request(problem, settings,
                                              (False, prune_010, False))
                request["max_deep_exchange_distance"] = distance
                request["enable_turn_yaku"] = turn_yaku
                raw, wall_ms = engine.calculate(request)
                rows = []
                for stat in raw.get("stats", []):
                    tile = int(stat.get("tile", -1))
                    if tile < 0:
                        continue
                    rows.append({
                        "tile": tile,
                        "ev": value_at(stat.get("exp_score", []), turn),
                        "win_prob": value_at(stat.get("win_prob", []), turn),
                        "tenpai_prob": value_at(stat.get("tenpai_prob", []), turn),
                    })
                rows.sort(key=lambda row: row["ev"], reverse=True)
                result = {
                    "hand": hand,
                    "config": name,
                    "max_deep_exchange_distance": distance,
                    "prune_010": prune_010,
                    "enable_turn_yaku": turn_yaku,
                    "searched": int(raw["searched"]),
                    "engine_time_ms": float(raw.get("time", 0)) / 1000.0,
                    "wall_time_ms": wall_ms,
                    "profile": raw.get("profile"),
                    "rows": rows,
                }
                results.append(result)
                print(hand, name, result["searched"], f"{wall_ms:.1f} ms",
                      flush=True)
    finally:
        engine.close()

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(results, ensure_ascii=False, indent=2) + "\n",
                           encoding="utf-8")


if __name__ == "__main__":
    main()
