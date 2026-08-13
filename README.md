# BHBench source submissions

This repository contains the preserved model-generated projects behind [bhbench.ssh.codes](https://bhbench.ssh.codes). Each submission is kept in its own folder so the generated source, assets, build files, tests, and existing build artifacts can be inspected as a complete project.

The files inside `submissions/` are imported from the preserved submission folders without source edits. The repository-level README, ignore file, and checksum manifest are organizational metadata added for this archive. Nested Git metadata and macOS `.DS_Store` files are not included.

## Submissions

| Benchmark entry | Folder | Notes |
| --- | --- | --- |
| Claude Fable 5 | [`claude-fable-5-cpp`](submissions/claude-fable-5-cpp) | Default C++ result |
| Claude Fable 5 | [`claude-fable-5-python`](submissions/claude-fable-5-python) | Published Python variant |
| Claude Opus 5 | [`claude-opus-5`](submissions/claude-opus-5) |  |
| GPT-5.6 Sol | [`gpt-5.6-sol-xhigh`](submissions/gpt-5.6-sol-xhigh) | xhigh reasoning result |
| GPT-5.6 Sol | [`gpt-5.6-sol-ultra`](submissions/gpt-5.6-sol-ultra) | ultra reasoning result |
| GPT-5.6 Sol | [`gpt-5.6-sol-xhigh-gargantua`](submissions/gpt-5.6-sol-xhigh-gargantua) | Gargantua variant by zoomx64 |
| GLM 5.2 | [`glm-5.2`](submissions/glm-5.2) |  |
| Qwen 3.8 Max | [`qwen-3.8-max`](submissions/qwen-3.8-max) |  |
| Grok 4.6 | [`grok-4.6`](submissions/grok-4.6) | High reasoning, generated with Grok Build |
| Claude Sonnet 5 | [`claude-sonnet-5`](submissions/claude-sonnet-5) |  |
| GPT-5.6 Terra | [`gpt-5.6-terra-xhigh`](submissions/gpt-5.6-terra-xhigh) | xhigh reasoning result |
| GPT-5.6 Luna | [`gpt-5.6-luna-xhigh`](submissions/gpt-5.6-luna-xhigh) | xhigh reasoning result |
| Muse Spark 1.2 | [`muse-spark-1.2`](submissions/muse-spark-1.2) |  |
| Composer 2.5 | [`composer-2.5`](submissions/composer-2.5) |  |
| Tencent HY3 | [`tencent-hy3`](submissions/tencent-hy3) |  |
| Kimi K3 | [`kimi-k3`](submissions/kimi-k3) |  |
| Inkling | [`inkling`](submissions/inkling) |  |
| Gemini 3.7 Flash | [`gemini-3.7-flash`](submissions/gemini-3.7-flash) | Generated with OpenCode |
| Gemini 3.6 Flash | [`gemini-3.6-flash`](submissions/gemini-3.6-flash) | Previous-generation entry |
| Grok 4.5 | [`grok-4.5`](submissions/grok-4.5) | Previous-generation entry |
| Claude Opus 4.8 | [`claude-opus-4.8`](submissions/claude-opus-4.8) | Previous-generation entry |
| Muse Spark 1.1 | [`muse-spark-1.1`](submissions/muse-spark-1.1) | Previous-generation entry |
| Qwen 3.8 Max Preview | [`qwen-3.8-max-preview`](submissions/qwen-3.8-max-preview) | Previous-generation entry |
| Grok Build 0.1 | [`grok-build-0.1`](submissions/grok-build-0.1) | Previous-generation entry |
| GPT-5.5 | [`gpt-5.5-xhigh`](submissions/gpt-5.5-xhigh) | Previous-generation xhigh result |
| Gemini 3.5 Flash | [`gemini-3.5-flash`](submissions/gemini-3.5-flash) | Previous-generation failed build, preserved as submitted |

## Scope

BHBench compares one-shot black-hole simulation projects produced by different models. The interactive browser adaptations used by the live website are maintained separately; this repository is the source-submission archive and does not normalize the projects into one build system.

Build and runtime requirements differ by submission. Consult the README and build files inside a folder before running it. No repository-wide license is applied to the submitted projects.
