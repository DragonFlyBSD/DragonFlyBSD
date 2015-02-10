# This build order provides more parallelism that gcc47, and is almost as
# efficient as possible.  Most of the support libraries could be built
# without cc_tools and the backend could be built at the same time as the
# drivers rather than afterwards, but this setup is reasonably fine grained.

SUBDIR_ORDERED=	cc_prep cc_tools support-libs drivers libbackend backends

# The SUBDIR_ORDERED definition is currently equivalent to SUBDIR_ORDERED=
# but it wasn't always -- there used to be "doc" directory which could be
# built at any time.  Just leave the redundant definition for now, maybe
# we'll need it again in the future.

SUBDIR= 	cc_prep
SUBDIR+=	cc_tools
SUBDIR+=	support-libs
SUBDIR+=	drivers
SUBDIR+=	libbackend
SUBDIR+=	backends

.include <bsd.subdir.mk>
