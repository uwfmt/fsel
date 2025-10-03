# fsel is a file selection tool

A CLI utility that manages file selections for batch operations on Unix.
Store, retrieve, and manipulate file lists across command runs.
Selections persist until you explicitly clear them.

## Why?

### This tool makes console file management more convenient

Unix is great for file operations due to the customizability of its shells.
However, by default they provide only basic file operations (copy, move,
rename, delete) and lack a persistent selection mechanism. When you need to
collect hundreds or thousands of files scattered across different directories,
traditional approaches fail:

- **Shell globs** are limited to single directory patterns
- **Find with -exec** requires knowing all criteria upfront, no iterative refinement
- **Manual file arguments** become unwieldy beyond a dozen paths
- **Xargs pipelines** don't preserve selections between commands

`fsel` solves this by decoupling file selection from file operations. You can:

1. Build your selection iteratively over multiple commands
2. Refine it using different search tools (`find`, `fzf`, `fd`, custom scripts)
3. Verify the selection before applying operations
4. Reuse the same selection for multiple different commands
5. Process thousands of files without command-line length limits

Example workflow:
```bash
find /project -name '*.log' | fsel          # Initial selection
find /backup -mtime +30 -name '*.log' | fsel  # Add more files
fsel -v                                     # Validate all paths exist
fsel | xargs tar czf logs.tar.gz            # Archive them
fsel | xargs -I{} rsync {} backup:/storage/ # Then backup
fsel -c                                     # Clear when done
```

### Unified selection buffer across different file managers

`fsel` provides a shared selection buffer that works across completely different
file management interfaces. This enables workflows that were previously impossible:

Real-world example using Emacs Dired with shell commands:

1. Navigate and mark files in Dired (visual interface, powerful filtering)
2. Export selection to `fsel` via [emacs-fm](https://github.com/uwfmt/emacs-fm)
   integration (one command `* S` in Dired buffer)
3. Switch to terminal and process files with any Unix tools

```bash
# In terminal, use the same selection:
fsel | xargs -I{} convert {} -resize 50% resized/{}
fsel | parallel gzip {}
fsel -c  # Clear when done
```

This works potentially with any file manager that implements `fsel` integration,
like it did for Dired (Emacs). The selection buffer is the universal interface!

> While I don't claim that `fsel` format should become the standard, the
> approach of having a standardized selection format that persists across
> different tool invocations certainly deserves the attention of file managment
> tools developers.

## Features

- Preserve selection between command calls using temporary files
- Accept arguments via stdin pipeline or as file arguments, including globbing
  pattern expansion
- Implement lockfile mechanism to prevent concurrency collisions
- Suited for really large selections (keep data on disk + uses index-based
  deduplication).
- `validate` (`-v`) — Validate the selection by checking if all stored file paths exist.

## Installation

### Requirements
- GNU/Linux system
- OpenSSL development libraries
- C compiler (gcc/clang)

```bash
git clone https://github.com/uwfmt/fsel.git
cd fsel
make
sudo make install  # Optional, installs to /usr/local/bin
```

## Usage

**Utility is in development yet. Names of options and commands may be changed in future versions.**

### Basic Commands
```bash
fsel ~/*.log
fsel /var/lelog/**/*.log

for f in `fsel`; do mv $f /var/archive; done
```

### Advanced Examples
```bash
ls -1 *.bak | fsel

fzf -m | fsel

find /home -name '*.conf' | fsel

for f in `fsel -sc`; do cp $f /mnt/backup; done
```

Forcely overwrite old selections when it needed:

``` bash
fsel -fr important_file.*

fsel -u


```

## Operational Modes

| Mode        | Flag | Description                                              |
|-------------|------|----------------------------------------------------------|
| `add`       |      | Save file paths to selection, existing paths are ignored  |
| `replace`   | `-r` | Replace existing selection with new one                  |
| `list`      |      | Output stored into selection file paths (default)        |
| `clear`     | `-c` | Clear the selection                                      |
| `unlock`    | `-u` | Remove stale lockfile                                    |
| `validate`  | `-v` | Validate the selection                                   |

Also remember that old good `man` page available for this utility.

## Options
Just `fsel -h` for help.

| Flag | Description                       |
|------|-----------------------------------|
| `-q` | Suppress informational messages   |
| `-s` | Sort files in selection on output |
| `-c` | Clear storage after output        |
| `-f` | Force operation ignoring lockfile |
| `-h` | Show usage information            |
| `-v` | Validate the selection            |
| `-l` | Long format output (like ls -l)   |

## Technical Details

- **Storage Location**: `$TMPDIR/fsel_<UID>.tmp` (defaults to `/tmp` if TMPDIR not set)
- **Index Files**: SHA-256 hashes in `$TMPDIR/fsel_<UID>.idx`
- **Lockfiles**: `$TMPDIR/fsel_<UID>.lock` for operation safety
- **Security**: 0600 permissions on all user files

## Integrations

* [emacs-fm](https://github.com/uwfmt/emacs-fm) — extension for Dired inside GNU/Emacs

## License [![License: GPL](https://img.shields.io/badge/License-GPLv3-green.svg)](https://opensource.org/licenses/gpl-3-0)

Under terms of GPL v3 - see [LICENSE](LICENSE) for full text.
