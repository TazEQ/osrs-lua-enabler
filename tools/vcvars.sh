#!/usr/bin/env bash
# tools/vcvars.sh — Bootstrap MSVC environment for bash shells.
# Usage: eval "$(tools/vcvars.sh [arch])"
# arch defaults to x64.

set -euo pipefail

ARCH="${1:-x64}"

# Already in a dev shell?
if command -v cl.exe &>/dev/null; then
    exit 0
fi

# --- Locate Visual Studio installation ---------------------------------------

VSWHERE="/c/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe"

if [[ ! -x "$VSWHERE" ]]; then
    echo "error: vswhere.exe not found. Is Visual Studio installed?" >&2
    exit 1
fi

VSINSTALL=$("$VSWHERE" -latest -property installationPath)

if [[ -z "$VSINSTALL" ]]; then
    echo "error: vswhere found no Visual Studio installation." >&2
    exit 1
fi

# --- Locate vcvarsall.bat ----------------------------------------------------

# Convert to Windows path for cmd.exe
VSINSTALL_WIN=$(cygpath -w "$VSINSTALL")
VCVARSALL="${VSINSTALL_WIN}\\VC\\Auxiliary\\Build\\vcvarsall.bat"

# Verify it exists (convert back to unix path for the test)
VCVARSALL_UNIX=$(cygpath -u "$VCVARSALL")
if [[ ! -f "$VCVARSALL_UNIX" ]]; then
    echo "error: vcvarsall.bat not found at: $VCVARSALL" >&2
    echo "Is the C++ workload installed?" >&2
    exit 1
fi

# --- Capture environment via temp batch file ----------------------------------

# Direct cmd.exe invocation with quoted paths is unreliable from MSYS2/Git Bash.
# A temp .bat file sidesteps all quoting issues.
TMPBAT=$(mktemp /tmp/vcvars_XXXXXX.bat)
trap 'rm -f "$TMPBAT"' EXIT

cat > "$TMPBAT" <<EOBAT
@call "$VCVARSALL" $ARCH >nul 2>&1
@if errorlevel 1 exit /b 1
@set
EOBAT

ENV_AFTER=$(cmd //C "$(cygpath -w "$TMPBAT")" | tr -d '\r' | sort)

if [[ -z "$ENV_AFTER" ]]; then
    echo "error: vcvarsall.bat failed to produce environment output." >&2
    exit 1
fi

# Vars that matter for MSVC compilation
EXPORT_VARS="PATH INCLUDE LIB LIBPATH WindowsSdkDir WindowsSdkVersion UCRTVersion VCToolsInstallDir VSCMD_ARG_TGT_ARCH"

while IFS='=' read -r key value; do
    # Only export vars we care about
    for var in $EXPORT_VARS; do
        if [[ "$key" == "$var" ]]; then
            # For PATH, convert Windows paths to Unix paths
            if [[ "$key" == "PATH" ]]; then
                value=$(cygpath -p "$value")
            fi
            # Use single quotes to avoid backslash escaping issues
            # (Windows paths often end with \ which would escape a closing double quote)
            printf "export %s='%s'\n" "$key" "$value"
            break
        fi
    done
done <<< "$ENV_AFTER"
