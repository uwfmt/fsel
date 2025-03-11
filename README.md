# fargs — helper for manipulating file selection in a shell

A utility to manage file selections for batch operations in Linux environments. Store, retrieve, and manipulate file lists between different command runs. It keeps the list persistently until you remove it explicitly.

## Features

- **Temporary file storage** with user-specific isolation
- **Deduplication** using SHA-256 hashing
- Four operational modes: Append/Replace/Output/Clear
- STDIN pipeline support
- Globbing patterns (`*`, `?`) expansion
- Lockfile mechanism for concurrent use prevention
- Optimized for large file lists (disk-based operations)
- Unicode filename support

## Installation

### Requirements
- GNU/Linux system
- OpenSSL development libraries
- C compiler (gcc/clang)

```bash
git clone https://github.com/yourusername/fargs.git
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

| Command       | Alias | Description                              |
|---------------|-------|------------------------------------------|
| `append`      | `a`   | Add files to existing list               |
| `replace`     | `r`   | Overwrite existing file list             |
| `out`         | `o`   | Output stored file paths                 |
| `clear`       | `c`   | Delete all stored paths                  |
| `unlock`      | `u`   | Remove stale lockfile                    |
| `help`        | `-h`  | Show usage information                   |

## Options

| Flag | Description                         |
|------|-------------------------------------|
| `-v` | Verbose output (show counts)        |
| `-s` | Sort output alphabetically          |
| `-c` | Clear storage after output          |
| `-f` | Force operation ignoring lockfile   |

## Technical Details

- **Storage Location**: `/tmp/fargs_<UID>.tmp`
- **Index Files**: SHA-256 hashes in `/tmp/fargs_<UID>.idx`
- **Lockfiles**: `/tmp/fargs.lock` for operation safety
- **Security**: 0600 permissions on all user files

## License [![License: BSD](https://img.shields.io/badge/License-BSD-yellow.svg)](https://opensource.org/licenses/BSD)


BSD License - see [LICENSE](LICENSE) for full text.
