#!/usr/bin/env python3
"""Benchmark deep-search pruning using nanikiru problems as input only.

Saved simulator/answer fields are never used as a baseline.  Every config is
recomputed by one currently running mahjong-cpp engine build.
"""

from __future__ import annotations

import argparse
import base64
import csv
import gzip
import json
import math
import statistics
import subprocess
import sys
import time
import urllib.request
from collections import defaultdict
from pathlib import Path

CONFIGS = {
    "000": (False, False, False),
    "100": (True, False, False),
    "010": (False, True, False),
    "001": (False, False, True),
    "111": (True, True, True),
}
DEFAULT_HAZARD_PERCENT = [0.02, 0.08, 0.29, 0.78, 1.70, 3.05, 4.67, 6.44,
                          8.23, 9.75, 11.08, 12.12, 12.76, 13.12, 13.23,
                          13.09, 11.70, 11.70]
DEFAULT_SETTINGS = {
    "simulator_enable_reddora": True,
    "simulator_enable_uradora": True,
    "simulator_enable_shanten_down": True,
    "simulator_enable_tegawari": True,
    "simulator_auto_disable_deep_search": True,
    "simulator_enable_riichi": True,
    "simulator_enable_calls": True,
    "simulator_enable_other_win_stop": True,
    "simulator_tsumo_win_share_percent": 30,
    "simulator_other_win_hazard_percent": DEFAULT_HAZARD_PERCENT,
}
TILE_INDEX = {f"{rank}{suit}": offset + rank - 1
              for suit, offset in (("m", 0), ("p", 9), ("s", 18), ("z", 27))
              for rank in range(1, 8 if suit == "z" else 10)}
TILE_INDEX.update({"0m": 34, "0p": 35, "0s": 36})


def _decode_text(text: str):
    value = text.strip()
    if value.startswith("NK3:"):
        payload = value[4:].replace("-", "+").replace("_", "/")
        payload += "=" * ((4 - len(payload) % 4) % 4)
        return json.loads(gzip.decompress(base64.b64decode(payload)))
    try:
        return json.loads(value)
    except json.JSONDecodeError:
        return json.loads(base64.b64decode(value))


def load_problem_corpus(path: Path):
    data = _decode_text(path.read_text(encoding="utf-8"))
    settings = {}
    if isinstance(data, list):
        problems = data
    elif isinstance(data, dict) and isinstance(data.get("p"), list):
        problems, settings = data["p"], data.get("s") or {}
    elif isinstance(data, dict) and isinstance(data.get("problems"), list):
        problems, settings = data["problems"], data.get("settings") or {}
    elif isinstance(data, dict) and isinstance(data.get("localStorage"), dict):
        snapshot = data["localStorage"]
        raw = snapshot.get("nanikiru-problems-v1", "[]")
        parsed = json.loads(raw) if isinstance(raw, str) else raw
        problems = parsed if isinstance(parsed, list) else parsed.get("problems", [])
        raw_settings = snapshot.get("nanikiru-review-settings-v1", "{}")
        settings = json.loads(raw_settings) if isinstance(raw_settings, str) else raw_settings
    elif isinstance(data, dict) and "nanikiru-problems-v1" in data:
        raw = data["nanikiru-problems-v1"]
        parsed = json.loads(raw) if isinstance(raw, str) else raw
        problems = parsed if isinstance(parsed, list) else parsed.get("problems", [])
    else:
        raise ValueError("problem array was not found in the supplied save/export")
    if not isinstance(problems, list):
        raise ValueError("problem corpus is not an array")
    return problems, {**DEFAULT_SETTINGS, **(settings or {})}


def parse_mpsz(text: str):
    result, digits = [], []
    for char in str(text or ""):
        if char.isdigit():
            digits.append(char)
        elif char in "mpsz":
            result.extend(TILE_INDEX[f"{digit}{char}"] for digit in digits)
            digits = []
    return result


def _tile_value(value, default):
    if isinstance(value, int):
        return value
    parsed = parse_mpsz(value)
    return parsed[0] if parsed else default


def _melds(problem):
    output = []
    for meld in problem.get("melds") or []:
        tiles = meld.get("tiles") or parse_mpsz(meld.get("mpsz", ""))
        tiles = [_tile_value(tile, -1) for tile in tiles]
        if tiles and all(tile >= 0 for tile in tiles):
            output.append({"type": int(meld.get("type", 1)), "tiles": tiles})
    return output


def build_request(problem, saved_settings, flags):
    settings = problem.get("settings") or {}
    turn = min(18, max(1, int(settings.get("turn") or 6)))
    hazard = saved_settings["simulator_other_win_hazard_percent"]
    request = {
        "game_mode": int(settings.get("game_mode", 1)),
        "round_wind": _tile_value(settings.get("round_wind"), 27),
        "seat_wind": _tile_value(settings.get("seat_wind"), 28),
        "dora_indicators": [_tile_value(x, -1) for x in settings.get("dora_indicators", [])],
        "hand": parse_mpsz(problem.get("hand", "")),
        "melds": _melds(problem),
        "enable_reddora": bool(saved_settings["simulator_enable_reddora"]),
        "enable_uradora": bool(saved_settings["simulator_enable_uradora"]),
        "enable_shanten_down": bool(saved_settings["simulator_enable_shanten_down"]),
        "enable_tegawari": bool(saved_settings["simulator_enable_tegawari"]),
        "auto_disable_deep_search": bool(saved_settings["simulator_auto_disable_deep_search"]),
        "enable_riichi": bool(saved_settings["simulator_enable_riichi"]),
        "enable_calls": bool(saved_settings["simulator_enable_calls"]),
        "enable_other_win_stop": bool(saved_settings["simulator_enable_other_win_stop"]),
        "other_win_hazard": [float(x) / 100.0 for x in hazard],
        "enable_turn_yaku": True,
        "calc_stats": True,
        "calc_yaku_stats": False,
        "calc_shapley_stats": False,
        "t_min": turn,
        "ron_rate": 1.0 - float(saved_settings["simulator_tsumo_win_share_percent"]) / 100.0,
        "remaining_tiles": min(70, max(0, (18 - turn) * 4)),
        "prune_high_shanten_deep_search": flags[0],
        "prune_shanten_down_with_multiple_discards": flags[1],
        "prune_noop_tegawari": flags[2],
        "version": "0.9.13",
    }
    if any(tile < 0 for tile in request["dora_indicators"]):
        raise ValueError("invalid dora indicator")
    return request, turn


class Engine:
    def __init__(self, url, executable=None, port=50091):
        self.url = url or f"http://127.0.0.1:{port}/"
        self.process = None
        if executable:
            self.process = subprocess.Popen([str(executable), str(port)],
                                            stdout=subprocess.DEVNULL,
                                            stderr=subprocess.STDOUT)
            for _ in range(80):
                try:
                    urllib.request.urlopen(self.url, timeout=.1)
                except Exception:
                    if self.process.poll() is not None:
                        raise RuntimeError("engine process exited during startup")
                    time.sleep(.05)
                else:
                    break

    def close(self):
        if self.process:
            self.process.terminate()
            try: self.process.wait(timeout=3)
            except subprocess.TimeoutExpired: self.process.kill()

    def calculate(self, payload):
        body = json.dumps(payload, separators=(",", ":")).encode()
        request = urllib.request.Request(self.url, body, {"Content-Type": "application/json"})
        started = time.perf_counter()
        with urllib.request.urlopen(request, timeout=600) as response:
            result = json.load(response)
        wall_ms = (time.perf_counter() - started) * 1000.0
        if not result.get("success"):
            raise RuntimeError(result.get("error") or "engine calculation failed")
        return result, wall_ms


def _tile_code(index):
    if index < 9: return f"{index + 1}m"
    if index < 18: return f"{index - 8}p"
    if index < 27: return f"{index - 17}s"
    if index < 34: return f"{index - 26}z"
    return f"0{'mps'[index - 34]}"


def summarize(raw, turn, wall_ms):
    def at(values):
        return float(values[min(max(1, turn), len(values) - 1)]) if values else 0.0
    rows = [{"tile": _tile_code(int(x["tile"])), "ev": at(x.get("exp_score", []))}
            for x in raw.get("stats", []) if int(x.get("tile", -1)) >= 0]
    rows.sort(key=lambda x: x["ev"], reverse=True)
    best = rows[0]["ev"] if rows else 0.0
    tolerance = max(1e-9, abs(best) * 1e-10)
    return {"searched": int(raw["searched"]),
            "time_ms": float(raw.get("time", 0)) / 1000.0,
            "wall_time_ms": wall_ms,
            "shanten": int(raw["shanten"]["all"]), "rows": rows,
            "best_ev": best,
            "best_discards": [x["tile"] for x in rows if abs(x["ev"] - best) <= tolerance]}


def _quantile(values, q):
    if not values: return 0.0
    ordered = sorted(values); position = (len(ordered) - 1) * q
    lower, upper = math.floor(position), math.ceil(position)
    return ordered[lower] if lower == upper else ordered[lower] * (upper-position) + ordered[upper] * (position-lower)


def _aggregate(items, key):
    base = [x["runs"]["000"] for x in items]; opt = [x["runs"][key] for x in items]
    bs, os = sum(x["searched"] for x in base), sum(x["searched"] for x in opt)
    bt, ot = sum(x["time_ms"] for x in base), sum(x["time_ms"] for x in opt)
    bw, ow = sum(x["wall_time_ms"] for x in base), sum(x["wall_time_ms"] for x in opt)
    diffs = [abs(a["best_ev"]-b["best_ev"]) for a,b in zip(base,opt)]
    return {"n": len(items), "baseline_searched": bs, "searched": os,
            "reduction": (bs-os)/bs if bs else 0, "baseline_time": bt, "time": ot,
            "speedup": bt/ot if ot else 0, "baseline_wall": bw, "wall": ow,
            "match": sum(set(a["best_discards"]) == set(b["best_discards"]) for a,b in zip(base,opt))/len(items) if items else 0,
            "mae": statistics.fmean(diffs) if diffs else 0, "max_diff": max(diffs, default=0)}


def write_reports(results, out_dir):
    out_dir.mkdir(parents=True, exist_ok=True)
    detail = out_dir / "nanikiru_pruning_detail.csv"
    fields = ["problem_id","genre","hand","melds","turn","round_wind","seat_wind","dora","original_shanten",
              "baseline_searched","baseline_time_ms","baseline_best_discards","baseline_best_ev",
              "optimized_searched","optimized_time_ms","optimized_best_discards","optimized_best_ev",
              "searched_reduction","searched_reduction_percent","speedup_ratio","best_ev_diff","best_ev_abs_diff",
              "best_discard_match","baseline_top1","optimized_top1","baseline_ev_of_optimized_top1",
              "optimized_ev_of_baseline_top1","baseline_gap_between_choices","optimized_gap_between_choices",
              "historical_problem_answers","fallback_fields"]
    with detail.open("w", newline="", encoding="utf-8-sig") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields); writer.writeheader()
        for item in results:
            p,b,o = item["problem"],item["runs"]["000"],item["runs"]["111"]
            bev={x["tile"]:x["ev"] for x in b["rows"]}; oev={x["tile"]:x["ev"] for x in o["rows"]}
            b1=b["rows"][0]["tile"] if b["rows"] else ""; o1=o["rows"][0]["tile"] if o["rows"] else ""
            reduction=b["searched"]-o["searched"]
            writer.writerow({"problem_id":p.get("id",""),"genre":p.get("genre",""),"hand":p.get("hand",""),
                "melds":p.get("melds_text", ""),"turn":item["turn"],"round_wind":item["round_wind"],
                "seat_wind":item["seat_wind"],"dora":item["dora"],"original_shanten":b["shanten"],
                "baseline_searched":b["searched"],"baseline_time_ms":b["time_ms"],
                "baseline_best_discards":" ".join(b["best_discards"]),"baseline_best_ev":b["best_ev"],
                "optimized_searched":o["searched"],"optimized_time_ms":o["time_ms"],
                "optimized_best_discards":" ".join(o["best_discards"]),"optimized_best_ev":o["best_ev"],
                "searched_reduction":reduction,"searched_reduction_percent":100*reduction/b["searched"] if b["searched"] else 0,
                "speedup_ratio":b["time_ms"]/o["time_ms"] if o["time_ms"] else 0,
                "best_ev_diff":o["best_ev"]-b["best_ev"],"best_ev_abs_diff":abs(o["best_ev"]-b["best_ev"]),
                "best_discard_match":set(b["best_discards"])==set(o["best_discards"]),"baseline_top1":b1,"optimized_top1":o1,
                "baseline_ev_of_optimized_top1":bev.get(o1,""),"optimized_ev_of_baseline_top1":oev.get(b1,""),
                "baseline_gap_between_choices":b["best_ev"]-bev.get(o1,b["best_ev"]),
                "optimized_gap_between_choices":o["best_ev"]-oev.get(b1,o["best_ev"]),
                "historical_problem_answers":" ".join(p.get("answers") or ([p.get("primary_answer")] if p.get("primary_answer") else [])),
                "fallback_fields":" ".join(item["fallback_fields"])})

    overall = _aggregate(results, "111")
    diffs=[abs(x["runs"]["111"]["best_ev"]-x["runs"]["000"]["best_ev"]) for x in results]
    reductions=[(x["runs"]["000"]["searched"]-x["runs"]["111"]["searched"])/x["runs"]["000"]["searched"] if x["runs"]["000"]["searched"] else 0 for x in results]
    lines=["# Nanikiru pruning benchmark", "",
           "> BASELINE is config 000 recomputed by this same build. Saved simulator results, answers, and primary_answer were not used as correctness labels.", "",
           "## Overall", "",
           f"- Problems: {len(results)}", f"- Searched: {overall['baseline_searched']} → {overall['searched']} ({overall['reduction']:.2%} reduction)",
           f"- Engine time: {overall['baseline_time']:.1f} ms → {overall['time']:.1f} ms ({overall['speedup']:.3f}x)",
           f"- Measured wall time: {overall['baseline_wall']:.1f} ms → {overall['wall']:.1f} ms",
           f"- BASELINE vs OPTIMIZED recommendation match: {overall['match']:.2%}", f"- Best EV MAE: {overall['mae']:.6f}",
           f"- EV abs difference median/p90/p95/p99/max: {_quantile(diffs,.5):.6f} / {_quantile(diffs,.9):.6f} / {_quantile(diffs,.95):.6f} / {_quantile(diffs,.99):.6f} / {max(diffs,default=0):.6f}",
           f"- Searched reduction mean/median/p90/p95/p99/max: {statistics.fmean(reductions) if reductions else 0:.2%} / {_quantile(reductions,.5):.2%} / {_quantile(reductions,.9):.2%} / {_quantile(reductions,.95):.2%} / {_quantile(reductions,.99):.2%} / {max(reductions,default=0):.2%}",
           "", "## Ablation", "", "| config | searched | reduction | wall time ms | speedup | top1 match | EV MAE | max EV diff |", "|---|---:|---:|---:|---:|---:|---:|---:|"]
    for key in CONFIGS:
        a=_aggregate(results,key); lines.append(f"| {key} | {a['searched']} | {a['reduction']:.2%} | {a['wall']:.1f} | {a['speedup']:.3f}x | {a['match']:.2%} | {a['mae']:.6f} | {a['max_diff']:.6f} |")
    for title, group_key in (("Shanten", lambda x: "4+" if x["runs"]["000"]["shanten"] >= 4 else str(x["runs"]["000"]["shanten"])), ("Genre", lambda x: x["problem"].get("genre") or "unclassified")):
        groups=defaultdict(list)
        for x in results: groups[group_key(x)].append(x)
        lines += ["", f"## {title}", "", "| group | n | searched reduction | speedup | recommendation match | EV MAE | max EV diff |", "|---|---:|---:|---:|---:|---:|---:|"]
        for name,items in sorted(groups.items(),key=lambda y:str(y[0])):
            a=_aggregate(items,"111"); lines.append(f"| {name} | {a['n']} | {a['reduction']:.2%} | {a['speedup']:.3f}x | {a['match']:.2%} | {a['mae']:.6f} | {a['max_diff']:.6f} |")
    changed=[x for x in results if set(x["runs"]["000"]["best_discards"]) != set(x["runs"]["111"]["best_discards"])]
    lines += ["",f"## Changed recommendations ({len(changed)})",""]
    for x in changed:
        p,b,o=x["problem"],x["runs"]["000"],x["runs"]["111"]
        fmt=lambda run:", ".join(f"{r['tile']}={r['ev']:.6f}" for r in run["rows"][:3])
        lines += [f"### {p.get('id','')} — {p.get('genre','')}","",f"- Hand: `{p.get('hand','')}`; melds: `{p.get('melds_text','')}`; turn: {x['turn']}; dora: `{x['dora']}`; shanten: {b['shanten']}",f"- BASELINE top3: {fmt(b)}",f"- OPTIMIZED top3: {fmt(o)}",f"- Searched/time: {b['searched']} / {b['time_ms']:.1f} ms → {o['searched']} / {o['time_ms']:.1f} ms",""]
    ranked_diff=sorted(results,key=lambda x:abs(x["runs"]["111"]["best_ev"]-x["runs"]["000"]["best_ev"]),reverse=True)[:20]
    ranked_reduction=sorted(results,key=lambda x:(x["runs"]["000"]["searched"]-x["runs"]["111"]["searched"])/max(1,x["runs"]["000"]["searched"]),reverse=True)[:20]
    ineffective=sorted(results,key=lambda x:(x["runs"]["000"]["searched"]-x["runs"]["111"]["searched"])/max(1,x["runs"]["000"]["searched"]))[:20]
    for title,items in (("Largest EV differences",ranked_diff),("Largest searched reductions",ranked_reduction),("Least effective speedups",ineffective)):
        lines += ["",f"## {title}",""]+[f"- {x['problem'].get('id','')}: {x['problem'].get('hand','')}" for x in items]
    report=out_dir/"nanikiru_pruning_report.md"; report.write_text("\n".join(lines)+"\n",encoding="utf-8")
    return detail, report


def main(argv=None):
    parser=argparse.ArgumentParser(); parser.add_argument("export",type=Path); parser.add_argument("--engine-url")
    parser.add_argument("--engine",type=Path); parser.add_argument("--output-dir",type=Path,default=Path("benchmark_reports")); parser.add_argument("--limit",type=int)
    args=parser.parse_args(argv); problems,settings=load_problem_corpus(args.export)
    if args.limit: problems=problems[:args.limit]
    engine=Engine(args.engine_url,args.engine)
    results=[]
    try:
        for index,problem in enumerate(problems,1):
            runs={}; turn=None
            for key,flags in CONFIGS.items():
                payload,turn=build_request(problem,settings,flags)
                raw,wall_ms=engine.calculate(payload); runs[key]=summarize(raw,turn,wall_ms)
            ps=problem.get("settings") or {}; fallback=[]
            for name in ("turn","round_wind","seat_wind","dora_indicators","objective"):
                if name not in ps: fallback.append(name)
            results.append({"problem":problem,"runs":runs,"turn":turn,
                "round_wind":ps.get("round_wind","1z"),"seat_wind":ps.get("seat_wind","2z"),
                "dora":" ".join(str(x) for x in (ps.get("dora_indicators") or [])),"fallback_fields":fallback})
            print(f"[{index}/{len(problems)}] {problem.get('id','')}",file=sys.stderr)
    finally: engine.close()
    detail,report=write_reports(results,args.output_dir); print(detail); print(report)


if __name__ == "__main__": main()
