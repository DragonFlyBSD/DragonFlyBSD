/*-
 * Copyright (c) 1988, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Timothy C. Stoehr.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)rogue.h	8.1 (Berkeley) 5/31/93
 * $FreeBSD: src/games/rogue/rogue.h,v 1.3.2.1 2001/12/17 12:43:23 phantom Exp $
 */

#include <curses.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/*
 * rogue.h
 *
 * This source herein may be modified and/or distributed by anybody who
 * so desires, with the following restrictions:
 *    1.)  This notice shall not be removed.
 *    2.)  Credit shall not be taken for the creation of this source.
 *    3.)  This code is not to be traded, sold, or used for personal
 *         gain or profit.
 */

#define boolean bool

#define NOTHING		((unsigned short)     0)
#define OBJECT		((unsigned short)    01)
#define MONSTER		((unsigned short)    02)
#define STAIRS		((unsigned short)    04)
#define HORWALL		((unsigned short)   010)
#define VERTWALL	((unsigned short)   020)
#define DOOR		((unsigned short)   040)
#define FLOOR		((unsigned short)  0100)
#define TUNNEL		((unsigned short)  0200)
#define TRAP		((unsigned short)  0400)
#define HIDDEN		((unsigned short) 01000)

#define ARMOR		((unsigned short)   01)
#define WEAPON		((unsigned short)   02)
#define SCROL		((unsigned short)   04)
#define POTION		((unsigned short)  010)
#define GOLD		((unsigned short)  020)
#define FOOD		((unsigned short)  040)
#define WAND		((unsigned short) 0100)
#define RING		((unsigned short) 0200)
#define AMULET		((unsigned short) 0400)
#define ALL_OBJECTS	((unsigned short) 0777)

#define LEATHER 0
#define RINGMAIL 1
#define SCALE 2
#define CHAIN 3
#define BANDED 4
#define SPLINT 5
#define PLATE 6
#define ARMORS 7

#define BOW 0
#define DART 1
#define ARROW 2
#define DAGGER 3
#define SHURIKEN 4
#define MACE 5
#define LONG_SWORD 6
#define TWO_HANDED_SWORD 7
#define WEAPONS 8

#define MAX_PACK_COUNT 24

#define PROTECT_ARMOR 0
#define HOLD_MONSTER 1
#define ENCH_WEAPON 2
#define ENCH_ARMOR 3
#define IDENTIFY 4
#define TELEPORT 5
#define SLEEP 6
#define SCARE_MONSTER 7
#define REMOVE_CURSE 8
#define CREATE_MONSTER 9
#define AGGRAVATE_MONSTER 10
#define MAGIC_MAPPING 11
#define CON_MON 12
#define SCROLS 13

#define INCREASE_STRENGTH 0
#define RESTORE_STRENGTH 1
#define HEALING 2
#define EXTRA_HEALING 3
#define POISON 4
#define RAISE_LEVEL 5
#define BLINDNESS 6
#define HALLUCINATION 7
#define DETECT_MONSTER 8
#define DETECT_OBJECTS 9
#define CONFUSION 10
#define LEVITATION 11
#define HASTE_SELF 12
#define SEE_INVISIBLE 13
#define POTIONS 14

#define TELE_AWAY 0
#define SLOW_MONSTER 1
#define INVISIBILITY 2
#define POLYMORPH 3
#define HASTE_MONSTER 4
#define MAGIC_MISSILE 5
#define CANCELLATION 6
#define DO_NOTHING 7
#define DRAIN_LIFE 8
#define COLD 9
#define FIRE 10
#define WANDS 11

#define STEALTH 0
#define R_TELEPORT 1
#define REGENERATION 2
#define SLOW_DIGEST 3
#define ADD_STRENGTH 4
#define SUSTAIN_STRENGTH 5
#define DEXTERITY 6
#define ADORNMENT 7
#define R_SEE_INVISIBLE 8
#define MAINTAIN_ARMOR 9
#define SEARCHING 10
#define RINGS 11

#define RATION 0
#define FRUIT 1

#define NOT_USED	((unsigned short)   0)
#define BEING_WIELDED	((unsigned short)  01)
#define BEING_WORN	((unsigned short)  02)
#define ON_LEFT_HAND	((unsigned short)  04)
#define ON_RIGHT_HAND	((unsigned short) 010)
#define ON_EITHER_HAND	((unsigned short) 014)
#define BEING_USED	((unsigned short) 017)

#define NO_TRAP -1
#define TRAP_DOOR 0
#define BEAR_TRAP 1
#define TELE_TRAP 2
#define DART_TRAP 3
#define SLEEPING_GAS_TRAP 4
#define RUST_TRAP 5
#define TRAPS 6

#define STEALTH_FACTOR 3
#define R_TELE_PERCENT 8

#define UNIDENTIFIED	((unsigned short) 00)	/* MUST BE ZERO! */
#define IDENTIFIED	((unsigned short) 01)
#define CALLED		((unsigned short) 02)

#define DROWS 24
#define DCOLS 80
#define NMESSAGES 5
#define MAX_TITLE_LENGTH 30
#define MAXSYLLABLES 40
#define MAX_METAL 14
#define WAND_MATERIALS 30
#define GEMS 14

#define GOLD_PERCENT 46

#define MAX_OPT_LEN 40

#define HUNGER_STR_LEN 8

#define MAX_ID_TITLE_LEN 64

struct id {
	short value;
	char title[MAX_ID_TITLE_LEN];
	const char *real;
	unsigned short id_status;
};

/* The following #defines provide more meaningful names for some of the
 * struct object fields that are used for monsters.  This, since each monster
 * and object (scrolls, potions, etc) are represented by a struct object.
 * Ideally, this should be handled by some kind of union structure.
 */

#define m_damage damage
#define hp_to_kill quantity
#define m_char ichar
#define first_level is_protected
#define last_level is_cursed
#define m_hit_chance class
#define stationary_damage identified
#define drop_percent which_kind
#define trail_char d_enchant
#define slowed_toggle quiver
#define moves_confused hit_enchant
#define nap_length picked_up
#define disguise what_is
#define next_monster next_object

struct obj {				/* comment is monster meaning */
	unsigned long m_flags;	/* monster flags */
	const char *damage;		/* damage it does */
	short quantity;			/* hit points to kill */
	short ichar;			/* 'A' is for aquatar */
	short kill_exp;			/* exp for killing it */
	short is_protected;		/* level starts */
	short is_cursed;		/* level ends */
	short class;			/* chance of hitting you */
	short identified;		/* 'F' damage, 1,2,3... */
	unsigned short which_kind; /* item carry/drop % */
	short o_row, o_col, o;	/* o is how many times stuck at o_row, o_col */
	short row, col;			/* current row, col */
	short d_enchant;		/* room char when detect_monster */
	short quiver;			/* monster slowed toggle */
	short trow, tcol;		/* target row, col */
	short hit_enchant;		/* how many moves is confused */
	unsigned short what_is;	/* imitator's charactor (?!%: */
	short picked_up;		/* sleep from wand of sleep */
	unsigned short in_use_flags;
	struct obj *next_object;	/* next monster */
};

typedef struct obj object;

#define INIT_AW		NULL,NULL
#define INIT_RINGS	NULL,NULL
#define INIT_HP		12
#define INIT_STR	16,16
#define INIT_EXP	1,0
#define INIT_PACK	{0,"",0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,NULL}
#define INIT_GOLD	0
#define INIT_CHAR	'@'
#define INIT_MOVES	1250

struct fightr {
	object *armor;
	object *weapon;
	object *left_ring, *right_ring;
	short hp_current;
	short hp_max;
	short str_current;
	short str_max;
	object pack;
	long gold;
	short exp;
	long exp_points;
	short row, col;
	short fchar;
	short moves_left;
};

typedef struct fightr fighter;

struct dr {
	short oth_room;
	short oth_row,
	      oth_col;
	short door_row,
		  door_col;
};

typedef struct dr door;

struct rm {
	short bottom_row, right_col, left_col, top_row;
	door doors[4];
	unsigned short is_room;
};

typedef struct rm room;

#define MAXROOMS 9
#define BIG_ROOM 10

#define NO_ROOM (-1)

#define PASSAGE (-3)		/* cur_room value */

#define AMULET_LEVEL 26

#define R_NOTHING	((unsigned short) 01)
#define R_ROOM		((unsigned short) 02)
#define R_MAZE		((unsigned short) 04)
#define R_DEADEND	((unsigned short) 010)
#define R_CROSS		((unsigned short) 020)

#define MAX_EXP_LEVEL 21
#define MAX_EXP 10000001L
#define MAX_GOLD 999999
#define MAX_ARMOR 99
#define MAX_HP 999
#define MAX_STRENGTH 99
#define LAST_DUNGEON 99

#define STAT_LEVEL 01
#define STAT_GOLD 02
#define STAT_HP 04
#define STAT_STRENGTH 010
#define STAT_ARMOR 020
#define STAT_EXP 040
#define STAT_HUNGER 0100
#define STAT_LABEL 0200
#define STAT_ALL 0377

#define PARTY_TIME 10	/* one party somewhere in each 10 level span */

#define MAX_TRAPS 10	/* maximum traps per level */

#define HIDE_PERCENT 12

struct tr {
	short trap_type;
	short trap_row, trap_col;
};

typedef struct tr trap;

extern fighter rogue;
extern room rooms[];
extern trap traps[];
extern unsigned short dungeon[DROWS][DCOLS];
extern object level_objects;

extern struct id id_scrolls[];
extern struct id id_potions[];
extern struct id id_wands[];
extern struct id id_rings[];
extern struct id id_weapons[];
extern struct id id_armors[];

extern object mon_tab[];
extern object level_monsters;

#define MONSTERS 26

#define HASTED					01L
#define SLOWED					02L
#define INVISIBLE				04L
#define ASLEEP				   010L
#define WAKENS				   020L
#define WANDERS				   040L
#define FLIES				  0100L
#define FLITS				  0200L
#define CAN_FLIT			  0400L		/* can, but usually doesn't, flit */
#define CONFUSED	 		 01000L
#define RUSTS				 02000L
#define HOLDS				 04000L
#define FREEZES				010000L
#define STEALS_GOLD			020000L
#define STEALS_ITEM			040000L
#define STINGS			   0100000L
#define DRAINS_LIFE		   0200000L
#define DROPS_LEVEL		   0400000L
#define SEEKS_GOLD		  01000000L
#define FREEZING_ROGUE	  02000000L
#define RUST_VANISHED	  04000000L
#define CONFUSES		 010000000L
#define IMITATES		 020000000L
#define FLAMES			 040000000L
#define STATIONARY		0100000000L		/* damage will be 1,2,3,... */
#define NAPPING			0200000000L		/* can't wake up for a while */
#define ALREADY_MOVED	0400000000L

#define SPECIAL_HIT		(RUSTS|HOLDS|FREEZES|STEALS_GOLD|STEALS_ITEM|STINGS|DRAINS_LIFE|DROPS_LEVEL)

#define WAKE_PERCENT 45
#define FLIT_PERCENT 40
#define PARTY_WAKE_PERCENT 75

#define HYPOTHERMIA 1
#define STARVATION 2
#define POISON_DART 3
#define QUIT 4
#define WIN 5
#define KFIRE 6

#define UPWARD 0
#define UPRIGHT 1
#define RIGHT 2
#define DOWNRIGHT 3
#define DOWN 4
#define DOWNLEFT 5
#define LEFT 6
#define UPLEFT 7
#define DIRS 8

#define ROW1 7
#define ROW2 15

#define COL1 26
#define COL2 52

#define MOVED 0
#define MOVE_FAILED -1
#define STOPPED_ON_SOMETHING -2
#define CANCEL '\033'
#define LIST '*'

#define HUNGRY 300
#define WEAK 150
#define FAINT 20
#define STARVE 0

#define MIN_ROW 1

struct rogue_time {
	short year;		/* >= 1987 */
	short month;	/* 1 - 12 */
	short day;		/* 1 - 31 */
	short hour;		/* 0 - 23 */
	short minute;	/* 0 - 59 */
	short second;	/* 0 - 59 */
};

/* external routine declarations.
 */
#define rrandom random

/* hit.c */
void	mon_hit(object *);
void	rogue_hit(object *, boolean);
void	rogue_damage(short, object *, short);
int	get_damage(const char *, boolean);
int	get_number(const char *);
long	lget_number(const char *);
boolean	mon_damage(object *, short);
void	fight(boolean);
void	get_dir_rc(short, short *, short *, short);
short	get_hit_chance(const object *);
short	get_weapon_damage(const object *);
void	s_con_mon(object *);

/* init.c */
boolean	init(int, char**);
void	clean_up(const char *);
void	start_window(void);
void	stop_window(void);
void	byebye(int);
void	onintr(int);
void	error_save(int);

/* inventory.c */
void	inventory(const object *, unsigned short);
void	id_com(void);
void	mix_colors(void);
void	make_scroll_titles(void);
void	get_desc(const object *, char *);
void	get_wand_and_ring_materials(void);
void	single_inv(short);
struct id	*get_id_table(const object *);
void	inv_armor_weapon(boolean);
void	id_type(void);

/* level.c */
void	make_level(void);
void	clear_level(void);
void	put_player(short);
boolean	drop_check(void);
boolean	check_up(void);
void	add_exp(int, boolean);
int	hp_raise(void);
void	show_average_hp(void);

/* machdep.c */
#ifdef UNIX
void	md_slurp(void);
void	md_control_keybord(boolean);
void	md_heed_signals(void);
void	md_ignore_signals(void);
int	md_get_file_id(const char *);
int	md_link_count(const char *);
void	md_gct(struct rogue_time *);
void	md_gfmt(const char *, struct rogue_time *);
boolean	md_df(const char *);
const char	*md_gln(void);
void	md_sleep(int);
char	*md_getenv(const char *);
char	*md_malloc(int);
void	md_exit(int);
void	md_lock(boolean);
void	md_shell(const char *);
#endif

/* message.c */
void	message(const char *, boolean);
void	remessage(short);
void	check_message(void);
short	get_input_line(const char *, const char *, char *,
		       const char *, boolean, boolean);
int	rgetchar(void);
void	print_stats(int);
void	sound_bell(void);
boolean	is_digit(short);
int	r_index(const char *, int, boolean);

/* monster.c */
void	put_mons(void);
object	*gr_monster(object *, int);
void	mv_mons(void);
void	party_monsters(int, int);
short	gmc_row_col(int, int);
short	gmc(object *);
void	mv_1_monster(object *, short, short);
void	move_mon_to(object *, short, short);
boolean	mon_can_go(const object *, short, short);
void	wake_up(object *);
void	wake_room(short, boolean, short, short);
const char	*mon_name(const object *);
void	wanderer(void);
void	show_monsters(void);
void	create_monster(void);
int	rogue_can_see(int, int);
char	gr_obj_char(void);
void	aggravate(void);
boolean	mon_sees(const object *, int, int);
void	mv_aquatars(void);

/* move.c */
short	one_move_rogue(short, short);
void	multiple_move_rogue(short);
boolean	is_passable(int, int);
boolean	can_move(short, short, short, short);
void	move_onto(void);
boolean	is_direction(short, short *);
boolean	reg_move(void);
void	rest(int);

/* object.c */
void	put_objects(void);
void	place_at(object *, int, int);
object	*object_at(object *, short, short);
object	*get_letter_object(int);
void	free_stuff(object *);
const char	*name_of(const object *);
object	*gr_object(void);
void	get_food(object *, boolean);
void	put_stairs(void);
short	get_armor_class(const object *);
object	*alloc_object(void);
void	free_object(object *);
void	show_objects(void);
void	put_amulet(void);
void	c_object_for_wizard(void);

/* pack.c */
object	*add_to_pack(object *, object *, int);
void	take_from_pack(object *, object *);
object	*pick_up(int, int, short *);
void	drop(void);
void	wait_for_ack(void);
short	pack_letter(const char *, unsigned short);
void	take_off(void);
void	wear(void);
void	unwear(object *);
void	do_wear(object *);
void	wield(void);
void	do_wield(object *);
void	unwield(object *);
void	call_it(void);
short	pack_count(const object *);
boolean	has_amulet(void);
void	kick_into_pack(void);

/* play.c */
void	play_level(void);

/* random.c */
int	get_rand(int, int);
boolean	rand_percent(int);
boolean	coin_toss(void);

/* ring.c */
void	put_on_ring(void);
void	do_put_on(object *, boolean);
void	remove_ring(void);
void	un_put_on(object *);
void	gr_ring(object *, boolean);
void	inv_rings(void);
void	ring_stats(boolean);

/* room.c */
void	light_up_room(int);
void	light_passage(int, int);
void	darken_room(short);
char	get_dungeon_char(int, int);
char	get_mask_char(unsigned short);
void	gr_row_col(short *, short *, unsigned short);
short	gr_room(void);
short	party_objects(short);
short	get_room_number(int, int);
boolean	is_all_connected(void);
void	draw_magic_map(void);
void	dr_course(object *, boolean, short, short);
void	edit_opts(void);
void	do_shell(void);

/* save.c */
void	save_game(void);
void	save_into_file(const char *);
void	restore(const char *);

/* score.c */
void	killed_by(const object *, short);
void	win(void);
void	quit(boolean);
void	put_scores(const object *, short);
boolean	is_vowel(short);
void	xxxx(char *, short);
long	xxx(boolean);

/* spec_hit.c */
void	special_hit(object *);
void	rust(object *);
void	cough_up(object *);
boolean	seek_gold(object *);
void	check_gold_seeker(object *);
boolean	check_imitator(object *);
boolean	imitating(short, short);
boolean	m_confuse(object *);
boolean	flame_broil(object *);

/* throw.c */
void	throw(void);
void	rand_around(short, short *, short *);

/* trap.c */
void	trap_player(short, short);
void	add_traps(void);
void	id_trap(void);
void	show_traps(void);
void	search(short, boolean);

/* use.c */
void	quaff(void);
void	read_scroll(void);
void	vanish(object *, short, object *);
void	eat(void);
void	tele(void);
void	hallucinate(void);
void	unhallucinate(void);
void	unblind(void);
void	relight(void);
void	take_a_nap(void);
void	cnfs(void);
void	unconfuse(void);

/* zap.c */
void	zapp(void);
void	wizardize(void);
void	bounce(short, short, short, short, short);

/*
 * external variable declarations.
 */
extern  char    hunger_str[HUNGER_STR_LEN];
extern  char    login_name[MAX_OPT_LEN];
extern  const char   *error_file;
