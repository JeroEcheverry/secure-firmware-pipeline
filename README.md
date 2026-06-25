# secure-firmware-pipeline

A DevSecOps CI/CD pipeline that builds, hardens, and **cryptographically signs** ESP32 firmware — treating the firmware binary itself as part of the software supply chain.

> **Thesis:** firmware is supply chain too. A `.bin` can carry hardcoded secrets, can be built non-reproducibly, and can be tampered with in transit before a user flashes it. This pipeline defends every one of those stages — not just the source code, but the compiled artifact that actually runs on the chip.

---

## Why this exists

Most security pipelines stop at the source code. This one goes further: it scans the source for secrets, produces a **reproducible** build, hardens its own supply chain, and ends by **signing the compiled binary** so that anyone downloading the firmware can verify it came from this pipeline, unmodified — without trusting the download channel.

The target is an ESP32 firmware (built with PlatformIO), but the pipeline pattern applies to any embedded build.

---

## Threat model

Each layer of the pipeline neutralizes a specific, concrete attack:

| Layer | Threat it prevents | Mechanism |
|-------|--------------------|-----------|
| **Secret scanning + merge gate** | A hardcoded credential (API key, private key) reaching `main` or a release | `gitleaks` scans the **full git history** (regex + entropy) on every push; a branch ruleset makes the check **required**, blocking the merge if a secret is found |
| **Reproducible build** | Non-deterministic builds — you can't prove `source → binary`, and the toolchain could change silently under you | The PlatformIO `platform` is **pinned to an exact version**; the build runs in a clean, ephemeral CI runner |
| **Pipeline supply-chain hardening** | A compromised GitHub Action (the *tj-actions* class of attack): a mutable tag gets repointed to malicious code that runs with your token | Every Action is **pinned to an immutable commit SHA**; **Dependabot** watches for new versions and opens controlled update PRs |
| **Keyless signing** | A tampered or forged firmware redistributed as "official" | The binary is signed with **Sigstore / cosign keyless** (ephemeral keys, GitHub OIDC identity, public transparency log) and published with a verification bundle |

The first two layers protect **how the firmware is produced**. The signing layer protects **how it is distributed** — the last mile, into the hands of whoever flashes it.

---

## How it works

On every push, two workflows run:

**`gitleaks.yml`** — scans the repository's full history for secrets. This check is a **required status check**: a pull request cannot be merged into `main` while it fails.

**`build.yml`** — the build-and-sign pipeline:

1. **Checkout** the code (SHA-pinned action)
2. **Set up Python** (SHA-pinned action)
3. **Install PlatformIO** (`pip install platformio`)
4. **Compile** the firmware (`pio run`) → produces `.pio/build/esp32dev/firmware.bin`
5. **Install cosign** (SHA-pinned Sigstore installer)
6. **Sign the binary** keyless (`cosign sign-blob ... --bundle firmware.bundle --yes`)
7. **Publish** both the `firmware.bin` and its `firmware.bundle` as a downloadable artifact

The job declares `permissions: id-token: write` so it can request the GitHub OIDC token that powers keyless signing.

---

## Verifying a signed firmware

Anyone can verify that a downloaded `firmware.bin` came from this pipeline and was not modified. Download the artifact (both `firmware.bin` and `firmware.bundle`), then run:

```bash
cosign verify-blob firmware.bin \
  --bundle firmware.bundle \
  --certificate-identity-regexp "^https://github.com/JeroEcheverry/secure-firmware-pipeline/.*" \
  --certificate-oidc-issuer "https://token.actions.githubusercontent.com"
```

- `--certificate-identity-regexp` checks **who** signed it — that the signature came from this repo's workflow, not someone else's.
- `--certificate-oidc-issuer` checks that the identity was vouched for by **GitHub Actions** (not some other OIDC provider).

If the binary was tampered with by even one byte, or signed by anything other than this pipeline, verification fails.

---

## Stack

- **Target:** ESP32 (`esp32dev`), Arduino framework
- **Build:** PlatformIO (platform pinned to an exact version for reproducibility)
- **CI/CD:** GitHub Actions
- **Secret scanning:** gitleaks
- **Signing:** cosign + Sigstore (keyless: Fulcio + Rekor + GitHub OIDC)
- **Dependency hygiene:** Dependabot (github-actions ecosystem)

---

## Security posture, in one line

Every external dependency is pinned to an immutable SHA, every secret is gated out before merge, every build is reproducible, and every binary is signed and publicly verifiable. The pipeline moves trust from *"trust me"* to *"verify it yourself."*
