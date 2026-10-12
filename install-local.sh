#!/usr/bin/env bash
set -euo pipefail

varpeggio_package_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
varpeggio_bin_dir="${HOME}/.local/bin"
varpeggio_data_dir="${XDG_DATA_HOME:-${HOME}/.local/share}"

varpeggio_binary="${varpeggio_package_dir}/varpeggio"
if [[ ! -x "${varpeggio_binary}" ]]; then varpeggio_binary="${varpeggio_package_dir}/build/varpeggio"; fi
install -Dm755 "${varpeggio_binary}" "${varpeggio_bin_dir}/varpeggio"
install -Dm644 "${varpeggio_package_dir}/com.voncloft.VArpeggio.svg" "${varpeggio_data_dir}/icons/hicolor/scalable/apps/com.voncloft.VArpeggio.svg"
install -Dm644 "${varpeggio_package_dir}/com.voncloft.VArpeggio.desktop" "${varpeggio_data_dir}/applications/com.voncloft.VArpeggio.desktop"
sed -i "s|^Exec=.*|Exec=${varpeggio_bin_dir}/varpeggio|" "${varpeggio_data_dir}/applications/com.voncloft.VArpeggio.desktop"

printf 'VArpeggio is installed. Open the KDE launcher and search for VArpeggio.\n'
