#include <iostream>
#include <algorithm>
#include <queue>
#include <vector>
#include <memory>
#include <time.h>

#define DEBUG_FLAG
//#define OUTPUT_DIJKSTRA
#define G 16
#define min(x, y) ((x) < (y) ? (x) : (y))

enum mat_enum { LAND, OCEAN, HILL, BERTH };
enum robot_status { NOTHING, TO_OBJ, AT_OBJ, TO_SHIP, AT_SHIP, RECOVERY };
enum robot_mov { ROBOT_GET, ROBOT_PULL, ROBOT_NOTHING };
enum ship_enum { SHIP_SHIPPING, SHIP_NORMAL, SHIP_WAITING };

const int robot_num = 10;
const int berth_num = 10;
const int ship_num = 5;
const int inf_dist = 1e6;

int tot_value = 0;

// 右 左 上 下 不动
const int mov_x[6] = {0, 0, -1, 1, 0, 0};
const int mov_y[6] = {1, -1, 0, 0, 0, 0};

inline int dis_man(int ax, int ay, int bx, int by) {
    return abs(ax - bx) + abs(ay - by);
}

inline int dis_oc(int ax, int ay, int bx, int by) {
    return abs(ax - bx)*abs(ax - bx) + abs(ay - by)*abs(ay - by);
}

inline bool in_mat(int x, int y) {
    return 0 <= x && x < 200 && 0 <= y && y < 200;
}

class Mat {
public:
    mat_enum type;
    bool has_obj;
    int dist[berth_num];
} mat[207][207];

inline bool is_land(int x, int y) {
    return mat[x][y].type == LAND || mat[x][y].type == BERTH;
}

inline bool is_legal(int x, int y) {
    return in_mat(x, y) && is_land(x, y);
}

class Obj {
public:
    Obj(): x(-1), y(-1) {};
    Obj(int _x, int _y, int _value, int _t) : x(_x), y(_y), value(_value), time_appear(_t), obtain(0) {};

    int x, y, value, time_appear, obtain;
};
std::vector<Obj> objects;
void remove_outdated_obj(int frame_id) {
    std::vector<Obj>::iterator it = objects.begin();
    while (it != objects.end()) {
        if (it->time_appear + 1000 < frame_id) {
            mat[it->x][it->y].has_obj = false;
            it++;
        }
        else break;
    }
    objects.erase(objects.begin(), it);
    fprintf(stderr, "# Frame %d objects.size() = %lu\n", frame_id, objects.size());
}

class Berth {
public:
    Berth() {}

    bool is_in(int ax, int ay) {
        return x <= ax && ax <= x + 3 && y <= ay && ay <= y + 3;
    }

    int id, x, y, transport_time, velocity;
    int occupy;
    int stock;
}berth[berth_num+1];

struct PQnode2 {
    PQnode2() {};
    PQnode2(int _x, int _y, int _d, int _dr) : x(_x), y(_y), d(_d), dr(_dr) {};
    int x, y, d, dr; // dist real

    bool operator < (const PQnode2 &x) const {
        return d > x.d;
    }
};
std::priority_queue<PQnode2> pqueue2;

int robot_cnt = 0;

struct Pos {
    Pos() {}
    Pos(int _x, int _y, int _dir, int _v) : x(_x), y(_y), dir(_dir), v(_v) {}

    int x, y, dir, v;
    bool operator < (const Pos &x) const {
        return v < x.v;
    }
};

void calc_is_safe(int id);

class Robot {
public:
    Robot() {}
    Robot(int _x, int _y, int _id) : x(_x), y(_y), id(_id), status(NOTHING), frame(1), carry(0) {
        for (int i = 0; i < 200; ++i) {
            for (int j = 0; j < 200; ++j) {
                dis[i][j] = inf_dist;
                vis[i][j] = 0;
            }
        }
    }

    void init() {
        for (int i = 0; i < 200; ++i) {
            for (int j = 0; j < 200; ++j) {
                reachable[i][j] = 0;
            }
        }

        pqueue2.push(PQnode2(x, y, 0, 0));
        reachable[x][y] = 1;
        int tx, ty;
        while (!pqueue2.empty()) {
            PQnode2 p = pqueue2.top();
            pqueue2.pop();

            for (int i = 0; i < 4; ++i) {
                tx = p.x + mov_x[i];
                ty = p.y + mov_y[i];

                if (is_legal(tx, ty)) {
                    if (!reachable[tx][ty]) {
                        reachable[tx][ty] = 1;
                        pqueue2.push(PQnode2(tx, ty, 0, 0));
                    }
                }
            }
        }

        calc_best_berth();
    }

    void sync(int _carry, int _x, int _y, int status_code, int _frame) {
        if (_x != x || _y != y || _frame != frame || _carry != carry) {
            fprintf(stderr, "# ROBOT %d Sync Error\n", id);
            fprintf(stderr, "# rec: (%d, %d) %d, carry:%d\n", x, y, frame, carry);
            fprintf(stderr, "# get: (%d, %d) %d, carry:%d\n", _x, _y, _frame, _carry);
            x = _x; y = _y; frame = _frame; carry = _carry;
        }
        if (status_code == 0) {
            fprintf(stderr, "#ERROR# ROBOT %d has code 0\n", id);
//            if (status == RECOVERY) return;
//            status = RECOVERY;
        }
        if (status == TO_OBJ && dest_obj.x == x && dest_obj.y == y) {
            status = AT_OBJ;
        }
        if (status == TO_SHIP && berth[dest_berth].is_in(x, y)) {
            status = AT_SHIP;
        }

        if (status == TO_OBJ && dest_obj.time_appear + 1000 <= frame) {
            status = NOTHING;
        }

        frame++;
    }

    void think() {
        // 大状态机！
        // enum robot_status { NOTHING, TO_OBJ, AT_OBJ, TO_SHIP, AT_SHIP, RECOVERY };
        act_before_move = ROBOT_NOTHING;
        is_safe = 1;
        assign_move_id = -1;
        next_moves.clear();

        if (status == TO_OBJ) {
            calc_moves();
            return;
        }
        if (status == AT_OBJ) {
            act_before_move = ROBOT_GET;
            status = TO_SHIP;
            calc_best_berth();
            calc_moves();
            return;
        }
        if (status == TO_SHIP) {
            calc_moves();
            return;
        }
        if (status == AT_SHIP) {
            act_before_move = ROBOT_PULL;
            status = NOTHING;
            // 下面接NOTHING的判断，看看有没有物体能捡
        }
        if (status == NOTHING) {
            if (choose_obj()) {
                status = TO_OBJ;
                fprintf(stderr, "# ROBOT:%d get object\n", id);
                clear_dis();
                calc_moves();
            }
            else {
                fprintf(stderr, "# ROBOT:%d no object\n", id);
                calc_moves();
            }
            return;
        }
        if (status == RECOVERY) {
            fprintf(stderr, "# ROBOT:%d is in RECOVERY\n", id);
        }
    }

    void act() {
        if (act_before_move == ROBOT_GET) {
            printf("get %d\n", id);
            carry = 1;
        }
        if (act_before_move == ROBOT_PULL) {
            printf("pull %d\n", id);
            carry = 0;
        }
        if (next_moves[assign_move_id].dir != 4) {
            printf("move %d %d\n", id, next_moves[assign_move_id].dir);
//            fprintf(stderr, "robot %d: (%d,%d)->(%d,%d)\n", id, x, y, next_moves[assign_move_id].x, next_moves[assign_move_id].y);
            x = next_moves[assign_move_id].x;
            y = next_moves[assign_move_id].y;
            if (!is_legal(x, y)) {
                fprintf(stderr, "robot %d: (%d,%d)->(%d,%d)\n", id, x, y, next_moves[assign_move_id].x, next_moves[assign_move_id].y);
            }
        }
        else {
            fprintf(stderr, "robot %d: no move, amid:%d, status: %d, carry: %d, obj:(%d,%d)\n", id, assign_move_id, status, carry, dest_obj.x, dest_obj.y);
            for (int i = 0; i < next_moves.size(); ++i) {
                fprintf(stderr, "# ROBOT:%d MOVEINFO mid:%d, dir:%d, (%d,%d)->(%d,%d), dis:%d\n", id, i, next_moves[i].dir, x, y, next_moves[i].x, next_moves[i].y, next_moves[i].v);
            }
        }
    }

    bool choose_obj() {
        int obj_id = calc_best_obj();
        if (obj_id == -1) return false;

        dest_obj = objects[obj_id];
        objects.erase(objects.begin() + obj_id);
#ifdef DEBUG_FLAG
        tot_value += dest_obj.value;
#endif
        return true;
    }

    void calc_moves() {
        calc_is_safe(id);

        int tx, ty;
        if (status == NOTHING) {
            // 没有目标，随便走
            for (int i = 0; i < 5; ++i) {
                tx = x + mov_x[i];
                ty = y + mov_y[i];
                if (is_legal(tx, ty)) {
                    next_moves.push_back(Pos(tx, ty, i, rand()));
                }
            }
        }
        if (status == TO_OBJ) {
            // 根据dis下降
            if (!vis[x][y]) calc_path_to_obj();
            for (int i = 0; i < 5; ++i) {
                tx = x + mov_x[i];
                ty = y + mov_y[i];
                if (is_legal(tx, ty)) {
//                    if (!vis[tx][ty]) calc_path_to_obj();
                    next_moves.push_back(Pos(tx, ty, i, dis[tx][ty]));
                }
            }
        }
        if (status == TO_SHIP) {
            for (int i = 0; i < 5; ++i) {
                tx = x + mov_x[i];
                ty = y + mov_y[i];
                if (is_legal(tx, ty)) {
                    next_moves.push_back(Pos(tx, ty, i, mat[tx][ty].dist[dest_berth]));
                }
            }
        }

        std::sort(next_moves.begin(), next_moves.end());
        if (is_safe) {
            assign_move_id = 0;
        }
        if (next_moves.size() == 1) fprintf(stderr, "# ERROR, ROBOT:%d NO WAY\n", id);
//        for (int i = 0; i < next_moves.size(); ++i) {
//            fprintf(stderr, "# ROBOT:%d dir:%d dis:%d\n", id, next_moves[i].dir, next_moves[i].v);
//        }
    }

    int calc_best_obj() {
        /*
        // 选当前价值最大的且能拿得到的
        int max_val = 0, res = -1;
        int obj_x, obj_y, obj_dis, obj_v;
        for (int i = 0; i < objects.size(); ++i) {
            obj_x = objects[i].x;
            obj_y = objects[i].y;
            obj_v = objects[i].value;
            obj_dis = mat[obj_x][obj_y].dist[dest_berth];
            if (objects[i].time_appear + 1000 < frame + obj_dis) continue;
            if (max_val < obj_v) {
                max_val = obj_v;
                res = i;
            }
        }
        return res;
        */

        // 简单以最近的来选
        int min_dis = inf_dist, res = -1;
        int obj_x, obj_y, obj_dis;
        for (int i = 0; i < objects.size(); ++i) {
            obj_x = objects[i].x;
            obj_y = objects[i].y;
            obj_dis = mat[obj_x][obj_y].dist[dest_berth];
            if (!reachable[obj_x][obj_y]) continue;
            if (objects[i].time_appear + 1000 + 500 < frame + obj_dis) continue;
            if (min_dis > obj_dis) {
                min_dis = obj_dis;
                res = i;
            }
        }
        return res;
    }

    void clear_dis() {
        for (int i = 0; i < 200; ++i) {
            for (int j = 0; j < 200; ++j) {
                dis[i][j] = inf_dist;
                vis[i][j] = 0;
            }
        }
    }

    void calc_path_to_obj() {
        fprintf(stderr, "# ROBOT:%d call calc_path_to_obj()\n", id);
        // 以 vx,vy 为源的最短路，从 x,y 出发
        /* A* 寻路，评估权重是 已经走的距离+曼哈顿距离
         * 同时存下来搜的时候的距离矩阵
         * */
        int vx = dest_obj.x;
        int vy = dest_obj.y;
        clear_dis();

        pqueue2.push(PQnode2(vx, vy, 0, 0));
        int tx, ty;
        while (!pqueue2.empty()) {
            PQnode2 p = pqueue2.top();
            pqueue2.pop();
//        fprintf(stderr, "PQ2 loop: (%d, %d): %d\n", p.x, p.y, p.dr);
            if (vis[p.x][p.y]) continue;
            vis[p.x][p.y] = 1;
            dis[p.x][p.y] = p.dr;



            int rand_base = rand() % 4;
            for (int i, j = 0; j < 4; ++j) {
                i = (rand_base + j) % 4;
                tx = p.x + mov_x[i];
                ty = p.y + mov_y[i];

                if (is_legal(tx, ty)) {
                    if (p.dr < dis[tx][ty]) {
                        pqueue2.push(PQnode2(
                                tx, ty,
                                p.dr + 2 * dis_man(tx, ty, x, y), //  + calc_force(id, tx, ty)
                                // 加入曼哈顿距离，A star 思想
                                p.dr + 1));
                    }
                }
            }

            if (p.x == x && p.y == y) {
                // 找到了
                break;
            }
        }

        while (!pqueue2.empty()) {
            // 拓宽道路，至少有横向宽度为3的路可以走
            PQnode2 p = pqueue2.top();
            pqueue2.pop();
            if (vis[p.x][p.y]) continue;
            vis[p.x][p.y] = 1;
            dis[p.x][p.y] = p.dr;
        }

#ifdef OUTPUT_DIJKSTRA
    fprintf(stderr, "#3 get_path() final dist: %d\n", dis[x][y]);
    for (int i = 0; i < 200; ++i) {
        for (int j = 0; j < 200; ++j) {
            int x = dis[i][j];
            if (x == inf_dist) x = -1;
            fprintf(stderr, "%4d", x);
        }
        fprintf(stderr, "\n");
    }
    fflush(stderr);
#endif

    }

    void calc_best_berth() {
        int min_dis = inf_dist, min_berth = -1;
        for (int i = 0; i < berth_num; ++i) {
            if (min_dis > mat[x][y].dist[i]) {
                min_dis = mat[x][y].dist[i];
                min_berth = i;
            }
        }
        dest_berth = min_berth;
        if (min_berth == -1) {
            fprintf(stderr, "# ROBOT:%d NO Reachable Berth\n", id);
            min_berth = 0;
        }
//        dest_berth = 0;
    }


    int id, x, y, frame, carry;
    robot_status status;
    int dis[201][201], vis[201][201], reachable[201][201];
    int dest_berth = 0;
    Obj dest_obj;
    robot_mov act_before_move;
    std::vector<Pos> next_moves;
    int assign_move_id;
    int is_safe;
} robot[robot_num+1];

void calc_is_safe(int id) {
    for (int i = 0; i < robot_num; ++i) {
        if (i == id) continue;
        if (dis_man(robot[id].x, robot[id].y, robot[i].x, robot[i].y) < 3) {
            robot[id].is_safe = 0;
            break;
        }
    }
}


struct Dsu {
    // 并查集
    int f[robot_num];

    void init() { for (int i = 0; i < robot_num; ++i) f[i] = i; }
    int find(int x) { return f[x] == x ? x : f[x] = find(f[x]); }
    void unite(int x, int y) { f[find(x)] = find(y); }
} dsu;


int conflict_mat[201][201][2];
int robot_priority[robot_num+1];
std::vector<int> conflict_vec[robot_num];
int conflict_vec_cnt = 0;

bool conflict_dfs(int vec_id, int rpp) {
    // rpp表示在robot_priority里处理到第几个了
//    fprintf(stderr, "dfs rpp:%d\n", rpp);
    if (rpp == conflict_vec[vec_id].size()) return true;

    int robot_id = conflict_vec[vec_id][rpp];
    int moves_len = robot[robot_id].next_moves.size();
    Pos tp;
    for (int i = 0; i < moves_len; ++i) {
        // 检查conflict
        tp = robot[robot_id].next_moves[i];
        if (conflict_mat[tp.x][tp.y][1] != -1) continue;
        if (conflict_mat[tp.x][tp.y][0] != -1 && conflict_mat[tp.x][tp.y][0] == conflict_mat[robot[robot_id].x][robot[robot_id].y][1]) {
            // 这一句表示对方从 tp 到我们这个位置上来
            // 我们默认是要去tp的位置
            continue;
        }
        conflict_mat[tp.x][tp.y][1] = robot_id;
        robot[robot_id].assign_move_id = i;
        if (conflict_dfs(vec_id, rpp+1)) return true;
        conflict_mat[tp.x][tp.y][1] = -1;
    }
    return false;
}

void handle_conflict() {
    for (int i = 0; i < 200; ++i) {
        for (int j = 0; j < 200; ++j) {
            conflict_mat[i][j][0] = -1;
            conflict_mat[i][j][1] = -1;
        }
    }
    conflict_vec_cnt = 0;
    for (int i = 0; i < robot_num; ++i) {
        conflict_vec[i].clear();
    }
    for (int i = 0; i < robot_num; ++i) {
        conflict_mat[ robot[i].x ][ robot[i].y ][0] = i;
    }

    // 分组填入conflict_vec, 把一个大的dfs拆成几个小的
    dsu.init();
    for (int i = 0; i < robot_num; ++i) {
        if (robot[i].is_safe) continue;
        for (int j = i + 1; j < robot_num; ++j) {
            if (robot[j].is_safe) continue;
            if (dis_man(robot[i].x, robot[i].y, robot[j].x, robot[j].y) < 3) {
                if (dsu.find(i) != dsu.find(j)) {
                    dsu.unite(i, j);
                }
            }
        }
    }

    /* 1. 有货的优先
     * 2. id小的优先
     * */
    int robot_pp = 0;
    for (int i = 0; i < robot_num; ++i) {
        if (robot[i].status == TO_SHIP) robot_priority[robot_pp++] = i;
    }
    for (int i = 0; i < robot_num; ++i) {
        if (robot[i].status != TO_SHIP) robot_priority[robot_pp++] = i;
    }

    int rbi, rbj;
    for (int i = 0; i < robot_num; ++i) {
        rbi = robot_priority[i];
        if (robot[rbi].is_safe) continue;
        if (dsu.find(rbi) != rbi) continue;
        // 对于并查集中不是根结点的，先跳过
        conflict_vec[conflict_vec_cnt].push_back(rbi);
        for (int j = 0; j < robot_num; ++j) {
            rbj = robot_priority[j];
            if (rbj == rbi) continue;
            if (robot[rbj].is_safe) continue;
            if (dsu.find(rbj) == rbi) {
                conflict_vec[conflict_vec_cnt].push_back(rbj);
            }
        }
        conflict_vec_cnt++;
    }

    for (int i = 0; i < conflict_vec_cnt; ++i) {
        if (!conflict_dfs(i, 0)) {
            fprintf(stderr, "# ERROR: NO POSSIBLE SOLUTION\n");
        }
    }

}

int ship_capacity;
class Ship {
public:
    Ship() {};

    void go() {
        if (berth_id == -1) return;
        fprintf(stderr, "#SHIP LEAVE\n");
        printf("go %d\n", id);
        berth[berth_id].occupy = 0;
        berth_id = -1;
        status = SHIP_SHIPPING;
        return;
    }

    void update(int state_code, int _id, int _frame) {
        frame++;
        if (state_code == 0)
            _status = SHIP_SHIPPING;
        else if (state_code == 1)
            _status = SHIP_NORMAL;
        else {
            _status = SHIP_WAITING;
        }

        if (_status != status || _id != berth_id) {
//            fprintf(stderr, "#ERROR Ship %d Sync Faild\n", id);
            status = _status;
            berth_id = _id;
        }
        if (status == SHIP_NORMAL) {
            if (berth_id == -1) {
//                for (int i = 0; i < berth_num; ++i) {
//                    if (berth[i].occupy == 0) {
//                        berth[i].occupy = 1;
//                        fprintf(stderr, "#SHIP %d CAME %d\n", id, i);
//                        berth_id = i;
//                        state_code = SHIP_SHIPPING;
//                        return;
//                    }
//                }
                if (!berth[0].occupy) {
                    fprintf(stderr, "#SHIP %d CAME %d\n", id, 0);
                    printf("ship %d %d\n", id, 0);
                    berth_id = 0;
                    state_code = SHIP_SHIPPING;
                    return;
                }
            }
            // 在泊位上
            if (loads == ship_capacity) {
                // 装满了，走人
                fprintf(stderr, "#SHIP FULL\n");
                printf("go %d\n", id);
                berth[berth_id].occupy = 0;
                berth_id = -1;
                status = SHIP_SHIPPING;
                return;
            }
            // loads < ship_capacity
            int max_obj_num = inf_dist;
            max_obj_num = min(max_obj_num, berth[berth_id].stock);
            max_obj_num = min(max_obj_num, berth[berth_id].velocity);
            max_obj_num = min(max_obj_num, ship_capacity - loads);
            berth[berth_id].stock -= max_obj_num;
            loads += max_obj_num;
        }
    }

    int id, frame;
    ship_enum status, _status;
    int berth_id;
    int loads;
} ship[ship_num+1];


struct PQnode {
    PQnode() {};
    PQnode(int _x, int _y, int _d) : x(_x), y(_y), d(_d) {};
    int x, y, d;

    bool operator < (const PQnode &x) const {
        return d > x.d;
    }
};
std::priority_queue<PQnode> pqueue;

void dijkstra(int berth_id) {
    // 给每个泊位计算最短路
    int base_x, base_y;
    base_x = berth[berth_id].x;
    base_y = berth[berth_id].y;

    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            mat[ base_x + i ][ base_y + j ].dist[berth_id] = 0;
            pqueue.push(PQnode(base_x + i, base_y + j, 0));
        }
    }

    int tx, ty;
    int vis[201][201];
    for (int i = 0; i < 200; ++i) {
        for (int j = 0; j < 200; ++j) {
            vis[i][j] = 0;
        }
    }

    while (!pqueue.empty()) {
        PQnode p = pqueue.top();
        pqueue.pop();
        if (vis[p.x][p.y]) continue;
//        fprintf(stderr, "dijk (%d,%d) d:%d\n", p.x, p.y, p.d);
        vis[p.x][p.y] = 1;
        mat[p.x][p.y].dist[berth_id] = p.d;

        for (int i = 0; i < 4; ++i) {
            tx = p.x + mov_x[i];
            ty = p.y + mov_y[i];

            if (in_mat(tx, ty) && is_land(tx, ty)) {
                if (mat[tx][ty].dist[berth_id] > p.d + 1) {
                    pqueue.push(PQnode(tx, ty, p.d+1));
                }
            }
        }
    }

}

void debug_print_dist(int berth_id) {
    for (int i = 0; i < 200; ++i) {
        for (int j = 0; j < 200; ++j) {
            int x = mat[i][j].dist[berth_id];
            if (x == inf_dist) x = -1;
            fprintf(stderr, "%3d", x);
        }
        fprintf(stderr, "\n");
    }
    fflush(stderr);
}

void Init() {
    srand((unsigned int)time(NULL));
    // 地图+机器人
    char tmp[210];
    for (int row = 0; row < 200; ++row) {
        scanf("%s", tmp);
        for (int col = 0; col < 200; ++col) {
            switch (tmp[col]) {
                case '.': mat[row][col].type = LAND; break;
                case '*': mat[row][col].type = OCEAN; break;
                case '#': mat[row][col].type = HILL; break;
                case 'A':
                    mat[row][col].type = LAND;
                    robot[robot_cnt] = Robot(row, col, robot_cnt);
                    robot_cnt++;
                    break;
                case 'B': mat[row][col].type = BERTH; break;
            }
            for (int i = 0; i < berth_num; ++i) {
                mat[row][col].dist[i] = inf_dist;
            }
        }
    }


    // 泊位信息
    for (int i = 0; i < 10; ++i) {
        scanf("%d%d%d%d%d", &berth[i].id, &berth[i].x, &berth[i].y, &berth[i].transport_time, &berth[i].velocity);
    }

    // 船舶容量
    scanf("%d", &ship_capacity);

    char okk[100];
    scanf("%s", okk);

    // 初始化船
    for (int i = 0; i < ship_num; ++i) {
        ship[i].id = i;
        ship[i].frame = 0;
    }

#ifdef DEBUG_FLAG
    for (int r = 0; r < 10; ++r) {
        fprintf(stderr, "robot %d: (%d, %d)\n", r, robot[r].x, robot[r].y);
    }
    for (int i = 0; i < 10; ++i) {
        fprintf(stderr, "berth %d: (%d, %d) t:%d v:%d\n", berth[i].id, berth[i].x, berth[i].y, berth[i].transport_time, berth[i].velocity);
    }
    fprintf(stderr, "capacity %d\n", ship_capacity);
    fprintf(stderr, "OK from Judge: %s\n", okk);
#endif

    /*
     * 给每一个泊位都初始化一下全图的最短路
     * 存在mat[x][y].dist[berth_id]里
     * 可以在机器人建起物品后快速判断去哪个泊位
     * 同时也用于去向泊位的过程中选择路径
     * */
    for (int i = 0; i < berth_num; ++i) {
        dijkstra(i);
    }


    // 每个机器人初始化可到达的范围、可到达的港口
    for (int i = 0; i < robot_num; ++i) {
        robot[i].init();
    }
//    debug_print_dist(0);


    fprintf(stdout, "OK\n");
    fflush(stdout);

    fprintf(stderr, "#1 Init Finish\n");
}

void Input(int &frame_id) {
    int money;
    int k;
    scanf("%d%d", &frame_id, &money);
    scanf("%d", &k);

//    fprintf(stderr, "#2 Input frame: %d, money: %d, k: %d\n", frame_id, money, k);
    // 新增的货物信息
    int x, y, value;
    for (int i = 0; i < k; ++i) {
        scanf("%d%d%d", &x, &y, &value);
        objects.push_back(Obj(x, y, value, frame_id));
        mat[x][y].has_obj = true;
    }

    // 当前的机器人信息
    int carry, status; // 还有x, y
    for (int i = 0; i < 10; ++i) {
        scanf("%d%d%d%d", &carry, &x, &y, &status);
        robot[i].sync(carry, x, y, status, frame_id);
    }

    // 当前的船信息
    int dest_berth;
    for (int i = 0; i < 5; ++i) {
        scanf("%d%d", &status, &dest_berth);
        ship[i].update(status, dest_berth, frame_id);
    }
    char okk[100];
    scanf("%s", okk);
}



int main() {
    fprintf(stderr, "#0 Program start");
    Init();
    int frame_id;
    for (int frame = 0; frame < 15000; frame++) {
        if (frame == 12900) {
            for (int i = 0; i < 5; ++i) {
                ship[i].go();
            }
        }
        Input(frame_id);
        fprintf(stderr, "#2 Input Finish\n");
        remove_outdated_obj(frame_id);

        for (int i = 0; i < robot_num; ++i) robot[i].think();
        handle_conflict();
        for (int i = 0; i < robot_num; ++i) robot[i].act();
        fprintf(stdout, "OK\n");
        fflush(stdout);
        fprintf(stderr, "#4 Output Finish\n");
    }
    fprintf(stderr, "#TOTAL VALUE %d\n", tot_value);
    return 0;
}