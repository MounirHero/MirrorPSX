"""Check project-owned Markdown encoding and local references."""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOCS = [ROOT / "README.md", ROOT / "THIRD_PARTY_NOTICES.md", *sorted((ROOT / "docs").glob("*.md"))]


class DocumentationTests(unittest.TestCase):
    def test_utf8_and_no_mojibake(self):
        markers = ("\u00c2", "\u00c3", "\u00e2\u2020", "\u00e2\u201d", "\u00e2\u20ac", "\ufffd")
        for path in DOCS:
            with self.subTest(document=path.name):
                text = path.read_text(encoding="utf-8")
                for marker in markers:
                    self.assertFalse(marker in text, f"Corrupted text in {path.name}: {ascii(marker)}")
                self.assertIsNone(re.search(r"[\x00-\x08\x0b\x0c\x0e-\x1f]", text))

    def test_fences_and_local_links(self):
        for path in DOCS:
            with self.subTest(document=path.name):
                text = path.read_text(encoding="utf-8")
                self.assertEqual(len(re.findall(r"^```", text, re.M)) % 2, 0)
                for target in re.findall(r"\]\(([^)]+)\)", text):
                    if target.startswith(("https://", "http://", "#")):
                        continue
                    relative = target.split("#", 1)[0]
                    self.assertTrue(not relative or (path.parent / relative).exists(),
                                    f"Broken link in {path.name}: {target}")


if __name__ == "__main__":
    unittest.main()
