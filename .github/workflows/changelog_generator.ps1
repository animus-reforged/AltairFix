param(
    [string]$Tag,
    [string]$Repository
)

Write-Host "Current tag: $Tag"

# Ensure we have all tags
git fetch --tags --force

# Get all tags sorted by version
$allTags = @(git tag --sort=-version:refname)
Write-Host "Available tags: $($allTags -join ', ')"

# Find previous tag
$previousTag = $null

# Check if current tag exists in the list
$currentIndex = $allTags.IndexOf($Tag)

if ($currentIndex -ge 0) {
    # Current tag exists, get the next one in the sorted list
    if (($currentIndex + 1) -lt $allTags.Count) {
        $previousTag = $allTags[$currentIndex + 1]
    }
} else {
    # Current tag doesn't exist yet (typical for new releases)
    # Get the most recent existing tag
    if ($allTags.Count -gt 0) {
        $previousTag = $allTags[0]
    }
}

Write-Host "Previous tag found: $previousTag"

# Set the range for git log
if ($previousTag) {
    # Check if the current tag exists, if not use HEAD
    $tagExists = git tag -l $Tag
    if ($tagExists) {
        Write-Host "Getting commits between $previousTag and $Tag"
        $range = "$previousTag..$Tag"
    } else {
        Write-Host "Getting commits between $previousTag and HEAD (tag $Tag doesn't exist yet)"
        $range = "$previousTag..HEAD"
    }
} else {
    Write-Host "No previous tag found, getting all commits"
    $range = ""
}

# Build the body text
$body = "## Changes`n"

if ($range) {
    $body += (git log --pretty=format:"- %s ([`%h`](https://github.com/$Repository/commit/%H))" $range) -join "`n"
} else {
    # No range, so get ALL commits
    $body += (git log --pretty=format:"- %s ([`%h`](https://github.com/$Repository/commit/%H))") -join "`n"
}

if ($env:GITHUB_OUTPUT) {
    # Running inside GitHub Actions: export as output
    "body<<EOF" | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
    $body        | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
    "EOF"        | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
} else {
    # Local run: just show in terminal
    Write-Output $body
}