---
name: deploy
description: Build the HTML reference for https://sp2.clau.se and publish it to the host pupa (via WSL ssh + rsync into /home/sp2docsdigger/www). Use when the user asks to deploy, publish or upload the site.
---

# Deploy the HTML reference to sp2.clau.se

The host `pupa` serves `/home/sp2docsdigger/www` at the root of `https://sp2.clau.se` (nginx and SSL are already set
up; do not touch their config). WSL has passwordless `ssh pupa` and passwordless `sudo` there.

## 1. Build the site for the root base

The default build (`site.bat` alone) targets `/overlunky` of GitHub Pages and would break every link on this host,
so always rebuild with the host's URL and base. In PowerShell from the repository root:

```powershell
$env:DD_SITE_URL = 'https://sp2.clau.se'; $env:DD_SITE_BASE = '/'; cmd /c site.bat
```

- Do not set `DD_SITE_BASE=/` from Git Bash: MSYS rewrites the `/` into a Windows path.
- `site.bat` needs `build\api.json` (and `build\lua`, `build\lua-emmylua` for the downloads). If it is missing, or the
  user asks for fresh output, run `cmd /c dig.bat` first (about 2.5 minutes).
- If the build or its checks fail, stop and report; never deploy a failed build.

## 2. Upload

Preview first, then deploy:

```powershell
wsl -e bash .claude/skills/deploy/deploy.sh build/site/starlight --dry-run
wsl -e bash .claude/skills/deploy/deploy.sh build/site/starlight
```

`deploy.sh` mirrors the directory with rsync over `sudo rsync` on the host: files no longer in the build are deleted,
everything is owned by `sp2docsdigger` with modes 755/644. The old site stays whole until the upload finishes.

## 3. Verify

```powershell
wsl -e bash -c "curl -sS -o /dev/null -w '%{http_code}\n' https://sp2.clau.se/ https://sp2.clau.se/pagefind/pagefind.js https://sp2.clau.se/old-anchors.json"
```

All should print `200`. Report the rsync stats (files transferred / deleted) and the URL to the user.
