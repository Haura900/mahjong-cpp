#!/usr/bin/env python3
"""Build two Git revisions, execute identical JSON requests, and compare outputs."""
import argparse, json, os, shutil, socket, subprocess, sys, tempfile, time
from pathlib import Path
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[1]
TOLERANCE = 1e-5

def mpsz_tiles(value):
    bases = {"m": 0, "p": 9, "s": 18, "z": 27}; result=[]; digits=""
    for char in value.replace(" ", ""):
        if char.isdigit(): digits += char
        elif char in bases:
            result += [bases[char] + (4 if x == "0" else int(x) - 1) for x in digits]; digits=""
        else: raise ValueError(f"invalid tile character: {char}")
    if digits: raise ValueError("mpsz suit is missing")
    return result

def make_request(case):
    request = {"game_mode":0,"round_wind":27,"seat_wind":28,"dora_indicators":[27],
      "hand":mpsz_tiles(case["hand"]),"melds":[],"enable_reddora":True,"enable_uradora":True,
      "enable_shanten_down":True,"enable_tegawari":True,"enable_riichi":True,"enable_calls":False,
      "enable_turn_yaku":False,"t_min":1,"t_max":4,"extra":0,"calc_stats":True,"version":"engine-ab"}
    request.update({key:value for key,value in case.items() if key not in {"name","hand"}})
    return request

def free_port():
    sock=socket.socket(); sock.bind(("127.0.0.1",0)); port=sock.getsockname()[1]; sock.close(); return port

class Engine:
    def __init__(self, revision, directory): self.revision,self.directory=revision,directory; self.proc=None
    def build(self):
        subprocess.run(["git","worktree","add","--detach",str(self.directory),self.revision],cwd=ROOT,check=True)
        build=self.directory/"build-engine-ab"; subprocess.run(["cmake","-S",str(self.directory),"-B",str(build),"-DCMAKE_BUILD_TYPE=Release","-DBUILD_TEST=OFF","-DBUILD_SAMPLES=OFF","-DBUILD_TOOLS=OFF"],check=True)
        subprocess.run(["cmake","--build",str(build),"--config","Release","--target","nanikiru","--parallel"],check=True)
        candidates=list(build.rglob("nanikiru.exe"))+list(build.rglob("nanikiru")); self.binary=candidates[0]
    def start(self):
        self.port=free_port(); self.proc=subprocess.Popen([str(self.binary),str(self.port)],cwd=self.binary.parent,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        for _ in range(100):
            if self.proc.poll() is not None: raise RuntimeError(f"engine exited: {self.revision}")
            try: socket.create_connection(("127.0.0.1",self.port),.05).close(); return
            except OSError: time.sleep(.05)
        raise TimeoutError(f"engine did not start: {self.revision}")
    def run(self, payload, timeout):
        body=json.dumps(payload).encode(); start=time.perf_counter()
        with urlopen(Request(f"http://127.0.0.1:{self.port}/",body,{"Content-Type":"application/json"}),timeout=timeout) as response: result=json.load(response)
        return result,(time.perf_counter()-start)*1000
    def close(self):
        if self.proc: self.proc.terminate(); self.proc.wait(timeout=5)

def best(response):
    if not response.get("success"): raise ValueError(response.get("err_msg","malformed engine response"))
    turn=response["config"]["t_max"]
    return max(response["stats"],key=lambda stat:stat["exp_score"][turn])

def compare(base, candidate, tolerance=TOLERANCE):
    a,b=best(base),best(candidate); turn=base["config"]["t_max"]
    fields=("exp_score","win_prob","tenpai_prob")
    deltas={field:max(abs(x-y) for sa,sb in zip(base["stats"],candidate["stats"]) for x,y in zip(sa[field],sb[field])) for field in fields}
    return {"recommendation_match":a["tile"] == b["tile"],"ev_difference":abs(a["exp_score"][turn]-b["exp_score"][turn]),"max_differences":deltas,"exact":a["tile"] == b["tile"] and all(x <= tolerance for x in deltas.values()),"states":(base.get("searched"),candidate.get("searched")),"edges":(base.get("profile",{}).get("edges"),candidate.get("profile",{}).get("edges"))}

def main():
    parser=argparse.ArgumentParser(); parser.add_argument("--base",default="engine-v0.9.13"); parser.add_argument("--candidate",default="HEAD"); parser.add_argument("--corpus",default="benchmark_corpus/smoke.json"); parser.add_argument("--timeout",type=float,default=300); parser.add_argument("--keep-worktrees",action="store_true"); parser.add_argument("--report",default="")
    args=parser.parse_args(); corpus=json.loads((ROOT/args.corpus).read_text(encoding="utf-8")); results=[]
    with tempfile.TemporaryDirectory(prefix="mahjong-engine-ab-") as temp:
        temp=Path(temp); engines=[Engine(args.base,temp/"base"),Engine(args.candidate,temp/"candidate")]
        try:
            for engine in engines: engine.build(); engine.start()
            for case in corpus:
                payload=make_request(case); old,old_ms=engines[0].run(payload,args.timeout); new,new_ms=engines[1].run(payload,args.timeout); verdict=compare(old,new); verdict.update({"case":case["name"],"base_ms":old_ms,"candidate_ms":new_ms,"speedup":old_ms/new_ms}); results.append(verdict); print(f"{case['name']}: {old_ms:.1f} ms -> {new_ms:.1f} ms ({old_ms/new_ms:.2f}x), EV Δ={verdict['ev_difference']:.3g}, rec={verdict['recommendation_match']}, states={verdict['states']}, edges={verdict['edges']}")
        finally:
            for engine in engines: engine.close()
            if not args.keep_worktrees:
                for engine in engines: subprocess.run(["git","worktree","remove","--force",str(engine.directory)],cwd=ROOT,check=False)
    report={"base":args.base,"candidate":args.candidate,"tolerance":TOLERANCE,"results":results}
    if args.report: Path(args.report).write_text(json.dumps(report,indent=2),encoding="utf-8")
    return 0 if all(row["exact"] for row in results) else 1
if __name__ == "__main__": sys.exit(main())
