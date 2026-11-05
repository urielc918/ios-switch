#!/bin/bash
# Summarize an xnu CI run: which of our patches applied or were skipped
# (blacktop's build.sh only warns "Skipping patch ..."), the compile errors
# grouped by file, and whether a kernel came out.
#
#   tools/ci-check.sh [RUN_ID]        (default: the newest xnu run)
#
# The full log is saved as logs/ci-RUN_ID.log (git-ignored).
set -euo pipefail
cd "$(dirname "$0")/.."
REPO=${REPO:-urielc918/ios-switch}
run=${1:-$(gh run list -R "$REPO" --workflow xnu --limit 1 --json databaseId --jq '.[0].databaseId')}
mkdir -p logs
log=logs/ci-$run.log
gh run view "$run" -R "$REPO" --json status,conclusion,displayTitle,createdAt,updatedAt \
	--jq '"run \(env.run): \(.status) \(.conclusion) | \(.displayTitle) | \(.createdAt) -> \(.updatedAt)"' 2>/dev/null ||
	gh run view "$run" -R "$REPO" | head -3
gh run view "$run" -R "$REPO" --log > "$log" 2>/dev/null || { echo "no log yet (run still going?)"; exit 1; }

applied=$(grep -c "Applying patch: " "$log" || true)
echo "patches applied: $applied"
echo "patches skipped:"
grep -o "Skipping patch [^ ]*" "$log" | sed 's|.*/||; s/^/  /' | sort -u || true
echo "  (0031 and 0041 are expected: they need the HFS sources staged by 0026-stage-hfs.sh first)"

echo "compile errors (first per file):"
grep -E "[^ ]+\.(c|cpp|s|h|hpp|iig)(:[0-9]+){1,2}: (fatal )?error:" "$log" |
	sed -E 's/^.*\t//; s/^[^ ]*Z //' |
	sed -E 's|^.*/(osfmk\|bsd\|iokit\|libkern\|pexpert\|security\|san\|libsa\|makedefs)/|\1/|' |
	sort -u | awk -F: '!seen[$1]++' | head -60 || true
nerr=$(grep -cE ": (fatal )?error:" "$log" || true)
echo "error lines in log: $nerr"
if grep -q "kernel.development.sweet" "$log" && grep -qiE "DONE|BUILD SUCCEEDED|Kernel built" "$log"; then
	echo "kernel: probably built (check the artifacts: gh run download $run -R $REPO)"
fi
echo "log: $log"
