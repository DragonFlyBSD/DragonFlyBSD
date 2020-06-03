#!/bin/sh

# The DragonFly kernel already uses a "pipe" identifier

gsed -i 's/enum pipe/enum i915_pipe/g' $*
