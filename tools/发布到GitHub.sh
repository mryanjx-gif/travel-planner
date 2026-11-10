#!/bin/sh
# 把本地仓库公开到 GitHub：在临时副本里改写历史，去掉 PO 的真实旅行数据、作者邮箱换成 GitHub noreply，再推送。
# 本地仓库一点不动。改写是确定的：同样的输入每次得到同样的提交，以后再跑是快进推送。
# 用法（Git Bash）：sh tools/发布到GitHub.sh <GitHub 用户名> [仓库地址]   不给地址只改写和检查，不推送。
set -e
user="$1"; url="$2"
[ -n "$user" ] || { echo "用法：sh tools/发布到GitHub.sh <GitHub 用户名> [仓库地址]"; exit 1; }
src=$(cd "$(dirname "$0")/.." && pwd)
tmp=$(mktemp -d)
git clone -q --no-local "$src" "$tmp/repo"
cd "$tmp/repo"

# 真实数据和由它生成的东西（新增类似文件时加在这里）
export DROP='testdata tests/golden/plans.json tests/golden/marker-clusters.json tests/golden/roads.json tests/golden/unlock-colors.json claude/v7.0.0-*.png'
export NOREPLY="$user@users.noreply.github.com"
FILTER_BRANCH_SQUELCH_WARNING=1 git filter-branch -f \
  --index-filter 'git rm -r -q --cached --ignore-unmatch $DROP' \
  --env-filter 'GIT_AUTHOR_EMAIL=$NOREPLY; GIT_COMMITTER_EMAIL=$NOREPLY' \
  -- main >/dev/null

# 检查：历史里不该再有这些
bad=$(git log main --format='%ae %ce' | grep -v "$NOREPLY" | head -1 || true)
[ -z "$bad" ] || { echo "还有别的邮箱：$bad"; exit 1; }
left=$(git rev-list --objects main | grep -E '^[0-9a-f]+ (testdata/|tests/golden/(plans|marker-clusters|roads|unlock-colors)\.json|claude/v7\.0\.0-.*\.png)' | head -1 || true)
[ -z "$left" ] || { echo "历史里还有真实数据：$left"; exit 1; }

echo "改写完：$tmp/repo（$(git rev-list --count main) 个提交）"
[ -n "$url" ] || exit 0
git push "$url" main
