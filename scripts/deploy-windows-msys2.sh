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

declare -A queued_paths=()
declare -A scanned_paths=()

queue=("$output_dir/$(basename "$exe_path")")
queued_paths["$queue"]=1

extract_dependencies() {
    local binary="$1"

    ldd "$binary" | awk '
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
