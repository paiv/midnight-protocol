#include <stddef.h>
#include <stdint.h>

#define TRACE 0


typedef int8_t i8;
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef double r64;


extern void* __heap_base;


#define HOST_IMPORT(module, name) \
    __attribute__((import_module(#module))) \
    __attribute__((import_name(#name)))

HOST_IMPORT(host, time_now) r64 host_time_now(void);
HOST_IMPORT(host, random) r64 host_random(void);
// HOST_IMPORT(host, sqrlog) r64 host_sqrlog(r64, r64);
#if TRACE
HOST_IMPORT(host, trace_log) void host_trace_log(u32 x);
#endif


typedef struct __attribute__((packed)) {
    u32 memory_size; // pages
    u32 time_limit; // ms
    u32 difficulty_level;
} setup_data;


static setup_data Config;


struct memory_arena {
    u8* end;
    u8 nomemory;
    u8* memory;
    u8 arena[];
};
static struct memory_arena* memory_arena = nullptr;


template <typename T>
const T&
min(const T& a, const T& b) {
    return b < a ? b : a;
}


extern "C" {

#if TRACE
static u32 malloc_calls = 0;
static u32 malloc_allocs = 0;
#endif

void*
malloc(size_t size) {
    #if TRACE
    ++malloc_calls;
    malloc_allocs += size;
    #endif
    void* p = memory_arena->memory;
    if ((u8*)p + size >= memory_arena->end) {
        memory_arena->nomemory = 1;
        return nullptr;
    }
    memory_arena->memory += size;
    return p;
}


#if 0
void*
memcpy(void* dest, const void* src, size_t n) {
    u32* pi = (u32*) dest;
    const u32* si = (const u32*) src;
    size_t a = n / sizeof(u32);
    for (size_t i = 0; i < a; ++i) {
        *pi++ = *si++;
    }
    u8* p = (u8*) pi;
    const u8* s = (const u8*) si;
    size_t b = n % sizeof(u32);
    for (size_t i = 0; i < b; ++i) {
        *p++ = *s++;
    }
    return dest;
}
#endif


void*
memset(void* dest, int c, size_t n) {
    #if 0
    u8 v = (u32(c) & 0xff);
    u32 x = v | (v << 8) | (v << 16) | (v << 24);
    u32* pw = (u32*) dest;
    size_t a = n / sizeof(u32);
    for (size_t i = 0; i < a; ++i) {
        *pw++ = x;
    }
    u8* p = (u8*) pw;
    size_t b = n % sizeof(u32);
    for (size_t i = 0; i < b; ++i) {
        *p++ = v;
    }
    #else
    u8* p = (u8*) dest;
    for (; n; n--, p++) *p = c;
    #endif
    return dest;
}

}


typedef struct random_generator {
    u64 state;

    u32 next(void) {
        u64 oldstate = state;
        state = oldstate * 6364136223846793005ULL + 1442695040888963407ULL;
        u32 xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
        u32 rot = oldstate >> 59u;
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    }

    void seed(u64 seed) {
        state = 0U;
        next();
        state += seed;
        next();
    }

    u32 range(u32 bound) {
        u32 threshold = -bound % bound;
        for (;;) {
            u32 r = next();
            if (r >= threshold) {
                return r % bound;
            }
        }
    }
} random_generator;


static random_generator random;


template <typename V, size_t N>
struct ao_array {
    size_t _size;
    V values[N];

    constexpr size_t size() const { return _size; }
    constexpr size_t capacity() const { return N; }
    void clear() { _size = 0; }

    void append(const V& value) {
        #if TRACE
        if (_size >= N) {
            host_trace_log(0xffffff);
            return;
        }
        #endif
        values[_size++] = value;
    }

    void erase(size_t pos) {
        for (size_t i = pos + 1; i < _size; ++i) {
            values[i-1] = values[i];
        }
        --_size;
    }
};


template <typename V>
struct ao_list {
    V value;
    struct ao_list<V>* tail;
};


template <typename V, typename S = struct ao_list<V>>
static
void
ao_list_upsert(S** s, const V& value) {
    for (S* p = *s; p; s = &p->tail, p = *s) {
        if (p->value == value) {
            return;
        }
    }
    S* el = (S*) malloc(sizeof(S));
    if (!el) { return; }
    *el = {.value=value, .tail=nullptr};
    *s = el;
}

template <typename V, typename S = struct ao_list<V>>
static
u8
ao_list_has(S* const* s, const V& value) {
    for (S* p = *s; p; s = &p->tail, p = *s) {
        if (p->value == value) {
            return 1;
        }
    }
    return 0;
}


template <typename V, size_t Buckets>
struct ao_set {
    struct ao_list<V>* buckets[Buckets];

    u32 hash_key(const V& key) const {
        const u8* p = (const u8*)&key;
        u32 h = 0;
        for (u32 i = 0; i < sizeof(V); ++i) {
            h = u64(h * 31 + *p++); // % 0x1fffffffffffffff;
        }
        return h % Buckets;
    }

    void clear() {
        memset(buckets, 0, sizeof(buckets));
    }

    void insert(const V& key) {
        auto h = hash_key(key);
        ao_list_upsert(&buckets[h], key);
    }

    u8 has(const V& key) const {
        auto h = hash_key(key);
        return ao_list_has(&buckets[h], key);
    }
};


template <typename V, size_t Buckets, size_t N>
struct ao_aset {
    struct ao_array<V, N> buckets[Buckets];

    u32 hash_key(const V& key) const {
        const u8* p = (const u8*)&key;
        u32 h = 0;
        for (u32 i = 0; i < sizeof(V); ++i) {
            h = u64(h * 31 + *p++); // % 0x1fffffffffffffff;
        }
        return h % Buckets;
    }

    void clear() {
        memset(buckets, 0, sizeof(buckets));
    }

    void insert(const V& key) {
        auto h = hash_key(key);
        auto& l = buckets[h];
        for (u32 i = 0; i < l.size(); ++i) {
            if (l.values[i] == key) { return; }
        }
        l.append(key);
    }

    u8 has(const V& key) const {
        auto h = hash_key(key);
        auto& l = buckets[h];
        for (u32 i = 0; i < l.size(); ++i) {
            if (l.values[i] == key) { return 1; }
        }
        return 0;
    }
};


template <typename K, typename V>
struct kv_list {
    K key;
    V value;
    struct kv_list<K,V>* tail;
};


template <typename K, typename V, typename S = struct kv_list<K,V>>
static
void
kv_list_upsert(S** s, const K& key, const V& value) {
    for (S* p = *s; p; s = &p->tail, p = *s) {
        if (p->key == key) {
            p->value = value;
            return;
        }
    }
    S* el = (S*) malloc(sizeof(S));
    if (!el) { return; }
    *el = {.key=key, .value=value, .tail=nullptr};
    *s = el;
}

template <typename K, typename V, typename S = struct kv_list<K,V>>
static
V&
kv_list_get(S** s, const K& key) {
    for (S* p = *s; p; s = &p->tail, p = *s) {
        if (p->key == key) {
            return p->value;
        }
    }
    S* el = (S*) malloc(sizeof(S));
    if (!el) {
        static V x;
        x = V();
        return x;
    }
    *el = {.key=key, .value=V(), .tail=nullptr};
    *s = el;
    return el->value;
}


template <typename K, typename V, size_t Buckets>
struct ao_map {
    struct kv_list<K,V>* buckets[Buckets];

    u32 hash_key(const K& key) const {
        const u8* p = (const u8*)&key;
        u32 h = 0;
        for (u32 i = 0; i < sizeof(K); ++i) {
            h = u64(h * 31 + *p++); // % 0x1fffffffffffffff;
        }
        return h % Buckets;
    }

    void clear() {
        memset(buckets, 0, sizeof(buckets));
    }

    void insert(const K& key, const V& value) {
        auto h = hash_key(key);
        kv_list_upsert(&buckets[h], key, value);
    }

    V& get(const K& key) {
        auto h = hash_key(key);
        return kv_list_get<K,V>(&buckets[h], key);
    }
};


typedef struct __attribute__((packed)) {
    u8 current_player;
    u8 board[5][5];
    u8 progs[5];
} game_state_data;


typedef union __attribute__((packed)) {
    struct {
        u8 ver;
        u8 from;
        u8 to;
        u8 pid;
    };
    u8 raw[4];
} player_move_data;


typedef union {
    player_move_data data;
    struct {
        u8 _reserved;
        u8 from;
        u8 to;
        u8 pid;
    };
    u32 v;
} player_move;


const player_move player_pass = {.v=0xffffffff};


typedef union {
    struct {
        u8 a, b;
    };
    u8 v[2];
} u8x2;


typedef union {
    game_state_data data;
    struct {
        u8 current_player;
        union {
            u8 board[5][5];
            u8 pieces[25];
        };
        union {
            u8 progs[5];
            struct {
                u8 decked_prog;
                union {
                    u8 player_progs[2][2];
                    u8x2 pprogs[2];
                    struct {
                        u8 p1_progs[2];
                        u8 p2_progs[2];
                    };
                };
            };
        };
        u8 ended;
        u8 win;
    };
} game_state;


typedef struct __attribute((packed)) {
    union {
        u64 v;
        struct {
            u8 _reserved: 4;
            u8 prog1: 2;
            u8 prog2: 2;
            u8 prog3: 2;
            u8 prog4: 2;
            u8 prog_fix: 1;
            u8 player: 1;
            u8 player1_king: 5;
            u8 player1_p1: 5;
            u8 player1_p2: 5;
            u8 player1_p4: 5;
            u8 player1_p5: 5;
            u8 player2_king: 5;
            u8 player2_p1: 5;
            u8 player2_p2: 5;
            u8 player2_p4: 5;
            u8 player2_p5: 5;
        };
    };
} packed_state;


static
const i8 Progs[][5] = {
    {-10, -1, 1}, // dagger
    {-20, 10}, // harpoon
    {-11, -9, -1, 1}, // jackhammer
    {-1, 1, 9, 11}, // onion
    {-11, -9, 9, 11}, // shuriken
};


static inline
u8
is_king1(u8 piece) {
    return piece == 13;
}


static inline
u8
is_king2(u8 piece) {
    return piece == 23;
}


static inline
u8
is_king(u8 piece) {
    return piece % 10 == 3;
}


static inline
u8
on_board(i8 pos) {
    i8 x = pos % 10;
    i8 y = pos / 10;
    return !(x < 0 || x > 4 || y < 0 || y > 4);
}


static inline
const u8x2&
own_progs(const game_state& state, u8 uid) {
    return state.pprogs[uid-1];
}


static inline
u8
is_own(u8 piece, u8 uid) {
    return piece / 10 == uid;
}


static inline
u8
get_piece(const game_state& state, u8 pos) {
    i8 x = pos % 10;
    i8 y = pos / 10;
    return state.board[y][x];
}


static inline
void
set_piece(game_state& state, u8 pos, u8 piece) {
    i8 x = pos % 10;
    i8 y = pos / 10;
    state.board[y][x] = piece;
}


static
u32
is_terminal(const game_state& state) {
    u8 p1 = 0, p2 = 0;
    for (u32 y = 0; y < 5; ++y) {
        for (u32 x = 0; x < 5; ++x) {
            u8 piece = state.board[y][x];
            if (is_king1(piece)) {
                if (y == 0 && x == 2) { return 1; }
                p1 = 1;
            }
            else if (is_king2(piece)) {
                if (y == 4 && x == 2) { return 1; }
                p2 = 1;
            }
        }
    }
    return !(p1 && p2);
}


static
packed_state
pack_state(const game_state& state) {
    packed_state packed = {.v=0};
    packed.player = state.current_player - 1;
    u32 pi1 = 0, pi2 = 0;
    for (u32 i = 0; i < 25; ++i) {
        u8 piece = state.pieces[i];
        if (!piece) { continue; }
        if (is_own(piece, 1)) {
            if (is_king(piece)) {
                packed.player1_king = i;
            }
            else {
                switch (pi1++) {
                    case 0: packed.player1_p1 = i; break;
                    case 1: packed.player1_p2 = i; break;
                    case 2: packed.player1_p4 = i; break;
                    case 3: packed.player1_p5 = i; break;
                }
            }
        }
        else {
            if (is_king(piece)) {
                packed.player2_king = i;
            }
            else {
                switch (pi2++) {
                    case 0: packed.player2_p1 = i; break;
                    case 1: packed.player2_p2 = i; break;
                    case 2: packed.player2_p4 = i; break;
                    case 3: packed.player2_p5 = i; break;
                }
            }
        }
    }
    u8 progs[4];
    for (u32 i = 0; i < 4; ++i) {
        progs[i] = state.progs[i+1];
    }
    if (progs[0] > progs[1]) {
        u8 t = progs[0];
        progs[0] = progs[1];
        progs[1] = t;
    }
    if (progs[2] > progs[3]) {
        u8 t = progs[2];
        progs[2] = progs[3];
        progs[3] = t;
    }
    u8 fix = 0, seen0 = 0;
    for (u32 i = 0; i < 4; ++i) {
        u8 pid = progs[i];
        switch (i) {
            case 0: packed.prog1 = pid; break;
            case 1: packed.prog2 = pid; break;
            case 2: packed.prog3 = pid; break;
            case 3: packed.prog4 = pid; break;
        }
        if (pid == 0) { seen0 = 1; }
        else if (pid == 4 && seen0) {
            fix = 1;
        }
    }
    packed.prog_fix = fix;
    return packed;
}


static
game_state
next_state(const game_state& state, const player_move& mv) {
    if (state.ended) { return state; }
    auto next = state;
    next.current_player = 3 - state.current_player;
    u8 uid = state.current_player;
    u8 piece = get_piece(state, mv.from);
    // if (!piece) { return next; }
    // if (!is_own(piece, uid)) { return next; }
    // if (!on_board(mv.to)) { return next; }
    // u8 target = get_piece(state, mv.to);
    // if (is_own(target, uid)) { return next; }
    // if (!is_own_prog(state, mv.pid, uid)) { return next; }
    // if (!is_prog_move(mv, uid)) { return next; }
    set_piece(next, mv.from, 0);
    set_piece(next, mv.to, piece);
    next.ended = is_terminal(next);
    if (next.ended) {
        next.current_player = state.current_player;
        next.win = 1;
    }
    else {
        for (u32 i = 0; i < 2; ++i) {
            u8 pid = state.player_progs[uid-1][i];
            if (pid == mv.pid) {
                next.player_progs[uid-1][i] = next.decked_prog;
                next.decked_prog = pid;
                break;
            }
        }
    }
    return next;
}


typedef struct {
    u64 parent;
    u32 wins;
    u32 rounds;
} monte_node;


typedef struct ao_array<player_move, 100> mc_valid;
typedef struct ao_map<u64, monte_node, 0x8000> mc_stats;
typedef struct ao_aset<u64, 0x100, 10> mc_seen;
typedef struct ao_array<u64, 100> mc_path;


typedef struct {
    game_state root_state;
    u64 root_id;
    u32 time_limit;
    u32 max_path;
    mc_stats stats;
} mc_context;


static
void
valid_moves(mc_valid& valid, const game_state& state, u8 uid) {
    valid.clear();
    i8 rotate = 3 - 2 * uid;
    for (u32 y = 0; y < 5; ++y) {
        for (u32 x = 0; x < 5; ++x) {
            u8 piece = state.board[y][x];
            if (!is_own(piece, uid)) { continue; }
            for (u8 pid : own_progs(state, uid).v) {
                for (i8 d : Progs[pid]) {
                    if (!d) { continue; }
                    u8 from = y * 10 + x;
                    u8 to = from + d * rotate;
                    if (!on_board(to)) { continue; }
                    u8 target = get_piece(state, to);
                    if (is_own(target, uid)) { continue; }
                    valid.append({.from=from, .to=to, .pid=pid});
                }
            }
        }
    }
}


static
u8
mc_dive(const game_state& root_state, const player_move& first_move) {
    u8 uid = root_state.current_player;
    mc_seen seen = {};
    auto state = root_state;
    seen.insert(pack_state(state).v);
    state = next_state(state, first_move);
    seen.insert(pack_state(state).v);
    mc_valid valid;
    while (!state.ended) {
        valid_moves(valid, state, state.current_player);
        while (valid.size()) {
            u32 i = random.range(valid.size());
            auto mv = valid.values[i];
            auto nextState = next_state(state, mv);
            auto k = pack_state(nextState).v;
            if (seen.has(k)) {
                valid.erase(i);
            }
            else {
                seen.insert(k);
                state = nextState;
                break;
            }
        }
        if (!valid.size()) { break; }
    }
    return state.current_player == uid;
}


static inline
r64
uct1(r64 wins, r64 rounds, r64 parent_rounds) {
    const r64 c = 1.4142135623730951;
    // return wins / rounds + c * std::sqrt(std::log(parent_rounds) / rounds);
    // return wins / rounds + c * host_sqrlog(parent_rounds, rounds);
    return (wins + parent_rounds / 100) / rounds;
}


#if TRACE
static u32 max_path = 0;
#endif

static
u8
mc_playout(mc_context* context) {
    auto parent_state = context->root_state;
    u64 parent_id = context->root_id;
    player_move selected_move = player_pass;
    u64 selected_id = parent_id;
    mc_seen seen = {};
    mc_path path = {};
    seen.insert(parent_id);
    path.append(parent_id);
    mc_valid valid;
    game_state best_state;

    while (!parent_state.ended && path.size() < path.capacity()) {
        if ((context->max_path && path.size() >= context->max_path) || memory_arena->nomemory) { break; }
        valid_moves(valid, parent_state, parent_state.current_player);
        if (!valid.size()) { return 0; }
        player_move best_move = player_pass;
        u64 best_id = 0;
        r64 bestW = -1e20;
        for (u32 vi = 0; vi < valid.size(); ++vi) {
            auto& mv = valid.values[vi];
            auto ns = next_state(parent_state, mv);
            u64 nsid = pack_state(ns).v;
            if (seen.has(nsid)) { continue; }
            seen.insert(nsid);
            r64 wei = 0;
            auto parent_rounds = context->stats.get(parent_id).rounds;
            auto& stats = context->stats.get(nsid);
            if (!stats.parent) {
                context->stats.insert(nsid, {.parent=parent_id, .wins=0, .rounds=1});
                wei = uct1(0, 1, parent_rounds);
            }
            else {
                wei = uct1(stats.wins, stats.rounds, parent_rounds);
            }
            if (ns.ended) {
                wei = 100;
            }
            if (wei > bestW) {
                bestW = wei;
                best_id = nsid;
                best_move = mv;
                best_state = ns;
            }
        }

        if (best_move.from == player_pass.from) {
            parent_state.ended = true;
            parent_state.win = 0;
            break;
        }

        path.append(best_id);
        auto& stats = context->stats.get(best_id);
        if (stats.rounds == 1) {
            selected_move = best_move;
            selected_id = best_id;
            break;
        }
        else {
            parent_state = best_state;
            parent_id = best_id;
        }
    }

    u8 win = 0;
    if (parent_state.ended) {
        win = parent_state.win;
    }
    else {
        win = mc_dive(parent_state, selected_move);
    }

    #if TRACE
    if (path.size() > max_path) {
        max_path = path.size();
    }
    #endif

    #if 0
    if (win && path.size() > 1) {
        auto q = path.values[1];
        auto& node = context->stats.get(q);
        if (node.rounds > 1000) { return 0; }
    }
    #endif

    for (size_t i = path.size(); i; --i) {
        auto q = path.values[i-1];
        auto& node = context->stats.get(q);
        node.wins += win;
        node.rounds += 1;
        win = !win;
    }

    return 1;
}


static
player_move
mc_best_move(mc_context* context) {
    game_state& root_state = context->root_state;
    u8 uid = root_state.current_player;
    player_move best = player_pass;
    r64 bestScore = -1;
    mc_valid valid;
    valid_moves(valid, root_state, uid);

    for (u32 i = 0; i < valid.size(); ++i) {
        auto& mv = valid.values[i];
        u64 q = pack_state(next_state(root_state, mv)).v;
        // u64 q = root_states[mv.v];
        const monte_node& node = context->stats.get(q);
        if (!node.rounds) { continue; }
        r64 score = r64(node.wins) / r64(node.rounds);
        if (score > bestScore) {
            bestScore = score;
            best = mv;
        }
    }
    return best;
}


static
player_move
monte_move(mc_context* context, const game_state& root_state) {
    if (root_state.ended) {
        return player_pass;
    }
    r64 start = host_time_now();
    r64 time_limit = context->time_limit;

    context->root_state = root_state;
    auto root_id = pack_state(root_state).v;
    context->root_id = root_id;
    context->stats.clear();
    context->stats.insert(root_id, {0, 0, 1});

    u32 total_runs = 0;
    for (u32 dt = 0; ; ++dt) {
        ++total_runs;
        if (!mc_playout(context)) { break; }

        if (dt == 10000) {
            dt = 0;
            r64 now = host_time_now();
            r64 elapsed = now - start;
            if (elapsed >= time_limit) { break; }
        }
    }
    #if TRACE
    host_trace_log(total_runs);
    #endif

    return mc_best_move(context);
}


__attribute__((export_name("select_move")))
u8
select_move(void) {
    game_state state;
    state.data = *(game_state_data*)__heap_base;
    state.ended = is_terminal(state);

    memory_arena->memory = memory_arena->arena;
    memory_arena->nomemory = 0;

    mc_context* context = (mc_context*) malloc(sizeof(mc_context));
    memset(context, 0, sizeof(mc_context));

    context->time_limit = Config.time_limit;
    switch (Config.difficulty_level) {
        case 0:
            context->time_limit = min<u32>(500, Config.time_limit);
            context->max_path = 3;
            break;
        case 1:
            context->time_limit = min<u32>(1000, Config.time_limit);
            context->max_path = 5;
            break;
    }

    #if TRACE
    malloc_calls = 0;
    malloc_allocs = 0;
    max_path = 0;
    #endif

    random.seed(host_random());
    player_move mv = monte_move(context, state);

    #if TRACE
    host_trace_log(malloc_calls);
    host_trace_log(malloc_allocs);
    host_trace_log(max_path);
    #endif

    player_move_data res;
    res = mv.data;
    res.ver = 1;
    *(player_move_data*)__heap_base = res;

    return 1;
}


__attribute__((export_name("setup")))
void
setup(void) {
    Config = *(setup_data*)__heap_base;
    if (!Config.time_limit) { Config.time_limit = 100000; }

    const u32 stack_size = 0x20000;
    u8* heap = (u8*)__heap_base + stack_size; // data | <-stack | heap->
    memory_arena = (struct memory_arena*)heap;
    memory_arena->memory = memory_arena->arena;
    memory_arena->nomemory = 0;
    memory_arena->end = (u8*)__heap_base + u64(Config.memory_size * 0x10000);
}
