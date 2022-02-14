// cc -std=c++20 -lc++ -O3 -o brute brute.cpp
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <deque>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <unistd.h>


typedef int8_t i8;
typedef int32_t i32;
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
typedef float r32;
typedef double r64;


namespace chrono = std::chrono;
using std::deque;
using std::unordered_map;
using std::unordered_set;
using std::vector;


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
    struct {
        u8 from;
        u8 to;
        u8 pid;
        u8 _reserved;
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


typedef struct {
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
    packed_state packed = {};
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


static
i32
get_score(const game_state& state, u8 uid) {
    i32 score = 0;

#if 0
    fprintf(stderr, "pieces: ");
    for (u32 i = 0; i < 25; ++i) {
        u8 piece = state.pieces[i];
        if (!piece) { continue; }
        fprintf(stderr, "%02d(%d) ", piece, is_own(piece, uid));
        score += 10 * (2 * is_own(piece, uid) - 1);
    }
    if (state.ended) {
        score = 100 * (2 * (uid == state.current_player) - 1);
    }
    fprintf(stderr, ", score: %d\n", score);
    score = 0;
#endif

    if (state.ended) {
        score = 100 * (2 * (uid == state.current_player) - 1);
        return score;
    }
    for (u32 i = 0; i < 25; ++i) {
        u8 piece = state.pieces[i];
        if (!piece) { continue; }
        score += 10 * (2 * is_own(piece, uid) - 1);
    }
    return score;
}


static
vector<player_move>
valid_moves(const game_state& state, u8 uid) {
    vector<player_move> valid;
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
                    valid.push_back({.from=from, .to=to, .pid=pid});
                }
            }
        }
    }
    return valid;
}


template<typename T>
static
const T&
random_element(const vector<T>& v) {
    std::default_random_engine rng(std::random_device{}());
    std::uniform_int_distribution<u32> distribution(0, v.size()-1);
    u32 i = distribution(rng);
    return v[i];
}


static
player_move
random_move(const game_state& state) {
    auto valid = valid_moves(state, state.current_player);
    if (!valid.size()) {
        return {};
    }
    return random_element(valid);
}


typedef struct {
    u32 depth;
    game_state state;
    player_move moved;
} brute_state;


static
player_move
brute_move(const game_state& state, u32 max_depth) {
    if (state.ended) {
        return player_pass;
    }
    u8 uid = state.current_player;
    deque<brute_state> fringe;
    unordered_set<u64> seen;
    seen.insert(pack_state(state).v);
    for (auto& mv : valid_moves(state, state.current_player)) {
        fringe.push_back({1, next_state(state, mv), mv});
    }
    unordered_map<u32, i32> stats;
    unordered_map<u32, u32> hits;
    u32 total = 0;
    while (!fringe.empty()) {
        auto search = fringe.front();
        fringe.pop_front();
        auto k = pack_state(search.state).v;
        if (seen.find(k) != seen.end()) { continue; }
        seen.insert(k);
        ++total;
        vector<player_move> valid;
        u8 is_terminal = search.state.ended || search.depth >= max_depth;
        if (!is_terminal) {
            valid = valid_moves(search.state, search.state.current_player);
            is_terminal = valid.empty();
        }
        if (is_terminal) {
            i32 score = get_score(search.state, uid) - search.depth;
#if 0
            for (u32 y = 0; y < 5; ++y) {
                for (u32 x = 0; x < 5; ++x) {
                    fprintf(stderr, "%02d ", search.state.board[y][x]);
                }
                fprintf(stderr, "\n");
            }
            fprintf(stderr, "score: %d\n\n", score);
#endif
            i8 me = search.state.current_player == uid;
            if ((me && score > 0) || (!me && score < 0)) {
                auto q = search.moved.v;
                stats[q] += score;
                hits[q] += 1;
            }
            continue;
        }
        for (auto& mv : valid) {
            fringe.push_back({search.depth+1, next_state(search.state, mv), search.moved});
        }
    }
    auto best = player_pass;
    r64 bestScore = -std::numeric_limits<r64>::infinity();
    for (auto& p : stats) {
        r64 s = r64(p.second) / r64(hits[p.first]);
        player_move q = {.v=p.first};
        fprintf(stderr, "%02u-%02u(%u): %.2f %d / %u\n", q.from, q.to, q.pid, s, p.second, hits[p.first]);
        if (s > bestScore) {
            bestScore = s;
            best = q;
        }
    }
    return best;
}


static u32 seen_in_dive = 0;
static
u8
mc_dive(const game_state& root_state, const player_move& first_move) {
    u8 uid = root_state.current_player;
    std::default_random_engine rng(std::random_device{}());
    unordered_set<u64> seen;
    auto state = root_state;
    seen.insert(pack_state(state).v);
    state = next_state(state, first_move);
    seen.insert(pack_state(state).v);
    while (!state.ended) {
        auto valid = valid_moves(state, state.current_player);
        while (!valid.empty()) {
            std::uniform_int_distribution<u32> distribution(0, valid.size()-1);
            u32 i = distribution(rng);
            auto mv = valid[i];
            auto nextState = next_state(state, mv);
            auto k = pack_state(nextState).v;
            if (seen.find(k) != seen.end()) {
                valid.erase(valid.begin() + i);
            }
            else {
                seen.insert(k);
                state = nextState;
                break;
            }
        }
        if (valid.empty()) { break; }
    }
    if (seen.size() > seen_in_dive) {
        seen_in_dive = seen.size();
    }
    return state.current_player == uid;
}


static
player_move
shallow_move(const game_state& state, r64 time_limit) {
    if (state.ended) {
        return player_pass;
    }
    chrono::duration<r64> tlimit(time_limit);
    auto start = chrono::steady_clock::now();
    auto valid = valid_moves(state, state.current_player);
    if (valid.empty()) { return player_pass; }
    unordered_map<u32, i32> stats;
    u32 rounds = 1;
    for (u32 vi = 0; ; ) {
        i32 score = 2 * mc_dive(state, valid[vi]) - 1;
        stats[vi] += score;
        if (++vi >= valid.size()) {
            vi = 0;
            ++rounds;
        }
        auto now = chrono::steady_clock::now();
        chrono::duration<r64> elapsed = now - start;
        if (elapsed >= tlimit) { break; }
    }
    auto best = player_pass;
    r64 bestScore = -std::numeric_limits<r64>::infinity();
    for (auto& p : stats) {
        r64 s = r64(p.second) / r64(rounds);
        player_move q = valid[p.first];
        fprintf(stderr, "%02u-%02u(%u): %.2f %d / %u\n", q.from, q.to, q.pid, s, p.second, rounds);
        if (s > bestScore) {
            bestScore = s;
            best = q;
        }
    }
    return best;
}


typedef struct {
    u64 parent;
    u32 wins;
    u32 rounds;
} monte_node;


static inline
r64
uct1(r64 wins, r64 rounds, r64 parent_rounds) {
    const r64 c = 1.4142135623730951;
    return wins / rounds + c * std::sqrt(std::log(parent_rounds) / rounds);
}


static
player_move
monte_move(const game_state& root_state, r64 time_limit) {
    if (root_state.ended) {
        return player_pass;
    }
    chrono::duration<r64> tlimit(time_limit);
    auto start = chrono::steady_clock::now();

    unordered_map<u64, monte_node> stats;
    u64 root_id = pack_state(root_state).v;
    stats[root_id] = {0, 0, 1};

    vector<u64> xchildren;
    {
        auto valid = valid_moves(root_state, root_state.current_player);
        for (auto& mv : valid) {
            auto s = next_state(root_state, mv);
            u64 stateQ = pack_state(s).v;
            xchildren.push_back(stateQ);
        }
    }

    u32 total = 0;
    unordered_map<u32, u32> bestIstats;
    u32 maxPath = 0, maxSeen = 0;
    while (1) {
        total += 1;
        auto parent_state = root_state;
        u64 parent_id = root_id;
        player_move selected_move = player_pass;
        player_move debug_move = player_pass;
        u64 selected_id = parent_id;
        unordered_set<u64> seen;
        seen.insert(parent_id);
        vector<u64> path;
        path.push_back(parent_id);

        while (!parent_state.ended) {
            auto valid = valid_moves(parent_state, parent_state.current_player);
            if (valid.empty()) { return player_pass; }
            r64 bestW = -1e20;
            u64 bestQ = 0;
            u32 bestI = -1;
            player_move best_move = player_pass;
            game_state best_state;
            // for (auto& mv : valid) {
            for (u32 vi = 0; vi < valid.size(); ++vi) {
                auto mv = valid[vi];
                auto ns = next_state(parent_state, mv);
                u64 stateQ = pack_state(ns).v;
                if (seen.find(stateQ) != seen.end()) { continue; }
                seen.insert(stateQ);
                auto it = stats.find(stateQ);
                r64 wei = 0;
                if (it == stats.end()) {
                    stats[stateQ] = {.parent=parent_id, .wins=0, .rounds=1};
                    wei = uct1(0, 1, stats[parent_id].rounds);
                }
                else {
                    auto node = it->second;
                    wei = uct1(node.wins, node.rounds, stats[parent_id].rounds);
                }
                if (ns.ended) {
                    wei = 100;
                }
                if (wei > bestW) {
                    bestW = wei;
                    bestQ = stateQ;
                    bestI = vi;
                    best_move = mv;
                    best_state = ns;
                }
            }
            if (best_move.from == player_pass.from) {
                parent_state.ended = true;
                parent_state.win = 0;
                break;
            }
            path.push_back(bestQ);
            if (stats[bestQ].rounds == 1) {
                selected_move = best_move;
                selected_id = bestQ;
                break;
            }
            else {
                if (parent_id == root_id) {
                    bestIstats[bestI] += 1;
                }
                parent_state = best_state;
                parent_id = bestQ;
            }
        }

        u8 win = 0;
        if (parent_state.ended) {
            win = parent_state.win;
        }
        else {
            win = mc_dive(parent_state, selected_move);
        }

        if (path.size() > maxPath) {
            maxPath = path.size();
        }
        if (seen.size() > maxSeen) {
            maxSeen = seen.size();
        }

        for (auto it = path.rbegin(); it != path.rend(); ++it) {
            auto q = *it;
            auto node = stats[q];
            node.wins += win;
            node.rounds += 1;
            stats[q] = node;
            win = !win;
        }

        auto now = chrono::steady_clock::now();
        chrono::duration<r64> elapsed = now - start;
        if (elapsed >= tlimit) { break; }
    }

    fprintf(stderr, "root rounds %u, total %u\n", stats[root_id].rounds, total);
    fprintf(stderr, "max path: %u, max seen: %u, seen in dive: %u\n", maxPath, maxSeen, seen_in_dive);
    fprintf(stderr, "node stats (%lu):\n", stats.size());
    for (auto& p : stats) {
        auto& node = p.second;
        if (node.parent == root_id) {
            fprintf(stderr, "  node %llu: %u / %u\n", p.first, node.wins, node.rounds);
        }
    }
    fprintf(stderr, "best I:\n");
    auto valid_i = valid_moves(root_state, root_state.current_player);
    for (auto& p : bestIstats) {
        auto mv = valid_i[p.first];
        fprintf(stderr, "  %u: %u %02u-%02u(%u)\n", p.first, p.second, mv.from, mv.to, mv.pid);
    }

    player_move best = player_pass;
    r64 bestScore = -1;
    auto valid = valid_moves(root_state, root_state.current_player);
    for (auto& mv : valid) {
        u64 stateQ = pack_state(next_state(root_state, mv)).v;
        auto xit = std::find(xchildren.begin(), xchildren.end(), stateQ);
        if (xit == xchildren.end()) {
            fprintf(stderr, "xchildren missing %llu\n", stateQ);
        }
        monte_node& node = stats[stateQ];
        if (!node.rounds) { continue; }
        r64 score = r64(node.wins) / r64(node.rounds);
        fprintf(stderr, "%02u-%02u(%u): %.2f %u / %u\n", mv.from, mv.to, mv.pid, score, node.wins, node.rounds);
        if (score > bestScore) {
            bestScore = score;
            best = mv;
        }
    }
    return best;
}


int main(void) {
    game_state_data statein = {};
    read(STDIN_FILENO, &statein, sizeof(statein));

    game_state state = {};
    state.current_player = statein.current_player;
    for (u32 y = 0; y < 5; ++y) {
        for (u32 x = 0; x < 5; ++x) {
            state.board[y][x] = statein.board[y][x];
        }
    }
    for (u32 x = 0; x < 5; ++x) {
        state.progs[x] = statein.progs[x];
    }
    state.ended = is_terminal(state);

    // player_move mv = random_move(state);
    // player_move mv = brute_move(state, 5);
    // player_move mv = shallow_move(state, 2);
    player_move mv = monte_move(state, 3);

    player_move_data res = {};
    res.ver = 1;
    res.from = mv.from;
    res.to = mv.to;
    res.pid = mv.pid;
    write(STDOUT_FILENO, res.raw, sizeof(res.raw));

    return 0;
}
