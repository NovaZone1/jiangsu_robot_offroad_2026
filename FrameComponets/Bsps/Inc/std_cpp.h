#pragma once
#ifdef __cplusplus
extern "C"
{
#endif
    /* C/C++ bridge adapted from V1_main. Call init once from main; call the
     * framework task once from StartDefaultTask. All registries have one owner. */
    void MainInitCpp(void);
    void RobotSystemCpp(void);
    void FrameTickCpp(void);
#ifdef __cplusplus
}
#endif
