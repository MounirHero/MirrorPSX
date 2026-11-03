# Continuous integration and the private SDK

The `PS5 release artifacts` workflow imports the **private OpenProspero SDK
during the build**. It never expects the SDK to be committed to MirrorPSX or
available in a developer's sibling directory on the runner.

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

1. Check out MirrorPSX and validate the version and required configuration.
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

The workflow does not upload an SDK cache and does not publish a GitHub release,
create a tag, install anything on a PS5 or modify the private SDK repository.
After downloading and reviewing the artifact, follow [RELEASING.md](RELEASING.md)
to create a draft release.

## Running it

Open **Actions â†’ PS5 release artifacts â†’ Run workflow**, choose the default
branch and enter a native package version such as `10.00`. The result contains
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
