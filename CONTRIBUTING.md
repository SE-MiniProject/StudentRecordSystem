# Contributing

## Branches

```
<type>/<JIRA-KEY>-<slug>      feat/SRS-14-user-login
```

`feat/` · `fix/` · `db/` · `test/` · `docs/` · `chore/`

## Commits

```
SRS-14: validate password against stored hash
```

Key first, imperative summary, body explains *why*.

## Pull requests

- Title: `SRS-14: Add login endpoint and session token issue`
- Body: story key, what changed, requirement IDs, how to verify

## Merge requires

- [ ] CI green
- [ ] One approving review (a `db/` PR needs the DB owner; `server/` or `client/` needs someone who can build it)
- [ ] Tests for the new logic; requirement IDs in the PR body
- [ ] Docs updated if behaviour, the API contract, or the schema changed

## Conventions

- You own your lane: `client/` FE · `server/` BE · `db/` DB · `docs/` shared.
- `docs/api.md` is frozen: agree changes with both client and server owners, in their own PR.