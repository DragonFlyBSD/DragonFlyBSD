#!/bin/sh

set -e

if eval false; then
  echo 'Really?'
  return 1
fi

echo "Your shell is good"
return 0
