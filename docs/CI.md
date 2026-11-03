# Continuous integration and the private SDK

The `PS5 release` workflow imports the **private OpenProspero SDK
during the build**. It never expects the SDK to be committed to MirrorPSX or
available in a developer's sibling directory on the runner.

This is a build and GitHub Releases workflow. Both native build and publication
use `environment: {name: main, deployment: false}` to access the configured
secrets/variables without creating GitHub deployment records. No server, site
or console receives a deployment from this workflow.

## Verification workflows

Like MemDBG, MirrorPSX separates routine verification from release creation.
`Verify` runs on pushes to main, pull requests and manual dispatches, and can be
called by the native release workflow. It needs no private SDK credentials.

| Job | Checks / output |
| --- | --- |
| Tooling (Ubuntu + Windows) | Markdown encoding/links, release contracts, CI preflight and PowerShell syntax. |
| Linux host | Debug build and real decoder, receiver, UI, PIN and media tests; JUnit and failure logs. This is host validation, not a Linux distribution promise. |
| Windows preview | MinGW Debug build, bundled DLLs, real tests, JUnit/logs and a portable preview artifact with assets/licenses. |
| Verify result | A single required-check candidate that fails if any verification job fails. |

Desktop jobs run independently with explicit timeouts. MSYS2 package downloads
are cached; SDK sources/archives are not. Failed tests keep diagnostics.
Configure branch protection to require **Verify result** if desired; the workflow
does not change repository rules.

## One-time repository configuration

Create a GitHub Actions environment named `main` and configure:

| Kind | Name | Value |
| --- | --- | --- |
| Secret | `OPENPROSPERO_READ_TOKEN` | A read-only fine-grained token with Contents access to the private SDK and its private submodule repositories. |
| Secret | `MIRROR_BROKER_TOKEN` | The existing shared broker configuration value: exactly eight hexadecimal digits representing a nonzero 32-bit value. Reuse it across releases and compatible clients. |
| Variable, optional | `OPENPROSPERO_REF` | A full 40-character commit SHA to pin the SDK. Leave unset to use the latest commit of the SDK repository's default branch automatically. |
| Variable, optional | `OPENPROSPERO_REPOSITORY` | Private SDK repository in owner/name form; defaults to `OpenProspero/sdk`. |

The normal MirrorPSX `GITHUB_TOKEN` is scoped to this repository and is not a
substitute for access to another private repository. The SDK checkout uses the
dedicated secret, recursively imports submodules/LFS inputs and disables
persistent Git credentials. See the upstream
[checkout instructions for private repositories](https://github.com/actions/checkout#checkout-multiple-repos-private).
Grant the read token only the required repository access. Never paste it or
the broker value into the workflow, documentation, issues or build output.

Configure the environment's permitted branches to match the intended release
branch. The workflow also checks that dispatch came from the repository's
default branch. It has read-only repository permissions, runs only through
manual `workflow_dispatch`, and does not expose secrets to pull-request jobs.

## Build flow

1. Require the complete Verify workflow, then validate the release configuration.
2. Import OpenProspero privately into `_deps/sdk`: use the optional pinned
   `OPENPROSPERO_REF`, or automatically resolve the latest default-branch commit.
   Record the actual SHA in the workflow summary and build environment.
3. Install the Ubuntu/LLVM 19 build dependencies.
4. Fetch public source inputs at the commits recorded in
   `tools/ci-dependencies.json`: FFmpeg, libplist, UI/runtime references, Mesa
   and the platform library. Bootstrap requires a clean runner and does not
   overwrite existing checkouts.
5. Build the SDK host tools, native RADV, the shared external broker, the native
   title and its clean-room runtime. The broker receives its persistent value
   only in its build step; a random per-run broker is not generated in CI.
6. Run exporter tests and build/validate the PKG and equivalent folder ZIP.
7. Upload only `dist/ps5-vNN.NN/` as the workflow artifact. No SDK checkout,
   build tree, credential header, token file or private build receipt is uploaded.

The workflow does not upload an SDK cache, install anything on a PS5 or modify
the private SDK repository. By default it only uploads the checked release set.
The manual **release_type** choice can instead create a draft, publish a
prerelease, or publish a stable release after all jobs pass and downloaded
checksums verify. It refuses existing releases/tags and defaults to artifacts
only. Publishing uses the `main` environment and a separate job with repository
write access. See [RELEASING.md](RELEASING.md).

## Running it

Open **Actions → PS5 release → Run workflow**, choose the default branch, enter
a native package version such as `10.00` and select the intended release type.
The result contains
the PKG, folder ZIP, matching external companion, license, installation guide,
manifest and checksums.

With no `OPENPROSPERO_REF`, each run fetches the latest SDK automatically.
To reproduce a previous run, set the optional variable to its recorded SHA.
Public dependency changes go through `tools/ci-dependencies.json`. Do not regenerate the broker
value merely because a build is retried; that would invalidate existing clients.

## Validation limits

Workflow structure, configuration checks and exporter tests can run locally.
The authenticated build itself requires the configured GitHub environment and
access to the private SDK/submodules. A successful workflow still does not
verify console launch or streaming; use [CONSOLE_TESTING.md](CONSOLE_TESTING.md).

## Public dependency mirrors

The original ps5-homebrew-ui and PS5_Mesa repository URLs became inaccessible.
The lock file uses public mirrors that contain the exact same recorded commits;
compiler/runtime code revisions are unchanged. Upstream licenses and attribution
remain intact. CI authenticates the SDK separately and does not send its read
token to these public clones.
