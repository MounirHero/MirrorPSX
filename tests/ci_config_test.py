"""Check CI configuration and exercise its preflight without real credentials."""
import os
import subprocess
import sys
import unittest
from unittest.mock import MagicMock, patch
from urllib.error import HTTPError
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

    def test_publication_is_manual_and_opt_in(self):
        choice = WORKFLOW["on"]["workflow_dispatch"]["inputs"]["release_type"]
        self.assertEqual(choice["default"], "artifacts")
        self.assertEqual(choice["options"], ["artifacts", "draft", "prerelease", "stable"])
        publish = WORKFLOW["jobs"]["publish"]
        self.assertEqual(publish["if"], "inputs.release_type != 'artifacts'")
        self.assertEqual(publish["needs"], "build")
        self.assertEqual(publish["environment"], "main")
        command = publish["steps"][-1]["run"]
        self.assertIn("sha256sum --strict -c", command)
        self.assertNotIn("${{ inputs.", command)

    def publication_preflight(self, response):
        command = WORKFLOW["jobs"]["publish"]["steps"][-1]["run"]
        source = command.split("<<'PY'\n", 1)[1].split("\nPY", 1)[0]
        environment = dict(GITHUB_API_URL="https://api.github.com", GH_REPO="owner/repo",
                           GH_TOKEN="synthetic-publish-token", RELEASE_VERSION="10.00")
        with patch.dict(os.environ, environment), patch("urllib.request.urlopen", side_effect=response):
            exec(compile(source, "publish-preflight", "exec"), {})

    def test_publication_rejects_existing_tags_and_api_failures(self):
        def missing(request, **kwargs):
            raise HTTPError(request.full_url, 404, "Not Found", {}, None)

        self.publication_preflight(missing)
        with self.assertRaises(SystemExit):
            self.publication_preflight(lambda *args, **kwargs: MagicMock())

        def unauthorized(request, **kwargs):
            raise HTTPError(request.full_url, 401, "Unauthorized", {}, None)

        with self.assertRaises(HTTPError):
            self.publication_preflight(unauthorized)
        with self.assertRaises(TimeoutError):
            self.publication_preflight(TimeoutError("network timeout"))

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
