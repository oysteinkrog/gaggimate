# 6. Consolidate and wrap up

If you ran the build wave-by-wave with the per-leaf pipeline, the buckets
are already curated and verified — there is no separate raw-research pass
to fold in. Consolidation itself is a good fit for one final small
Workflow: fan the three artifacts (bibliography, compendium, convergence
report) out in `parallel()`, each as its own agent reading the finished
corpus (bibliography on sonnet; the COMPENDIUM synthesis on opus;
convergence report on sonnet), then leader-commit. Seven things,
in order:

1. Bibliography consolidation.
2. `COMPENDIUM.md` — synthesized cross-bucket narrative.
3. `FINAL-CONVERGENCE-REPORT.md` — what was built, deferred, open.
4. Register the qmd collection (use the template in `../templates/`
   substituting `{{TOPIC_SLUG}}`, `{{COLLECTION_NAME}}`,
   `{{EXAMPLE_QUERY}}`).
5. Add a row to the **work-domain → collection routing table** in
   `knowledge-base/CLAUDE.md` (this is the canonical routing source the
   root `CLAUDE.md` points at — give it the "when your work touches…"
   trigger terms, the `<collection>` name, and the harness link), and
   add the topic's row to the registry in
   `knowledge-base/REGISTRY.md` (its line is already in the graduation log
   in `knowledge-base/TOPICS.md`).
6. **Squash the branch into 5 structural commits** before opening the
   PR — see §7. The squash's final step upserts the new topic's row into
   the build-cost ledger `knowledge-base/BUILD-COST.json` and re-renders
   `knowledge-base/BUILD-COST.md` (always the last thing before push) so
   the new topic's token cost lands in the report without disturbing the
   numbers already recorded for older topics.
7. Optional: per-topic `/kb-<slug>` skill modelled on `/kb-clock-sync`.

Validate every commit with the existing
`knowledge-base/scripts/validate-frontmatter.py`. Treat warnings as
commit-blockers.
