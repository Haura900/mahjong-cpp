"""Guard the Engine A/B option manifest against request-schema drift."""
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SCHEMA = json.loads((ROOT / "data/config/request_schema.json").read_text(encoding="utf-8"))
OPTIONS = (ROOT / "docs/engine-benchmark/options.js").read_text(encoding="utf-8")

# These describe a hand/table or transport envelope, not calculator controls.
NON_CALCULATION = {"game_mode", "round_wind", "seat_wind", "dora_indicators", "hand",
                   "melds", "nuki_count", "wall", "version", "ip"}

class EngineBenchmarkOptionsTest(unittest.TestCase):
    def manifest_keys(self):
        return set(re.findall(r"key:'([a-z_]+)'", OPTIONS))

    def preset(self):
        body = re.search(r"DRILL_STANDARD_PRESET = Object\.freeze\(\{(.*?)\}\);", OPTIONS, re.S).group(1)
        return set(re.findall(r"\b([a-z_]+):", body))

    def test_every_request_calculation_option_has_metadata(self):
        missing = set(SCHEMA["properties"]) - NON_CALCULATION - self.manifest_keys()
        self.assertFalse(missing, f"register request-schema options in OPTION_METADATA: {sorted(missing)}")

    def test_standard_preset_matches_drill_full_request(self):
        # nanikiru-drill-generator docs/app.js buildSimulatorEnginePayload defaults.
        required = {"t_max", "extra", "enable_reddora", "enable_uradora",
                    "enable_shanten_down", "enable_tegawari", "auto_disable_deep_search",
                    "enable_riichi", "enable_calls", "enable_turn_yaku", "calc_stats",
                    "calc_exp_score_only", "ron_rate", "enable_other_win_stop",
                    "other_win_hazard", "calc_yaku_stats", "calc_shapley_stats", "yaku_filter",
                    "state_tag"}
        self.assertTrue(required <= self.preset(), required - self.preset())
        self.assertIn("calc_exp_score_only: false", OPTIONS)
        self.assertIn("calc_yaku_stats: true", OPTIONS)
        self.assertIn("calc_shapley_stats: true", OPTIONS)

    def test_exp_only_dependency_and_capability_are_declared(self):
        self.assertIn("incompatibleWith:['calc_exp_score_only']", OPTIONS)
        engines = json.loads((ROOT / "docs/engine-benchmark/engines.json").read_text(encoding="utf-8"))
        values = {e["id"]: e["capabilities"]["calc_exp_score_only"] for e in engines}
        self.assertEqual(values, {"v0.9.13": False, "candidate": True})
        adaptive = {e["id"]: e["capabilities"]["adaptive_deep_search_mode"] for e in engines}
        self.assertEqual(adaptive, {"v0.9.13": False, "candidate": True})

if __name__ == "__main__":
    unittest.main()
