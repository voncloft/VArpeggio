#!/usr/bin/env bash
set -euo pipefail

keyflow_package_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
keyflow_bin_dir="${HOME}/.local/bin"
keyflow_data_dir="${XDG_DATA_HOME:-${HOME}/.local/share}"

install -Dm755 "${keyflow_package_dir}/keyflow" "${keyflow_bin_dir}/keyflow"
install -Dm644 "${keyflow_package_dir}/org.keyflow.Studio.svg" "${keyflow_data_dir}/icons/hicolor/scalable/apps/org.keyflow.Studio.svg"
install -Dm644 "${keyflow_package_dir}/org.keyflow.Studio.desktop" "${keyflow_data_dir}/applications/org.keyflow.Studio.desktop"
sed -i "s|^Exec=.*|Exec=${keyflow_bin_dir}/keyflow|" "${keyflow_data_dir}/applications/org.keyflow.Studio.desktop"

printf 'Keyflow is installed. Open the KDE launcher and search for Keyflow MIDI Studio.\n'
