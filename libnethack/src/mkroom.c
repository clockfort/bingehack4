/* vim:set cin ft=c sw=4 sts=4 ts=8 et ai cino=Ls\:0t0(0 : -*- mode:c;fill-column:80;tab-width:8;c-basic-offset:4;indent-tabs-mode:nil;c-file-style:"k&r" -*-*/
/* Copyright (c) Stichting Mathematisch Centrum, Amsterdam, 1985. */
/* NetHack may be freely redistributed.  See license for details. */

/*
 * Entry points:
 *      mkroom() -- make and stock a room of a given type
 *      nexttodoor() -- return TRUE if adjacent to a door
 *      has_dnstairs() -- return TRUE if given room has a down staircase
 *      has_upstairs() -- return TRUE if given room has an up staircase
 *      courtmon() -- generate a court monster
 *      save_rooms() -- save level->rooms into file fd
 *      rest_rooms() -- restore level->rooms from file fd
 */

#include "hack.h"


static boolean isbig(struct mkroom *);
static struct mkroom *pick_room(struct level *lev, boolean strict);
static void mkshop(struct level *lev);
static void mkzoo(struct level *lev, int type);
static void mkswamp(struct level *lev);
static void mktemple(struct level *lev);
static coord *shrine_pos(struct level *lev, int roomno);
static const struct permonst *morguemon(const d_level * dlev);
static const struct permonst *squadmon(const d_level * dlev);
static void save_room(struct memfile *mf, struct mkroom *);
static void rest_room(struct memfile *mf, struct level *lev, struct mkroom *r);
static boolean has_dnstairs(struct level *lev, struct mkroom *);
static boolean has_upstairs(struct level *lev, struct mkroom *);

#define sq(x) ((x)*(x))

extern const struct shclass shtypes[];  /* defined in shknam.c */


static boolean
isbig(struct mkroom *sroom)
{
    int area = (sroom->hx - sroom->lx + 1)
        * (sroom->hy - sroom->ly + 1);

    return (boolean) (area > 20);
}

/* make and stock a room of a given type */
void
mkroom(struct level *lev, int roomtype)
{
    if (roomtype >= SHOPBASE)
        mkshop(lev);    /* someday, we should be able to specify shop type */
    else
        switch (roomtype) {
        case COURT:
            mkzoo(lev, COURT);
            break;
        case ZOO:
            mkzoo(lev, ZOO);
            break;
        case BEEHIVE:
            mkzoo(lev, BEEHIVE);
            break;
        case MORGUE:
            mkzoo(lev, MORGUE);
            break;
        case BARRACKS:
            mkzoo(lev, BARRACKS);
            break;
        case SWAMP:
            mkswamp(lev);
            break;
        case TEMPLE:
            mktemple(lev);
            break;
        case LEPREHALL:
            mkzoo(lev, LEPREHALL);
            break;
        case COCKNEST:
            mkzoo(lev, COCKNEST);
            break;
        case ANTHOLE:
            mkzoo(lev, ANTHOLE);
            break;
        default:
            impossible("Tried to make a room of type %d.", roomtype);
        }
}


static void
mkshop(struct level *lev)
{
    struct mkroom *sroom;
    int styp, j;

    /* first determine shoptype */
    for (sroom = &lev->rooms[0];; sroom++) {
        if (sroom->hx < 0)
            return;
        if (sroom - lev->rooms >= lev->nroom) {
            pline("lev->rooms not closed by -1?");
            return;
        }
        if (sroom->rtype != OROOM)
            continue;
        if (has_dnstairs(lev, sroom) || has_upstairs(lev, sroom))
            continue;
        if (sroom->doorct == 1)
            break;
    }
    if (!sroom->rlit) {
        int x, y;

        for (x = sroom->lx - 1; x <= sroom->hx + 1; x++)
            for (y = sroom->ly - 1; y <= sroom->hy + 1; y++)
                lev->locations[x][y].lit = 1;
        sroom->rlit = 1;
    }

    /* pick a shop type at random */
    j = rnd(100);
    for (styp = 0; (j -= shtypes[styp].prob) > 0; styp++)
        continue;

    /* big rooms cannot be wand or book shops, * - so make them general stores */
    if (isbig(sroom) &&
        (shtypes[styp].symb == WAND_CLASS ||
         shtypes[styp].symb == SPBOOK_CLASS))
        styp = 0;

    sroom->rtype = SHOPBASE + styp;

    /* set room bits before stocking the shop */
    topologize(lev, sroom);

    /* stock the room with a shopkeeper and artifacts */
    stock_room(styp, lev, sroom);
}


/* pick an unused room, preferably with only one door */
static struct mkroom *
pick_room(struct level *lev, boolean strict)
{
    struct mkroom *sroom;
    int i = lev->nroom;

    for (sroom = &lev->rooms[rn2(lev->nroom)]; i--; sroom++) {
        if (sroom == &lev->rooms[lev->nroom])
            sroom = &lev->rooms[0];
        if (sroom->hx < 0)
            return NULL;
        if (sroom->rtype != OROOM)
            continue;
        if (!strict) {
            if (has_upstairs(lev, sroom) ||
                (has_dnstairs(lev, sroom) && rn2(3)))
                continue;
        } else if (has_upstairs(lev, sroom) || has_dnstairs(lev, sroom))
            continue;
        if (sroom->doorct == 1 || !rn2(5) || wizard)
            return sroom;
    }
    return NULL;
}

static void
mkzoo(struct level *lev, int type)
{
    struct mkroom *sroom;

    if ((sroom = pick_room(lev, FALSE)) != 0) {
        sroom->rtype = type;
        fill_zoo(lev, sroom);
    }
}

void
fill_zoo(struct level *lev, struct mkroom *sroom)
{
    struct monst *mon;
    int sx, sy, i;
    int sh, tx, ty, goldlim, type = sroom->rtype;
    int rmno = (sroom - lev->rooms) + ROOMOFFSET;
    coord mm;

    tx = ty = goldlim = 0;

    sh = sroom->fdoor;
    switch (type) {
    case COURT:
        if (lev->flags.is_maze_lev) {
            for (tx = sroom->lx; tx <= sroom->hx; tx++)
                for (ty = sroom->ly; ty <= sroom->hy; ty++)
                    if (IS_THRONE(lev->locations[tx][ty].typ))
                        goto throne_placed;
        }
        i = 100;
        do {    /* don't place throne on top of stairs */
            somexy(lev, sroom, &mm);
            tx = mm.x;
            ty = mm.y;
        } while (occupied(lev, tx, ty) && --i > 0);
    throne_placed:
        /* TODO: try to ensure the enthroned monster is an M2_PRINCE */
        break;
    case BEEHIVE:
        tx = sroom->lx + (sroom->hx - sroom->lx + 1) / 2;
        ty = sroom->ly + (sroom->hy - sroom->ly + 1) / 2;
        if (sroom->irregular) {
            /* center might not be valid, so put queen elsewhere */
            if ((int)lev->locations[tx][ty].roomno != rmno ||
                lev->locations[tx][ty].edge) {
                somexy(lev, sroom, &mm);
                tx = mm.x;
                ty = mm.y;
            }
        }
        break;
    case ZOO:
    case LEPREHALL:
        goldlim = 500 * level_difficulty(&lev->z);
        break;
    }
    for (sx = sroom->lx; sx <= sroom->hx; sx++)
        for (sy = sroom->ly; sy <= sroom->hy; sy++) {
            if (sroom->irregular) {
                if ((int)lev->locations[sx][sy].roomno != rmno ||
                    lev->locations[sx][sy].edge ||
                    (sroom->doorct &&
                     distmin(sx, sy, lev->doors[sh].x, lev->doors[sh].y) <= 1))
                    continue;
            } else if (!SPACE_POS(lev->locations[sx][sy].typ) ||
                       (sroom->doorct &&
                        ((sx == sroom->lx && lev->doors[sh].x == sx - 1) ||
                         (sx == sroom->hx && lev->doors[sh].x == sx + 1) ||
                         (sy == sroom->ly && lev->doors[sh].y == sy - 1) ||
                         (sy == sroom->hy && lev->doors[sh].y == sy + 1))))
                continue;
            /* don't place monster on explicitly placed throne */
            if (type == COURT && IS_THRONE(lev->locations[sx][sy].typ))
                continue;
            mon =
                makemon((type == COURT) ? courtmon(&lev->z) :
                        (type == BARRACKS) ? squadmon(&lev->z) :
                        (type == MORGUE) ? morguemon(&lev->z) :
                        (type == BEEHIVE) ? (sx == tx && sy == ty ?
                                             &mons[PM_QUEEN_BEE] :
                                             &mons[PM_KILLER_BEE]) :
                        (type == LEPREHALL) ? &mons[PM_LEPRECHAUN] :
                        (type == COCKNEST) ? &mons[PM_COCKATRICE] :
                        (type == ANTHOLE) ? antholemon(&lev->z) :
                        NULL, lev, sx, sy, NO_MM_FLAGS);
            if (mon) {
                mon->msleeping = 1;
                if (type == COURT && mon->mpeaceful) {
                    mon->mpeaceful = 0;
                    set_malign(mon);
                }
            }
            switch (type) {
            case ZOO:
            case LEPREHALL:
                if (sroom->doorct) {
                    int distval =
                        dist2(sx, sy, lev->doors[sh].x, lev->doors[sh].y);
                    i = sq(distval);
                } else
                    i = goldlim;
                if (i >= goldlim)
                    i = 5 * level_difficulty(&lev->z);
                goldlim -= i;
                mkgold((long)rn1(i, 10), lev, sx, sy);
                break;
            case MORGUE:
                if (!rn2(5))
                    mk_tt_object(lev, CORPSE, sx, sy);
                if (!rn2(10))   /* lots of treasure buried with dead */
                    mksobj_at((rn2(3)) ? LARGE_BOX : CHEST, lev, sx, sy, TRUE,
                              FALSE);
                if (!rn2(5))
                    make_grave(lev, sx, sy, NULL);
                break;
            case BEEHIVE:
                if (!rn2(3))
                    mksobj_at(LUMP_OF_ROYAL_JELLY, lev, sx, sy, TRUE, FALSE);
                break;
            case BARRACKS:
                if (!rn2(20))   /* the payroll and some loot */
                    mksobj_at((rn2(3)) ? LARGE_BOX : CHEST, lev, sx, sy, TRUE,
                              FALSE);
                break;
            case COCKNEST:
                if (!rn2(3)) {
                    struct obj *sobj = mk_tt_object(lev, STATUE, sx, sy);

                    if (sobj) {
                        for (i = rn2(5); i; i--)
                            add_to_container(sobj,
                                             mkobj(lev, RANDOM_CLASS, FALSE));
                        sobj->owt = weight(sobj);
                    }
                }
                break;
            case ANTHOLE:
                if (!rn2(3))
                    mkobj_at(FOOD_CLASS, lev, sx, sy, FALSE);
                break;
            }
        }
    switch (type) {
    case COURT:
        {
            struct obj *chest;

            lev->locations[tx][ty].typ = THRONE;
            somexy(lev, sroom, &mm);
            mkgold((long)rn1(50 * level_difficulty(&lev->z), 10), lev, mm.x,
                   mm.y);
            /* the royal coffers */
            chest = mksobj_at(CHEST, lev, mm.x, mm.y, TRUE, FALSE);
            chest->spe = 2;     /* so it can be found later */
            lev->flags.has_court = 1;
            break;
        }
    case BARRACKS:
        lev->flags.has_barracks = 1;
        break;
    case ZOO:
        lev->flags.has_zoo = 1;
        break;
    case MORGUE:
        lev->flags.has_morgue = 1;
        break;
    case SWAMP:
        lev->flags.has_swamp = 1;
        break;
    case BEEHIVE:
        lev->flags.has_beehive = 1;
        break;
    }
}

/* make a swarm of undead around mm */
void
mkundead(struct level *lev, coord * mm, boolean revive_corpses, int mm_flags)
{
    int cnt = (level_difficulty(&lev->z) + 1) / 10 + rnd(5);
    const struct permonst *mdat;
    struct obj *otmp;
    coord cc;

    while (cnt--) {
        mdat = morguemon(&lev->z);
        if (enexto(&cc, lev, mm->x, mm->y, mdat) &&
            (!revive_corpses || !(otmp = sobj_at(CORPSE, lev, cc.x, cc.y)) ||
             !revive(otmp)))
            makemon(mdat, lev, cc.x, cc.y, mm_flags);
    }
    lev->flags.graveyard = TRUE;        /* reduced chance for undead corpse */
}

static const struct permonst *
morguemon(const d_level * dlev)
{
    int i = rn2(100), hd = rn2(level_difficulty(dlev));

    if (hd > 10 && i < 10)
        return (In_hell(dlev) ||
                In_endgame(dlev)) ? mkclass(dlev, S_DEMON,
                                            0) : &mons[ndemon(dlev, A_NONE)];
    if (hd > 8 && i > 85)
        return mkclass(dlev, S_VAMPIRE, 0);

    return (i < 20) ? &mons[PM_GHOST]
        : (i < 40) ? &mons[PM_WRAITH] : mkclass(dlev, S_ZOMBIE, 0);
}

const struct permonst *
antholemon(const d_level * dlev)
{
    int mtyp;

    /* Same monsters within a level, different ones between levels */
    switch ((level_difficulty(dlev) + ((long)u.ubirthday)) % 3) {
    default:
        mtyp = PM_GIANT_ANT;
        break;
    case 0:
        mtyp = PM_SOLDIER_ANT;
        break;
    case 1:
        mtyp = PM_FIRE_ANT;
        break;
    }
    return (mvitals[mtyp].mvflags & G_GONE) ? NULL : &mons[mtyp];
}

static void
mkswamp(struct level *lev)
{
    struct mkroom *sroom;
    int sx, sy, i, eelct = 0;

    for (i = 0; i < 5; i++) {   /* turn up to 5 rooms swampy */
        sroom = &lev->rooms[rn2(lev->nroom)];
        if (sroom->hx < 0 || sroom->rtype != OROOM || has_upstairs(lev, sroom)
            || has_dnstairs(lev, sroom))
            continue;

        /* satisfied; make a swamp */
        sroom->rtype = SWAMP;
        for (sx = sroom->lx; sx <= sroom->hx; sx++)
            for (sy = sroom->ly; sy <= sroom->hy; sy++)
                if (!OBJ_AT_LEV(lev, sx, sy) && !MON_AT(lev, sx, sy) &&
                    !t_at(lev, sx, sy) && !nexttodoor(lev, sx, sy)) {
                    if ((sx + sy) % 2) {
                        lev->locations[sx][sy].typ = POOL;
                        if (!eelct || !rn2(4)) {
                            /* mkclass() won't do, as we might get kraken */
                            makemon(rn2(5) ? &mons[PM_GIANT_EEL]
                                    : rn2(2) ? &mons[PM_PIRANHA]
                                    : &mons[PM_ELECTRIC_EEL], lev, sx, sy,
                                    NO_MM_FLAGS);
                            eelct++;
                        }
                    } else if (!rn2(4)) /* swamps tend to be moldy */
                        makemon(mkclass(&lev->z, S_FUNGUS, 0), lev, sx, sy,
                                NO_MM_FLAGS);
                }
        lev->flags.has_swamp = 1;
    }
}

static coord *
shrine_pos(struct level *lev, int roomno)
{
    static coord buf;
    struct mkroom *troom = &lev->rooms[roomno - ROOMOFFSET];

    buf.x = troom->lx + ((troom->hx - troom->lx) / 2);
    buf.y = troom->ly + ((troom->hy - troom->ly) / 2);
    return &buf;
}

static void
mktemple(struct level *lev)
{
    struct mkroom *sroom;
    coord *shrine_spot;
    struct rm *loc;

    if (!(sroom = pick_room(lev, TRUE)))
        return;

    /* set up Priest and shrine */
    sroom->rtype = TEMPLE;
    /* 
     * In temples, shrines are blessed altars
     * located in the center of the room
     */
    shrine_spot = shrine_pos(lev, (sroom - lev->rooms) + ROOMOFFSET);
    loc = &lev->locations[shrine_spot->x][shrine_spot->y];
    loc->typ = ALTAR;
    loc->altarmask = induced_align(&lev->z, 80);
    priestini(lev, sroom, shrine_spot->x, shrine_spot->y, FALSE);
    loc->altarmask |= AM_SHRINE;
    lev->flags.has_temple = 1;
}

boolean
nexttodoor(struct level * lev, int sx, int sy)
{
    int dx, dy;
    struct rm *loc;

    for (dx = -1; dx <= 1; dx++)
        for (dy = -1; dy <= 1; dy++) {
            if (!isok(sx + dx, sy + dy))
                continue;
            if (IS_DOOR((loc = &lev->locations[sx + dx][sy + dy])->typ) ||
                loc->typ == SDOOR)
                return TRUE;
        }
    return FALSE;
}

boolean
has_dnstairs(struct level * lev, struct mkroom * sroom)
{
    if (sroom == lev->dnstairs_room)
        return TRUE;
    if (lev->sstairs.sx && !lev->sstairs.up)
        return (boolean) (sroom == lev->sstairs_room);
    return FALSE;
}

boolean
has_upstairs(struct level * lev, struct mkroom * sroom)
{
    if (sroom == lev->upstairs_room)
        return TRUE;
    if (lev->sstairs.sx && lev->sstairs.up)
        return (boolean) (sroom == lev->sstairs_room);
    return FALSE;
}


int
somex(struct mkroom *croom)
{
    return rn2(croom->hx - croom->lx + 1) + croom->lx;
}

int
somey(struct mkroom *croom)
{
    return rn2(croom->hy - croom->ly + 1) + croom->ly;
}

boolean
inside_room(struct mkroom * croom, xchar x, xchar y)
{
    return ((boolean)
            (x >= croom->lx - 1 && x <= croom->hx + 1 && y >= croom->ly - 1 &&
             y <= croom->hy + 1));
}

boolean
somexy(struct level * lev, struct mkroom * croom, coord * c)
{
    int try_cnt = 0;
    int i;

    if (croom->irregular) {
        i = (croom - lev->rooms) + ROOMOFFSET;

        while (try_cnt++ < 100) {
            c->x = somex(croom);
            c->y = somey(croom);
            if (!lev->locations[c->x][c->y].edge &&
                (int)lev->locations[c->x][c->y].roomno == i)
                return TRUE;
        }
        /* try harder; exhaustively search until one is found */
        for (c->x = croom->lx; c->x <= croom->hx; c->x++)
            for (c->y = croom->ly; c->y <= croom->hy; c->y++)
                if (!lev->locations[c->x][c->y].edge &&
                    (int)lev->locations[c->x][c->y].roomno == i)
                    return TRUE;
        return FALSE;
    }

    if (!croom->nsubrooms) {
        c->x = somex(croom);
        c->y = somey(croom);
        return TRUE;
    }

    /* Check that coords doesn't fall into a subroom or into a wall */

    while (try_cnt++ < 100) {
        c->x = somex(croom);
        c->y = somey(croom);
        if (IS_WALL(lev->locations[c->x][c->y].typ))
            continue;
        for (i = 0; i < croom->nsubrooms; i++)
            if (inside_room(croom->sbrooms[i], c->x, c->y))
                goto you_lose;
        break;
    you_lose:;
    }
    if (try_cnt >= 100)
        return FALSE;
    return TRUE;
}

/*
 * Search for a special room given its type (zoo, court, etc...)
 * Special values :
 *     - ANY_SHOP
 *     - ANY_TYPE
 */

struct mkroom *
search_special(struct level *lev, schar type)
{
    struct mkroom *croom;

    for (croom = &lev->rooms[0]; croom->hx >= 0; croom++)
        if ((type == ANY_TYPE && croom->rtype != OROOM) ||
            (type == ANY_SHOP && croom->rtype >= SHOPBASE) ||
            croom->rtype == type)
            return croom;
    for (croom = &lev->subrooms[0]; croom->hx >= 0; croom++)
        if ((type == ANY_TYPE && croom->rtype != OROOM) ||
            (type == ANY_SHOP && croom->rtype >= SHOPBASE) ||
            croom->rtype == type)
            return croom;
    return NULL;
}


const struct permonst *
courtmon(const d_level * dlev)
{
    int i = rn2(60) + rn2(3 * level_difficulty(dlev));

    if (i > 100)
        return mkclass(dlev, S_DRAGON, 0);
    else if (i > 95)
        return mkclass(dlev, S_GIANT, 0);
    else if (i > 85)
        return mkclass(dlev, S_TROLL, 0);
    else if (i > 75)
        return mkclass(dlev, S_CENTAUR, 0);
    else if (i > 60)
        return mkclass(dlev, S_ORC, 0);
    else if (i > 45)
        return &mons[PM_BUGBEAR];
    else if (i > 30)
        return &mons[PM_HOBGOBLIN];
    else if (i > 15)
        return mkclass(dlev, S_GNOME, 0);
    else
        return mkclass(dlev, S_KOBOLD, 0);
}

#define NSTYPES (PM_CAPTAIN - PM_SOLDIER + 1)

static const struct {
    unsigned pm;
    unsigned prob;
} squadprob[NSTYPES] = {
    {
    PM_SOLDIER, 80}, {
    PM_SERGEANT, 15}, {
    PM_LIEUTENANT, 4}, {
    PM_CAPTAIN, 1}
};

static const struct permonst *
squadmon(const d_level * dlev)
{       /* return soldier types. */
    int sel_prob, i, cpro, mndx;

    sel_prob = rnd(80 + level_difficulty(dlev));

    cpro = 0;
    for (i = 0; i < NSTYPES; i++) {
        cpro += squadprob[i].prob;
        if (cpro > sel_prob) {
            mndx = squadprob[i].pm;
            goto gotone;
        }
    }
    mndx = squadprob[rn2(NSTYPES)].pm;
gotone:
    if (!(mvitals[mndx].mvflags & G_GONE))
        return &mons[mndx];
    else
        return NULL;
}

/*
 * save_room : A recursive function that saves a room and its subrooms
 * (if any).
 */
static void
save_room(struct memfile *mf, struct mkroom *r)
{
    short i;

    /* no tag; we tag room saving once per level, because the rooms don't
       change in number once the level is created */
    mwrite8(mf, r->lx);
    mwrite8(mf, r->hx);
    mwrite8(mf, r->ly);
    mwrite8(mf, r->hy);
    mwrite8(mf, r->rtype);
    mwrite8(mf, r->rlit);
    mwrite8(mf, r->doorct);
    mwrite8(mf, r->fdoor);
    mwrite8(mf, r->nsubrooms);
    mwrite8(mf, r->irregular);

    for (i = 0; i < r->nsubrooms; i++)
        save_room(mf, r->sbrooms[i]);
}

/*
 * save_rooms : Save all the rooms on disk!
 */
void
save_rooms(struct memfile *mf, struct level *lev)
{
    short i;

    mtag(mf, ledger_no(&lev->z), MTAG_ROOMS);
    mfmagic_set(mf, ROOMS_MAGIC);       /* "RDAT" */
    /* First, write the number of rooms */
    mwrite32(mf, lev->nroom);
    for (i = 0; i < lev->nroom; i++)
        save_room(mf, &lev->rooms[i]);
}

static void
rest_room(struct memfile *mf, struct level *lev, struct mkroom *r)
{
    short i;

    r->lx = mread8(mf);
    r->hx = mread8(mf);
    r->ly = mread8(mf);
    r->hy = mread8(mf);
    r->rtype = mread8(mf);
    r->rlit = mread8(mf);
    r->doorct = mread8(mf);
    r->fdoor = mread8(mf);
    r->nsubrooms = mread8(mf);
    r->irregular = mread8(mf);

    for (i = 0; i < r->nsubrooms; i++) {
        r->sbrooms[i] = &lev->subrooms[lev->nsubroom];
        rest_room(mf, lev, &lev->subrooms[lev->nsubroom]);
        lev->subrooms[lev->nsubroom++].resident = NULL;
    }
}

/*
 * rest_rooms : That's for restoring rooms. Read the rooms structure from
 * the disk.
 */
void
rest_rooms(struct memfile *mf, struct level *lev)
{
    short i;

    mfmagic_check(mf, ROOMS_MAGIC);     /* "RDAT" */
    lev->nroom = mread32(mf);
    lev->nsubroom = 0;
    for (i = 0; i < lev->nroom; i++) {
        rest_room(mf, lev, &lev->rooms[i]);
        lev->rooms[i].resident = NULL;
    }
    lev->rooms[lev->nroom].hx = -1;     /* restore ending flags */
    lev->subrooms[lev->nsubroom].hx = -1;
}

/*mkroom.c*/
