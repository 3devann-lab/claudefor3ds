#!/usr/bin/env bash
# Installs makerom and bannertool (needed to package ClaudeFor3DS.cia) into a directory on PATH.
#
# Usage: scripts/install_cia_tools.sh [install-dir]   (default: /usr/local/bin)
# Uses the projects' prebuilt Linux releases, and builds from source when no usable binary is found.
set -euo pipefail

DEST="${1:-/usr/local/bin}"
WORK="$(mktemp -d)"
trap 'rm -rf "${WORK}"' EXIT
mkdir -p "${DEST}"

gh_api() {
	if [ -n "${GITHUB_TOKEN:-}" ]; then
		curl -fsSL --retry 3 -H "Authorization: Bearer ${GITHUB_TOKEN}" "$1"
	else
		curl -fsSL --retry 3 "$1"
	fi
}

# Download URLs of release assets of repo $1 whose names match regex $2, newest release first.
release_assets() {
	gh_api "https://api.github.com/repos/$1/releases?per_page=20" | python3 -c '
import json, re, sys
pattern = re.compile(sys.argv[1], re.I)
for release in json.load(sys.stdin):
    for asset in release.get("assets", []):
        if pattern.search(asset["name"]):
            print(asset["browser_download_url"])
' "$2"
}

# True when $1 is an x86-64 Linux executable that starts on this system.
usable() {
	file -L "$1" 2>/dev/null | grep -qi 'ELF.*x86-64' || return 1
	local out
	out="$("$1" 2>&1 </dev/null || true)"
	! grep -qiE 'GLIBC_|GLIBCXX_|error while loading shared libraries|cannot execute' <<<"${out}"
}

# Fetches archive $1, finds an executable called $2 inside and installs it.
install_from_url() {
	local url="$1" name="$2" dir
	dir="$(mktemp -d -p "${WORK}")"
	echo "Trying ${url}"
	curl -fsSL --retry 3 -o "${dir}/archive" "${url}" || return 1
	case "${url}" in
	*.zip) unzip -q -o "${dir}/archive" -d "${dir}/x" || return 1 ;;
	*.tar.gz | *.tgz) mkdir -p "${dir}/x" && tar -xzf "${dir}/archive" -C "${dir}/x" || return 1 ;;
	*) mkdir -p "${dir}/x" && cp "${dir}/archive" "${dir}/x/${name}" ;;
	esac
	local candidate
	while IFS= read -r candidate; do
		chmod +x "${candidate}"
		if usable "${candidate}"; then
			install -m 755 "${candidate}" "${DEST}/${name}"
			echo "Installed ${name} from ${url}"
			return 0
		fi
	done < <(find "${dir}/x" -type f -name "${name}*" ! -name '*.exe' ! -name '*.dll')
	return 1
}

install_tool() {
	local name="$1" repo="$2" pattern="$3" url
	if command -v "${name}" >/dev/null 2>&1 && usable "$(command -v "${name}")"; then
		echo "${name} already installed"
		return 0
	fi
	while IFS= read -r url; do
		install_from_url "${url}" "${name}" && return 0
	done < <(release_assets "${repo}" "${pattern}" || true)
	return 1
}

build_makerom() {
	echo "Building makerom from source"
	apt-get install -y --no-install-recommends build-essential >/dev/null 2>&1 || true
	git clone --depth 1 https://github.com/3DSGuy/Project_CTR.git "${WORK}/ctr"
	make -C "${WORK}/ctr/makerom" deps >/dev/null 2>&1 || true
	make -C "${WORK}/ctr/makerom" -j"$(nproc)"
	install -m 755 "$(find "${WORK}/ctr/makerom" -type f -name makerom -perm -u+x | head -n1)" "${DEST}/makerom"
}

build_bannertool() {
	echo "Building bannertool from source"
	apt-get install -y --no-install-recommends build-essential zip >/dev/null 2>&1 || true
	git clone --depth 1 --recursive https://github.com/Epicpkmn11/bannertool.git "${WORK}/bt"
	make -C "${WORK}/bt" -j"$(nproc)"
	install -m 755 "$(find "${WORK}/bt/output" -type f -name bannertool | head -n1)" "${DEST}/bannertool"
}

install_tool makerom 3DSGuy/Project_CTR 'makerom.*(ubuntu|linux).*x86_64.*\.(zip|tar\.gz)$' || build_makerom
install_tool bannertool Epicpkmn11/bannertool 'bannertool.*\.zip$' ||
	install_tool bannertool diasurgical/bannertool 'bannertool.*\.zip$' || build_bannertool

makerom -version 2>/dev/null | head -n2 || true
echo "makerom: $(command -v makerom)"
echo "bannertool: $(command -v bannertool)"
