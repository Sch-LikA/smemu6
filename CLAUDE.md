# CLAUDE.md — Smemu6 Project Guidelines

**Project:** Smemu6 — Z80 emulator of Smaky 6 (1978 Swiss computer)
**Repository:** https://github.com/Sch-LikA/smemu6
**Focus:** Cycle-accurate emulation, SAMOS OS, SDCC C + CALM assembly workflows

## Who You Are (Marcel)
**Role:** Senior Security Expert, Program Manager
**Expertise:** System admin, shell scripting, web dev, security, infrastructure, hardware, networking
**Learning:** Low-level coding, C, assembler
**Response Adjustment:** Never over-explain known areas. Never skip needed context for learning. Assume C/assembler reading competency; build foundational understanding.

## Communication Style
- **No preambles.** Start with the answer. Skip "Great question!" and warmups.
- **Match response length.** Simple questions = short. Complex = detailed. No padding or restatement.
- **Show approach options.** For significant tasks: present 2-3 approaches, wait for selection.
- **Be explicit about uncertainty.** If unsure: say so. Never fill gaps with plausible guesses.

## Critical Workflow

### 0. Strict Scope Discipline
Only modify what's directly related to current task. No refactoring, renaming, reorganizing, or reformatting unless explicitly requested. Note other issues at end but do not touch them.

Before making any change that significantly alters content you've already created (rewriting sections, removing paragraphs, restructuring flow, changing tone): stop. Describe exactly what you're about to change and why. Wait for confirmation before proceeding.

Before deleting any file, overwriting existing code, dropping database records, or removing dependencies: stop. List exactly what will be affected. Ask for explicit confirmation. Only proceed after you say yes in the current message. "You mentioned this earlier" is not confirmation.

The following require explicit in-session confirmation, no exceptions:
- Deploying or pushing to any environment
- Running migrations or schema changes
- Sending any external API call
- Sending confidential information (passwords, tokens, keys, secrets)
- Executing any command with irreversible side effects
- Sending, posting, publishing, or scheduling anything on your behalf (emails, calendar invites, document shares, or any action outside this conversation)

I must see you say yes in the current message before proceeding.

### 1. Commit After Every Code Modification
Each source code change commits immediately: `git commit -m "type: description"`. Never accumulate uncommitted changes.

### 2. Update Documentation First
Before committing code: update TODO.md, sdcc/SDCC_TODO.md, calm/README.md, feature docs, example READMEs.

### 3. Use repo-local tmp/
All temporary files, logs, test output → `tmp/` at project root, NOT `/tmp`.

## Problem-Solving Approach

For any task involving architecture decisions, debugging complex issues, or non-trivial features: work through the problem step by step before writing any code. Show your reasoning. Identify where you're uncertain. Then implement.

For questions involving system architecture, performance tradeoffs, database design, or long-term technical decisions: use extended thinking mode. Work through the problem step by step. Surface tradeoffs you haven't considered. Flag assumptions that might not hold at scale. Then give a clear recommendation.

## Core Principles

**Ask, don't assume.** If something is unclear, ask before writing a single line. Never make silent assumptions about intent, architecture, or requirements.

**Simplest solution first.** Always implement the simplest thing that could work. Do not add abstractions or flexibility that weren't explicitly requested.

**Don't touch unrelated code.** If a file or function is not directly part of the current task, do not modify it, even if you think it could be improved.

**Flag uncertainty explicitly.** If you are not confident about an approach or technical detail, say so before proceeding. Confidence without certainty causes more damage than admitting a gap.

## Decision Logging

Maintain [MEMORY.md](MEMORY.md) in this project. After any significant decision:
- What was decided
- Why it was chosen
- What was rejected and why

Read MEMORY.md at the start of every session. Never contradict a logged decision without flagging it first.

## Error Logging

Maintain [ERRORS.md](ERRORS.md) in this project. When an approach takes more than 2 attempts to work, log it:
- What didn't work
- What worked instead
- Note for next time

Check ERRORS.md before suggesting approaches to similar tasks.

## Session Summaries

When you say "session end", "wrapping up", or "see you later": I will write a session summary to MEMORY.md. Include:
- **Worked on:** What tasks were tackled this session
- **Completed:** What was finished (with commit messages or file references)
- **In progress:** What is unfinished and needs next session
- **Decisions made:** Any significant decisions logged (with MEMORY.md references)
- **Next session priorities:** What should be done first next time

This ensures continuity across sessions and preserves important context.

---

**For technical reference, workflow details, decisions, and findings:** See MEMORY.md

