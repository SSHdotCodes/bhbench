# BHBench source submissions

This repository contains the preserved model-generated projects behind [bhbench.ssh.codes](https://bhbench.ssh.codes). Each submission is kept in its own folder so the generated source, assets, build files, tests, and existing build artifacts can be inspected as a complete project.

The files inside `submissions/` are imported from the preserved submission folders without source edits. The repository-level README, ignore file, and checksum manifest are organizational metadata added for this archive. Nested Git metadata and macOS `.DS_Store` files are not included.

## Live browser adaptations

The eleven newly imported working renderers now run live on the site, along with the existing Fable 5.1 browser port. Their WebGL2 adaptations, native lookup tables, and browser validation page are in [`browser/`](browser/README.md). Original files under `submissions/` remain unchanged.

## Current models

| Benchmark entry | Folder | Notes |
| --- | --- | --- |
| StepFun 5 Preview | [`stepfun-5-preview-high`](submissions/stepfun-5-preview-high) | OpenCode Go; high reasoning; original C++/OpenGL source; four live scenes |
| Claude Haiku 5.5 | [`claude-haiku-5.5-xhigh`](submissions/claude-haiku-5.5-xhigh) | Claude Code; xhigh reasoning; original C++/Metal render; 25 physics checks passed |
| Claude Opus 5.5 | [`claude-opus-5.5`](submissions/claude-opus-5.5) | Preserved C++/Metal source already published on the live site |
| GPT 6.1 Sol | [`gpt-6.1-sol-xhigh`](submissions/gpt-6.1-sol-xhigh) | Codex; xhigh reasoning |
| Claude Sonnet 5.5 | [`claude-sonnet-5.5-xhigh`](submissions/claude-sonnet-5.5-xhigh) | Claude Code; xhigh reasoning |
| Grok 4.7 | [`grok-4.7`](submissions/grok-4.7) | Grok Build; high reasoning |
| GPT 6 Luna | [`gpt-6-luna-xhigh`](submissions/gpt-6-luna-xhigh) | Native render unavailable; original source preserved |
| Mistral Large 4 | [`mistral-large-4`](submissions/mistral-large-4) | OpenCode; Default reasoning |
| MiMo V2.6 Flash | [`mimo-v2.6-flash`](submissions/mimo-v2.6-flash) | OpenCode; Default reasoning; local model run |
| Qwen 3.8 27B | [`qwen-3.8-27b`](submissions/qwen-3.8-27b) | OpenCode; Default reasoning; local model run |
| DeepSeek V4 Flash | [`deepseek-v4-flash`](submissions/deepseek-v4-flash) | OpenCode; Default reasoning |
| Ling 3.0 Flash | [`ling-3.0-flash`](submissions/ling-3.0-flash) | Native render unavailable; original source preserved |
| GPT 6 Astra | [`gpt-6-astra-xhigh`](submissions/gpt-6-astra-xhigh) | xhigh reasoning, generated with Codex |
| Claude Fable 5.1 | [`claude-fable-5.1`](submissions/claude-fable-5.1) | xhigh reasoning, generated with Claude Code |
| GLM 5.3 | [`glm-5.3`](submissions/glm-5.3) | max reasoning, generated with OpenCode |
| GLM 5.3 Flash | [`glm-5.3-flash`](submissions/glm-5.3-flash) | Originally submitted as ox-alpha; max reasoning, generated with OpenCode |
| Qwen 3.8 Max 0902 | [`qwen-3.8-max-0902`](submissions/qwen-3.8-max-0902) | Generated with OpenCode |
| Qwen 3.8 Flash Next (NVFP4) | [`qwen-3.8-flash-next-nvfp4`](submissions/qwen-3.8-flash-next-nvfp4) | Final standalone local generation with OpenCode |
| GPT-5.6 Terra | [`gpt-5.6-terra-xhigh`](submissions/gpt-5.6-terra-xhigh) | xhigh reasoning result |
| Muse Spark 1.3 | [`muse-spark-1.3-xhigh`](submissions/muse-spark-1.3-xhigh) | xhigh reasoning, generated with Muse Code |
| Muse Spark 1.3 Max | [`muse-spark-1.3-max`](submissions/muse-spark-1.3-max) | max reasoning, generated with Muse Code |
| Composer 2.5 | [`composer-2.5`](submissions/composer-2.5) |  |
| Tencent HY3 | [`tencent-hy3`](submissions/tencent-hy3) |  |
| Kimi K3 | [`kimi-k3`](submissions/kimi-k3) |  |
| Inkling | [`inkling`](submissions/inkling) |  |
| Gemini 3.8 Flash | [`gemini-3.8-flash`](submissions/gemini-3.8-flash) | Original C++/Metal project, generated with OpenCode |

## Old models

Superseded results are kept here with their original submission paths so existing links continue to work.

| Benchmark entry | Folder | Notes |
| --- | --- | --- |
| GPT 6 Sol | [`gpt-6-sol-xhigh`](submissions/gpt-6-sol-xhigh) | Codex; xhigh reasoning |
| Qwen 3.7 Flash | [`qwen-3.7-flash`](submissions/qwen-3.7-flash) | OpenCode; Default reasoning |
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
| GPT-5.6 Luna | [`gpt-5.6-luna-xhigh`](submissions/gpt-5.6-luna-xhigh) | xhigh reasoning result |
| Muse Spark 1.2 | [`muse-spark-1.2`](submissions/muse-spark-1.2) |  |
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


## October 6, 2026 refresh

Added 13 distinct model submissions and the missing Claude Opus 5.5 source. Separate quantization and harness experiments of existing models are not additional entries. New imports preserve source, assets, tests and build instructions; generated CMake build directories and nested Git metadata are excluded. Failed native rendering is recorded without repairing the model output. [Catalog metadata](catalog.json) records the new submissions and current/old grouping.

## October 7, 2026 addition

Added Claude Haiku 5.5 from the original Claude Code run at xhigh effort. Its complete submitted project, seven renders and existing binaries are preserved without source or shader edits. The native validation executable passed all 25 physics checks. The distinct-model catalog now has 26 current models and 17 old models.

## October 9, 2026 addition

Added StepFun 5 Preview from the original OpenCode Go run at high reasoning. All 121 assistant responses identify `step-5-preview-free`. The original project and four native captures are preserved without source edits, excluding generated CMake build files. Its submitted ray tracer, spacetime grid, lensing view and Flamm funnel run live in the browser. The distinct-model catalog now contains 25 current models and 17 old models (42 total).
