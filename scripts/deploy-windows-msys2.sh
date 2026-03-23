#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 5 ]]; then
    echo "usage: $0 <output-dir> <exe-path> <tool-dir> <msys-prefix> <qml-dir>" >&2
    exit 1
fi

output_dir="$1"
exe_path="$2"
tool_dir="$3"
msys_prefix="$4"
qml_dir="$5"

mkdir -p "$output_dir"
cp "$exe_path" "$output_dir/"

export PATH="${tool_dir}:${msys_prefix}/share/qt6/bin:${PATH}"
export QT_PLUGIN_PATH="${msys_prefix}/share/qt6/plugins"
export QML2_IMPORT_PATH="${msys_prefix}/share/qt6/qml"

ldd_timeout="${LDD_TIMEOUT:-10}"
ldd_timeout_cmd=()
if command -v timeout >/dev/null; then
    ldd_timeout_cmd=(timeout --kill-after=5s "${ldd_timeout}s")
else
    echo "warning: timeout(1) not found, ldd might hang" >&2
fi

declare -A queued_paths=()
declare -A scanned_paths=()

queue=("$output_dir/$(basename "$exe_path")")
queued_paths["$queue"]=1

extract_dependencies() {
    local binary="$1"

    local ldd_output
    local ldd_status=0
    if [[ ${#ldd_timeout_cmd[@]} -gt 0 ]]; then
        set +e
        ldd_output="$("${ldd_timeout_cmd[@]}" ldd "$binary" 2>&1)"
        ldd_status=$?
        set -e
        if [[ $ldd_status -eq 124 ]]; then
            echo "ldd timed out for $binary" >&2
        elif [[ $ldd_status -ne 0 ]]; then
            echo "ldd exited with status $ldd_status for $binary" >&2
        fi
    else
        ldd_output="$(ldd "$binary" 2>&1)" || true
    fi

    printf '%s\n' "$ldd_output" | awk '
        /=>/ && $(NF-1) != "not" { print $(NF-1) }
        /^\// { print $1 }
    ' | grep -iv "system32" || true
}

enqueue_dependency() {
    local dependency="$1"
    local file_name

    [[ -n "$dependency" ]] || return 0

    file_name="${dependency##*/}"
    if [[ ! -e "$output_dir/$file_name" ]]; then
        echo "Copied $dependency"
        cp "$dependency" "$output_dir/"
    fi

    if [[ -z "${queued_paths["$dependency"]+x}" ]]; then
        queue+=("$dependency")
        queued_paths["$dependency"]=1
    fi
}

while [[ ${#queue[@]} -gt 0 ]]; do
    current="${queue[0]}"
    queue=("${queue[@]:1}")

    if [[ -n "${scanned_paths["$current"]+x}" ]]; then
        continue
    fi
    scanned_paths["$current"]=1

    while IFS= read -r dependency; do
        enqueue_dependency "$dependency"
    done < <(extract_dependencies "$current")
done

windeployqt6.exe --no-translations --qmldir="$qml_dir" "$output_dir/$(basename "$exe_path")"
