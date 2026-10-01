#include "bsp_dwt.h"
#include "pid.hpp"
#include "signator.hpp"
#include "adrc.hpp"
#include "hyperPID.hpp"
#include "StateCore.hpp"
#include "Action.hpp"
#include "OffroadApp.hpp"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <limits>

TestDwt test_dwt = {};
TestCoreDebug test_core_debug = {};
uint32_t test_tick = 0, test_primask = 0;
static int first_calls, second_calls, stop_calls;

static void First(StateCore *)
{
    ++first_calls;
}

static void Second(StateCore *)
{
    ++second_calls;
}

static void StopDriver(void *)
{
    ++stop_calls;
}

static bool DutyDriver(void *, float)
{
    return true;
}

static bool DutyFailure(void *, float)
{
    return false;
}

static GraySensor::Sample gray_sample = {};
static Ultrasonic::Sample range_sample = {};

static bool ReadGray(void *, GraySensor::Sample *sample)
{
    *sample = gray_sample;
    return true;
}

static bool ReadRange(void *, Ultrasonic::Sample *sample)
{
    *sample = range_sample;
    return true;
}

static char log_result[256];

static void LogSink(const char *data, size_t size)
{
    assert(size < sizeof(log_result));
    memcpy(log_result, data, size);
    log_result[size] = 0;
}

class NeverComplete : public BaseAction
{
public:
    bool timed_out = false, canceled = false;

    bool OnUpdate() override
    {
        return false;
    }

    void OnTimeout() override
    {
        timed_out = true;
    }

    void OnCancel() override
    {
        canceled = true;
    }
};

int main()
{
    // Actual DWT accumulator: 32-bit rollover, integer conversion, IRQ restore.
    BspDwt_Init(72);
    test_dwt.CYCCNT = 0xFFFFFFF0U;
    uint64_t before = BspDwt_GetTimeline_USec();
    test_dwt.CYCCNT = 0x10;
    uint64_t after = BspDwt_GetTimeline_USec();
    assert(after >= before && after - before <= 1);
    test_dwt.CYCCNT += 72000;
    assert(BspDwt_GetTimeline_USec() - after == 1000);
    test_primask = 1;
    BspDwt_CntUpdate();
    assert(test_primask == 1);
    test_primask = 0;

    // Actual PID: initialized derivative history, limits, zero-dt, reset.
    PidGeneral pid;
    pid.Init(1, 0, 0);
    pid.ManualDt(0.001f);
    pid.SetLimit(0, 5, 0.9f);
    assert(pid.Calc(10, 0) == 5);
    pid.ManualDt(0);
    assert(isfinite(pid.Calc(10, 0)));
    pid.Reset();
    assert(pid.Calc(0, 0) == 0);
    PidGeneral inc;
    inc.Init(1, 0, 0);
    inc.ManualDt(0.001f);
    inc.IncreLize();
    assert(inc.Calc(1, 0) == 1 && inc.Calc(2, 0) == 2);
    assert(inc.Calc(std::numeric_limits<float>::quiet_NaN(), 0) == 0);

    // Real CMSIS scalar matrix backend and template instantiations.
    Matrix<2, 2> matrix(4, 7, 2, 6), inverse;
    assert(matrix.inverse(inverse));
    assert(matrix(0, 0) == 4 && matrix(1, 1) == 6); // inverse must not mutate input
    Matrix<2, 2> identity = matrix * inverse;
    assert(fabsf(identity(0, 0) - 1) < 1e-5f && fabsf(identity(0, 1)) < 1e-5f);
    assert(fabsf(identity(1, 1) - 1) < 1e-5f && fabsf(identity(1, 0)) < 1e-5f);
    Matrix<2, 2> singular(1, 2, 2, 4), unchanged = Matrix<2, 2>::identity();
    assert(!singular.inverse(unchanged) && unchanged(0, 0) == 1);
    Matrix<2, 1> column(3, 5);
    assert(column.transpose()(0, 1) == 5);
    assert(((matrix + matrix) - matrix)(0, 1) == 7);
    assert((matrix * 2.0f)(1, 0) == 4);
    KalmanObserver<1, 1, 1> kalman;
    kalman.F(0, 0) = kalman.G(0, 0) = kalman.H(0, 0) = kalman.P(0, 0) = 1;
    kalman.Q(0, 0) = 0.01f;
    kalman.R(0, 0) = 0.1f;
    kalman.Observe(Matrix<1, 1>(2), Matrix<1, 1>(2));
    assert(fabsf(kalman.x(0, 0) - 2) < 1e-5f && kalman.u(0, 0) == 2);
    kalman.Observe(Matrix<1, 1>(1), Matrix<1, 1>(3));
    assert(fabsf(kalman.y(0, 0) - 3) < 1e-5f);
    IVIdentifier identifier(1, 1000, 10, 1);
    for (int i = 0; i < 100; ++i)
    {
        identifier.Update(0, 0, 0);
    }
    assert(identifier.GetTheta() == 0 && isfinite(identifier.GetEstimatedJ()));
    identifier.Reset();
    assert(identifier.theta_iv_accum_ == 0 && identifier.var_r == 0);
    IVIdentifier invalid_identifier(0, 0, 0, 0);
    invalid_identifier.Update(0, 0, 0);
    assert(isfinite(invalid_identifier.GetTheta()));
    ADRC adrc;
    assert(adrc.Calc(1, 0) == 0 && adrc.CalcSpeed(1, 0) == 0);
    adrc.Init(ADRC::Sec_Ord, 15, 2, 0, 0, 0.1f, 0.001f, 2);
    assert(adrc.Calc(1, 0) == 0); // invalid inertia is rejected
    adrc.Init(ADRC::Sec_Ord, 15, 2, 0.01f, 0, 0.1f, 0.001f, 2);
    for (int i = 0; i < 100; ++i)
    {
        adrc.Observe(0, 0);
        float current = adrc.Calc(100, 0);
        assert(isfinite(current) && fabsf(current) <= 2);
    }
    adrc.Init(ADRC::Thr_Ord, 15, 2, 0.01f, 0, 0.1f, 0.001f, 2);
    assert(isfinite(adrc.Calc(1, 0)) && fabsf(adrc.Calc(1, 0)) <= 2);
    HyPID hyper_parameters;
    assert(hyper_parameters.Coeffs.Kp == 0);

    // Actual graph: transition must change reference without overwriting state 0.
    StateCore &core = StateCore::GetInstance();
    core.Enable();
    core.Run(); // empty graph is safe
    StateGraph graph("test");
    StateBlock &first = graph.AddState("first"), &second = graph.AddState("second");
    first.StateAction = First;
    second.StateAction = Second;
    bool transition = true;
    assert(!first.LinkTo(nullptr, second));
    assert(first.LinkTo(&transition, second));
    core.RegistGraph(graph);
    core.RegistGraph(graph);
    assert(core.graphNums == 1);
    core.Enable();
    core.Run();
    assert(first_calls == 1 && &core.GetCurState() == &second);
    assert(strcmp(graph.states[0].name, "first") == 0 && graph.states[0].StateAction == First);
    core.Run();
    assert(second_calls == 1);
    core.Disable();
    core.Run();
    assert(second_calls == 1);

    // Actual action manager: timeout across tick wrap, cancellation, duplicate slots.
    Action.Init();
    NeverComplete action;
    test_tick = 0xFFFFFFFAU;
    assert(Action.LaunchAsync(&action, 10) == &action);
    assert(Action.LaunchAsync(&action, 10) == nullptr);
    test_tick = 4;
    Action.ExecutorRun();
    assert(action.timed_out && action.GetState() == Action.FAILED);
    NeverComplete cancel;
    assert(Action.LaunchAsync(&cancel));
    Action.CancelAll();
    assert(cancel.canceled);
    bool blocked = false;
    uint32_t sequence = 0;
    test_tick = 0;
    Action.Wait(10, &blocked, &sequence);
    assert(blocked);
    test_tick = 10;
    Action.Wait(10, &blocked, &sequence);
    assert(!blocked && sequence == 0);
    Action.Wait(10);
    Action.WaitUntil(false); // optional null parameters are safe

    // Actual modules and App: unbound hardware cannot pass self-check or drive.
    DcMotor left, right;
    Ultrasonic range;
    GraySensor gray;
    OffroadApp app(left, right, range, gray);
    System.Init();
    assert(System.RegistApp(app));
    assert(!System.RegistApp(app));
    System.Run();
    assert(System.GetState() == Systems::SELF_CHECK && !app.WatchPoint());
    assert(!left.SetDuty(1));
    assert(!left.Bind({nullptr, nullptr, StopDriver, nullptr}));
    assert(left.Bind({nullptr, DutyDriver, StopDriver, nullptr}));
    assert(right.Bind({nullptr, DutyDriver, StopDriver, nullptr}));
    assert(left.SetDuty(2) && left.GetDuty() == 1);
    assert(!left.SetDuty(std::numeric_limits<float>::quiet_NaN()) && left.GetDuty() == 0);
    gray.Bind(nullptr, ReadGray);
    range.Bind(nullptr, ReadRange);
    test_tick = 100;
    gray_sample.channels = 3;
    gray_sample.line[2] = 1;
    gray_sample.valid = true;
    gray_sample.timestamp_ms = test_tick;
    range_sample = {150, test_tick, true};
    app.SampleSensors();
    float error = 0;
    assert(gray.GetLineError(error) && error == 1);
    assert(app.WatchPoint());
    System.Run();
    assert(System.GetState() == Systems::READY);
    System.system_start_to_work_flag = true;
    System.Run();
    assert(System.GetState() == Systems::WORKING);
    app.Control();
    assert(left.GetDuty() == 0 && right.GetDuty() == 0); // unfinished strategy stays stopped
    test_tick += 51;
    System.UpdateApplications();
    assert(System.GetState() == Systems::ERROR);
    gray_sample.channels = FRAME_GRAY_MAX_CHANNELS + 1;
    assert(!gray.Update());
    range_sample.distance_mm = -1;
    assert(!range.Update());
    left.Bind({nullptr, DutyFailure, StopDriver, nullptr});
    assert(!left.SetDuty(0.5f) && left.GetDuty() == 0);
    assert(stop_calls > 0);

    // Bounded monitor output, including truncation of long strings.
    Monitor &monitor = Monitor::GetInstance();
    monitor.Init(LogSink);
    char long_message[512];
    memset(long_message, 'x', sizeof(long_message) - 1);
    long_message[511] = 0;
    monitor.LogInfo("%s", long_message);
    assert(strlen(log_result) <= 127 && log_result[strlen(log_result) - 1] == '\n');
    puts("PASS: matrix/CMSIS, Kalman input, IV reset, ADRC guards, DWT rollover, PID, states, "
         "actions and module guards");
}
