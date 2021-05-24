#!/bin/bash

set -e

if [[ $# -eq 0 ]] ; then
  exit 1
fi

dot_file=$1
graph="${1%.*}".ps

mime_type="application/postscript"
default_app=$(grep -i ^exec $(locate -n 1 $(xdg-mime query default $mime_type | cut -d';' -f 1)) | perl -pe 's/.*=(\S+).*/$1/')

dot -Tps $dot_file -o $graph
$default_app $graph

rm $dot_file
rm $graph

exit 0
