# Legacy PowerShell entrypoint. Package installation belongs in an MSYS2 UCRT64
# shell so pacman uses the correct native Windows toolchain prefix.
Write-Host "Open an MSYS2 UCRT64 shell in the repository root and run:"
Write-Host "  bash rrclient/win32/install-deps.sh"
Write-Host "This installs dependencies only; the Win64 source port is unfinished."
