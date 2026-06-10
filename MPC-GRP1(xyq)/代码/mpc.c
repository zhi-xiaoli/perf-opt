#include "mpc.h"

//新增2个内存管理函数
void init_state_soa(StateSoA_t *s, int horizon) {
    s->capacity = horizon + 1;
    size_t size = s->capacity * sizeof(float);
    // 16字节对齐以适配 NEON 指令
    s->x    = (float*)aligned_alloc(16, size);
    s->y    = (float*)aligned_alloc(16, size);
    s->psi  = (float*)aligned_alloc(16, size);
    s->v    = (float*)aligned_alloc(16, size);
    s->cte  = (float*)aligned_alloc(16, size);
    s->epsi = (float*)aligned_alloc(16, size);
}

void free_state_soa(StateSoA_t *s) {
    free(s->x); free(s->y); free(s->psi);
    free(s->v); free(s->cte); free(s->epsi);
}

// 参考路径函数实现
static inline float ref_path(float x) {
    return sinf(0.2f * x) + 0.1f * x;
}

static inline float ref_path_derivative(float x) {
    return 0.2f * cosf(0.2f * x) + 0.1f;
}

// 角度归一化函数实现
static inline float normalize_angle(float angle) {
    while (angle > M_PI)
        angle -= 2.0f * M_PI;
    while (angle < -M_PI)
        angle += 2.0f * M_PI;
    return angle;
}

// 内存管理函数实现
void init_controls(Controls_t *ctrl, int horizon) {
    ctrl->horizon = horizon;
    ctrl->delta = (float*)aligned_alloc(16, horizon * sizeof(float));
    ctrl->a = (float*)aligned_alloc(16, horizon * sizeof(float));
}

void free_controls(Controls_t *ctrl) {
    if (ctrl->delta) free(ctrl->delta);
    if (ctrl->a) free(ctrl->a);
    ctrl->delta = NULL;
    ctrl->a = NULL;
    ctrl->horizon = 0;
}

static inline void copy_controls(Controls_t *dest, const Controls_t *src) {
    dest->horizon = src->horizon;
    memcpy(dest->delta, src->delta, src->horizon * sizeof(float));
    memcpy(dest->a, src->a, src->horizon * sizeof(float));
}

// 核心MPC函数实现
//旧
void simulate(State_t *s, float *cos_psi, float *sin_psi, float delta, float a) {
    float vx = *cos_psi;
    float vy = *sin_psi;

    float delta_psi = s->v * tanf(delta) / LF * DT;
    float cos_dpsi = cosf(delta_psi);
    float sin_dpsi = sinf(delta_psi);

    float new_vx = vx * cos_dpsi - vy * sin_dpsi;
    float new_vy = vy * cos_dpsi + vx * sin_dpsi;

    s->x += s->v * new_vx * DT;
    s->y += s->v * new_vy * DT;

    // 推进速度
    s->v += a * DT;
    if (s->v < 0.01f) s->v = 0.01f;
    else if (s->v > 20.0f) s->v = 20.0f;

    // 更新方向角与 cos/sin
    *cos_psi = new_vx;
    *sin_psi = new_vy;
    s->psi = atan2f(new_vy, new_vx);
}

//SoA优化后
static inline void simulate_soa(StateSoA_t *s, int t, float delta, float a, 
                                float *curr_cos, float *curr_sin) {
    float v = s->v[t];
    float delta_psi = v * tanf(delta) / LF * DT;
    
    float cos_dpsi = cosf(delta_psi);
    float sin_dpsi = sinf(delta_psi);

    // 更新方向向量
    float next_cos = (*curr_cos) * cos_dpsi - (*curr_sin) * sin_dpsi;
    float next_sin = (*curr_sin) * cos_dpsi + (*curr_cos) * sin_dpsi;

    // 更新位置和速度到下一时刻 (t+1)
    s->x[t+1] = s->x[t] + v * next_cos * DT;
    s->y[t+1] = s->y[t] + v * next_sin * DT;
    
    float next_v = v + a * DT;
    s->v[t+1] = (next_v < 0.01f) ? 0.01f : ((next_v > 20.0f) ? 20.0f : next_v);
    
    s->psi[t+1] = atan2f(next_sin, next_cos);
    *curr_cos = next_cos;
    *curr_sin = next_sin;
}


float local_cost(const State_t *s,
                const Controls_t *u,
                const Controls_t *prev_u,
                int t) {
    float ref_x = s->x;
    float f_x = ref_path(ref_x);
    float f_prime = ref_path_derivative(ref_x);
    float desired_psi = atanf(f_prime);

    float cte = s->y - f_x;
    float epsi = normalize_angle(s->psi - desired_psi);
    float v_err = REF_V - s->v;

    float cost = WEIGHT_CTE * cte * cte +
                 WEIGHT_EPSI * epsi * epsi +
                 WEIGHT_V * v_err * v_err +
                 WEIGHT_DELTA * u->delta[t] * u->delta[t] +
                 WEIGHT_A * u->a[t] * u->a[t];
        
    if (prev_u != NULL && t > 0) {
        float delta_diff = u->delta[t] - prev_u->delta[t - 1];
        float a_diff = u->a[t] - prev_u->a[t - 1];

        cost += WEIGHT_DELTA_DIFF * delta_diff * delta_diff;
        cost += WEIGHT_A_DIFF * a_diff * a_diff;

        float delta_rate = fabsf(delta_diff / DT);
        if (delta_rate > MAX_DELTA_RATE){
            float penalty = delta_rate - MAX_DELTA_RATE;
            cost += 1000.0f * penalty * penalty;
        }
    }

    if (s->v < 0.0f) {
        cost += 5000.0f * s->v * s->v;
    }

    if (fabsf(epsi) > M_PI / 2.0f) {
        float penalty = fabsf(epsi) - (M_PI / 2.0f);
        cost += 1000.0f * penalty * penalty;
    }

    return cost;
}

//旧
float rollout_tail_cost(State_t start,
                       float cos_psi,
                       float sin_psi,
                       const Controls_t *u_seq,
                       int start_t, int end_t) {
    float cost = 0.0f;
    State_t s = start;
    int horizon_len = u_seq->horizon;
    int max_t = (start_t + horizon_len - 1 < end_t) ? (start_t + horizon_len - 1) : end_t;

    for (int t = start_t; t <= max_t; ++t) {
        simulate(&s, &cos_psi, &sin_psi, u_seq->delta[t], u_seq->a[t]);
        cost += local_cost(&s, u_seq, u_seq, t);
    }
    return cost;
}

//soa优化后
float rollout_tail_cost_soa(const StateSoA_t *base_traj, int start_t, const Controls_t *u_seq) {
    float total_cost = 0.0f;
    int horizon = u_seq->horizon;

    // 1. 从 SoA 中提取当前时间步的起始状态作为局部标量
    // 这样做可以充分利用寄存器，避免在循环中频繁访问内存
    float cur_x   = base_traj->x[start_t];
    float cur_y   = base_traj->y[start_t];
    float cur_psi = base_traj->psi[start_t];
    float cur_v   = base_traj->v[start_t];
    
    // 预计算三角函数，减少循环内开销
    float c_cos = cosf(cur_psi);
    float c_sin = sinf(cur_psi);

    // 2. 预测时界内的模拟与代价累加
    for (int t = start_t; t < horizon; ++t) {
        // --- 模拟步骤 (等同于原 simulate 函数，但使用标量) ---
        float delta = u_seq->delta[t];
        float a     = u_seq->a[t];

        // 计算航向角变化 
        float delta_psi = cur_v * tanf(delta) / LF * DT;
        float cos_dpsi = cosf(delta_psi);
        float sin_dpsi = sinf(delta_psi);

        // 更新方向向量 (旋转矩阵简化版)
        float next_cos = c_cos * cos_dpsi - c_sin * sin_dpsi;
        float next_sin = c_sin * cos_dpsi + c_cos * sin_dpsi;

        // 更新位置 
        cur_x += cur_v * next_cos * DT;
        cur_y += cur_v * next_sin * DT;
        
        // 更新速度并应用硬约束 
        cur_v += a * DT;
        if (cur_v < 0.01f) cur_v = 0.01f;
        else if (cur_v > 20.0f) cur_v = 20.0f;

        // 更新航向角和缓存的三角函数值
        cur_psi = atan2f(next_sin, next_cos);
        c_cos = next_cos;
        c_sin = next_sin;

        // 代价计算步骤 (等同于 local_cost) 
        float ref_y = ref_path(cur_x);
        float f_prime = ref_path_derivative(cur_x);
        float desired_psi = atanf(f_prime);

        float cte = cur_y - ref_y;
        float epsi = normalize_angle(cur_psi - desired_psi);
        float v_err = REF_V - cur_v;

        // 基础权重代价累加 
        total_cost += WEIGHT_CTE * cte * cte +
                      WEIGHT_EPSI * epsi * epsi +
                      WEIGHT_V * v_err * v_err +
                      WEIGHT_DELTA * delta * delta +
                      WEIGHT_A * a * a;

        // 惩罚项：如果航向偏差过大，增加额外代价 
        if (fabsf(epsi) > M_PI / 2.0f) {
            float penalty = fabsf(epsi) - (M_PI / 2.0f);
            total_cost += 1000.0f * penalty * penalty;
        }
    }

    return total_cost;
}

//旧梯度计算
void compute_gradient(State_t current,
                     Controls_t *u_seq,
                     Controls_t *grad) 
{
    int horizon = u_seq->horizon;
    State_t traj[horizon+1];
    float cos_arr[horizon+1];
    float sin_arr[horizon+1];
    
    traj[0] = current;
    cos_arr[0] = cosf(current.psi);
    sin_arr[0] = sinf(current.psi);
    
    for (int t = 0; t < horizon; t++) {
        traj[t+1] = traj[t];
        cos_arr[t+1] = cos_arr[t];
        sin_arr[t+1] = sin_arr[t];
        simulate(&traj[t+1], &cos_arr[t+1], &sin_arr[t+1], 
                 u_seq->delta[t], u_seq->a[t]);
    }

    float cost_base_tail[horizon];
    #pragma omp parallel for
    for (int t = 0; t < horizon; t++) {
        cost_base_tail[t] = rollout_tail_cost(
            traj[t], cos_arr[t], sin_arr[t], 
            u_seq, t, horizon-1
        );
    }

    #pragma omp parallel for
    for (int t = 0; t < horizon; t++) {

        float orig_delta = u_seq->delta[t];
        float orig_a = u_seq->a[t];
        
        // delta梯度
        u_seq->delta[t] = orig_delta + GRAD_EPS;
        float cost_plus_delta = rollout_tail_cost(
            traj[t], cos_arr[t], sin_arr[t], 
            u_seq, t, horizon-1
        );
        u_seq->delta[t] = orig_delta;
        
        // a梯度
        u_seq->a[t] = orig_a + GRAD_EPS;
        float cost_plus_a = rollout_tail_cost(
            traj[t], cos_arr[t], sin_arr[t], 
            u_seq, t, horizon-1
        );
        u_seq->a[t] = orig_a;
        
        // 有限差分
        grad->delta[t] = (cost_plus_delta - cost_base_tail[t]) / GRAD_EPS;
        grad->a[t] = (cost_plus_a - cost_base_tail[t]) / GRAD_EPS;
    }
}

//SoA优化后的梯度计算
void compute_gradient_soa(State_t current, Controls_t *u_seq, Controls_t *grad) {
    int horizon = u_seq->horizon;
    StateSoA_t traj;
    init_state_soa(&traj, horizon);

    // 1. 初始化基础状态
    traj.x[0] = current.x;
    traj.y[0] = current.y;
    traj.psi[0] = current.psi;
    traj.v[0] = current.v;

    float c_cos = cosf(current.psi);
    float c_sin = sinf(current.psi);

    // 2. 正向生成基础轨迹 (SoA 写入)
    for (int t = 0; t < horizon; t++) {
        simulate_soa(&traj, t, u_seq->delta[t], u_seq->a[t], &c_cos, &c_sin);
    }

    // 3. 计算基础代价
    float cost_base_tail[horizon];
    #pragma omp parallel for
    for (int t = 0; t < horizon; t++) {
        cost_base_tail[t] = rollout_tail_cost_soa(&traj, t, u_seq);
    }

    // 4. 并行有限差分计算梯度
    #pragma omp parallel for
    for (int t = 0; t < horizon; t++) {
        float orig_delta = u_seq->delta[t];
        float orig_a = u_seq->a[t];

        // 计算 Delta 梯度
        u_seq->delta[t] = orig_delta + GRAD_EPS;
        float cost_plus_delta = rollout_tail_cost_soa(&traj, t, u_seq);
        u_seq->delta[t] = orig_delta;

        // 计算 A 梯度
        u_seq->a[t] = orig_a + GRAD_EPS;
        float cost_plus_a = rollout_tail_cost_soa(&traj, t, u_seq);
        u_seq->a[t] = orig_a;

        grad->delta[t] = (cost_plus_delta - cost_base_tail[t]) / GRAD_EPS;
        grad->a[t] = (cost_plus_a - cost_base_tail[t]) / GRAD_EPS;
    }

    free_state_soa(&traj);
}


void neon_update_controls(Controls_t* u_seq, const Controls_t* grad) {
    int horizon = u_seq->horizon;
    const float32x4_t v_lr = vdupq_n_f32(LEARNING_RATE);
    const float32x4_t v_max_delta = vdupq_n_f32(MAX_DELTA);
    const float32x4_t v_min_delta = vdupq_n_f32(-MAX_DELTA);
    const float32x4_t v_max_a = vdupq_n_f32(MAX_A);
    const float32x4_t v_min_a = vdupq_n_f32(-MAX_A);

    #pragma omp parallel for simd
    for (int i = 0; i < horizon; i += 4) {
        // Delta处理
        float32x4_t v_delta = vld1q_f32(&u_seq->delta[i]);
        float32x4_t v_grad_d = vld1q_f32(&grad->delta[i]);
        v_delta = vmlsq_f32(v_delta, v_lr, v_grad_d / 10000); 
        v_delta = vmaxq_f32(vminq_f32(v_delta, v_max_delta), v_min_delta);
        vst1q_f32(&u_seq->delta[i], v_delta);

        // A处理
        float32x4_t v_a = vld1q_f32(&u_seq->a[i]);
        float32x4_t v_grad_a = vld1q_f32(&grad->a[i]);
        v_a = vmlsq_f32(v_a, v_lr, v_grad_a / 1000); 
        v_a = vmaxq_f32(vminq_f32(v_a, v_max_a), v_min_a);
        vst1q_f32(&u_seq->a[i], v_a);
    }
}

void print_predicted_trajectory(State_t s, const Controls_t* plan_out) {
    int horizon = plan_out->horizon;
    //StateSoA_t state = s;
    //float cos_psi = cosf(state.psi);
    //float sin_psi = sinf(state.psi);
    //float cte_avg = 0.0f;
    float cur_x = s.x;
    float cur_y = s.y;
    float cur_psi = s.psi;
    float cur_v = s.v;
    
    float c_cos = cosf(cur_psi);
    float c_sin = sinf(cur_psi);
    float cte_avg = 0.0f;

    for (int t = 0; t < horizon; ++t) {
        float delta = plan_out->delta[t];
        float a = plan_out->a[t];

        // 模拟逻辑 (直接写在这里或调用专门的标量模拟函数)
        float delta_psi = cur_v * tanf(delta) / LF * DT;
        float cos_dpsi = cosf(delta_psi);
        float sin_dpsi = sinf(delta_psi);

        float next_cos = c_cos * cos_dpsi - c_sin * sin_dpsi;
        float next_sin = c_sin * cos_dpsi + c_cos * sin_dpsi;

        cur_x += cur_v * next_cos * DT;
        cur_y += cur_v * next_sin * DT;
        cur_v += a * DT;
        // 限制速度
        //cur_v = (cur_v < 0.01f) ? 0.01f : ((cur_v > 20.0f) ? 20.0f : cur_v);
        //cur_psi = atan2f(next_sin, next_cos);
        if (cur_v < 0.01f) cur_v = 0.01f;
        else if (cur_v > 20.0f) cur_v = 20.0f;

        c_cos = next_cos;
        c_sin = next_sin;

        //计算CTE
        float ref_y = ref_path(cur_x);
        float cte = cur_y - ref_y;
        cte_avg += fabsf(cte);
    }

    printf("Average CTE over horizon: %.3f\n", cte_avg / (float)horizon);
}

void mpc_control_with_plan(State_t current, Controls_t *plan_out) {
    Controls_t u_seq;
    Controls_t grad;
    u_seq.horizon = plan_out->horizon;
    grad.horizon = plan_out->horizon;
    init_controls(&u_seq, u_seq.horizon);
    init_controls(&grad, grad.horizon);
    int horizon = plan_out->horizon;
    
    // 初始化控制序列
    #pragma omp parallel for
    for (int i = 0; i < horizon; ++i) { 
        u_seq.delta[i] = 0.0f;
        u_seq.a[i] = 0.0f;
    }
    
    #pragma omp parallel for
    for (int iter = 0; iter < MAX_ITER; ++iter) {
        compute_gradient_soa(current, &u_seq, &grad);
        neon_update_controls(&u_seq, &grad);
        // 控制变化率约束
        for (int t = 1; t < horizon; ++t) {
            float delta_rate = u_seq.delta[t] - u_seq.delta[t - 1];
            if (fabsf(delta_rate) > MAX_DELTA_RATE * DT) {
                u_seq.delta[t] = u_seq.delta[t - 1] + SIGN(delta_rate) * MAX_DELTA_RATE * DT;
            }

            float a_rate = u_seq.a[t] - u_seq.a[t - 1];
            if (fabsf(a_rate) > MAX_A_RATE * DT) {
                u_seq.a[t] = u_seq.a[t - 1] + SIGN(a_rate) * MAX_A_RATE * DT;
            }
        }
    }
    
    #pragma omp parallel for
    for (int i = 0; i < horizon; ++i) {
        plan_out->delta[i] = u_seq.delta[i];
        plan_out->a[i] = u_seq.a[i];
    }
    
    free_controls(&u_seq);
    free_controls(&grad);
}

