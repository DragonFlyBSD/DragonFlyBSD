
#define ALPHA		(8)
#define SIZE		(1 << ALPHA)
#define MASK		(SIZE - 1)
#define ind(x)		((x) & (SIZE - 1))
#define barrel(a)	(((a) << 20) ^ ((a) >> 12))  /* beta=32,shift=20 */

struct ibaa_state {
	uint32_t	IBAA_memory[SIZE];
	uint32_t	IBAA_results[SIZE];
	uint32_t	IBAA_aa;
	uint32_t	IBAA_bb;
	uint32_t	IBAA_counter;
	int		IBAA_byte_index;
	int		memIndex;
};

#define L15_STATE_SIZE  256

struct l15_state {
	uint8_t		L15_x;
	uint8_t		L15_y;
	uint8_t		L15_start_x;
	uint8_t		L15_state[L15_STATE_SIZE];
	uint8_t		stateIndex;
};
