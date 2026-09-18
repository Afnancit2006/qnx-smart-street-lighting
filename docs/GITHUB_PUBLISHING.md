# GitHub publishing checklist

This package is already structured as a repository. Publish it only after the
team has reviewed the hackathon's originality rules and can explain the code.
Do not add the confidential organizer brief.

## 1. Review before the first commit

From the project directory:

```sh
make check
git status --short
git diff --check
```

Confirm that `build/`, `build-qnx/`, and generated `run/` data are ignored. The
repository should contain source, tests, documentation, the historical profile,
and the final approved presentation/report only.

## 2. Create team-owned commits

Set your own Git identity; do not use a shared or invented author:

```sh
git config user.name "YOUR NAME"
git config user.email "YOUR VERIFIED GITHUB EMAIL"
git add .
git commit -m "document LumiGrid RT architecture and host baseline"
```

During the event, use smaller factual commits such as:

```text
add absolute sensor sampling timer
route emergency events at queue priority 31
add stale-sensor fail-safe test
integrate confirmed BSP GPIO adapter
record QNX target latency results
```

## 3. Create and push the public repository

Create an empty public GitHub repository named `lumigrid-rt` without generated
README or license files, then run the commands GitHub shows. A typical sequence
is:

```sh
git branch -M main
git remote add origin https://github.com/YOUR-ACCOUNT/lumigrid-rt.git
git push -u origin main
```

If `origin` already exists, inspect it with `git remote -v` before changing it.
Never paste a personal access token into a tracked file or screenshot.

## 4. Verify the public view

- README renders and its local links work.
- The MIT license is visible.
- Host CI completes successfully.
- No build binaries, generated run folders, credentials, serial numbers, or
  confidential event documents appear in the repository.
- The report calls laptop measurements host results and QNX/Pi measurements
  target results.
- The release used for judging is tagged only after the team freezes it.

## 5. Suggested release command

After the QNX/Pi evidence and final team review:

```sh
git tag -a hackathon-rc1 -m "LumiGrid RT hackathon release candidate"
git push origin hackathon-rc1
```

Keep an offline ZIP and a second SD card in addition to the public repository.
