# sizeof

A blazing-fast, multithreaded CLI utility written in C to calculate and display the total size of files or directories.

When calculating the size of a folder, `sizeof` utilizes a multithreaded worker pool (leveraging all available CPU cores by default) with depth-first work-stealing, inode deduplication for hard links, and direct `fstatat` system calls.

---

## Features

- **Blazing Fast**: Uses all available CPU cores (14 on M-series) with minimal lock contention and work stealing.
- **Single File & Folder Support**: Instant metadata stat for individual files; concurrent tree traversal for folders.
- **Hard Link Deduplication**: Automatic deduplication of multi-linked files (`st_nlink > 1`) using a 1024-bucket thread-safe hash table.
- **Dual Size Metrics**: Supports both apparent content size (`st_size`) and filesystem allocated disk space (`st_blocks * 512`).
- **Flexible Output**:
  - Default: Human-readable size with comma-separated byte counts and directory stats.
  - `-b`, `--bytes`: Raw bytes (ideal for shell scripts and pipelines).
  - `-s`, `--short`: Concise human-readable string (e.g. `1.45 GB`).
  - `-v`, `--verbose`: Full breakdown of files, directories, symlinks, disk usage, and duration.
  - `--json`: Machine-readable JSON output.
- **Cross-Shell PATH Integration**: Ready to run directly from `fish`, `zsh`, and `bash`.

---

## Installation & Building

```bash
# Build binary
make

# Run test suite
make test

# Install to ~/.local/bin (already in PATH)
make install
```

---

## Usage

```bash
# Inspect a single file
sizeof some.file

# Inspect a folder (multithreaded)
sizeof /some/folder

# Inspect current folder
sizeof

# Multiple files or folders
sizeof file1.txt folder1 file2.bin

# Output only raw byte count
sizeof -b /some/folder

# Output short human-readable string
sizeof -s /some/folder

# Verbose breakdown
sizeof -v /some/folder

# Custom thread count
sizeof -t 8 /some/folder

# View allocated disk space instead of apparent size
sizeof -d /some/folder

# JSON output
sizeof --json /some/folder
```

---

## Options

| Flag | Description |
|------|-------------|
| `-b, --bytes` | Print size in raw bytes only |
| `-k, --kb` | Print size in Kilobytes (KiB) |
| `-m, --mb` | Print size in Megabytes (MiB) |
| `-g, --gb` | Print size in Gigabytes (GiB) |
| `-s, --short` | Print only human-readable size string (e.g. `1.45 GB`) |
| `-d, --disk` | Calculate disk space allocated (`st_blocks * 512`) |
| `-a, --apparent` | Calculate apparent content size [default] |
| `-v, --verbose` | Multi-line breakdown (files, dirs, time, disk vs apparent) |
| `-t, --threads <N>` | Set number of worker threads (default: hardware CPU count) |
| `-x, --one-file-system` | Do not cross filesystem mount boundaries |
| `--si` | Use decimal 1000-based units (kB, MB) instead of 1024 |
| `--no-color` | Disable colored terminal output |
| `--json` | Output results in JSON format |
| `-h, --help` | Show help message |
| `--version` | Display version information |
