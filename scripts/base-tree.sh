#!/bin/sh
# @ai-generated(solo)
# base-tree.sh <ref> <dir>: a fresh detached worktree of <ref> at <dir>; prints its commit.
set -eu
ref=$1
dir=$2

# CI checks out one commit, so the base may exist only on the remote; a short sha does not fetch.
if ! rev=$(git rev-parse --verify --quiet "$ref^{commit}"); then
	git fetch --no-tags --depth=1 origin "${ref#origin/}" >&2
	rev=$(git rev-parse --verify FETCH_HEAD^{commit})
fi

git worktree remove --force "$dir" 2>/dev/null || rm -rf "$dir"
git worktree prune
git worktree add --detach "$dir" "$rev" >&2
echo "$rev"
