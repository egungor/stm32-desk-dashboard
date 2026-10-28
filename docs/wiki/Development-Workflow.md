# Development Workflow

## Branches and commits

- Never commit to `main`; every change goes through a pull request.
- One GitHub issue per focused change. Branch: `<type>/<issue>-<short-description>`,
  lowercase, kebab-case, e.g. `feature/32-windows-hid`, `fix/23-hid-reconnect`,
  `docs/38-status-refresh`. Types: feature, fix, refactor, test, docs,
  chore, hardware.
- Commit messages: `feat(...)`, `fix(...)`, `test(...)`, `refactor(...)`,
  `docs(...)`, `build(...)`, `ci(...)`, `chore(...)`. One logical change per
  commit.
- Pull requests follow the template: summary, issue, changes,
  verification (automated and hardware/manual), risks.

## Screens in the documentation

The README and the wiki show every screen from `docs/images/screens/`.
When a change alters a screen, run `tools/doc-screens/make.sh` and commit
the new pictures with the change
([Screen Gallery](Screen-Gallery#how-the-pictures-are-made)). New demo
data goes into `firmware/simulator/doc_shots.c`.

## Definition of done

- the code is there, builds and its tests pass;
- documentation is updated if an interface or the architecture changed;
- what could not be tested (hardware, Windows) is named explicitly;
- the PR says what was verified and how.

## Working with Claude Code

The repository is developed with Claude Code
([CLAUDE.md](https://github.com/egungor/stm32-desk-dashboard/blob/main/CLAUDE.md), [AGENTS.md](https://github.com/egungor/stm32-desk-dashboard/blob/main/AGENTS.md)). The human
developer decides the architecture, runs every hardware and Windows test
and merges.

- Specialist agents in `.claude/agents/`: architect, firmware, protocol,
  windows-audio, reviewer.
- Claude never reports a hardware or Windows result it did not observe;
  it labels everything VERIFIED / NOT VERIFIED / REQUIRES HARDWARE /
  REQUIRES WINDOWS / NOT TESTED.
- Claude's sandbox is Linux: it builds the firmware with the Arm
  toolchain, the host with GCC, Clang and MinGW, and runs Windows test
  programs under Wine. None of that replaces a test on the board or on
  Windows.

More: [docs/development-workflow.md](https://github.com/egungor/stm32-desk-dashboard/blob/main/docs/development-workflow.md).
