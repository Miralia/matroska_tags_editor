#!/usr/bin/env bash
set -euo pipefail

app_path="${1:?usage: bundle_macos_dylibs.sh <app bundle>}"
app_name="$(basename "${app_path}" .app)"
executable="${app_path}/Contents/MacOS/${app_name}"
frameworks_dir="${app_path}/Contents/Frameworks"

if [[ ! -x "${executable}" ]]; then
  executable="$(find "${app_path}/Contents/MacOS" -type f -perm -111 | head -n 1)"
fi

if [[ -z "${executable}" || ! -x "${executable}" ]]; then
  echo "Could not find app executable in ${app_path}" >&2
  exit 1
fi

mkdir -p "${frameworks_dir}"

seen_list=""
queue=("${executable}")

while ((${#queue[@]})); do
  binary="${queue[0]}"
  queue=("${queue[@]:1}")

  if printf '%s' "${seen_list}" | grep -Fxq -- "${binary}"; then
    continue
  fi
  seen_list="${seen_list}${binary}"$'\n'

  while IFS= read -r dep; do
    [[ -z "${dep}" ]] && continue
    case "${dep}" in
      /opt/homebrew/*|/usr/local/*)
        ;;
      *)
        continue
        ;;
    esac

    dep_name="$(basename "${dep}")"
    bundled_dep="${frameworks_dir}/${dep_name}"
    if [[ ! -f "${bundled_dep}" ]]; then
      cp -L "${dep}" "${bundled_dep}"
      chmod u+w "${bundled_dep}"
      install_name_tool -id "@executable_path/../Frameworks/${dep_name}" "${bundled_dep}" || true
      queue+=("${bundled_dep}")
    fi

    install_name_tool -change "${dep}" "@executable_path/../Frameworks/${dep_name}" "${binary}" || true
  done < <(otool -L "${binary}" | awk 'NR > 1 { print $1 }')
done
