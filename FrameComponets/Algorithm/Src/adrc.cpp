#include "adrc.hpp"

/**
 * @brief 初始化 ESO
 * @param _omega_o 观测器带宽 (Hz)
 * @param _J 转动惯量 (kg*m^2)
 * @param _Kt 转矩常数 (N*m/A)
 * @param _dt 控制周期 (单位：s)
 */
void ESO::Init(float _omega_o, float _J, float _Kt, float _dt)
{
    z1 = z2 = z3 = 0;
    beta1 = beta2 = beta3 = b0 = J = dt = 0;
    eta_filt.y_prev = 0;
    if (!isfinite(_omega_o) || !isfinite(_J) || !isfinite(_Kt) || !isfinite(_dt) || _omega_o <= 0 ||
        _J <= 0 || _Kt == 0 || _dt <= 0)
    {
        return;
    }
    J = _J;
    dt = _dt;
    b0 = _Kt / _J;
    _base_omega_o = _omega_o * (2.0f * PI); // 转换为 rad/s

    // 初始化衰减滤波器
    eta_filt.InitHz(8.0f, dt);

    // 根据带宽计算 ESO 增益
    beta1 = 3.0f * _base_omega_o;
    beta2 = 3.0f * _base_omega_o * _base_omega_o;
    beta3 = _base_omega_o * _base_omega_o * _base_omega_o;
}

/**
 * @brief 根据带宽计算 ESO 增益
 * @param _omega_o 观测器带宽 (Hz)
 */
void ESO::UpdateOmega_O(float _omega_o)
{
    if (!isfinite(_omega_o) || _omega_o <= 0)
    {
        return;
    }
    _omega_o = _omega_o * (2.0f * PI); // 转换为 rad/s

    beta1 = 3.0f * _omega_o;
    beta2 = 3.0f * _omega_o * _omega_o;
    beta3 = _omega_o * _omega_o * _omega_o;
}

/**
 * @brief 非线性误差处理函数
 */
float ESO::GetNonlinearError(float raw_err)
{
    float abs_e = (raw_err > 0) ? raw_err : -raw_err;
    float sign_e = (raw_err > 0) ? 1.0f : (raw_err < 0 ? -1.0f : 0.0f);

    if (abs_e > delta)
    {
        // 非线性段
        return powf(abs_e, alpha) * sign_e;
    }
    else
    {
        // 线性段
        float linear_gain = 1.0f / powf(delta, 1.0f - alpha);
        return raw_err * linear_gain;
    }
}

/**
 * @brief 更新观测器状态
 * @param u_current 实际给电机的电流 (A) -> last_u
 * @param y_measure 测量到的位置 (rad) -> measure_theta_total
 */
void ESO::Observe(float u_current, float y_measure, float fric_comp)
{
    if (dt <= 0 || b0 == 0 || !isfinite(u_current) || !isfinite(y_measure) || !isfinite(fric_comp))
    {
        return;
    }
    // 单位是 A
    if (fabs(fric_comp) > 0.001f)
    {
        u_current -= fric_comp;
    }

    // 计算观测误差
    float e = z1 - y_measure;

    // 更新状态 (欧拉积分)
    z1 += (z2 - beta1 * e) * dt;

    z2 += (z3 - beta2 * e + b0 * u_current) * dt;

    // 加了一个衰减系数，相当于重新配置了 z3 的极点位置
    float eta_f = eta_filt.Filter(GetDynamicEta(e, 0.8f));
    z3 += (-beta3 * e - eta_f * z3) * dt;
}

/// @brief 动态获取衰减系数
float ESO::GetDynamicEta(float error, float wc)
{
    if (fabs(error) >= wc)
    {
        return 0.0f;
    }
    else if (fabs(error) > 0)
    {
        return eta * (1.001f - fabs(error) / wc);
    }
    else
    {
        return eta;
    }
}

/**
 * @brief 使用默认参数快速初始化 ADRC
 */
void ADRC::FInit(ADRCType _ad_t, float _J, float _Kt, float _dt, float _max_curr)
{
    ADRC::Init(_ad_t, 15.0f, 2.7f, _J, 0.0f, _Kt, _dt, _max_curr);
}

/**
 * @brief 位置模式下（三阶）ADRC控制
 * @param target_pos 目标位置 (rad)
 */
void ADRC::Init(ADRCType _ad_t, float wo, float wc, float _J, float _B, float _Kt, float _dt,
                float _max_curr)
{
    _inited = false;
    ad_t = None_Enabled;
    i_des = i_fric = 0;
    if ((_ad_t != Sec_Ord && _ad_t != Thr_Ord) || !isfinite(wo) || !isfinite(wc) || !isfinite(_J) ||
        !isfinite(_B) || !isfinite(_Kt) || !isfinite(_dt) || !isfinite(_max_curr) || wo <= 0 ||
        wc <= 0 || _J <= 0 || _Kt == 0 || _dt <= 0 || _max_curr <= 0)
    {
        return;
    }
    lpf_w.y_prev = lpf_TL.y_prev = 0;
    J = _J;
    B = _B;
    Kt = _Kt;
    max_current = _max_curr;

    // 初始化 ESO（wo 接受Hz）
    eso.Init(wo, J, Kt, _dt);
    ad_t = _ad_t;

    if (_ad_t == Sec_Ord)
    {
        // 初始化 TD
        input_td.Init(28.0f, _dt, StdMath::RpmToRadS(4000.0f),
                      fmax((_Kt * _max_curr - B) * 0.75f, 1e-5f) / J);

        // 初始化转速低通滤波器
        lpf_w.Init(200.0f, _dt);

        // 初始化负载转矩低通滤波器
        lpf_TL.InitHz(5.0f, _dt);

        // 将w_c转为rad
        wc = wc * (2.0f * PI);

        kp = 2 * wc;
    }
    else if (_ad_t == Thr_Ord)
    {
        // 初始化 TD
        // input_td_3rd.Init(9.0f, _dt);
        // 计算r
        float r_maxacc = fmax((_Kt * _max_curr - B), 1e-5f) / J; // 留一点余量
        input_nltd_3rd.Init(r_maxacc * 0.75f, 3 * _dt, _dt, StdMath::RpmToRadS(7500.0f));

        // 将w_c转为rad
        wc = wc * (2.0f * PI);

        kp = wc * wc;
        kd = 2.0f * 0.85f * wc;

        // 初始化负载转矩低通滤波器
        lpf_TL.InitHz(40.0f, _dt);

        // 初始化摩擦模型
        fric_comp.Init(0.25f, 0.05f, 40.0f, 120.0f);

        // 初始化方波激励器
        square_injector.InitHz(0.45f, 100.0f);
    }

    _inited = true;
}

float ADRC::CalcSpeed(float target_speed, float measure_theta_total)
{
    (void)measure_theta_total; // observation is supplied separately via Observe()
    if (!_inited || ad_t != Sec_Ord || !isfinite(target_speed))
    {
        return 0;
    }
    // === 观测器更新严禁在本循环进行！  ===
    // 输入微分跟踪
    input_td.Update(target_speed);

    // 提取状态
    float omega_hat = eso.z2; // 估计速度
    float omega_hat_filtered = lpf_w.Filter(omega_hat);
    debug_3 = StdMath::RadSToRpm(omega_hat_filtered);

    // 扰动补偿 (包含摩擦补偿)
    float i_dist = -(eso.z3 / eso.b0);
    i_dist = lpf_TL.Filter(i_dist);

    // 误差反馈
    float error = input_td.v1 - omega_hat_filtered;
    debug_2 = error;
    float i_acc = (J * kp * error) / Kt;
    debug_0 = i_acc / 20.0f * 16384.0f;

    // 加速度前馈
    float i_feedforward = (J * input_td.v2) / Kt * coeff_feedforward;

    // 总控制输出
    float i_total = i_acc + i_dist + i_feedforward;
    debug_1 = i_total / 20.0f * 16384.0f;

    //
    if (i_total > max_current)
    {
        i_total = max_current;
    }
    else if (i_total < -max_current)
    {
        i_total = -max_current;
    }

    debug_2 = input_nltd_3rd.v3;

    // Debug
    debug_omega = omega_hat_filtered * 60.0f / (2.0f * 3.1415926f); // 转换为rpm;
    debug_TL = i_dist * -1000.0f; // 转换为 mA
    debug_ltd_targ_omega = input_td.v2 / eso.b0 * 1000.0f; // 目标力矩
    debug_ltd_targ_pos = input_td.v1 * 60.0f / (2.0f * 3.1415926f); // 目标速度

    return i_total;
}

/**
 * @brief 位置模式下（三阶）ADRC控制
 * @param target_pos 目标位置 (rad)
 */
float ADRC::CalcPos(float target_pos, float measure_theta_total)
{
    (void)measure_theta_total;
    if (!_inited || ad_t != Thr_Ord || !isfinite(target_pos))
    {
        return 0;
    }
    // 首先更新微分跟踪器，获取目标轨迹（包括位置、速度和加速度）
    input_nltd_3rd.Update(target_pos);

    // 提取 ESO 状态
    float pos_hat = eso.z1; // 估计位置
    float vel_hat = eso.z2; // 估计速度
    float dist_hat = eso.z3; // 每个控制器使用独立的局部计算状态
    dist_hat = lpf_TL.Filter(dist_hat);

    debug_0 = pos_hat;

    // 利用TD的输出作为参考输入，计算误差
    float err_p = input_nltd_3rd.v1 - pos_hat;
    float err_v = input_nltd_3rd.v2 - vel_hat;

    // PD 控制律
    float u0 = kp * err_p + kd * err_v + input_nltd_3rd.v3;

    debug_1 = u0 / eso.b0;

    // 扰动补偿
    i_des = (u0 - dist_hat) / eso.b0;

    // 零速摩擦补偿
    if (_friccomp_enabled)
    {
        i_fric = fric_comp.Get_Compensation(i_des, vel_hat);
        i_des += i_fric;
    }

    // 方波注入
    // i_des += square_injector.AutoGetValue();

    // 限幅
    if (i_des > max_current)
    {
        i_des = max_current;
    }
    else if (i_des < -max_current)
    {
        i_des = -max_current;
    }

    // Debug
    debug_omega = vel_hat * 60.0f / (2.0f * 3.1415926f); // 转换为rpm;
    debug_TL = dist_hat / eso.b0 * 1000.0f; // 转换为 mA
    debug_ltd_targ_omega = input_nltd_3rd.v2 * 60.0f / (2.0f * 3.1415926f); // 目标速度
    debug_ltd_targ_pos = input_nltd_3rd.v1 * 8192.0f / (2.0f * 3.1415926f); // 目标位置

    debug_current = i_des * 1000.0f; // 转换为 mA

    return i_des;
}
