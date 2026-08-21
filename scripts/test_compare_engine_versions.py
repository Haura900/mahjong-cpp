import importlib.util, unittest
from pathlib import Path
spec=importlib.util.spec_from_file_location("compare",Path(__file__).with_name("compare_engine_versions.py")); mod=importlib.util.module_from_spec(spec); spec.loader.exec_module(mod)
class CompareTest(unittest.TestCase):
 def response(self,ev=10,tile=1): return {"success":True,"config":{"t_max":1},"stats":[{"tile":tile,"exp_score":[0,ev],"win_prob":[0,.1],"tenpai_prob":[0,.2]}],"searched":2,"profile":{"edges":3}}
 def test_mpsz_and_same_request(self): self.assertEqual(mod.mpsz_tiles("147m258p369s11122z"),[0,3,6,10,13,16,20,23,26,27,27,27,28,28]); self.assertEqual(mod.make_request({"name":"x","hand":"1m"})["hand"],[0])
 def test_exact_and_tolerance(self): self.assertTrue(mod.compare(self.response(),self.response(10.000001))["exact"]); self.assertFalse(mod.compare(self.response(),self.response(11))["exact"])
 def test_malformed_response(self):
  with self.assertRaises(ValueError): mod.best({"success":False,"err_msg":"bad"})
if __name__ == "__main__": unittest.main()
