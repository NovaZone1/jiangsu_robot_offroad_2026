#pragma once
#include "linear_math.hpp"
#include "bsp_dwt.h"

/**
 * @brief 线性化的微分跟踪器
 * @name Lineared Time Differentiator (Linear TD)
 * @note 其实就是一个临界阻尼的二阶系统，用于平滑阶跃输入并计算其导数
 */
class LinearTD_2nd
{
public:
    float v1 = 0.0f; // 跟踪平滑速度，一阶量
    float v2 = 0.0f; // 跟踪微分，二阶量

    /// @brief 响应速度 r: 越大跟踪越快，但噪声变大
    float r = 20.0f;

    /// @brief 采样周期 dt
    float dt = 0.002f;

    /// @brief 最大跟踪速度 (0代表不限制)
    float max_v = 0.0f;
    /// @brief 最大跟踪加速度 (0代表不限制)
    float max_a = 0.0f;

    /**
     * @brief 初始化跟踪微分器参数
     * @param _r   响应速度
     * @param _dt  采样周期
     * @param _max_v 最大速度限制 (Default: 0)
     * @param _max_a 最大加速度限制 (Default: 0)
     */
    void Init(float _r, float _dt, float _max_v = 0.0f, float _max_a = 0.0f);

    /**
     * @brief 计算下一时刻的平滑值
     * @param target_input 原始的阶跃目标输入
     */
    void Update(float target_input);
};

/**
 * @brief 三阶线性跟踪微分器 (3rd-Order Linear TD)
 * @note 平滑位置、速度、加速度。输出的加速度是连续的。
 */
class LinearTD_3rd
{
public:
    float v1 = 0.0f; // 位置
    float v2 = 0.0f; // 速度
    float v3 = 0.0f; // 加速度

    float r = 20.0f;
    float dt = 0.002f;

    float max_v = 0.0f; // 最大速度 (0代表不限制)
    float max_a = 0.0f; // 最大加速度 (0代表不限制)

    /**
     * @brief 初始化跟踪微分器参数
     * @param _r   响应速度
     * @param _dt  采样周期
     */
public:
    void Init(float _r, float _dt, float _max_v, float _max_a);

    /**
     * @brief 计算下一时刻的平滑值
     * @param target_input 原始的阶跃目标输入
     */
    void Update(float target_input);
};

/**
 * @brief 一阶低通滤波器
 * @note 这个跟手写的不同点是，这个是基于截止频率来设计的，更直观一些
 */
class LowPassFilter
{
public:
    float y_prev = 0.0f;
    float alpha = 1.0f; // 低通滤波系数 (0~1)

    /**
     * @brief 初始化低通滤波器
     * @param cutoff_freq_rad 截止频率 （注意！！单位是 rad/s ）
     */
    void Init(float cutoff_freq_rad, float dt);

    /**
     * @brief 初始化低通滤波器
     * @param cutoff_freq_rad 截止频率 （注意！！单位是 Hz ）
     */
    void InitHz(float cutoff_freq_hz, float dt);

    /**
     * @brief 执行低通滤波
     * @param input 当前输入值
     * @return 滤波后输出值
     */
    float Filter(float input);
};

/**
 * @brief 一阶高通滤波器 (High Pass Filter)
 * @note 高通滤波器可以去除信号中的直流分量（就是把信号的均值变为0）
 */
class HighPassFilter
{
public:
    float alpha = 0.0f; // 滤波系数
    float prev_input = 0.0f; // 上一次输入 x[k-1]
    float prev_output = 0.0f; // 上一次输出 y[k-1]

    /**
     * @brief 初始化高通滤波器
     * @param cutoff_freq_hz 截止频率 (Hz)
     * @param dt 采样周期 (s)
     */
    void Init(float cutoff_freq_hz, float dt);

    /**
     * @brief 执行高通滤波
     * @param input 当前输入
     * @return 滤波后输出
     */
    float Filter(float input);

    // 重置状态
    void Reset()
    {
        prev_input = 0.0f;
        prev_output = 0.0f;
    }
};

/**
 * @brief 摩擦补偿器
 */
class FrictionCompensator
{
public:
    FrictionCompensator() {};

    float fric_static = 0.0f; // 静摩擦补偿 (电流：A)
    float fric_dynamic = 0.0f; // 动摩擦补偿 (电流：A)
    float vel_transition_start = 0.0f; // 速度过渡起点 (rad/s)
    float vel_transition_end = 0.0f; // 速度过渡终点 (rad/s)

    /// @brief 限制补偿带宽，防止补偿突变，把ESO干爆
    LowPassFilter lpf_fric;

    /**
     * @brief 初始化摩擦补偿器参数
     * @param F_static 静摩擦补偿 (电流：A)
     * @param F_dynamic 动摩擦补偿 (电流：A)
     * @param V_tran_start 速度过渡起点 (rad/s)
     * @param V_tran_end 速度过渡终点 (rad/s)
     */
    void Init(float F_static, float F_dynamic, float V_tran_start, float V_tran_end);

    /**
     * @brief 根据摩擦力模型，获得当前的摩擦力大小
     * @note 可以用于补偿ESO的模型输入
     */
    float GetFriction(float v_real);

    /**
     * @brief 简化的摩擦力模型
     * @note 只有静摩擦补偿，会瞬间阶跃变号，用于快速估算
     */
    float GetSimpleFriction(float v_real);

    /**
     * @brief 根据真实速度和意图，计算摩擦补偿电流
     */
    float Get_Compensation(float real_u, float real_velo);
};

/**
 * @brief 方波激励信号发生器
 */
class SquareInjector
{
public:
    SquareInjector() {};
    float amplitude = 0.0f; // 方波幅值
    float period = 1.0f; // 方波周期 (s)

    void Init(float amp, float per);

    void InitHz(float amp, float freq_hz);

    float GetValue(float time_sec);

    float AutoGetValue();
};

/**
 * @brief 卡尔曼观测器
 * @param con_dim   控制输入维度
 * @param state_dim 状态向量维度
 * @param meas_dim  观测向量维度
 */
template <uint8_t con_dim, uint8_t state_dim, uint8_t meas_dim> class KalmanObserver
{
public:
    KalmanObserver() {};

    Matrix<state_dim, 1> x; // 状态向量
    Matrix<meas_dim, 1> y; // 观测向量估计值 (Output Estimate)

    Matrix<con_dim, 1> u; // 控制输入向量

    /// @brief 离散状态转移矩阵
    Matrix<state_dim, state_dim> F;
    /// @brief 离散控制输入矩阵
    Matrix<state_dim, con_dim> G;
    /// @brief 观测矩阵
    Matrix<meas_dim, state_dim> H;
    /// @brief 过程噪声协方差矩阵
    Matrix<state_dim, state_dim> Q;
    /// @brief 测量噪声协方差矩阵
    Matrix<meas_dim, meas_dim> R; // 必须正定
    /// @brief 估计误差协方差矩阵
    Matrix<state_dim, state_dim> P;

    /**
     * @brief 卡尔曼滤波迭代步骤
     * @param control_input 当前控制输入
     * @param measurement 当前传感器的实际测量值
     */
    void Observe(const Matrix<con_dim, 1> &control_input, const Matrix<meas_dim, 1> &measurement)
    {
        // 首先进行预测，也就是状态转移
        u = control_input;
        x = F * x + G * u;
        y = H * x;
        P = F * P * F.transpose() + Q;

        // 获得新息
        Matrix<meas_dim, 1> y_tilde = measurement - y;

        // 计算新息的协方差
        Matrix<meas_dim, meas_dim> S = H * P * H.transpose() + R;

        // 为防止数值不稳定，给 S 加一个很小的对角线项
        S = S + Matrix<meas_dim, meas_dim>::identity() * 1e-6f;

        // 计算 S 的逆矩阵
        Matrix<meas_dim, meas_dim> S_inv;

        // 尝试求逆
        if (S.inverse(S_inv)) // 求逆成功，执行更新
        {
            // 计算卡尔曼增益
            Matrix<state_dim, meas_dim> K = P * H.transpose() * S_inv;

            // 更新状态估计
            x = x + K * y_tilde;

            // 更新误差协方差
            Matrix<state_dim, state_dim> I = Matrix<state_dim, state_dim>::identity();
            P = (I - K * H) * P;
            y = H * x;
        }
        else
        {
            /**
             * @brief 求逆失败
             * @note 如果程序能跑到这里说明求逆炸缸了，一般是R太小了，或者系统本身能观性有问题。
             * 那就跳过修正更新只靠预测
             */
        }
    }
};

class IVIdentifier
{
public:
    /**
     * @brief 构造函数
     * @param b0_nominal  ADRC控制器当前设置的b0值
     * @param fs          采样频率 (Hz)，例如 1000
     * @param cut_freq    高通滤波截止频率 (Hz)，用于去直流
     * @param forget_time 协方差统计的"记忆时间" (秒)
     */
    IVIdentifier(float b0_nominal, float fs, float cut_freq, float forget_time);

    /**
     * @brief 1kHz 周期调用更新函数
     * @param r_cmd  当前的参考指令 (速度或位置指令) -> 工具变量
     * @param u_ctrl 当前的控制量 (电流/力矩) -> 回归变量
     * @param z3_obs 当前ESO观测到的总扰动 z3 -> 观测变量
     */
    void Update(float r_cmd, float u_ctrl, float z3_obs);

    // 获取辨识出的 theta (即 b_real - b0)
    float GetTheta() const
    {
        return theta_iv;
    }

    // 获取辨识出的真实惯量 J (单位与 b0 定义一致)
    float GetEstimatedJ(float Kt = 1.0f) const;

    // 获取辨识过程的置信度/激发程度 (分母的大小)
    float GetExcitationLevel() const
    {
        return cov_self;
    }

    // 重置算法状态
    void Reset();

    // 参数
    float b0_;
    float lambda_; // 遗忘因子 (0 < lambda < 1)
    float lambad_rho;
    float hpf_alpha_; // 高通滤波器系数

    // 状态变量
    float theta_iv; // 最终辨识参数
    float theta_iv_accum_; // 累计和，用于计算平均值
    float cov_cross; // 互相关系数 (Cov(r, z3))
    float cov_self; // 自相关系数 (Cov(r, u))

    float cov_ru;
    float var_r;
    float var_u;
    float rho_ru;

    float Kt_ = 0.01562; // 转矩常数
    float J_hat_ = 0.0f; // 估计的惯量

    float r_ac;
    float u_ac;
    float z3_ac;

    // 使用封装好的高通滤波器对象
    HighPassFilter hpf_r_;
    HighPassFilter hpf_u_;
    HighPassFilter hpf_z3_;
};
