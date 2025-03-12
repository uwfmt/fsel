# fsel — a utility for managing file selections in a shell

A CLI utility to manage file selections for batch operations in Linux
environments. Store, retrieve, and manipulate file lists between different
command runs. It keeps the list persistently until you remove it explicitly.

## Features

- Preserve selection between command calls using temporary files
- Accept arguments via stdin pipeline or as file arguments, including globbing
  pattern expansion
- Implement lockfile mechanism to prevent concurrency collisions
- Optimized for large selections (uses index-based deduplication)

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
# Append files from different places into selection
fsel save ~/*.log
fsel save /var/lelog/**/*.log

# Use prepared selection in any shell operation
for f in `fsel out`; do mv $f /var/archive; done

# Clear storage
fsel clear 
```

### Advanced Examples
```bash
# Add paths from current directory, they will be converted to absolute paths
# You could use "s" as short alias for "save"
ls -1 *.bak | fsel s

# Select even more from interactive TUI utility like `fzf``
fzf -m | fsel s

# Add resultsf of `find``
find /home -name '*.conf' | fsel s

# Use sorted selection and clear after using it
for f in `fsel -oc o`; do cp $f /mnt/backup; done
```

Forcely overwrite old selections when it needel:

``` bash
# Force replace locked list
fsel r -f important_file.*

# Unlock when operation failed
fsel unlock
> Release existing lock? [Y/N] y
```

## Operational Modes

| Command   | Alias | Description                                              |
|-----------|-------|----------------------------------------------------------|
| `save`    | `s`   | Save file paths to selection, existing paths are ignored |
| `replace` | `r`   | Replace existing selection with a new one                |
| `out`     | `o`   | Output stored into selection file paths                  |
| `clear`   | `c`   | Clear the selection                                      |
| `unlock`  | `u`   | Remove stale lockfile                                    |
| `help`    |       | Show usage information                                   |

Also check man page for using details.

## Options

| Flag | Description                       |
|------|-----------------------------------|
| `-q` | Suppress informational messages   |
| `-o` | Order output alphabetically       |
| `-c` | Clear storage after output        |
| `-f` | Force operation ignoring lockfile |
| `-h` | Show usage information            |

## Technical Details

- **Storage Location**: `/tmp/fs_<UID>.tmp`
- **Index Files**: SHA-256 hashes in `/tmp/fs_<UID>.idx`
- **Lockfiles**: `/tmp/fs.lock` for operation safety
- **Security**: 0600 permissions on all user files

## License [![License: GPL](https://img.shields.io/badge/License-GPLv3-green.svg)](https://opensource.org/licenses/gpl-3-0)

Under terms of GPL v3 - see [LICENSE](LICENSE) for full text.
