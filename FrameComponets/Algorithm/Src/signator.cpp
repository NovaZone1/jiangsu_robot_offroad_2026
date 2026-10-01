#include "signator.hpp"

/*************************************     二阶TD      *************************************/

void LinearTD_2nd::Init(float _r, float _dt, float _max_v, float _max_a)
{
    r = _r;
    dt = _dt;
    max_v = _max_v;
    max_a = _max_a;
    v1 = 0.0f;
    v2 = 0.0f;
}

void LinearTD_2nd::Update(float target_input)
{
    // 误差
    float error = v1 - target_input;

    // 计算期望加加速度jerk，一般是按临界阻尼配置
    float jerk = -r * r * error - 2.0f * r * v2;

    v2 += jerk * dt;

    if (max_a > 0.0f)
    {
        v2 = StdMath::fclamp(v2, max_a);
    }

    // 欧拉积分更新
    v1 += v2 * dt;

    if (max_v > 0.0f)
    {
        v1 = StdMath::fclamp(v1, max_v);
    }
}

/*************************************     三阶TD      *************************************/

void LinearTD_3rd::Init(float _r, float _dt, float _max_v, float _max_a)
{
    r = _r;
    dt = _dt;
    max_v = _max_v;
    max_a = _max_a;
    v1 = 0.0f;
    v2 = 0.0f;
    v3 = 0.0f;
}

void LinearTD_3rd::Update(float target_input)
{
    float error = v1 - target_input;

    // 计算加加速度
    float jerk = -r * r * r * error - 3.0f * r * r * v2 - 3.0f * r * v3;

    // 积分更新加速度 v3
    v3 += jerk * dt;

    // 限制加速度
    v3 = StdMath::fclamp(v3, max_a);

    // 积分更新速度 v2
    v2 += v3 * dt;

    // 限制速度
    v2 = StdMath::fclamp(v2, max_v);

    // 积分更新位置 v1
    v1 += v2 * dt;
}

/*************************************     一阶低通滤波器 *************************************/

void LowPassFilter::Init(float cutoff_freq_rad, float dt) // 这个的截止频率是以 rad/s 为单位的
{
    if (cutoff_freq_rad <= 0 || dt <= 0)
    {
        alpha = 1;
        return;
    }
    // 计算环节时间常数
    float time_const = 1.0f / cutoff_freq_rad;

    // 计算滤波系数
    alpha = dt / (time_const + dt);
}

void LowPassFilter::InitHz(float cutoff_freq_hz, float dt) // 这个的截止频率是以 Hz 为单位的
{
    if (cutoff_freq_hz <= 0 || dt <= 0)
    {
        alpha = 1;
        return;
    }
    float omega_c = 2 * PI * cutoff_freq_hz; // Hz转rad/s

    // 同上
    float time_const = 1.0f / omega_c;
    alpha = dt / (time_const + dt);
}

float LowPassFilter::Filter(float input)
{
    // 很简单的一阶低通滤波，懒得打注释了
    float y = alpha * input + (1.0f - alpha) * y_prev;
    y_prev = y;
    return y;
}

/*************************************     一阶高通滤波器 *************************************/

void HighPassFilter::Init(float cutoff_freq_hz, float dt)
{
    if (cutoff_freq_hz <= 0 || dt <= 0)
    {
        alpha = 0;
        Reset();
        return;
    }
    // 先计算环节的时间常数
    float time_const = 1.0f / (2.0f * 3.1415926f * cutoff_freq_hz);

    // 计算滤波系数
    alpha = time_const / (time_const + dt);

    Reset();
}

float HighPassFilter::Filter(float input)
{
    // 计算一阶高通滤波输出
    float output = alpha * (prev_output + input - prev_input);

    prev_input = input;
    prev_output = output;

    return output;
}

/*************************************     摩擦力补偿器      *************************************/

void FrictionCompensator::Init(float F_static, float F_dynamic, float V_tran_start,
                               float V_tran_end)
{
    fric_static = F_static;
    fric_dynamic = F_dynamic;
    vel_transition_start = V_tran_start;
    vel_transition_end = V_tran_end;

    // 截止频率12rad/s，周期1ms
    lpf_fric.Init(5.0f, 0.001f);
}

float FrictionCompensator::GetFriction(float v_real)
{
    // 不再需要判断意图，只试图恢复真实物理世界的摩擦力
    float i_total = 0.0f;

    // 若速度很快，直接使用动摩擦补偿
    if (fabs(v_real) > vel_transition_end)
    {
        // 根据符号判定，摩擦力与速度方向相反
        i_total = -fric_dynamic * StdMath::signf(v_real);
    }

    // 速度在过渡区间，线性插值
    else if (fabs(v_real) > vel_transition_start)
    {
        // 计算线性比
        float ratio =
            (fabs(v_real) - vel_transition_start) / (vel_transition_end - vel_transition_start);
        // 根据符号判定，摩擦力与速度方向相反
        i_total = -(fric_static + (fric_dynamic - fric_static) * ratio) * StdMath::signf(v_real);
    }

    // 速度很小，使用静摩擦补偿
    else
    {
        i_total = -fric_static * StdMath::signf(v_real);
    }

    return i_total;
}

float FrictionCompensator::GetSimpleFriction(float v_real)
{
    // 简单模型：只有静摩擦补偿
    float i_total = 0.0f;

    // 使用静摩擦补偿
    i_total = -fric_static * StdMath::signf(v_real) * 0.75f;

    return i_total;
}

float FrictionCompensator::Get_Compensation(float real_u, float real_velo)
{
    // 构造一个p，表示控制器的能量意图；当p > 0，系统希望加速；反之希望刹车
    float p = real_u * real_velo;

    float i_total = 0.0f;

    // 当系统希望加速时，使用正向摩擦补偿
    if (p > 0 && fabs(real_u) > 0.25f)
    {
        // 若速度很快，直接使用动摩擦补偿
        if (fabs(real_velo) > vel_transition_end)
        {
            // 根据符号补偿
            i_total = fric_dynamic * StdMath::signf(real_velo);
        }
        // 速度在过渡区间，线性插值
        else if (fabs(real_velo) > vel_transition_start)
        {
            // 计算线性比
            float ratio = (fabs(real_velo) - vel_transition_start) /
                          (vel_transition_end - vel_transition_start);
            // 根据符号补偿
            i_total =
                (fric_static + (fric_dynamic - fric_static) * ratio) * StdMath::signf(real_velo);
        }
        // 速度很小，使用静摩擦补偿
        else
        {
            i_total = fric_static * StdMath::signf(real_velo);
        }
    }
    // 当系统希望能量减小，或不变时，实际上摩擦力在帮忙刹车，不进行补偿
    else
    {
        i_total = 0.0f;
    }

    // 低通滤波，防止突变
    i_total = lpf_fric.Filter(i_total);

    return i_total;
};

/*************************************     方波发生器      *************************************/

void SquareInjector::Init(float amp, float per)
{
    amplitude = amp;
    period = per > 0 ? per : 1;
}

void SquareInjector::InitHz(float amp, float freq_hz)
{
    amplitude = amp;
    period = freq_hz > 0 ? 1.0f / freq_hz : 1.0f;
}

float SquareInjector::GetValue(float time_sec)
{
    float phase = fmod(time_sec, period);
    if (phase < (period / 2.0f))
    {
        return amplitude;
    }
    else
    {
        return -amplitude;
    }
}

float SquareInjector::AutoGetValue()
{
    static float start_time = BspDwt_GetTimeline_Sec();
    float current_time = BspDwt_GetTimeline_Sec();
    return GetValue(current_time - start_time);
}

/*************************************     IV辨识器      *************************************/

// 构造函数初始化
IVIdentifier::IVIdentifier(float b0_nominal, float fs, float cut_freq, float forget_time)
    : b0_(b0_nominal)
{
    if (!isfinite(fs) || fs <= 0)
    {
        fs = 1000.0f;
    }
    if (!isfinite(cut_freq) || cut_freq <= 0)
    {
        cut_freq = 1.0f;
    }
    if (!isfinite(forget_time) || forget_time <= 0)
    {
        forget_time = 0.1f;
    }
    if (!isfinite(b0_))
    {
        b0_ = 0;
    }
    float dt = 1.0f / fs;

    // 配置高通滤波器
    hpf_r_.Init(cut_freq, dt);
    hpf_u_.Init(cut_freq, dt);
    hpf_z3_.Init(cut_freq, dt);

    // 计算遗忘系数（不能太短）
    if (forget_time < 0.1f)
    {
        forget_time = 0.1f;
    }
    lambda_ = StdMath::fclamp(1.0f - (dt / forget_time), 0.0f, 0.999999f);

    lambad_rho = StdMath::fclamp(1.0f - (dt / 0.125f), 0.0f, 0.999999f);

    Reset();
}

void IVIdentifier::Reset()
{
    theta_iv = 0.0f;
    cov_cross = 0.0f;
    cov_self = 0.0f;
    theta_iv_accum_ = cov_ru = var_r = var_u = rho_ru = 0;
    hpf_alpha_ = J_hat_ = r_ac = u_ac = z3_ac = 0;
    hpf_r_.Reset();
    hpf_u_.Reset();
    hpf_z3_.Reset();
}

void IVIdentifier::Update(float r_cmd, float u_ctrl, float z3_obs)
{
    if (!isfinite(r_cmd) || !isfinite(u_ctrl) || !isfinite(z3_obs))
    {
        return;
    }
    // 我们首先通过高通滤波获取动态分量，相当于去均值
    r_ac = hpf_r_.Filter(r_cmd);
    u_ac = hpf_u_.Filter(u_ctrl);
    z3_ac = hpf_z3_.Filter(z3_obs);

    // 接着计算相关性矩阵
    cov_cross = lambda_ * cov_cross + r_ac * z3_ac; // 互相关矩阵
    cov_self = lambda_ * cov_self + r_ac * u_ac; // 自相关矩阵

    // 计算相关系数
    cov_ru = lambad_rho * cov_ru + r_ac * u_ac;
    var_r = lambad_rho * var_r + r_ac * r_ac;
    var_u = lambad_rho * var_u + u_ac * u_ac;

    // 为了防止激发模态不足，导致的不可辨识问题，这里利用 参考输入的自相关判断
    // 计算r和u的相关系数
    rho_ru = StdMath::fclamp(cov_ru / (sqrtf(var_r * var_u) + 1e-6f), 1.0f);

    // 激发模态充分的前提下，计算辨识参数 theta_iv
    if (rho_ru > 0.225f && fabsf(cov_self) > 1e-12f)
    {
        float raw_theta = cov_cross / cov_self;

        // 一阶低通滤波防止突变
        theta_iv = 0.99f * theta_iv + 0.01f * raw_theta;

        // 再加一级低通滤波
        theta_iv_accum_ = 0.999f * theta_iv_accum_ + 0.001f * theta_iv;
    }

    // 计算估计的转动惯量 J_hat_
    float real_b = theta_iv_accum_ + b0_;

    if (real_b < 0.0001f)
    {
        real_b = 0.0001f; // 保护数据
    }
    J_hat_ = StdMath::fclamp(Kt_ / real_b, 0.1f);
}

float IVIdentifier::GetEstimatedJ(float Kt) const
{
    // 原理: theta = b_real - b0
    // b_real = theta + b0
    // J_real = Kt / b_real
    float b_real = theta_iv + b0_;

    // 保护：防止分母为0或负数（惯量不可能是负的）
    if (b_real < 0.0001f)
    {
        b_real = 0.0001f;
    }

    return Kt / b_real;
}
