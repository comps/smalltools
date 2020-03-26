#!/bin/bash
#
# Copyright (c) 2020 Red Hat, Inc. All rights reserved.
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.
#
# AUTHOR: Jiri Jaburek <jjaburek@redhat.com>
#

#
# This program creates jails (chroots) from the list of binaries given via
# CLI arguments, complete with all their shared library dependencies, preserving
# all symlinks to save as much space as possible.
#
# These jails/chroots can then be used to run chosen binaries isolated from the
# main OS filesystem.
# They are also useful for making minimal init ramdisks.
#


# use bashisms to get around [] limitations where the left side of `=' must not
# be an '(', otherwise it gets treated as opening for ( EXPRESSION )
ldd_files()
{
    local src="$1" line=
    while read -a line; do
        # /lib64/ld-linux-x86-64.so.2 (0x00007f6468404000)
        if [[ "${line[0]::1}" == "/" && "${line[1]::3}" == "(0x" && -e "${line[0]}" ]]; then
            echo "${line[0]}"
        # libc.so.6 => /lib64/libc.so.6 (0x00007f538fd16000)
        elif [[ "${line[1]}" == "=>" && "${line[2]::1}" == "/" && -e "${line[2]}" ]]; then
            echo "${line[2]}"
        fi
    done < <(ldd "$src")
}

# like readlink, but print absolute path to target (which must exist)
# (unlike -f, DO NOT resolve symlinks recursively, only one level)
abs_readlink()
{
    local out= rp= drp=
    out=$(readlink "$1") || return $?
    rp=$(realpath --no-symlinks "$1")
    drp=$(dirname "$rp")
    ( cd "$drp"; realpath --no-symlinks "$out" )
}

# src must be absolute!
copy_to_target()
{
    local src="$1" target="$2" parent=
    [ -e "$target/./$src" ] && return 0
    parent=$(dirname "$src")
    mkdir -p "$target/./$parent"
    cp -a "$src" "$target/./$src"
}

# take absolute filenames from stdin
recursive_copy()
{
    local target="$1" file= link=
    while read file; do
        copy_to_target "$file" "$target"
        # copy any symlinks, recursively (in case of symlinks to symlinks)
        # - this can save us a lot of disk space if binaries refer to multiple
        #   levels of the symlink chain
        while link=$(abs_readlink "$file"); do
            copy_to_target "$link" "$target"
            file="$link"
        done
    done
}

make_base_fstree()
{
    # symlinks are critical here as 'ldd' may resolve some libraries or their
    # symlinks as paths that are reachable only via symlinks
    local root="$1"
    mkdir "$root"
    (
        cd "$root"
        mkdir -p dev/{shm,pts} etc proc run sys tmp usr/{bin,sbin,lib,lib64}
        for i in bin sbin lib lib64; do
            ln -s usr/$i $i
        done
    )
}


if [ $# -lt 2 ]; then
    echo "usage: $0 <jailroot> [/path/to/binary]..."
    exit 1
fi

jailroot="$1"
if [ ! -e "$jailroot" ]; then
    make_base_fstree "$jailroot"
fi
shift
for binary in "$@"; do
    # the binary itself (and its symlinks, see ie. /etc/alternatives/)
    recursive_copy "$jailroot" <<<"$binary"
    # its library deps
    recursive_copy "$jailroot" < <(ldd_files "$binary")
done

# vim: sts=4 sw=4 et :
