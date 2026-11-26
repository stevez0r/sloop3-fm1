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

## Writing the guide

These rules cover `docs/guide/guide.src.html`, `README.md` and anything else written for people who use SLOOP. The
reader is new to grooveboxes: they know what a beat and a note are, but not an envelope, an LFO or swing. Use US
English.

Check every claim against the firmware (`fw/main.c`, `app/`, `seq/`) before you write it. Timings, ranges, key
numbers and on-screen text must match the code.

### The actor rule

Every sentence needs a real actor, and the subject must be the thing that actually does the verb.

| The sentence describes | Write it as | Example |
|---|---|---|
| Something the reader does | An imperative | "Hold `ARP` to repeat a note in time." |
| Something SLOOP does | SLOOP, or the part that really acts, as the subject, in active voice | "SLOOP saves what's playing a few seconds after your last change." |
| What a control or setting is for | Its name, then a verb that says what it sets | "`SWG` sets how much every second step is delayed." |
| Something the reader sees or hears | A visible part as the subject | "The screen shows SAVED." "HOME blinks." |

Screens show, lights blink, SLOOP saves, a knob sets a value. A key doesn't repeat, a menu doesn't stay, a pattern
doesn't come back. When an object seems to act, find who acts and rewrite.

| Before | After |
|---|---|
| ROLL: a held key repeats in time | ROLL: Repeat a held note in time |
| Hold HOME for under a second to open the menu. It stays open when you let go. | Hold `HOME` until the menu opens. Release `HOME`; the menu stays open until you press another button. |
| LIGHTS: What the key lights show | LIGHTS: Set how bright the buttons glow and which keys light up |
| Leave one: it comes back as saved. | When you leave a pattern, SLOOP reloads its last saved version. |

### Habits to remove

These show up in drafts, especially machine-written or translated ones. Fix each one when you see it.

| Habit | Looks like | Fix |
|---|---|---|
| Objects acting on their own | "the key repeats", "the menu stays" | Apply the actor rule. |
| Description where an action belongs | "What the key lights show" | Lead with a verb: "Set…", "Choose…", "Play…". |
| Colon chains and telegraphic fragments | "Hold: a layer. Let go: back to playing." | Write the sentence: "Hold a button to open its layer. Release it to go back." |
| Context before the action | "On a page with one page, SELECT sets the tempo." | Goal or condition first, then the action, in one sentence: "To change the tempo, turn `SELECT`." |
| Orphan pronouns | "It stays open." "This saves it." | Name the thing. |
| Dropped articles and verbs | "SAVE held + key tapped" in prose | Full sentence in prose. Shorthand only in diagrams and tables. |
| Literal phrasing | "the one you left goes back to how it was" | Say it the way a person would say it out loud. |
| Explaining what instead of why | "This setting changes the setting." | Say what changes for the listener, or cut it. |

### Words

Use one word for one action, everywhere.

| Word | Means |
|---|---|
| press | push a button or key and release it; the default |
| tap | press and release quickly, when the button also does something when held |
| hold | keep pressed while you do something else |
| release | stop holding |
| turn | rotate a knob |
| select | choose an item on the screen or a track; never the `SELECT` knob without its formatting |

Name controls exactly as printed or shown, in their formatting: `<kbd>SAVE</kbd>`, `<span class="kn">SELECT</span>`,
`<span class="wk">key 5</span>`. Screen text is uppercase, as it appears: SAVED, MENU CLOSED.

### Shapes

- **Task headings:** verb and object. "Save a pattern", "Set the pattern length". Concept headings are noun
  phrases: "Tracks, patterns and songs".
- **Steps:** one action per step, the action in bold, then what happens in a new sentence. "**Press `PLAY`.** The
  demo starts." Start with the goal when the step isn't obvious: "To hear the demo, press `PLAY`."
- **Reference tables:** the control's name in the first column, then a verb phrase: "Sets the delay time", "Repeat a
  held note in time". Ranges and defaults in their own column or in brackets: "[1–128, default 16]".
- **Concepts:** one plain sentence that defines the term, then a real example. "Swing delays every second step, so
  a straight beat feels looser. Try 20 on a hi-hat pattern."
- **Notes:** Tip for a better way, Note for something to know, Warning only when work can be lost.
- Keep sentences short, but write whole sentences. Cut a sentence when the reader wouldn't miss it.
