# Claude Code Web --- Efficient Workflow for ZODAR

## Goal

Reduce unnecessary context and token usage while keeping Claude
effective and preserving project quality.

## 1. Keep Caveman enabled

-   Use Caveman to keep responses concise.
-   Prefer short summaries over long explanations.
-   Do not omit important technical details, test results, risks, or
    blockers.

## 2. Use focused Skills

Create and use these Skills when relevant: - **C++** --- project
conventions, architecture, performance, and safe C++23 practices. -
**Vulkan** --- Vulkan 1.3 baseline, optional Vulkan 1.4 capabilities,
feature detection, validation, and fallbacks. - **Debugging** ---
reproduce issues, inspect targeted logs, identify root causes, and
verify fixes. - **Code Review** --- review diffs for correctness,
regressions, performance, security, and maintainability.

Load a Skill only when its subject is relevant. Avoid copying the same
instructions into multiple Skills and `CLAUDE.md`.

## 3. Use Subagents selectively

Use Subagents for independent, bounded tasks such as: - Auditing a
subsystem. - Reviewing a proposed change. - Investigating a build or
test failure. - Checking architecture or performance risks.

Give each Subagent a narrow task and ask for a concise report with
evidence. Avoid parallel agents editing the same files.

## 4. Keep `CLAUDE.md` short

`CLAUDE.md` should contain only essential, always-relevant project
rules: - Read only files relevant to the current task. - Do not scan the
entire repository unless necessary. - Avoid printing full source files
and build logs. - Use targeted searches and focused tests. - Preserve
existing functionality and architecture. - Update `PROGRESS.md` and
`ARCHITECTURE.md` when relevant. - Report what changed, tests run,
results, and remaining risks.

Put detailed subsystem instructions in Skills or documentation, not in
the root `CLAUDE.md`.

## 5. Start fresh sessions between major stages

-   Use a new Claude Code Web session for each independent milestone or
    major task.
-   Before ending a session, update `PROGRESS.md` with completed work,
    current state, test results, and the next step.
-   Update `ARCHITECTURE.md` when technical decisions change.
-   Commit and push completed work before switching sessions, when
    appropriate.
-   In the new session, ask Claude to read `CLAUDE.md`, `PROGRESS.md`,
    and only the documentation/files needed for the task.
-   Use `/compact` when continuing a long task in the same session. Use
    `/clear` only when you want to discard the current conversation
    context.

## 6. Token-saving task rules

For every task: 1. Inspect only the relevant files. 2. Make the smallest
safe change. 3. Run focused tests first; run the full suite when the
change warrants it. 4. Do not repeat unchanged investigations. 5. Do not
paste huge logs; summarize them and include only relevant errors. 6. Do
not start unrelated roadmap work. 7. Do not claim a feature works unless
it was actually tested.

## Important limitations

-   These practices can reduce unnecessary context use, but they do not
    increase Claude's account usage limits.
-   Caveman, Skills, Subagents, and session resets do not make the
    underlying model more intelligent.
-   Excessive summarization can remove important details. Preserve
    decisions, constraints, test evidence, and unresolved risks.
-   Availability of plugins, hooks, Subagents, and persistent cloud
    configuration depends on the features enabled in Claude Code Web.
