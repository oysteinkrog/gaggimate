# 3. Model selection per wave

**Sonnet is the default for every subagent.** Opus is expensive at
fan-out scale; reserve it for the few lanes whose cognitive
load actually warrants the frontier. Match the model to the cognitive
load, not to "this leaf is important". Only the coordinator (the main
loop running this skill) stays on opus — it carries the planning,
synthesis, and convergence judgement.

- **Sonnet (default)** for breadth and most everything: literature
  sweeps, inventories, "list everything in domain X", per-bucket curation
  that reorganizes existing material, and the write stage of a typical
  leaf. Cheap, fast, fine for high-coverage work. This is what a leaf
  gets unless you deliberately override it.
- **Opus (override, sparingly)** for genuine depth: mathematical
  derivations, architecture synthesis, cross-cutting frameworks,
  adversarial red-teaming, anything that must detect a subtle
  inconsistency. Also the frontier-judgment lanes — the synthesis
  wave's "past-SOTA" leaves and the adversarial corpus survey — where
  the best available reasoning changes the output quality. Set it per
  leaf in `args.leaves[].model` only for the few hardest leaves in a
  wave — not the default.
- **Mix within a wave** is the norm. A literature sweep of 15 topics
  uses sonnet for ~13 and opus for the 2–3 most contested. Set the model
  per leaf in `args.leaves[].model`; do not pick "the model for wave N".

**Verify and fix stage models** (set in the template, not per leaf):

- The **fidelity** verify lens stays on **opus** — opening a source and
  confirming it actually says what the leaf claims is the subtle-
  hallucination catch and the whole point of the pipeline. This is the
  one verify lane worth opus.
- The **resolvability** and **coherence** verify lenses run on
  **sonnet** — "does this URL/SHA/file:line exist" and "is this
  internally + cross-leaf consistent" are mechanical checks.
- The **fix** stage runs on **sonnet** — the hard work (detecting the
  bad claim) already happened in verify; applying the surgical
  correction is lower-judgment.

The user pays for every opus run. When in doubt, default to
sonnet; prototype one opus leaf on the hardest topic, see the output,
then decide whether any sibling leaves actually need to be promoted.
