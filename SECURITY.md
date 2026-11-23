# Security

## Using it safely

This firmware runs on an ESP32 Cheap Yellow Display that joins your WiFi. Keep the board on your own home network, not on a public or shared one, and do not forward any of its ports to the internet. Before you give a used board away, erase it (`pio run -t erase`) so your WiFi password goes with you.

## Reporting a problem

Please report security problems privately, not in a public issue: open the repo's
**Security** tab and choose **Report a vulnerability**
(https://github.com/revjmoney/CYD-SINGLE-CELL-PET/security/advisories/new).
Say what you found and how to reproduce it. I will reply there.

## What is in place

- Dependabot alerts and security updates
- Secret scanning with push protection
- CodeQL code scanning
- Private vulnerability reporting
- CI on every push and pull request: builds Cell Pet, runs its checks, and fails if
  private data (home addresses, emails, tokens, keys) shows up in the repo

[Rev. J. Money - 2026] · by JMSCNC, makers of RotatoCAM
