#pragma once
#include "signator.hpp"

/**
 * @brief 扩张状态观测器
 * @note 默认是线性化状态，如需启用FAL函数，请激活
 */
class ESO
{
public:
    /// @brief z1: 估计角度 (rad)
    float z1 = 0.0f;
    /// @brief z2: 估计角速度 (rad/s)
    float z2 = 0.0f;
    /// @brief z3: 估计的总扰动加速度 (rad/s^2)
    float z3 = 0.0f;

    /// @brief 基础观测器带宽 (rad/s)
    float _base_omega_o = 30.0f;

    float beta1 = 0, beta2 = 0, beta3 = 0;
    float b0 = 0;
    float J = 0;
    float dt = 0;

    float eta = 2.0f; // 衰减系数

    /// @brief 衰减低通滤波器
    LowPassFilter eta_filt;

    /// @brief 非线性误差处理函数参数
    float alpha = 0.5f;
    float delta = 0.01f;

    /**
     * @brief 初始化 ESO
     * @param _omega_o 观测器带宽 (Hz)
     * @param _J 转动惯量 (kg*m^2)
     * @param _Kt 转矩常数 (N*m/A)
     * @param _dt 控制周期 (单位：s)
     */
    void Init(float _omega_o, float _J, float _Kt, float _dt);

    /**
     * @brief 根据带宽计算 ESO 增益
     * @param _omega_o 观测器带宽 (Hz)
     */
    void UpdateOmega_O(float _omega_o);

    /// @brief 非线性误差处理函数
    float GetNonlinearError(float raw_err);

    /**
     * @brief 更新观测器状态
     * @param u_current 实际给电机的电流 (A) -> last_u
     * @param y_measure 测量到的位置 (rad) -> measure_theta_total
     */
    void Observe(float u_current, float y_measure, float fric_comp = 0.0f);

    /// @brief 动态获取衰减系数
    float GetDynamicEta(float error, float wc);
};

/**
 * @brief 非线性微分跟踪器
 * @note Fhan实现
 */
class NonlinearTD
{
private:
    // 符号函数 (Fhan 核心组件)
    float fsign(float x)
    {
        if (x > 1e-6f)
        {
            return 1.0f;
        }
        if (x < -1e-6f)
        {
            return -1.0f;
        }
        return 0.0f;
    }

    // 辅助函数：限幅
    float fclamp(float val, float limit)
    {
        if (limit <= 0.0f)
        {
            return val;
        }
        if (val > limit)
        {
            return limit;
        }
        if (val < -limit)
        {
            return -limit;
        }
        return val;
    }

    /**
     * @brief 韩京清 Fhan 最速控制综合函数
     * @param x1 位置误差 (v1 - target)
     * @param x2 当前速度 (v2)
     * @param r  最大加速度 (物理意义明确)
     * @param h  滤波步长 (通常略大于 dt)
     * @return 期望的加速度
     */
    float fhan(float x1, float x2, float r, float h)
    {
        if (r <= 0 || h <= 0)
        {
            return 0;
        }
        float d = r * h * h; // 零速区长度
        float a0 = h * x2; // 预测量
        float y = x1 + a0; // 预测位置误差

        // 判断当前处于抛物线区域还是线性区域
        float a1 = sqrtf(d * (d + 8.0f * fabsf(y)));
        float a2 = a0 + fsign(y) * (a1 - d) * 0.5f;

        // 变结构控制量计算
        float sy = (fsign(y + d) - fsign(y - d)) * 0.5f;
        float a = (a0 + y - a2) * sy + a2;
        float sa = (fsign(a + d) - fsign(a - d)) * 0.5f;

        return -r * ((a / d) - fsign(a)) * sa - r * fsign(a);
    }

public:
    float v1 = 0.0f; // 跟踪位置
    float v2 = 0.0f; // 跟踪速度
    float v3 = 0.0f; // 跟踪加速度 (由 Fhan 计算得出)

    float r = 1000.0f; // 这里的 r 直接等于 max_a (最大加速度)
    float h = 0.005f; // 滤波因子 (比 dt 大几倍)
    float dt = 0.001f; // 采样时间

    float max_v = 0.0f; // 最大速度限制

    // 初始化
    // 注意：_r 在这里直接代表最大物理加速度 (Max_Acceleration)
    // _h 建议设置为 dt 的 2~5 倍
    void Init(float _r, float _h, float _dt, float _max_v)
    {
        r = _r;
        h = _h;
        dt = _dt;
        max_v = _max_v;

        v1 = 0.0f;
        v2 = 0.0f;
        v3 = 0.0f;
    }

    void Update(float target_input)
    {
        if (!isfinite(target_input) || r <= 0 || h <= 0 || dt <= 0)
        {
            return;
        }
        // 计算输入误差
        float error = v1 - target_input;

        // 通过 fhan 计算不超调所需的“最佳加速度”
        // Fhan 会自动根据当前速度 v2 和物理限制 r 来决定是加速还是刹车
        float optimal_acc = fhan(error, v2, r, h);

        // 将计算结果存入 v3 (作为加速度输出)
        v3 = optimal_acc;

        // 积分更新速度 v2
        v2 += v3 * dt;

        // 速度限幅 (处理 Max_V)
        // 在非线性 TD 中，直接限幅 v2 是安全的，
        // 因为 fhan 下次计算时会读取到被限幅后的 v2，并自动重新计算刹车点。
        v2 = fclamp(v2, max_v);

        // 积分更新位置 v1
        v1 += v2 * dt;
    }

    void ResetR(float _r)
    {
        r = _r;
    }
};

/**
 * @brief 自抗扰控制器
 * @details 结构：
 *      观测器：ESO(状态: theta, omega, T_load)
 */
class ADRC
{

private:
    bool _friccomp_enabled = false; // 是否启用摩擦补偿
    bool _squareinj_enabled = false; // 是否启用方波激励
    bool _inited = false; // 是否初始化完成

public:
    float debug_0 = 0, debug_1 = 0, debug_2 = 0, debug_3 = 0;

    float J = 0;
    float B = 0;
    float Kt = 0;
    float max_current = 0;

    // 负载估计系数
    float coeff_feedforward = 0.55f;

    /// @brief 扩张状态观测器实例
    ESO eso;

    /// @brief 方波激励器实例
    SquareInjector square_injector;

    /// @brief 摩擦补偿器实例
    FrictionCompensator fric_comp;

    /// @brief 输入微分跟踪器
    LinearTD_2nd input_td;
    LinearTD_3rd input_td_3rd;
    NonlinearTD input_nltd_3rd;

    /// @brief 低通滤波器
    LowPassFilter lpf_w, lpf_TL;

    float kp = 0;
    float kd = 0;

    float i_fric = 0.0f;

    enum ADRCType
    {
        None_Enabled,
        Sec_Ord, // 二阶ADRC（速度模式）
        Thr_Ord, // 三阶ADRC（位置模式）
    };

    ADRCType ad_t = None_Enabled;

    /**
     * @brief 初始化
     * @note 输入的wo和wc均为Hz单位，函数内会转换为rad/s
     * @param _ad_t `Sec_Ord` 或 `Thr_Ord`
     */
    void Init(ADRCType _ad_t, float wo, float wc, float _J, float _B, float _Kt, float _dt,
              float _max_curr);

    /**
     * @brief 初始化
     * @note 输入的wo和wc均为Hz单位，函数内会转换为rad/s
     * @param _ad_t `Sec_Ord` 或 `Thr_Ord`
     */
    void FInit(ADRCType _ad_t, float _J, float _Kt, float _dt, float _max_curr);

    /**
     * @brief 速度模式下（二阶）ADRC控制
     * @param target_speed 目标速度 (rad/s)
     */
    float CalcSpeed(float target_speed, float measure_theta_total);

    /**
     * @brief 位置模式下（三阶）ADRC控制
     * @param target_pos 目标位置 (rad)
     */
    float CalcPos(float target_pos, float measure_theta_total);

    /**
     * @brief 计算控制输出
     * @param target 目标值
     * @param measure_theta_total 累积测量的绝对角度 (rad)
     */
    float Calc(float target, float measure_theta_total)
    {
        if (!_inited)
        {
            return 0;
        }
        if (ad_t == Sec_Ord)
        {
            return CalcSpeed(target, measure_theta_total);
        }
        else if (ad_t == Thr_Ord)
        {
            return CalcPos(target, measure_theta_total);
        }
        else
        {
            return 0.0f;
        }
    }

    /**
     * @brief 观测当前状态
     */
    void Observe(float u, float y)
    {
        if (!_inited)
        {
            return;
        }
        if (_friccomp_enabled)
        {
            eso.Observe(u, y, i_fric);
        }
        else
        {
            eso.Observe(u, y);
        }
    }

    // 调试用的变量
    float debug_omega = 0;
    float debug_TL = 0;
    float debug_ltd_targ_omega = 0;
    float debug_ltd_targ_pos = 0;
    float debug_current = 0;

    float i_des = 0;
};
