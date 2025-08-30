param (
    [string]$Tag,
    [string]$Repository
)

# Find previous tag if available
$prev_tag = git describe --tags --abbrev=0 (git rev-list --tags --skip=1 --max-count=1) 2>$null

if ($prev_tag) {
    $range = "$prev_tag..HEAD"
} else {
    $range = "HEAD"
}

# Build the body text
$body = "## Changes`n"
$body += (git log --pretty=format:"- %s ([`%h`](https://github.com/$Repository/commit/%H))" $range) -join "`n"

if ($env:GITHUB_OUTPUT) {
    # Running inside GitHub Actions: export as output
    "body<<EOF" | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
    $body        | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
    "EOF"        | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
} else {
    # Local run: just show in terminal
    Write-Output $body
}