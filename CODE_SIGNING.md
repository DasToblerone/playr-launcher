# Code signing policy

This document describes how the Windows executable of Steam Game Randomizer is built and signed. It is written for the application to the [SignPath Foundation](https://signpath.org/) program for free code signing of open-source projects, and for anyone who wants to know where the released `SteamGameRandomizer.exe` comes from.

## What is signed

Only `SteamGameRandomizer.exe`, built from the source code in this repository. No third-party binaries are included or signed.

## How it is built

- Releases are built by the GitHub Actions workflow [`.github/workflows/release.yml`](.github/workflows/release.yml) on GitHub-hosted Windows runners, from the tagged commit, with the Microsoft C++ compiler.
- The workflow uploads the unsigned build as an artifact and submits it to SignPath for signing. The signed file is then attached to the GitHub release.
- The project has no dependencies besides the Windows SDK.

## Roles

This is a one-person project. [DasToblerone](https://github.com/DasToblerone) is the only author, reviewer and approver. Every signing request is approved manually by the maintainer, and only builds of tagged release commits are submitted.

## Privacy policy

Steam Game Randomizer does not collect or send any personal data and has no telemetry or analytics.

- It reads files that Steam stores on your computer: the list of installed games, play time and last played time, your collections, and your Steam profile name and picture.
- It makes HTTPS requests to Steam's public servers (`cdn.cloudflare.steamstatic.com` for game art and `store.steampowered.com` to find out whether an app is a game). These requests contain only public game IDs.
- It stores its settings and downloaded images in `%APPDATA%\GameRandomizer` on your computer. Deleting that folder resets the program.
- Nothing is sent to the author or to any other third party.

## Verifying a download

Open the file's properties in Windows, go to the *Digital Signatures* tab and check that the signature is valid. Releases that were built before code signing was set up are not signed.
