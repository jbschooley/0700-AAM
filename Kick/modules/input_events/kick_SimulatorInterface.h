#pragma once

// Stand-in for the panel MCU in the desktop simulator (SIM_BUILD). It exchanges the same
// CPktMgr::HWMsg_t messages that travel over the serial link on the device.
class SimulatorInterface
{
public:
    virtual ~SimulatorInterface() {}

    // App -> panel: LED, pad colour, slider LED and system messages meant for the MCU.
    virtual void sendMessageToHW( const CPktMgr::HWMsg_t& f_HwMsg ) = 0;

    // Panel -> app: button, pad, knob and slider events, as the MCU would send them.
    sigslot::signal<CPktMgr::HWMsg_t&> hwInputReceived;
};
