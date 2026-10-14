# Source from the repository root: . ./env.sh
# Uses $PS2DEV if it is already set, otherwise toolchain/ps2dev.
_here="$(cd "$(dirname "${BASH_SOURCE:-$0}")" && pwd)"
export PS2DEV="${PS2DEV:-$_here/toolchain/ps2dev}"
export PS2SDK="$PS2DEV/ps2sdk"
export GSKIT="$PS2DEV/gsKit"
export PATH="$PS2DEV/ee/bin:$PS2DEV/iop/bin:$PS2DEV/dvp/bin:$PS2SDK/bin:$PATH"
