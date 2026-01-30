# Agents Configuration for DragonFly LinuxKPI Port

This document describes the agents used in the DragonFly BSD LinuxKPI porting project.

## Project Overview

**Goal:** Port FreeBSD's LinuxKPI (Linux Kernel Programming Interface) to DragonFly BSD to enable modern DRM (Direct Rendering Manager) graphics drivers.

**Repository:** `port-linuxkpi` branch on gitea
**Primary Work:** Importing FreeBSD LinuxKPI, Concurrency Kit (CK), and adapting for DragonFly

## Agents

### 1. build-test (Subagent)

**Location:** `.opencode/agents/build-test.md`

**Purpose:** Runs build tests on DragonFly BSD VM and reports results.

**Responsibilities:**
- Sync VM to specified git branch
- Execute buildworld, buildkernel, quickworld, or quickkernel
- Capture and report build results
- Detect and report failures without investigation

**Parameters:**
- `build_target`: "buildworld", "buildkernel", or "both"
- `use_quick`: true/false - use quick builds for incremental changes
- `clean_first`: true/false - clean /usr/obj before building
- `branch`: git branch to sync (default: port-linuxkpi)

**VM Access:**
- Host: `root@devbox.sector.int`
- Port: `6021`
- Source: `/usr/src`

**Output Format:** Markdown report with:
- Status (PASS/FAIL)
- Parameters received
- Steps executed
- Build results and metrics
- Error details (if failed)

---

## Agent Restrictions & Safety Rules

### Critical Safety Rules (All Agents)

1. **NEVER push to origin/master**
   - Agents may only push to `port-linuxkpi` branch
   - Never force push (`--force` or `-f`)
   - Never push to protected branches

2. **NEVER modify production systems**
   - VM is the only target for builds/tests
   - No changes to production DragonFly systems
   - No package installations on host systems

3. **NEVER run destructive commands without confirmation**
   - `rm -rf /` or similar
   - `dd` commands
   - Disk formatting
   - System shutdown/reboot

4. **NEVER execute user-provided shell commands directly**
   - All commands must be hardcoded in agent logic
   - No command injection vulnerabilities
   - Validate all inputs

5. **NEVER access sensitive data**
   - No reading of `.env` files
   - No access to SSH private keys
   - No access to passwords or credentials
   - No access to `/etc/shadow` or similar

### Build Agent Specific Restrictions

6. **Build Agent: No retries**
   - Run each step exactly once
   - If build fails, report immediately and stop
   - Do not attempt automatic fixes

7. **Build Agent: No investigation**
   - Do not analyze build failures
   - Do not search for solutions
   - Do not modify source code to fix errors
   - Just report the failure to primary agent

8. **Build Agent: VM isolation**
   - Only execute commands on VM via SSH
   - Never execute build commands on host
   - Respect VM timeouts (2 hours for kernel, 3 hours for world)

9. **Build Agent: Read-only on source**
   - Sync git repository (fetch/reset)
   - Build and test only
   - No edits to source files
   - No commits from agent

### Communication Restrictions

10. **No external notifications**
    - No Slack/Discord/email notifications
    - No GitHub PR comments
    - Report only to primary agent via return value

11. **No persistent state**
    - Do not maintain databases or logs between runs
    - Each invocation is independent
    - Clean up temporary files after use

### Security Boundaries

12. **Network restrictions**
    - Only connect to specified VM (devbox.sector.int:6021)
    - Only connect to gitea remote for git operations
    - No external API calls
    - No webhooks

13. **File system restrictions**
    - Read-only access to repository (except git sync)
    - Write access limited to `/tmp` for logs
    - No access outside working directory
    - No symlinks outside allowed paths

### Error Handling

14. **Fail fast**
    - Stop immediately on any error
    - Do not continue to next step if current fails
    - Report error details concisely

15. **No infinite loops**
    - All loops must have timeouts
    - Maximum retry count: 0 (no retries)
    - Maximum execution time enforced

---

## Agent Invocation

### From Primary Agent

```javascript
// Example: Run quickkernel build test
const result = await runAgent("build-test", {
    build_target: "buildkernel",
    use_quick: true,
    clean_first: false,
    branch: "port-linuxkpi"
});

// Parse result
if (result.status === "PASS") {
    // Continue with next phase
} else {
    // Handle failure
    console.log(result.errors);
}
```

### Expected Response Format

```markdown
## Build Test Report

**Status:** PASS / FAIL

**Parameters Received:**
- build_target: [value]
- use_quick: [value]
- clean_first: [value]
- branch: [value]

**Steps Executed:**
1. VM Sync: [commit] - [SUCCESS/FAIL]
2. Clean: [SKIPPED/EXECUTED] - [SUCCESS/FAIL]
3. Build: [command] - [SUCCESS/FAIL]

**Results:**
[Detailed results]
```

---

## Development Workflow

1. **Primary Agent** makes code changes
2. **Primary Agent** commits and pushes to `port-linuxkpi`
3. **Primary Agent** invokes `build-test` agent
4. **Build-test agent** syncs VM and runs build
5. **Build-test agent** returns PASS/FAIL report
6. **Primary Agent** decides next steps based on results

---

## Emergency Procedures

If an agent violates restrictions:
1. Immediately abort agent execution
2. Report violation to user
3. Do not invoke agent again until fixed
4. Review agent configuration for bugs

---

## Version History

- **2026-01-30:** Initial agent configuration
  - Created build-test agent
  - Defined safety restrictions
  - Documented workflow
