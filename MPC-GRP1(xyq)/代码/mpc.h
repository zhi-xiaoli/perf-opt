#ifndef _MINIMAL_MPC_OPT_H
#define _MINIMAL_MPC_OPT_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <arm_neon.h>
#include <omp.h>
#include <time.h>

// 常量定义
#define DT 0.05f
#define LF 2.67f
#define REF_V 10.0f
#define MAX_ITER 50       
#define LEARNING_RATE 0.002f 
#define GRAD_EPS 1e-2f 

#define WEIGHT_CTE 10.0f
#define WEIGHT_EPSI 10.0f
#define WEIGHT_V 5.0f
#define WEIGHT_DELTA 0.1f
#define WEIGHT_A 0.1f
#define WEIGHT_DELTA_DIFF 10.0f
#define WEIGHT_A_DIFF 5.0f
#define MAX_ROLLOUT_LEN 50

#ifndef M_PI
#define M_PI 3.14159265358979323846f   
#endif

#define MAX_DELTA 0.5f
#define MAX_A 1.0f
#define MAX_DELTA_RATE 0.5f
#define MAX_A_RATE 2.0f

#define SIGN(x) ((x) > 0 ? 1.0f : ((x) < 0 ? -1.0f : 0.0f))

// 结构体定义
typedef struct {
    float x;
    float y;
    float psi;
    float v;
    float cte;
    float epsi;
} State_t;

typedef struct {
    int horizon;
    float *delta;
    float *a;
} Controls_t;

// 原有的 State_t 保留用于单点输入，新增 StateSoA_t 用于预测序列
typedef struct {
    float *x;
    float *y;
    float *psi;
    float *v;
    float *cte;
    float *epsi;
    int capacity; 
} StateSoA_t;

// 新增内存管理函数声明
void init_state_soa(StateSoA_t *s, int horizon);
void free_state_soa(StateSoA_t *s);

// 函数声明
// 参考路径函数
static inline float ref_path(float x);
static inline float ref_path_derivative(float x);

// 工具函数
static inline float normalize_angle(float angle);

// 内存管理函数
void init_controls(Controls_t *ctrl, int horizon);
void free_controls(Controls_t *ctrl);
static inline void copy_controls(Controls_t *dest, const Controls_t *src);

// 核心MPC函数
void simulate(State_t *s, float *cos_psi, float *sin_psi, float delta, float a);
static inline void simulate_soa(StateSoA_t *s, int t, float delta, float a, float *curr_cos, float *curr_sin);

float local_cost(const State_t *s, const Controls_t *u, const Controls_t *prev_u, int t);

float rollout_tail_cost(State_t start, float cos_psi, float sin_psi, 
                        const Controls_t *u_seq, int start_t, int end_t);
float rollout_tail_cost_soa(const StateSoA_t *base_traj, int start_t, const Controls_t *u_seq);

void compute_gradient(State_t current, Controls_t *u_seq, Controls_t *grad);
void compute_gradient_soa(State_t current, Controls_t *u_seq, Controls_t *grad);

void neon_update_controls(Controls_t* u_seq, const Controls_t* grad);
void print_predicted_trajectory(State_t s, const Controls_t* plan_out);
void mpc_control_with_plan(State_t current, Controls_t *plan_out);

// Linux接口函数
/* horizon：预测步长, input：输入车辆状态, output：输出控制序列
返回 0 表示成功，-3 表示控制超界
*/
int mpc_linux_iopointer(int horizon, const void *input, void *output); //input指向State_t, output指向Controls_t 
int mpc_linux_ioself_profiling(int horizon);
int mpc_linux_ioself(int horizon);

#endif // _MINIMAL_MPC_OPT_H