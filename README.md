# fargs — a utility for managing file selections in a shell

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
git clone https://github.com/uwfmt/fargs.git
cd fargs
make
sudo make install  # Optional, installs to /usr/local/bin
```

## Usage

### Basic Commands
```bash
# Append files
fargs append *.log /var/log/**/*.tmp

# Add more to the existing list
ls -1 *.bak | fargs append

# Even more
fzf -m | fargs append

# Output list
for f in `fargs out`; do mv $f /var/archive; done

# Clear storage
fargs clear  # Force clear
```

### Advanced Examples
```bash
# Append with verbose output
find /home -name '*.conf' | fargs a -v

# Output sorted list and clear after using it
for f in `fargs o -sc`; do cp $f /mnt/backup; done

# Force replace locked list
fargs r -f important_file.*

# Unlock if previous operation failed
fargs unlock
> Release existing lock? [Y/N] y
```

## Operational Modes

| Command   | Alias | Description                                             |
|-----------|-------|---------------------------------------------------------|
| `append`  | `a`   | Add file paths to selection, existing paths are ignored |
| `replace` | `r`   | Replace existing selection with a new one               |
| `out`     | `o`   | Output stored into selection file paths                 |
| `clear`   | `c`   | Clear the selection                                     |
| `unlock`  | `u`   | Remove stale lockfile                                   |
| `help`    |       | Show usage information                                  |

Also check man page for using details.

## Options

| Flag | Description                       |
|------|-----------------------------------|
| `-v` | Verbose output (show counts)      |
| `-s` | Sort output alphabetically        |
| `-c` | Clear storage after output        |
| `-f` | Force operation ignoring lockfile |
| `-h` | Show usage information            |

## Technical Details

- **Storage Location**: `/tmp/fargs_<UID>.tmp`
- **Index Files**: SHA-256 hashes in `/tmp/fargs_<UID>.idx`
- **Lockfiles**: `/tmp/fargs.lock` for operation safety
- **Security**: 0600 permissions on all user files

## License [![License: GPL](https://img.shields.io/badge/License-GPLv3-green.svg)](https://opensource.org/licenses/gpl-3-0)

Under terms of GPL v3 - see [LICENSE](LICENSE) for full text.
