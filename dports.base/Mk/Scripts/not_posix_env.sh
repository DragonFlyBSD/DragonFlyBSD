#!/bin/sh

# emulate non standard env -S bahviour for ENV variables

if [ "$#" -lt "2" ]; then
  echo "not_posix_env less than 2"
  exit 2
fi

heap=$(echo $1 |sed 's/\\ /\\_/g')

for consider in $heap
do
  consider=$(echo $consider |sed 's/\\_/ /g')
  export "$consider"
done
shift

env $@

