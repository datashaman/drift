# Issue tracker: GitHub

Issues and PRDs for this repository live as GitHub issues. Use the `gh` CLI for all operations.

## Repository

The repository is `datashaman/drift`. When commands run inside this clone, infer the repository from `git remote -v`; `gh` does this automatically.

## Conventions

- **Create an issue:** `gh issue create --title "..." --body "..."`. Use a heredoc for multi-line bodies.
- **Read an issue:** `gh issue view <number> --comments`, fetching its labels and filtering comments with `jq` when needed.
- **List issues:** `gh issue list --state open --json number,title,body,labels,comments --jq '[.[] | {number, title, body, labels: [.labels[].name], comments: [.comments[].body]}]'`, with appropriate `--label` and `--state` filters.
- **Comment on an issue:** `gh issue comment <number> --body "..."`.
- **Apply or remove labels:** `gh issue edit <number> --add-label "..."` or `gh issue edit <number> --remove-label "..."`.
- **Close an issue:** `gh issue close <number> --comment "..."`.

## Skill terminology

When a skill says **publish to the issue tracker**, create a GitHub issue.

When a skill says **fetch the relevant ticket**, run `gh issue view <number> --comments`.
