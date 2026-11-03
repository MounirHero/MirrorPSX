"""Check CI configuration and exercise its preflight without real credentials."""
import os
import subprocess
import sys
import unittest
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = yaml.load((ROOT / ".github/workflows/ps5-release.yml").read_text(), Loader=yaml.BaseLoader)
JOB = WORKFLOW["jobs"]["build"]


class CIConfigTests(unittest.TestCase):
    def test_only_trusted_manual_builds(self):
        self.assertEqual(set(WORKFLOW["on"]), {"workflow_dispatch"})
        self.assertIn("default_branch", JOB["if"])
        self.assertEqual(JOB["environment"], "main")
        self.assertEqual(WORKFLOW["permissions"], {"contents": "read"})

    def test_private_sdk_is_authenticated_and_not_persisted(self):
        step = next(s for s in JOB["steps"] if s.get("name") == "Import private OpenProspero SDK")
        self.assertEqual(step["with"]["token"], "${{ secrets.OPENPROSPERO_READ_TOKEN }}")
        self.assertEqual(step["with"]["ref"], "${{ vars.OPENPROSPERO_REF }}")
        self.assertEqual(step["with"]["submodules"], "recursive")
        self.assertEqual(step["with"]["persist-credentials"], "false")
        self.assertNotIn("MIRROR_BROKER_TOKEN", JOB["env"])

    def test_upload_is_limited_to_final_release(self):
        upload = next(s for s in JOB["steps"] if s.get("uses", "").startswith("actions/upload-artifact@"))
        self.assertEqual(upload["with"]["path"], "dist/ps5-v${{ inputs.version }}/")
        self.assertFalse(any("cache@" in s.get("uses", "") for s in JOB["steps"]))

    def test_native_release_requires_verification(self):
        self.assertEqual(JOB["needs"], "verify")
        verify = yaml.load((ROOT / ".github/workflows/verify.yml").read_text(encoding="utf-8"), Loader=yaml.BaseLoader)
        self.assertIn("pull_request", verify["on"])
        self.assertIn("workflow_call", verify["on"])
        self.assertEqual(verify["permissions"], {"contents": "read"})
        self.assertNotIn("secrets.", (ROOT / ".github/workflows/verify.yml").read_text(encoding="utf-8"))

    def test_publication_is_draft_only_and_opt_in(self):
        self.assertEqual(WORKFLOW["on"]["workflow_dispatch"]["inputs"]["draft_release"]["default"], "false")
        draft = WORKFLOW["jobs"]["draft"]
        self.assertEqual(draft["if"], "inputs.draft_release")
        self.assertEqual(draft["needs"], "build")
        command = draft["steps"][-1]["run"]
        self.assertIn("--draft --prerelease", command)
        self.assertIn("sha256sum -c", command)

    def preflight(self, **overrides):
        step = next(s for s in JOB["steps"] if s.get("name") == "Check release configuration")
        source = step["run"].split("<<'PY'\n", 1)[1].rsplit("\nPY", 1)[0]
        environment = dict(os.environ)
        environment.update(SDK_READ_TOKEN="synthetic-read-token", BROKER_TOKEN="1234abcd",
                           SDK_REF="a" * 40, RELEASE_VERSION="10.00")
        environment.update(overrides)
        return subprocess.run([sys.executable, "-c", source], env=environment, capture_output=True)

    def test_valid_preflight(self):
        self.assertEqual(self.preflight().returncode, 0)

    def test_sdk_revision_is_optional(self):
        self.assertEqual(self.preflight(SDK_REF="").returncode, 0)
        self.assertTrue(any(s.get("name") == "Record resolved SDK commit" for s in JOB["steps"]))

    def test_invalid_or_missing_credentials_and_moving_refs_are_rejected(self):
        for invalid in ({"SDK_READ_TOKEN": ""}, {"SDK_REF": "main"},
                        {"BROKER_TOKEN": "00000000"}, {"RELEASE_VERSION": "10.00; bad"}):
            with self.subTest(invalid=invalid):
                result = self.preflight(**invalid)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn(b"synthetic-read-token", result.stdout + result.stderr)
                self.assertNotIn(b"1234abcd", result.stdout + result.stderr)


if __name__ == "__main__":
    unittest.main()
