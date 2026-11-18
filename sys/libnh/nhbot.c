/* NetHack 5.0  nhbot.c */
/* NetHack may be freely redistributed.  See license for details. */
/* Written for nethack-web by Kevin Tam from 2026-11-18; the history of the "bot" branch of glorat/NetHack gives each change and its date. */

/* Bot builds only (NHWEB_BOT): never the client's.  A bot runner or replay
   metric reads what the hero needn't know straight from the core's memory,
   through nhbot.js, by the names listed here: the core's globals and the
   struct fields reached from them.  Reading something new means listing it
   below.  Nothing here changes the game. */

#include "hack.h"
#include <emscripten/emscripten.h>

/* Struct fields: F an integer, B a bitfield, P a pointer, to a listed struct
   when it names one.  A field inside a member is named with its path, as
   uz.dnum. */
#define NHBOT_STRUCTS \
    S(you, F(ux) F(uy) F(uz.dnum) F(uz.dlevel) F(uluck) F(moreluck)          \
           P(usteed, monst))                                                 \
    S(monst, P(nmon, monst) P(data, permonst) F(m_id) F(mx) F(my) F(mhp)     \
             F(mhpmax) B(minvis) B(mundetected))                             \
    S(permonst, F(pmidx) F(mlet))                                            \
    S(obj, P(nobj, obj) F(otyp) B(cursed) B(blessed))                        \
    S(rm, F(typ))                                                            \
    S(stairway, P(next, stairway) F(sx) F(sy) F(up))                         \
    S(dungeon, F(ledger_start))                                              \
    S(sinfo, F(in_moveloop))

/* Globals: G an object of a listed struct, GP a pointer to one (or, without
   a struct, a pointer read as an address), GA an array of them. */
#define NHBOT_GLOBALS \
    G(u, u, you)                                                             \
    G(program_state, program_state, sinfo)                                   \
    GA(levl, levl, rm, 2, COLNO, ROWNO)                                      \
    GA(dungeons, svd.dungeons, dungeon, 1, MAXDUNGEON, 0)                    \
    GP(fmon, fmon, monst)                                                    \
    GP(fobj, fobj, obj)                                                      \
    GP(invent, gi.invent, obj)                                               \
    GP(stairs, gs.stairs, stairway)                                          \
    GP(viz_array, gv.viz_array, )

#define NHBOT_VALUES \
    V(COLNO) V(ROWNO) V(MAXDUNGEON) V(COULD_SEE) V(LUCKSTONE) V(S_NYMPH)

EMSCRIPTEN_KEEPALIVE const char *nhbot_layout(void);

static char nhbot_buf[16384];
static size_t nhbot_len;

staticfn void nhbot_put(const char *, ...) PRINTF_F(1, 2);

staticfn void
nhbot_put(const char *fmt, ...)
{
    va_list args;
    int n;

    if (nhbot_len >= sizeof nhbot_buf)
        return;
    va_start(args, fmt);
    n = vsnprintf(nhbot_buf + nhbot_len, sizeof nhbot_buf - nhbot_len, fmt,
                  args);
    va_end(args);
    if (n > 0)
        nhbot_len += (size_t) n;
}

/* The comma before each item of a JSON object but its first */
static boolean nhbot_first;

staticfn void
nhbot_key(const char *name)
{
    nhbot_put("%s\"%s\":", nhbot_first ? "" : ",", name);
    nhbot_first = FALSE;
}

staticfn void
nhbot_field(const char *name, size_t offset, size_t size, char kind,
            const char *target)
{
    nhbot_key(name);
    nhbot_put("[%lu,%lu,\"%c\"", (unsigned long) offset, (unsigned long) size,
              kind);
    if (target && *target)
        nhbot_put(",\"%s\"", target);
    nhbot_put("]");
}

/* A bitfield set to all ones in an otherwise zeroed struct: its first byte,
   and its bits in the little-endian word from there */
staticfn void
nhbot_bits(const char *name, const unsigned char *bytes, size_t size,
           boolean is_signed)
{
    size_t i = 0;
    unsigned long mask = 0;
    int k;

    while (i < size && !bytes[i])
        ++i;
    for (k = 0; k < 4; ++k)
        mask |= (unsigned long) bytes[i + k] << (8 * k);
    nhbot_key(name);
    nhbot_put("[%lu,4,\"%c\",%lu]", (unsigned long) i, is_signed ? 'j' : 'b',
              mask);
}

/* JSON: {"structs": {name: {"size": n, "fields": {name: [offset, size,
   kind, target struct or bit mask]}}}, "globals": {name: {"addr": a,
   "type": struct, "ptr": 1 or "dims": [n, m]}}, "values": {name: n}};
   kinds are i signed, u unsigned, p pointer, b and j an unsigned and a
   signed bitfield. */
const char *
nhbot_layout(void)
{
    if (nhbot_len)
        return nhbot_buf;

#define S(name, fields) \
    {                                                                        \
        typedef struct name nhbot_t;                                         \
        nhbot_key(#name);                                                    \
        nhbot_put("{\"size\":%lu,\"fields\":{",                              \
                  (unsigned long) sizeof (nhbot_t));                         \
        nhbot_first = TRUE;                                                  \
        fields                                                               \
        nhbot_put("}}");                                                     \
        nhbot_first = FALSE;                                                 \
    }
#define F(f) \
    {                                                                        \
        __typeof__(((nhbot_t *) 0)->f) v =                                   \
            (__typeof__(((nhbot_t *) 0)->f)) -1;                             \
                                                                             \
        nhbot_field(#f, offsetof(nhbot_t, f), sizeof v, v > 0 ? 'u' : 'i',   \
                    0);                                                      \
    }
#define B(f) \
    {                                                                        \
        static union {                                                       \
            nhbot_t v;                                                       \
            unsigned char c[sizeof (nhbot_t) + 4];                           \
        } t;                                                                 \
                                                                             \
        t.v.f--;                                                             \
        nhbot_bits(#f, t.c, sizeof (nhbot_t), !(t.v.f > 0));                 \
    }
#define P(f, target) \
    nhbot_field(#f, offsetof(nhbot_t, f), sizeof ((nhbot_t *) 0)->f, 'p',    \
                #target);
    nhbot_put("{\"structs\":{");
    nhbot_first = TRUE;
    NHBOT_STRUCTS
#undef S
#undef F
#undef B
#undef P

#define G(name, var, type) \
    nhbot_key(#name);                                                        \
    nhbot_put("{\"addr\":%lu,\"type\":\"%s\"}", (unsigned long) &(var), #type);
#define GP(name, var, type) \
    nhbot_key(#name);                                                        \
    nhbot_put("{\"addr\":%lu,\"type\":\"%s\",\"ptr\":1}",                    \
              (unsigned long) &(var), #type);
#define GA(name, var, type, n, d1, d2) \
    nhbot_key(#name);                                                        \
    nhbot_put("{\"addr\":%lu,\"type\":\"%s\",\"dims\":[%d", (unsigned long) &(var), \
              #type, (int) (d1));                                            \
    if ((n) > 1)                                                             \
        nhbot_put(",%d", (int) (d2));                                        \
    nhbot_put("]}");
    nhbot_put("},\"globals\":{");
    nhbot_first = TRUE;
    NHBOT_GLOBALS
#undef G
#undef GP
#undef GA

#define V(name) \
    nhbot_key(#name);                                                        \
    nhbot_put("%ld", (long) (name));
    nhbot_put("},\"values\":{");
    nhbot_first = TRUE;
    NHBOT_VALUES
#undef V
    nhbot_put("}}");

    if (nhbot_len >= sizeof nhbot_buf - 1)
        panic("nhbot_layout: the layout outgrew its buffer");
    return nhbot_buf;
}
