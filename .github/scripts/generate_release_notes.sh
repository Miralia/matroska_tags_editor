#!/usr/bin/env bash
set -euo pipefail

release_tag="${1:-${GITHUB_REF_NAME:-}}"
current_ref="${2:-${GITHUB_SHA:-HEAD}}"
prerelease="${3:-false}"

previous_tag=""
if [[ "${prerelease}" != "true" ]] &&
   git rev-parse "${release_tag}^{commit}" >/dev/null 2>&1; then
  previous_tag="$(git describe --tags --abbrev=0 --match 'v*' "${release_tag}^" 2>/dev/null || true)"
fi

if [[ "${prerelease}" != "true" && -z "${previous_tag}" ]]; then
  previous_tag="$(git describe --tags --abbrev=0 --match 'v*' "${current_ref}^" 2>/dev/null || true)"
fi

if [[ "${prerelease}" == "true" ]]; then
  log_args=(-1 "${current_ref}")
elif [[ -n "${previous_tag}" ]]; then
  range="${previous_tag}..${current_ref}"
  log_args=("${range}")
else
  range="${current_ref}"
  log_args=("${range}")
fi

declare -A titles=(
  [feat]="Features"
  [fix]="Fixes"
  [perf]="Performance"
  [refactor]="Refactors"
  [build]="Build System"
  [ci]="CI/CD"
  [docs]="Documentation"
  [test]="Tests"
  [style]="Style"
  [chore]="Maintenance"
  [other]="Other Changes"
)

types=(feat fix perf refactor build ci docs test style chore other)

declare -A buckets
for type in "${types[@]}"; do
  buckets["${type}"]=""
done

conventional_re='^([a-zA-Z]+)(\([^)]+\))?(!)?:[[:space:]]+(.+)$'

while IFS=$'\t' read -r sha subject; do
  [[ -z "${sha}" ]] && continue

  type="other"
  if [[ "${subject}" =~ ${conventional_re} ]]; then
    candidate="${BASH_REMATCH[1],,}"
    case "${candidate}" in
      feat|fix|perf|refactor|build|ci|docs|test|style|chore)
        type="${candidate}"
        ;;
    esac
  fi

  buckets["${type}"]+="- ${subject} (${sha})"$'\n'
done < <(git log --reverse --format='%h%x09%s' "${log_args[@]}")

echo "## Changes"
echo
if [[ "${prerelease}" == "true" ]]; then
  echo "Triggered by \`${current_ref}\`."
elif [[ -n "${previous_tag}" ]]; then
  echo "Commits since \`${previous_tag}\`."
else
  echo "Initial release notes."
fi
echo

has_changes="false"
for type in "${types[@]}"; do
  if [[ -n "${buckets[${type}]}" ]]; then
    has_changes="true"
    echo "### ${titles[${type}]}"
    echo
    printf '%s' "${buckets[${type}]}"
    echo
  fi
done

if [[ "${has_changes}" == "false" ]]; then
  echo "No commits found for this release."
  echo
fi

if [[ "${prerelease}" != "true" && -n "${previous_tag}" && -n "${GITHUB_REPOSITORY:-}" ]]; then
  echo "### Compare"
  echo
  echo "https://github.com/${GITHUB_REPOSITORY}/compare/${previous_tag}...${current_ref}"
fi
