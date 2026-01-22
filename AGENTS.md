# AGENTS.md - AI Coding Agent Instructions

## Purpose

This file provides explicit instructions for AI coding agents working in this repository.  Agents MUST follow these guidelines strictly and avoid any actions not explicitly requested.

---

## Core Principles

### 1. No Assumptions

- **DO NOT** assume what the user wants beyond their explicit request
- **DO NOT** add features, optimizations, or "improvements" unless specifically asked
- **DO NOT** refactor code that wasn't mentioned in the request
- **ASK** for clarification when requirements are ambiguous

### 2. Scope Limitation

- Only modify files explicitly mentioned or directly required for the task
- Do not touch unrelated files, even if you notice potential issues
- Keep changes minimal and focused on the exact request

### 3. No Improvisation

- Follow existing code patterns and conventions in this repository
- Do not introduce new dependencies without explicit approval
- Do not change coding styles, formatting rules, or project structure

---

## Before Making Changes

1. **Understand the request completely** - If unclear, ask for clarification
2. **Identify the minimal set of changes** - Only what's necessary
3. **Check existing patterns** - Follow the established conventions in this codebase
4. **Verify scope** - Confirm you're only touching requested areas

---

## Prohibited Actions (Unless Explicitly Requested)

- [ ] Adding new dependencies or packages
- [ ] Refactoring existing code
- [ ] Changing project structure or file organization
- [ ] Modifying configuration files
- [ ] Adding comments or documentation
- [ ] Implementing additional features "for completeness"
- [ ] Optimizing code that works correctly
- [ ] Updating tests beyond what's required for the change
- [ ] Changing code style or formatting
- [ ] Removing code deemed "unnecessary"
- [x] NEVER push to origin, ever.
- [x] Run commands that could damage the host or run sudo.

---

## Required Actions

- [x] Stay within the explicit scope of the request
- [x] Follow existing code style and patterns exactly
- [x] Preserve all existing functionality unless told to change it
- [x] Report any blockers or ambiguities before proceeding
- [x] Document what was changed and why (in commit messages/PR descriptions)
- [x] Work on a branch, never in the default branch.

---

## When Uncertain

If you encounter any of these situations, **STOP and ask for clarification**:

1. The request could be interpreted multiple ways
2. Completing the task seems to require changes outside the stated scope
3. You notice potential bugs or issues in unrelated code
4. The existing code patterns conflict with best practices
5. You're unsure which approach the maintainers would prefer

---

## Communication Format

When responding to requests, structure your response as:

1. **Understanding**:  Restate what you understood from the request
2. **Plan**: Outline exactly what you will do (files to modify, approach)
3. **Questions**: List any clarifications needed before proceeding
4. **Changes**: After approval, make only the agreed-upon changes

---

## Project-Specific Rules

<!-- Add your project-specific rules below -->

### Technology Stack

- [List your tech stack here]

### Coding Standards

- [Link to or describe your coding standards]

### File Structure

- [Describe expected file organization]

### Testing Requirements

- [Describe testing expectations]

### Dependencies Policy

- [Describe how new dependencies should be handled]

### Development Environment

We're going to use a x86_64 VM for development, which is hosted remotely and can be accessed via SSH. It can be accessed:

- SSH access: `ssh root@devbox.sector.int -p 6021`
- Serial console: `devbox.sector.int:5555`
- QMP control: `devbox.sector.int:4444`
- Disk: qcow2 overlay (can revert to clean state if needed)
- Branch: `port-arm64`
- Sync workflow: commit locally, push to Gitea, then pull in `/usr/src` on the VM

### ARM64 Test Environment

The `tools/arm64-test/` directory contains a Makefile for testing the arm64 EFI loader and kernel in QEMU.

**Quick Start:**

```sh
cd tools/arm64-test
make copy-loader    # Fetch loader from VM
make run-gui        # Run with graphical display
make test           # Run with 25s timeout (headless)
```

**Available Targets:**

| Target | Description |
|--------|-------------|
| `make run` | Run loader interactively (Ctrl-A X to exit) |
| `make run-gui` | Run loader with graphical display |
| `make test` | Run loader with 25s timeout |
| `make setup` | Create EFI disk structure and copy loader |
| `make copy-loader` | Copy loader from remote VM |
| `make copy-kernel` | Copy kernel from remote VM (placeholder) |
| `make clean` | Remove temporary test files |

**Configuration Variables:**

- `VM_DIR` - Directory for VM files (default: `$HOME/vms`)
- `LOADER_EFI` - Path to loader.efi (default: `$VM_DIR/loader.efi`)
- `DISK_IMAGE` - Path to qcow2 disk (default: `$VM_DIR/dfly.qcow2`)
- `TEST_TIMEOUT` - Timeout in seconds (default: 25)
- `USE_DISK=1` - Use qcow2 disk instead of FAT directory

**Requirements:**

- `qemu-system-aarch64`
- EDK2 AARCH64 firmware (`edk2-aarch64-code.fd`)

---

## Contact

For questions about these guidelines, contact:  [maintainer contact info]

---

*Last updated:  2026-01-22*
