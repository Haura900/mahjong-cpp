import base64
import gzip
import json
import tempfile
import unittest
from pathlib import Path

from benchmark_nanikiru_pruning import load_problem_corpus


class CorpusParserTest(unittest.TestCase):
    def parse(self, value):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "save.txt"
            path.write_text(value, encoding="utf-8")
            return load_problem_corpus(path)[0]

    def test_root_array(self):
        self.assertEqual(self.parse('[{"id":"a"}]')[0]["id"], "a")

    def test_problems_object(self):
        self.assertEqual(self.parse('{"problems":[{"id":"b"}]}')[0]["id"], "b")

    def test_compact_save(self):
        self.assertEqual(self.parse('{"v":6,"p":[{"id":"c"}]}')[0]["id"], "c")

    def test_local_storage_snapshot(self):
        raw = {"localStorage": {"nanikiru-problems-v1": '[{"id":"d"}]'}}
        self.assertEqual(self.parse(json.dumps(raw))[0]["id"], "d")

    def test_nk3_export(self):
        raw = gzip.compress(b'{"v":6,"p":[{"id":"e"}]}')
        text = "NK3:" + base64.urlsafe_b64encode(raw).decode().rstrip("=")
        self.assertEqual(self.parse(text)[0]["id"], "e")


if __name__ == "__main__": unittest.main()
