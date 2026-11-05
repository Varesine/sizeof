#!/usr/bin/env bash
# ==============================================================================
# sizeof - Quick Installation Script
# https://github.com/
# ==============================================================================

set -euo pipefail

# Text styling
BOLD="\033[1m"
GREEN="\033[32m"
CYAN="\033[36m"
YELLOW="\033[33m"
RED="\033[31m"
DIM="\033[2m"
RESET="\033[0m"

# Default configuration
PREFIX="${HOME}/.local"
BIN_DIR="${PREFIX}/bin"
SYSTEM_INSTALL=false
UPDATE_PATH=true
ACTION="install"

# Determine script location
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

print_banner() {
    printf "${CYAN}${BOLD}"
    cat << "EOF"
  ____  _             ___   __ 
 / ___|(_)____  ___  / _ \ / _|
 \___ \| |_  / / _ \| | | | |_ 
  ___) | |/ / |  __/| |_| |  _|
 |____/|_/___| \___| \___/|_|  
EOF
    printf "${RESET}"
    printf "${DIM}  Blazing-fast multithreaded directory size analyzer${RESET}\n\n"
}

usage() {
    cat << EOF
Usage: $(basename "$0") [OPTIONS]

Options:
  --prefix <dir>   Install to custom prefix (default: ~/.local)
  --system         Install system-wide to /usr/local/bin (may require sudo)
  --no-path        Do not automatically update shell configuration files
  --uninstall      Uninstall sizeof from system
  -h, --help       Show this help message

Examples:
  ./install.sh
  ./install.sh --system
  ./install.sh --prefix /opt/custom
  ./install.sh --uninstall
EOF
    exit 0
}

# Parse command line options
while [[ $# -gt 0 ]]; do
    case "$1" in
        --prefix)
            PREFIX="$2"
            BIN_DIR="${PREFIX}/bin"
            shift 2
            ;;
        --system)
            PREFIX="/usr/local"
            BIN_DIR="/usr/local/bin"
            SYSTEM_INSTALL=true
            shift
            ;;
        --no-path)
            UPDATE_PATH=false
            shift
            ;;
        --uninstall)
            ACTION="uninstall"
            shift
            ;;
        -h|--help)
            usage
            ;;
        *)
            printf "${RED}Error:${RESET} Unknown option '%s'\n" "$1"
            usage
            ;;
    esac
done

do_uninstall() {
    local target="${BIN_DIR}/sizeof"
    if [[ -f "$target" ]]; then
        printf "Removing %s...\n" "$target"
        if [[ "$SYSTEM_INSTALL" == true ]] && [[ ! -w "$(dirname "$target")" ]]; then
            sudo rm -f "$target"
        else
            rm -f "$target"
        fi
        printf "${GREEN}✓ Successfully uninstalled sizeof.${RESET}\n"
    else
        printf "${YELLOW}sizeof not found at %s${RESET}\n" "$target"
    fi
    exit 0
}

if [[ "$ACTION" == "uninstall" ]]; then
    do_uninstall
fi

print_banner

# Step 1: Check compiler
printf "${BOLD}[1/4] Checking build environment...${RESET}\n"
CC_CMD=""
if command -v clang >/dev/null 2>&1; then
    CC_CMD="clang"
elif command -v gcc >/dev/null 2>&1; then
    CC_CMD="gcc"
elif command -v cc >/dev/null 2>&1; then
    CC_CMD="cc"
else
    printf "${RED}Error:${RESET} No C compiler found (clang or gcc required).\n"
    if [[ "$(uname)" == "Darwin" ]]; then
        printf "Install Command Line Tools with: ${BOLD}xcode-select --install${RESET}\n"
    else
        printf "Install build tools with: ${BOLD}sudo apt install build-essential${RESET} (Debian/Ubuntu) or ${BOLD}sudo dnf groupinstall \"Development Tools\"${RESET} (Fedora)\n"
    fi
    exit 1
fi
printf "  Using C compiler: ${CYAN}%s${RESET} (%s)\n" "$CC_CMD" "$($CC_CMD --version | head -n 1)"

# Step 2: Build binary
printf "\n${BOLD}[2/4] Compiling sizeof...${RESET}\n"
cd "$SCRIPT_DIR"
CC="$CC_CMD" make clean >/dev/null
CC="$CC_CMD" make >/dev/null

if [[ ! -x "${SCRIPT_DIR}/bin/sizeof" ]]; then
    printf "${RED}Error:${RESET} Build failed. Binary not found at bin/sizeof.\n"
    exit 1
fi

VERSION="$("${SCRIPT_DIR}/bin/sizeof" --version | awk '{print $2}')"
printf "  ${GREEN}✓${RESET} Successfully built ${BOLD}sizeof v%s${RESET}\n" "$VERSION"

# Step 3: Install binary
printf "\n${BOLD}[3/4] Installing binary to %s...${RESET}\n" "$BIN_DIR"
if [[ "$SYSTEM_INSTALL" == true ]] && [[ ! -w "$BIN_DIR" ]]; then
    printf "  Administrator privileges required to write to %s\n" "$BIN_DIR"
    sudo mkdir -p "$BIN_DIR"
    sudo install -m 755 "${SCRIPT_DIR}/bin/sizeof" "${BIN_DIR}/sizeof"
else
    mkdir -p "$BIN_DIR"
    install -m 755 "${SCRIPT_DIR}/bin/sizeof" "${BIN_DIR}/sizeof"
fi
printf "  ${GREEN}✓${RESET} Installed binary to ${CYAN}%s/sizeof${RESET}\n" "$BIN_DIR"

# Step 4: Configure PATH
printf "\n${BOLD}[4/4] Verifying PATH integration...${RESET}\n"
PATH_UPDATED=false

is_in_path() {
    [[ ":$PATH:" == *":$1:"* ]]
}

if is_in_path "$BIN_DIR"; then
    printf "  ${GREEN}✓${RESET} %s is already in your current PATH.\n" "$BIN_DIR"
else
    if [[ "$UPDATE_PATH" == true ]]; then
        # Check Zsh
        if [[ -f "${HOME}/.zshrc" ]]; then
            if ! grep -q "$BIN_DIR" "${HOME}/.zshrc" 2>/dev/null; then
                printf 'export PATH="%s:$PATH"\n' "$BIN_DIR" >> "${HOME}/.zshrc"
                PATH_UPDATED=true
                printf "  Added %s to ${CYAN}~/.zshrc${RESET}\n" "$BIN_DIR"
            fi
        fi

        # Check Zsh profile (macOS login shells)
        if [[ -f "${HOME}/.zprofile" ]]; then
            if ! grep -q "$BIN_DIR" "${HOME}/.zprofile" 2>/dev/null; then
                printf 'export PATH="%s:$PATH"\n' "$BIN_DIR" >> "${HOME}/.zprofile"
                PATH_UPDATED=true
                printf "  Added %s to ${CYAN}~/.zprofile${RESET}\n" "$BIN_DIR"
            fi
        fi

        # Check Bash
        if [[ -f "${HOME}/.bashrc" ]]; then
            if ! grep -q "$BIN_DIR" "${HOME}/.bashrc" 2>/dev/null; then
                printf 'export PATH="%s:$PATH"\n' "$BIN_DIR" >> "${HOME}/.bashrc"
                PATH_UPDATED=true
                printf "  Added %s to ${CYAN}~/.bashrc${RESET}\n" "$BIN_DIR"
            fi
        fi

        # Check Fish shell
        FISH_CONF="${HOME}/.config/fish/config.fish"
        if command -v fish >/dev/null 2>&1 || [[ -f "$FISH_CONF" ]]; then
            if command -v fish >/dev/null 2>&1; then
                fish -c "fish_add_path -q '$BIN_DIR'" 2>/dev/null || true
            fi
            if [[ -f "$FISH_CONF" ]] && ! grep -q "$BIN_DIR" "$FISH_CONF" 2>/dev/null; then
                printf 'set -gx PATH "%s" $PATH\n' "$BIN_DIR" >> "$FISH_CONF"
                PATH_UPDATED=true
                printf "  Added %s to ${CYAN}%s${RESET}\n" "$BIN_DIR" "$FISH_CONF"
            fi
        fi

        if [[ "$PATH_UPDATED" == true ]]; then
            printf "  ${YELLOW}Notice:${RESET} Shell profiles updated. Restart your terminal or run: ${BOLD}source ~/.zshrc${RESET}\n"
        else
            printf "  ${GREEN}✓${RESET} Shell profile configuration is up to date.\n"
        fi
    else
        printf "  ${DIM}Skipped PATH update (--no-path specified).${RESET}\n"
    fi
fi

# Summary
printf "\n${GREEN}${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${RESET}\n"
printf "${GREEN}${BOLD}  sizeof is ready to use!${RESET}\n"
printf "${GREEN}${BOLD}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${RESET}\n\n"
printf "  Quick Commands:\n"
printf "    ${CYAN}sizeof${RESET}                  ${DIM}# Calculate size of current folder${RESET}\n"
printf "    ${CYAN}sizeof some.file${RESET}        ${DIM}# Check a specific file's size${RESET}\n"
printf "    ${CYAN}sizeof /some/folder${RESET}     ${DIM}# Fast multithreaded folder scan${RESET}\n"
printf "    ${CYAN}sizeof ls${RESET}               ${DIM}# List contents sorted by size${RESET}\n"
printf "    ${CYAN}sizeof ls -top 3${RESET}        ${DIM}# Show the 3 largest files/folders${RESET}\n"
printf "    ${CYAN}sizeof -v /some/folder${RESET}  ${DIM}# Detailed breakdown (files, dirs, time)${RESET}\n\n"
