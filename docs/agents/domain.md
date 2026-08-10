# Domain docs

This is a single-context repository. The following rules describe how engineering skills should consume its domain documentation when exploring or changing the codebase.

## Before exploring, read these

- Read `CONTEXT.md` at the repository root when it exists.
- Read the ADRs under `docs/adr/` that affect the area being changed.

If either location does not exist, proceed silently. Do not flag its absence or suggest creating it upfront. The producer skill (`/grill-with-docs`) creates these files lazily when terminology or decisions are resolved.

## Expected layout

```text
/
├── CONTEXT.md
├── docs/
│   └── adr/
└── src/
```

## Use the glossary's vocabulary

When output names a domain concept—in an issue title, refactor proposal, hypothesis, or test name—use the term defined in `CONTEXT.md`. Do not drift to synonyms that the glossary explicitly avoids.

If a needed concept is absent from the glossary, first reconsider whether the proposed language belongs to the project. If the gap is real, note it for `/grill-with-docs`.

## Flag ADR conflicts

If proposed work contradicts an existing ADR, surface that conflict explicitly rather than silently overriding the decision. For example:

> Contradicts ADR-0007 (event-sourced orders), but may be worth reopening because…
