# Rules for agents working in this repo

## This is a fork. Commits and PRs go to the fork, never upstream.

This repo is `stevez0r/sloop3-fm1`, a fork of `isod89/sloop3-fm1`.

| Remote | Repo | Use |
|---|---|---|
| `origin` | `stevez0r/sloop3-fm1` | push branches, open PRs |
| `upstream` | `isod89/sloop3-fm1` | fetch only, to pull in the original author's changes |

- Push only to `origin`. Never push to `upstream`, and never re-enable its push URL.
- Open PRs against `stevez0r/sloop3-fm1` only. Always pass the repo explicitly:
  `gh pr create --repo stevez0r/sloop3-fm1 --base main`.
- Never open, comment on, or close issues or PRs on `isod89/sloop3-fm1`, and never fork-to-upstream PR. If a change
  seems worth offering to the original author, stop and ask the user.
- After opening a PR, check it landed on the fork:
  `gh pr view --json url --jq .url` must start with `https://github.com/stevez0r/sloop3-fm1/`.
- To sync with the original: `git fetch upstream && git merge upstream/main` on a branch, then PR it to the fork.

The account rules in `~/GitHub/AGENTS.md` also apply. Run `gh-whoami` before the first commit or push.

## The user guide

`docs/guide.html` is built: edit `docs/guide/guide.src.html`, then run `python3 tools/mkguide.py`. To take the
screenshots again from the firmware (after a UI change), run `tools/guide_shots.sh`. Pushing `docs/guide.html` to
`main` publishes it at https://stevez0r.github.io/sloop3-fm1/ (`.github/workflows/pages.yml`).
