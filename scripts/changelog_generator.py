#!/usr/bin/env python3
"""Generate changelog from git commits since the previous tag."""

import argparse
import logging
import os
import subprocess
import sys

logger = logging.getLogger(__name__)

IGNORE_PATTERNS = [
    r"^chore\(localization\):",
    r"^chore\(deps\)",
    r"^chore: Update translation progress chart$",
    r"^ci(\(.*?\))?:",
    r"^\w+\(ci\):",
    r"^docs?:",
    r"^bump(:|$)",
    r"^test(:|$)",
    r"^init(:|$)",
    r"^style(:|$)",
    r"^perf(:|$)",
    r"^build(:|$)",
    r"^merge ",
]

PR_NUMBER_RE = r"\s*\(#\d+\)$"


def run_git(*args: str) -> str:
    """Run a git command and return stdout."""
    result = subprocess.run(
        ["git"] + list(args),
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout


def get_sorted_tags() -> list[str]:
    """Return all tags sorted by version."""
    output = run_git("tag", "--sort=-version:refname")
    return [line.strip() for line in output.splitlines() if line.strip()]


def get_commits_in_range(range_spec: str, repository: str) -> list[str]:
    """Get formatted commit log entries for the given range."""
    format_str = f"- %s ([`%h`](https://github.com/{repository}/commit/%H))"
    output = run_git("log", f"--pretty=format:{format_str}", range_spec)
    return [line for line in output.splitlines() if line.strip()]


def should_ignore(title: str) -> bool:
    """Check if a commit title matches any ignore pattern."""
    import re
    return any(re.search(p, title, re.IGNORECASE if p.lower() == p else 0) for p in IGNORE_PATTERNS)


def build_changelog(tag: str, repository: str) -> str:
    """Build the changelog markdown string."""
    logger.info("Current tag: %s", tag)

    all_tags = get_sorted_tags()
    logger.info("Available tags: %s", ", ".join(all_tags) if all_tags else "none")

    # Find previous tag
    previous_tag = None
    try:
        current_index = all_tags.index(tag)
        if current_index + 1 < len(all_tags):
            previous_tag = all_tags[current_index + 1]
    except ValueError:
        # Tag doesn't exist yet (typical for new releases)
        if all_tags:
            previous_tag = all_tags[0]

    logger.info("Previous tag: %s", previous_tag or "none")

    # Determine commit range
    if previous_tag:
        # Check if current tag exists
        try:
            run_git("tag", "-l", tag)
            # If we get here without error, check if it actually returned the tag
            tag_exists = tag in run_git("tag", "-l", tag).strip()
        except subprocess.CalledProcessError:
            tag_exists = False

        if tag_exists:
            range_spec = f"{previous_tag}..{tag}"
        else:
            range_spec = f"{previous_tag}..HEAD"
    else:
        range_spec = ""

    # Build changelog body
    body = "## Changes\n\n"

    if range_spec:
        entries = get_commits_in_range(range_spec, repository)
    else:
        entries = get_commits_in_range("", repository)

    # Filter out ignored commits
    filtered_entries = []
    for entry in entries:
        # Extract commit message (before the link part)
        import re
        match = re.match(r"-\s+(.+?)\s+\(\[", entry)
        title = match.group(1) if match else entry
        if not should_ignore(title):
            filtered_entries.append(entry)

    if filtered_entries:
        body += "\n".join(filtered_entries)
    else:
        body += "No user-facing changes."

    return body


def main():
    parser = argparse.ArgumentParser(description="Generate changelog from git commits")
    parser.add_argument("--tag", required=True, help="Current tag for the release")
    parser.add_argument("--repository", required=True, help="GitHub repository (e.g., owner/repo)")
    parser.add_argument("--debug", action="store_true", help="Enable debug logging")
    args = parser.parse_args()

    logging.basicConfig(
        level=logging.DEBUG if args.debug else logging.INFO,
        format="[%(levelname)s] %(message)s",
    )

    try:
        body = build_changelog(args.tag, args.repository)
    except subprocess.CalledProcessError as e:
        logger.error("Error generating changelog: %s", e)
        sys.exit(1)

    # Write to GitHub Actions output file
    github_output = os.environ.get("GITHUB_OUTPUT")
    if github_output:
        with open(github_output, "a", encoding="utf-8") as f:
            f.write(f"body<<EOF\n{body}\nEOF\n")
        logger.info("Changelog written to GITHUB_OUTPUT")
    else:
        print(body)


if __name__ == "__main__":
    main()
