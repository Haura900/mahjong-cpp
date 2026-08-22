#!/usr/bin/env python3
"""Measure S/F/B/C/D on one native candidate server binary.

The five modes intentionally differ only in the deep-search controls.  Full
search is the reference; S remains the published drill-compatible preset.
"""
import argparse
import json
import socket
import subprocess
import time
from pathlib import Path
from urllib.request import Request, urlopen

from compare_engine_versions import mpsz_tiles

ROOT = Path(__file__).resolve().parents[1]
MODES = {
    "S": {"auto_disable_deep_search": True, "adaptive_deep_search_mode": 0},
    "F": {"auto_disable_deep_search": False, "adaptive_deep_search_mode": 0},
    "B": {"auto_disable_deep_search": True, "adaptive_deep_search_mode": 1},
    "C": {"auto_disable_deep_search": True, "adaptive_deep_search_mode": 2},
    "D": {"auto_disable_deep_search": True, "adaptive_deep_search_mode": 3},
}

def free_port():
    sock = socket.socket(); sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]; sock.close(); return port

def request_for(case, mode, full_stats):
    payload = {
        "game_mode": 1, "round_wind": 27, "seat_wind": 28,
        "dora_indicators": [27], "hand": mpsz_tiles(case["hand"]), "melds": [],
        "enable_reddora": True, "enable_uradora": True,
        "enable_shanten_down": True, "enable_tegawari": True,
        "enable_riichi": True, "enable_calls": True, "enable_turn_yaku": True,
        "extra": 1, "calc_stats": True, "calc_yaku_stats": full_stats,
        "calc_shapley_stats": full_stats, "ron_rate": .7,
        "remaining_tiles": max(0, (18 - case["t_min"]) * 4),
        "enable_other_win_stop": True,
        "other_win_hazard": [.0002,.0008,.0029,.0078,.0170,.0305,.0467,.0644,
            .0823,.0975,.1108,.1212,.1276,.1312,.1323,.1309,.1170,.1170],
        "version": "0.9.13",
        **{key:value for key,value in case.items() if key not in {"name", "hand"}},
        **MODES[mode],
    }
    payload.pop("name", None)
    return payload

def maximum_difference(a, b, field):
    result = 0.0
    for left, right in zip(a.get("stats", []), b.get("stats", [])):
        if field == "yaku_stats":
            for lx, rx in zip(left.get(field, []), right.get(field, [])):
                for key in ("occurrence_prob", "shapley_score"):
                    for x, y in zip(lx.get(key, []), rx.get(key, [])):
                        result = max(result, abs(x-y))
        else:
            for x, y in zip(left.get(field, []), right.get(field, [])):
                result = max(result, abs(x-y))
    return result

def summary(response):
    turn = response["config"]["t_min"]
    stats = response["stats"]
    best = max(stats, key=lambda row: row["exp_score"][turn])
    return {"recommendation": best["tile"], "best_ev": best["exp_score"][turn],
            "best_win": best.get("win_prob", [0])[turn],
            "best_tenpai": best.get("tenpai_prob", [0])[turn]}

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", required=True, help="built nanikiru executable")
    parser.add_argument("--corpus", default="benchmark_corpus/adaptive_deep_search.json")
    parser.add_argument("--report", default="benchmark_reports/adaptive_deep_search_ablation.json")
    parser.add_argument("--case", action="append", default=[],
                        help="benchmark only this named corpus case; repeatable")
    parser.add_argument("--timeout", type=float, default=600)
    parser.add_argument("--full-stats", action="store_true",
                        help="also calculate yaku occurrence and Shapley (slower)")
    args = parser.parse_args()
    corpus = json.loads((ROOT / args.corpus).read_text(encoding="utf-8"))
    if args.case:
        corpus = [case for case in corpus if case["name"] in args.case]
        if not corpus:
            raise ValueError("--case did not match the corpus")
    port = free_port(); proc = subprocess.Popen([args.binary, str(port)],
                                                 stdout=subprocess.DEVNULL,
                                                 stderr=subprocess.DEVNULL)
    try:
        for _ in range(100):
            try: socket.create_connection(("127.0.0.1", port), .05).close(); break
            except OSError: time.sleep(.05)
        else: raise RuntimeError("engine did not start")
        rows=[]
        for case in corpus:
            responses={}; timings={}
            for mode in MODES:
                payload=request_for(case, mode, args.full_stats); started=time.perf_counter()
                body=json.dumps(payload).encode()
                with urlopen(Request(f"http://127.0.0.1:{port}/", body,
                                     {"Content-Type":"application/json"}),
                             timeout=args.timeout) as r:
                    responses[mode]=json.load(r)
                if not responses[mode].get("success"):
                    raise RuntimeError(f"{case['name']} {mode}: {responses[mode]}")
                timings[mode]=(time.perf_counter()-started)*1000
            full=responses["F"]; full_summary=summary(full); modes={}
            for mode,response in responses.items():
                info=summary(response); profile=response.get("profile", {})
                modes[mode]={**info, "runtime_ms":timings[mode],
                    "speedup_vs_full":timings["F"]/timings[mode],
                    "recommendation_match_full":info["recommendation"]==full_summary["recommendation"],
                    "max_ev_error_vs_full":maximum_difference(response, full, "exp_score"),
                    "max_win_error_vs_full":maximum_difference(response, full, "win_prob"),
                    "max_tenpai_error_vs_full":maximum_difference(response, full, "tenpai_prob"),
                    "max_yaku_or_shapley_error_vs_full":maximum_difference(response, full, "yaku_stats"),
                    "states":response.get("searched"), "edges":profile.get("edges"),
                    "graph_build_us":profile.get("graph_build_us"), "csr_build_us":profile.get("csr_build_us"),
                    "dp_us":profile.get("dp_us"),
                    "necessary_tile_calculator_calls":profile.get("necessary_tile_calculator_calls"),
                    "unnecessary_tile_calculator_calls":profile.get("unnecessary_tile_calculator_calls"),
                    "adaptive":{k:profile.get(k, 0) for k in (
                        "adaptive_shanten_down_pruned", "adaptive_tegawari_candidates",
                        "adaptive_tegawari_accepted", "adaptive_tegawari_rejected",
                        "adaptive_full_search_reentries")}}
            standard_error=modes["S"]["max_ev_error_vs_full"]
            d_error=modes["D"]["max_ev_error_vs_full"]
            modes["D"]["ev_error_reduction_vs_standard"] = (
                None if standard_error == 0 else 1 - d_error / standard_error)
            rows.append({"case":case["name"], "shanten":responses["F"]["shanten"]["all"], "modes":modes})
            print(f"{case['name']}: S {timings['S']:.1f}ms, D {timings['D']:.1f}ms, F {timings['F']:.1f}ms")
    finally:
        proc.terminate(); proc.wait(timeout=5)
    output={"full_stats":args.full_stats, "modes":MODES, "results":rows}
    (ROOT / args.report).write_text(json.dumps(output, indent=2), encoding="utf-8")

if __name__ == "__main__": main()
