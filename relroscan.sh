#!/bin/bash

echo_file_properties()
{
file="$1"

pheaders=$(readelf --wide --program-headers "$file")  # -l
dynamic=$(readelf --wide --dynamic "$file")           # -d
symbols=$(readelf --wide --symbols "$file")           # -s
fheader=$(readelf --wide --file-header "$file")       # -h

has_relro() { local m=$'\n  GNU_RELRO   '; [[ "$pheaders" =~ $m ]]; }
has_full_relro() { local m=$'\n [^\n]* \(BIND_NOW\) '; [[ "$dynamic" =~ $m ]]; }
has_stack_canary() { local m='__stack_chk_fail'; [[ "$symbols" =~ $m ]]; }
has_nx() { local m=$'\n  GNU_STACK [^\n]* RW '; [[ "$pheaders" =~ $m ]]; }
has_pie() { local m=$'\n  Type: [^\n]* DYN '; [[ "$fheader" =~ $m ]]; }
has_rpath() { local m=$'\n [^\n]* \(RPATH\) '; [[ "$dynamic" =~ $m ]]; }
has_runpath() { local m=$'\n [^\n]* \(RUNPATH\) '; [[ "$dynamic" =~ $m ]]; }

if has_relro; then
  has_full_relro && echo -n " relro " || echo -n " prelro "
else
  echo -n " norelro "
fi
has_stack_canary && echo -n "canary " || echo -n "nocanary "
has_nx && echo -n "nx " || echo -n "nonx "
has_pie && echo -n "pie " || echo -n "nopie "  #actually PIC
has_rpath && echo -n "rpath " || echo -n "norpath "
has_runpath && echo -n "runpath " || echo -n "norunpath "
}

is_elf() { [[ $(head -c4 "$1") = $'\x7FELF' ]]; }

[ "$#" -ge 1 ] || { echo "usage: $0 <path> [path] ..."; exit 1; }
for path in "$@"; do
  while IFS= read -r -d '' file; do
    is_elf "$file" || continue
    echo_file_properties "$file"
    echo "-- $file"
  done < <(find -H "$path" -type f -print0)
done
