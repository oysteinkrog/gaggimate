# 7. Squash to 5 structural commits before PR

A converged KB build produces 100–400 wave-level micro-commits — fine
for in-flight reviewability, terrible for the PR reviewer. Squash to
**5 structural commits** in this exact shape and order before pushing
the PR. The shape mirrors the logical units of the deliverable and
gives reviewers a per-stage diff they can actually read.

## The 5 commits

| # | Subject | Paths included |
|---|---|---|
| 1 | `kb(<slug>): scaffold — MASTER-PLAN, CLAUDE.md, install script` | `<topic>/MASTER-PLAN.md`, `<topic>/CLAUDE.md`, `<topic>/scripts/install-collection.sh`, **any `knowledge-base/.schemas/` enum extensions** the topic needed (e.g. new `status`, `component_class`, or `form_factor` values) |
| 2 | `kb(<slug>): content — buckets 01–0N (<n> leaf files, <m> citations)` | All curated content buckets: `<topic>/01-foundations/` through `<topic>/09-far-future/` (or whatever buckets the topic uses, excluding `10-adversarial/`) |
| 3 | `kb(<slug>): adversarial audits — <k> wave audit files (W1–W<n>)` | `<topic>/10-adversarial/` only |
| 4 | `kb(<slug>): consolidation — COMPENDIUM, BIBLIOGRAPHY, FINAL-CONVERGENCE-REPORT` | The three Wave-13 consolidation artifacts at `<topic>/` root |
| 5 | `kb: register <slug> — REGISTRY row + graduation log + build-cost refresh` | `knowledge-base/REGISTRY.md` (the registry row) and `knowledge-base/TOPICS.md` (the graduation-log line) plus the build-cost ledger pair `knowledge-base/BUILD-COST.json` (durable source of truth, upserted for this topic) and the re-rendered `knowledge-base/BUILD-COST.md` |

Every commit body names the bead id filed during the interview.

## How to execute the squash

The cleanest path is *rebuild on top of fresh `main`*, not
`rebase -i` of the existing micro-commits — too many conflicts when
the schema, sibling topics, or shared infra moved on during the build.

```bash
# 1. Safety tag so all micro-commits remain reachable.
git tag backup-pre-squash HEAD

# 2. Mixed-reset to upstream master (working tree keeps your content).
git fetch origin
git reset --mixed origin/main

# 3. Restore everything outside <topic>/, REGISTRY.md, and TOPICS.md to upstream.
git diff --name-only HEAD \
  | rg -v "^knowledge-base/<topic>/" \
  | rg -v "^knowledge-base/REGISTRY.md$" \
  | rg -v "^knowledge-base/TOPICS.md$" \
  > /tmp/restore-list.txt
xargs git restore --source HEAD --worktree --staged < /tmp/restore-list.txt
# (this triggers the dcg guard; have the user run it manually if needed)

# 4. Restore schema extensions from the backup tag (they live OUTSIDE
#    <topic>/ but are part of the topic's scaffold commit).
git show backup-pre-squash:knowledge-base/.schemas/base.schema.json \
  > knowledge-base/.schemas/base.schema.json
git show backup-pre-squash:knowledge-base/.schemas/hardware.schema.json \
  > knowledge-base/.schemas/hardware.schema.json   # if the topic added enums

# 5. Hand-edit knowledge-base/REGISTRY.md to add the topic's registry row,
#    and knowledge-base/TOPICS.md to add its graduation-log line
#    (don't blindly restore — upstream may have added sibling topics).

# 6. Make commits 1–4 in order (see table above). Use HEREDOC for
#    multi-paragraph bodies. Stage paths explicitly per commit, never
#    `git add -A`.

# 7. Update the build-cost ledger — ALWAYS the last step before PR.
#    --topic upserts ONLY the new topic's row into the durable ledger
#    (knowledge-base/BUILD-COST.json) and re-renders BUILD-COST.md from it. Every
#    other topic's recorded numbers are preserved untouched — critical because
#    the session logs of previously-built topics may already be pruned from disk;
#    a full rescan would zero them out. Do NOT use the bare (no --topic) form
#    here — that refreshes all topics from on-disk sessions and would drop any
#    whose logs are gone.
python3 knowledge-base/scripts/kb-build-cost.py --topic <slug>
# Then make commit 5 (REGISTRY.md + TOPICS.md + BUILD-COST.json + BUILD-COST.md).

# 8. Validate before pushing.
python3 knowledge-base/scripts/validate-frontmatter.py knowledge-base/<topic>/
# Must report ok = <total>, invalid = 0.

# 9. Force-push to your fork (NOT to InitialForce).
git push --force-with-lease my <branch>
```

If you forgot a schema extension and only notice it at validation, add
it as a `--fixup=<scaffold-sha>` commit, then
`GIT_SEQUENCE_EDITOR=true git rebase -i --autosquash origin/main` to
fold it into the scaffold commit non-interactively.

## Why 5 commits, not 1 or 14

- **One mega-commit** loses the reviewer's ability to scope-check
  ("did the schemas really need to change?", "is the audit corpus
  separable for archival?"). Too coarse.
- **Per-bucket commits** (10–14) regress towards the original
  micro-commit problem and force the reviewer to jump tabs to
  understand the topic's shape.
- **5 structural commits** map 1:1 to the deliverable units a reviewer
  thinks in: setup, content, verification, synthesis, registration.
