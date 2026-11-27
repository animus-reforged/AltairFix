#!/usr/bin/env python3
"""Generate a changelog from git commits since a given commit SHA.

Run with:

    python tools/generate_changelog.py <since-sha> --output release_notes.md

The script groups conventional commits into Features and Bug Fixes, links each
entry to its GitHub commit, and skips documentation/CI/build-only changes.
"""

from __future__ import annotations

import argparse
import logging
import os
import re
import subprocess
import sys
from pathlib import Path

logger = logging.getLogger(__name__)

DEFAULT_REPOSITORY = "animus-reforged/AltairFix"

# Commit titles that are not useful to end users; these are omitted entirely.
IGNORE_PATTERNS = [
    re.compile(r"^chore\(localization\): Update translation progress chart$"),
    re.compile(r"^chore\(deps\)"),
    re.compile(r"^chore: Update translation progress chart$"),
    re.compile(r"^chore(\(.*?\))?: (release|bump|version)", re.IGNORECASE),
    re.compile(r"^ci(\(.*?\))?:"),
    re.compile(r"^\w+\(ci\):"),
    re.compile(r"^docs?(\(.*?\))?:"),
    re.compile(r"^\w+\(docs?\):"),
    re.compile(r"^bump(:|$)"),
    re.compile(r"^test(:|$)"),
    re.compile(r"^init(:|$)"),
    re.compile(r"^style(:|$)"),
    re.compile(r"^perf(:|$)"),
    re.compile(r"^build(:|$)"),
    re.compile(r"^merge ", re.IGNORECASE),
]

# Strip a trailing " (#123)" added by GitHub's squash merge.
PR_NUMBER_RE = re.compile(r"\s*\(#\d+\)$")

# Match "feat(scope): subject" and extract the type and optional scope.
TYPE_RE = re.compile(r"^(\w+)(\(.*?\))?:")

# Sections that receive their own heading; other types go under "Other".
GROUP_HEADINGS = (
    ("feat", "### Features"),
    ("fix", "### Bug Fixes"),
)
OTHER_HEADING = "### Other"


def get_commits(since_sha: str) -> list[tuple[str, str]]:
    """Return a list of (hash, subject) commits since SHA."""
    logger.info("Fetching commits since %s...", since_sha or "repository creation")
    # %H is the full hash; %s is the subject. The pipe keeps parsing simple.
    result = subprocess.run(
        ["git", "log", "--pretty=format:%H|%s", f"{since_sha}..HEAD"],
        capture_output=True,
        text=True,
        check=True,
    )
    commits: list[tuple[str, str]] = []
    for line in result.stdout.splitlines():
        line = line.strip()
        if not line:
            continue
        parts = line.split("|", 1)
        if len(parts) == 2:
            commits.append((parts[0], parts[1]))
    logger.info("Found %d total commits", len(commits))
    return commits


def should_ignore(title: str) -> bool:
    """Return True if a commit title is not user-facing."""
    return any(pattern.search(title) for pattern in IGNORE_PATTERNS)


def display_title(title: str) -> str:
    """Strip the conventional-commit type while keeping the scope.

    Examples:
        "feat(ui): Add X" -> "ui: Add X"
        "fix: Crash"      -> "Crash"
    """
    match = TYPE_RE.match(title)
    if not match:
        return title
    rest = title[match.end() :].strip()
    if match.group(2):
        return f"{match.group(2)[1:-1]}: {rest}"
    return rest


def build_changelog(since_sha: str, repository: str) -> str:
    """Build the changelog markdown string."""
    if not since_sha:
        logger.info(
            "No previous commit SHA provided - generating first release changelog"
        )
        return "## Changelog\n\nFirst release or unable to determine previous release."

    commits = get_commits(since_sha)
    if not commits:
        logger.info("No new commits found since %s", since_sha[:7])
        return "## Changelog\n\nNo new commits since last release."

    logger.info("Filtering out docs/internal commits...")
    # Group user-facing commits by conventional type so Features and Bug Fixes
    # are rendered first and everything else falls under Other.
    groups: dict[str, list[str]] = {}
    ignored_count = 0
    entry_count = 0

    for commit_hash, subject in commits:
        title = PR_NUMBER_RE.sub("", subject)
        if not title.strip() or should_ignore(title):
            ignored_count += 1
            continue

        short = commit_hash[:7]
        url = f"https://github.com/{repository}/commit/{commit_hash}"
        entry = f"- **{display_title(title)}** ([{short}]({url}))"
        match = TYPE_RE.match(title)
        group = match.group(1).lower() if match else "other"
        groups.setdefault(group, []).append(entry)
        entry_count += 1

    if ignored_count:
        logger.info("Skipped %d non-user-facing commits", ignored_count)

    sections: list[str] = []
    for group, heading in GROUP_HEADINGS:
        entries = groups.pop(group, None)
        if entries:
            sections.append(heading + "\n\n" + "\n".join(entries))

    if groups:
        rest = [entry for key in sorted(groups) for entry in groups[key]]
        sections.append(OTHER_HEADING + "\n\n" + "\n".join(rest))

    if not sections:
        logger.info("No user-facing changelog entries found")
        return "## Changelog\n\nNo new commits since last release."

    logger.info("Generated %d changelog entries", entry_count)
    return "## Changelog\n\n" + "\n\n".join(sections)


def main(argv: list[str] | None = None) -> int:
    """Parse arguments, generate the changelog, and write or print it."""
    parser = argparse.ArgumentParser(
        description="Generate a changelog from git commits since a given SHA."
    )
    parser.add_argument(
        "since_sha",
        nargs="?",
        default="",
        help="Commit SHA or tag to start from (default: first release)",
    )
    parser.add_argument(
        "--repository",
        default=os.environ.get("GITHUB_REPOSITORY", DEFAULT_REPOSITORY),
        help="GitHub repository in owner/name form for commit links",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Write the changelog to this file instead of stdout",
    )
    parser.add_argument("--debug", action="store_true", help="Enable debug logging")
    args = parser.parse_args(argv)

    logging.basicConfig(
        level=logging.DEBUG if args.debug else logging.INFO,
        format="[%(levelname)s] %(message)s",
    )

    try:
        changelog = build_changelog(args.since_sha, args.repository)
    except subprocess.CalledProcessError as exc:
        logger.error("Error generating changelog: %s", exc)
        return 1

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(changelog + "\n", encoding="utf-8")
        logger.info("Changelog written to %s", args.output)
    else:
        print(changelog)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
