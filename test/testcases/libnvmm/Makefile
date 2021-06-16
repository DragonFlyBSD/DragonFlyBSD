SUBDIR=	h_io_assist h_mem_assist

.include <bsd.subdir.mk>

test:
	kldload -n nvmm
	dfregress dfregress.run
	dfr2text dfregress.plist
