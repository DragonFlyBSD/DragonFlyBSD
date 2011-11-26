#ifndef _SYS_SERIALIZE2_H_
#define _SYS_SERIALIZE2_H_

#ifndef _KERNEL
#error "kernel only header file"
#endif

#ifndef _SYS_PARAM_H_
#include <sys/param.h>
#endif

#ifndef _SYS_SYSTM_H_
#include <sys/systm.h>
#endif

#ifndef _SYS_SERIALIZE_H_
#include <sys/serialize.h>
#endif

static __inline void
lwkt_serialize_array_enter(lwkt_serialize_t *_arr, int _arrcnt, int _s)
{
	KASSERT(_s < _arrcnt, ("nothing to be serialized\n"));
	while (_s < _arrcnt)
		lwkt_serialize_enter(_arr[_s++]);
}

static __inline int
lwkt_serialize_array_try(lwkt_serialize_t *_arr, int _arrcnt, int _s)
{
	int _i;

	KASSERT(_s < _arrcnt, ("nothing to be serialized\n"));
	for (_i = _s; _i < _arrcnt; ++_i) {
		if (!lwkt_serialize_try(_arr[_i])) {
			while (--_i >= _s)
				lwkt_serialize_exit(_arr[_i]);
			return 0;
		}
	}
	return 1;
}

static __inline void
lwkt_serialize_array_exit(lwkt_serialize_t *_arr, int _arrcnt, int _s)
{
	KASSERT(_arrcnt > _s, ("nothing to be deserialized\n"));
	while (--_arrcnt >= _s)
		lwkt_serialize_exit(_arr[_arrcnt]);
}

#endif	/* !_SYS_SERIALIZE2_H_ */
