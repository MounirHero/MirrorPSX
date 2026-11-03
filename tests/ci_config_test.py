"""Check CI configuration and exercise its preflight without real credentials."""
import os
import importlib.util
import io
import json
import subprocess
import sys
import unittest
from unittest.mock import patch
from urllib.error import HTTPError
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parents[1]
WORKFLOW = yaml.load((ROOT / ".github/workflows/ps5-release.yml").read_text(), Loader=yaml.BaseLoader)
JOB = WORKFLOW["jobs"]["build"]
SPEC = importlib.util.spec_from_file_location("resolve_release", ROOT / "tools/resolve-release.py")
IDENTITY = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(IDENTITY)


class CIConfigTests(unittest.TestCase):
    def test_only_trusted_release_builds(self):
        self.assertEqual(set(WORKFLOW["on"]), {"workflow_dispatch", "push", "schedule"})
        preflight = WORKFLOW["jobs"]["preflight"]
        self.assertIn("default_branch", preflight["if"])
        self.assertIn('git merge-base --is-ancestor', preflight["steps"][-1]["run"])
        self.assertEqual(JOB["environment"], {"name": "main", "deployment": "false"})
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
        self.assertEqual(upload["with"]["path"], "dist/ps5-v${{ needs.preflight.outputs.native_version }}/")
        self.assertFalse(any("cache@" in s.get("uses", "") for s in JOB["steps"]))

    def test_native_release_requires_verification(self):
        self.assertEqual(JOB["needs"], ["preflight", "verify"])
        verify = yaml.load((ROOT / ".github/workflows/verify.yml").read_text(encoding="utf-8"), Loader=yaml.BaseLoader)
        self.assertIn("pull_request", verify["on"])
        self.assertIn("workflow_call", verify["on"])
        self.assertEqual(verify["permissions"], {"contents": "read"})
        self.assertNotIn("secrets.", (ROOT / ".github/workflows/verify.yml").read_text(encoding="utf-8"))

    def test_memdbg_style_release_controls(self):
        inputs = WORKFLOW["on"]["workflow_dispatch"]["inputs"]
        self.assertEqual(set(inputs), {"version", "nightly", "draft", "prerelease"})
        self.assertEqual(inputs["version"]["required"], "false")
        for name in ("nightly", "draft", "prerelease"):
            self.assertEqual(inputs[name]["type"], "boolean")
            self.assertEqual(inputs[name]["default"], "false")
        publish = WORKFLOW["jobs"]["publish"]
        self.assertEqual(publish["needs"], ["preflight", "build"])
        self.assertEqual(publish["environment"], {"name": "main", "deployment": "false"})
        command = publish["steps"][-1]["run"]
        self.assertIn("sha256sum --strict -c", command)
        self.assertNotIn("${{ inputs.", command)

    def publication_preflight(self, response):
        command = WORKFLOW["jobs"]["publish"]["steps"][-1]["run"]
        source = command.split("<<'PY'\n", 1)[1].split("\nPY", 1)[0]
        environment = dict(GITHUB_API_URL="https://api.github.com", GH_REPO="owner/repo",
                           GH_TOKEN="synthetic-publish-token", RELEASE_TAG="v0.1.0",
                           GITHUB_SHA="a" * 40)
        with patch.dict(os.environ, environment), patch("urllib.request.urlopen", side_effect=response):
            exec(compile(source, "publish-preflight", "exec"), {})

    def test_publication_rejects_existing_tags_and_api_failures(self):
        def missing(request, **kwargs):
            raise HTTPError(request.full_url, 404, "Not Found", {}, None)

        self.publication_preflight(missing)
        with self.assertRaises(SystemExit):
            self.publication_preflight(lambda *args, **kwargs: io.BytesIO(b'{}'))

        def tag_response(request, **kwargs):
            if '/releases/tags/' in request.full_url:
                return missing(request)
            return io.BytesIO(json.dumps({'object': {'type': 'commit', 'sha': 'b' * 40}}).encode())

        with self.assertRaisesRegex(SystemExit, 'different commit'):
            self.publication_preflight(tag_response)

        def matching_tag(request, **kwargs):
            if '/releases/tags/' in request.full_url:
                return missing(request)
            return io.BytesIO(json.dumps({'object': {'type': 'commit', 'sha': 'a' * 40}}).encode())

        self.publication_preflight(matching_tag)

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


class ReleaseIdentityTests(unittest.TestCase):
    def resolve(self, **overrides):
        args = dict(version="0.1.0", native_version="10.00", event="workflow_dispatch",
                    ref_name="main", sha="a" * 40, now="2026-11-03T21:10:00Z")
        args.update(overrides)
        return IDENTITY.resolve(**args)

    def test_manual_version_and_tag_release_resolve_same_identity(self):
        manual = self.resolve(version="v0.2.0")
        tag = self.resolve(event="push", ref_name="v0.2.0")
        self.assertEqual(manual, tag)
        self.assertEqual(tag['tag'], 'v0.2.0')
        self.assertTrue(tag['stable'])
        self.assertEqual(tag['native_version'], '10.00')

    def test_prerelease_and_draft_are_not_stable(self):
        self.assertFalse(self.resolve(version="0.2.0-rc.1")['stable'])
        self.assertFalse(self.resolve(draft=True)['stable'])
        self.assertFalse(self.resolve(prerelease=True)['stable'])
        self.assertTrue(self.resolve(version="0.2.0-rc.1", draft=True)['prerelease'])
        self.assertFalse(self.resolve(nightly=True)['prerelease'])

    def test_nightly_is_immutable_and_can_be_started_manually(self):
        value = self.resolve(nightly=True)
        self.assertEqual(value['tag'], 'nightly-20261103-gaaaaaaaaaaaa')
        self.assertFalse(value['stable'])
        self.assertTrue(value['should_run'])
        with self.assertRaises(ValueError):
            self.resolve(nightly=True, draft=True)

    def test_rome_schedule_selects_exactly_one_slot_in_winter_and_summer(self):
        for date, selected in [('2026-11-03', 21), ('2026-07-03', 20)]:
            for hour in (20, 21):
                result = self.resolve(event='schedule', schedule=f'0 {hour} * * *',
                                      now=f'{date}T{hour}:30:00Z')
                self.assertEqual(result['should_run'], hour == selected)
                self.assertTrue(result['nightly'])

    def test_delayed_schedule_and_unchanged_commit_skip(self):
        self.assertTrue(self.resolve(event='schedule', schedule='0 21 * * *',
                                    now='2026-11-04T01:00:00Z')['should_run'])
        result = self.resolve(event='schedule', schedule='0 21 * * *',
                              existing_tags=['nightly-20261102-gaaaaaaaaaaaa'])
        self.assertFalse(result['should_run'])

    def test_invalid_identity_fails_before_building(self):
        for bad in ({'version': '10.00'}, {'version': '0.1.0-01'},
                    {'version': '0.1.0\nmalicious=x'}, {'native_version': '../other'},
                    {'sha': 'bad'}, {'event': 'pull_request'},
                    {'event': 'push', 'ref_name': 'main'}):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                self.resolve(**bad)


if __name__ == "__main__":
    unittest.main()
